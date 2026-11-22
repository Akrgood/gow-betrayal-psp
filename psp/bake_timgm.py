#!/usr/bin/env python3
"""Render a local K800i JAR's MIDI with TimGM6mb and pack mono IMA ADPCM for PSP."""
import argparse
import io
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from zipfile import ZipFile

from prepare_data import BANKS

RATE = 22050
BLOCK = 256
SAMPLES = 1 + (BLOCK - 4) * 2
MAX_PACK = 1024 * 1024


def hash_midi(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def midi_assets(jar):
    with ZipFile(jar) as z:
        for bank in BANKS:
            rp = z.read(bank)
            width, count = 2 + 2 * (rp[0] & 1), int.from_bytes(rp[1:3], "big")
            sizes = [int.from_bytes(rp[3 + width * i:3 + width * (i + 1)], "big") for i in range(count)]
            pos = 3 + width * count
            types = rp[pos:pos + count]
            pos += count + 4
            if pos + sum(sizes) != len(rp):
                raise ValueError(f"Invalid bank {bank}")
            for index, (size, kind) in enumerate(zip(sizes, types)):
                data = rp[pos:pos + size]
                pos += size
                if kind == 0 and data.startswith(b"MThd"):
                    yield bank, index, data


def duration(data):
    f = io.BytesIO(data)
    if f.read(4) != b"MThd":
        raise ValueError("Missing MIDI header")
    header = f.read(struct.unpack(">I", f.read(4))[0])
    fmt, tracks, division = struct.unpack_from(">HHH", header)
    if fmt not in (0, 1) or not 0 < division < 32768:
        raise ValueError("Expected MIDI format 0/1 and metric timing")
    tempos = [(0, 500000)]
    end = 0
    for _ in range(tracks):
        if f.read(4) != b"MTrk":
            raise ValueError("Missing MIDI track")
        t = io.BytesIO(f.read(struct.unpack(">I", f.read(4))[0]))

        def byte():
            b = t.read(1)
            if not b:
                raise ValueError("Truncated MIDI event")
            return b[0]

        def vlq():
            value = 0
            for _ in range(4):
                b = byte()
                value = (value << 7) | (b & 127)
                if b < 128:
                    return value
            raise ValueError("Invalid MIDI VLQ")

        status = tick = 0
        while True:
            tick += vlq()
            b = byte()
            if b == 255:
                kind, size = byte(), vlq()
                payload = t.read(size)
                if kind == 81 and size == 3:
                    tempos.append((tick, int.from_bytes(payload, "big")))
                if kind == 47:
                    break
            elif b in (240, 247):
                t.read(vlq())
                status = 0
            else:
                if b >= 128:
                    status = b
                else:
                    t.seek(-1, io.SEEK_CUR)
                if not 128 <= status < 240:
                    raise ValueError("Unsupported MIDI message")
                t.read(1 if status >> 4 in (12, 13) else 2)
        end = max(end, tick)
    elapsed = previous = 0
    tempo = 500000
    for tick, new_tempo in sorted(tempos, key=lambda event: event[0]):
        elapsed += (tick - previous) * tempo
        previous, tempo = tick, new_tempo
    return ((elapsed + (end - previous) * tempo) * RATE + division * 500000) // (division * 1000000)


def wav_chunks(wav):
    if wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise ValueError("Expected RIFF WAVE")
    i = 12
    while i + 8 <= len(wav):
        kind, size = struct.unpack_from("<4sI", wav, i)
        if i + 8 + size > len(wav):
            raise ValueError("Truncated WAVE chunk")
        yield kind, wav[i + 8:i + 8 + size]
        i += 8 + size + (size & 1)


def bake(jar, sf2, game, fluid, ffmpeg):
    entries, blobs = [], []
    assets = list(midi_assets(jar))
    if len(assets) != 11:
        raise ValueError(f"Expected 11 K800i MIDI assets, found {len(assets)}")
    offset = 8 + 16 * len(assets)
    with tempfile.TemporaryDirectory(prefix="gow-midi-") as d:
        tmp = Path(d)
        mid, wav, adpcm = (tmp / f"track.{ext}" for ext in ("mid", "wav", "adpcm.wav"))
        for bank, index, data in assets:
            # Keep the MIDI loop boundary; include a short release tail for one-shots.
            samples = duration(data)
            tail_samples = samples + RATE // 8
            if not 0 < samples <= 120 * RATE:
                raise ValueError(f"Unexpected MIDI duration in {bank}:{index}")
            mid.write_bytes(data)
            subprocess.run([fluid, "-n", "-i", "-q", "-g", "0.65", "-r", str(RATE), "-F",
                            str(wav), str(sf2), str(mid)], check=True,
                           env={**os.environ, "OMP_NUM_THREADS": "1"})
            subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                            "-af", f"apad,atrim=end_sample={tail_samples}", "-ac", "1",
                            "-ar", str(RATE), "-c:a", "adpcm_ima_wav", "-block_size", str(BLOCK),
                            str(adpcm)], check=True)
            chunks = dict(wav_chunks(adpcm.read_bytes()))
            if struct.unpack_from("<HHI", chunks[b"fmt "]) != (0x11, 1, RATE):
                raise ValueError("Unexpected IMA WAV encoding")
            if struct.unpack_from("<HHHH", chunks[b"fmt "], 12) != (BLOCK, 4, 2, SAMPLES):
                raise ValueError("Unexpected IMA WAV block layout")
            blob = chunks[b"data"]
            if len(blob) != (tail_samples + SAMPLES - 1) // SAMPLES * BLOCK:
                raise ValueError("Unexpected IMA WAV size")
            if not any(blob):
                raise ValueError(f"Silent MIDI render: {bank}:{index}")
            entries.append((hash_midi(data), len(data), offset, samples))
            blobs.append(blob)
            print(f"{bank}:{index} {samples / RATE:.2f} s, {len(blob)} bytes")
            offset += len(blob)
    if len({row[:2] for row in entries}) != len(entries) or offset > MAX_PACK:
        raise ValueError("Duplicate MIDI fingerprint or pack exceeds PSP memory budget")
    path = game / "data" / "timgm.dat"
    path.parent.mkdir(parents=True, exist_ok=True)
    scratch = path.with_suffix(".tmp")
    with scratch.open("wb") as out:
        out.write(struct.pack("<4sI", b"GWT1", len(entries)))
        for entry in entries:
            out.write(struct.pack("<IIII", *entry))
        for blob in blobs:
            out.write(blob)
    scratch.replace(path)
    print(f"{path}: {len(entries)} tracks, {offset} bytes")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("jar", type=Path)
    p.add_argument("soundfont", type=Path)
    p.add_argument("game_folder", type=Path)
    p.add_argument("--fluidsynth", default="fluidsynth")
    p.add_argument("--ffmpeg", default="ffmpeg")
    a = p.parse_args()
    bake(a.jar.resolve(), a.soundfont.resolve(), a.game_folder.resolve(), a.fluidsynth, a.ffmpeg)
