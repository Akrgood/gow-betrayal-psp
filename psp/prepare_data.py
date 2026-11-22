#!/usr/bin/env python3
"""Copy the exact K800i resource banks from a local JAR into a PSP game folder."""
import argparse
import shutil
from pathlib import Path
from zipfile import ZipFile

BANKS = tuple(f"RP{i}" for i in range(1, 34) if i not in (11, 31))


def prepare(jar: Path, game: Path) -> None:
    with ZipFile(jar) as z:
        manifest = z.read("META-INF/MANIFEST.MF").decode("utf-8")
        if "MIDlet-Version: 1.4.20" not in manifest.splitlines():
            raise ValueError("Expected the Sony Ericsson K800i MIDlet 1.4.20")
        for bank in BANKS:
            if z.namelist().count(bank) != 1 or not 3 <= z.getinfo(bank).file_size <= 2**20:
                raise ValueError(f"Missing, duplicate or invalid {bank}")
        with z.open("RP19") as f:
            if f.read(3)[1:] != b"\x00\x02":
                raise ValueError("RP19 does not match the K800i build")
        corrupt = z.testzip()
        if corrupt:
            raise ValueError(f"Corrupt JAR entry: {corrupt}")
        data = game / "data"
        data.mkdir(parents=True, exist_ok=True)
        for bank in BANKS:
            with z.open(bank) as src, (data / bank).open("wb") as dst:
                shutil.copyfileobj(src, dst)
    print(f"{len(BANKS)} original resource banks -> {data}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("jar", type=Path)
    p.add_argument("game_folder", type=Path)
    a = p.parse_args()
    prepare(a.jar, a.game_folder)
