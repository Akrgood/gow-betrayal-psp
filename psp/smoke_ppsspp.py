#!/usr/bin/env python3
"""Run a non-interactive PPSSPP smoke test. Run under xvfb-run, not a desktop."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time


def run(emu: Path, game: Path, output: Path, stage: str, layout: str, skip_intro: bool, language: int) -> None:
    output.mkdir(parents=True, exist_ok=False)
    config = output / "config"
    system = config / "ppsspp/PSP/SYSTEM"
    system.mkdir(parents=True)
    (system / "ppsspp.ini").write_text("[SystemParam]\nPSPModel = 0\n", encoding="utf-8")
    copy = (config / "ppsspp/PSP/GAME/GOWBETRAYAL" if layout == "memstick"
            else output / "GOWBETRAYAL")
    copy.mkdir(parents=True)
    shutil.copy2(game / "EBOOT.PBP", copy)
    shutil.copytree(game / "data", copy / "data")
    has_intro = (game / "INTRO.BIN").is_file()
    if has_intro:
        shutil.copy2(game / "INTRO.BIN", copy)
    elif skip_intro:
        raise ValueError("--skip-intro requires INTRO.BIN in the game folder")
    env = {**os.environ, "XDG_CONFIG_HOME": str(config), "SDL_VIDEODRIVER": "x11",
           "SDL_AUDIODRIVER": "dummy", "LIBGL_ALWAYS_SOFTWARE": "1"}
    with (output / "emulator.log").open("w") as log, subprocess.Popen(
        [str(emu), "--windowed", "--xres", "480", "--yres", "272", "--escape-exit",
         "--loglevel=3", str(copy / "EBOOT.PBP")], env=env, stdout=log, stderr=log
    ) as process:
        try:
            def window():
                return subprocess.check_output(
                    ["xdotool", "search", "--onlyvisible", "--name", "God of War: Betrayal"], text=True
                ).splitlines()[0]

            def hold(key, duration=.17):
                subprocess.run(["xdotool", "keydown", key], check=True)
                try:
                    time.sleep(duration)
                finally:
                    subprocess.run(["xdotool", "keyup", key], check=True)

            def shot(name, win):
                png = output / (name + ".png")
                subprocess.run(["ffmpeg", "-v", "error", "-f", "x11grab", "-draw_mouse", "0", "-window_id", win,
                                "-i", os.environ["DISPLAY"], "-frames:v", "1", "-threads", "1",
                                str(png)], check=True)
                assert struct.unpack(">II", png.read_bytes()[16:24]) == (480, 272), name

            deadline = time.monotonic() + 15
            boot_log = copy / "gow-psp.log"
            checked_intro = False
            while time.monotonic() < deadline and process.poll() is None:
                trace = boot_log.read_text() if boot_log.exists() else ""
                if has_intro and not checked_intro and "intro playing" in trace and "game initialized" not in trace:
                    win = window()
                    if skip_intro:
                        subprocess.run(["xdotool", "windowfocus", win], check=True)
                        hold("space")
                    else:
                        time.sleep(.65)
                        shot("intro", win)
                    checked_intro = True
                if "game initialized" in trace:
                    break
                time.sleep(.25)
            else:
                raise RuntimeError(f"Game did not boot; see {output}")
            if has_intro:
                if skip_intro:
                    if "intro skipped" not in trace:
                        raise RuntimeError("Intro skip did not work")
                else:
                    m = re.search(r"intro complete: (\d+)/(\d+) frames, (\d+) PCM blocks", trace)
                    if not m or int(m[1]) == 0 or int(m[3]) == 0:
                        raise RuntimeError("Intro did not finish with video and audio")
            win = window()
            subprocess.run(["xdotool", "windowfocus", win], check=True)

            time.sleep(8)
            shot("language", win)
            if stage in ("help", "story"):
                for _ in range(language):
                    hold("Down", .12)
                    time.sleep(.1)
                if language:
                    shot("language_selected", win)
                hold("z")  # PPSSPP defaults: Z=Cross, Space=Start, A=Square
                time.sleep(4)
                if (copy / "saves/rec-10.bin").read_bytes() != bytes([language]):
                    raise RuntimeError("Wrong language selected or not saved")
                shot("sound", win)
                hold("z")
                time.sleep(5)
                shot("menu", win)
            if stage == "help":
                hold("Right")  # Play Game -> Arena -> Help
                time.sleep(.15)
                hold("Right")
                time.sleep(.15)
                hold("z")
                time.sleep(3.5)
                if not any(line.endswith(" state=107") for line in boot_log.read_text().splitlines()):
                    raise RuntimeError("Help panel did not open")
                shot("help", win)
            if stage == "story":
                hold("z")
                time.sleep(4)
                hold("z")
                time.sleep(5)
                hold("space")
                time.sleep(5)
                shot("level", win)
                hold("Right", 1.0)
                hold("z")
                hold("a")
                time.sleep(.6)
                shot("action", win)
                hold("space")
                time.sleep(1)
                shot("pause", win)
                trace = boot_log.read_text().splitlines()
                if not any(line.endswith(" state=100") for line in trace):
                    raise RuntimeError("Opening level was never reached")
                if not any(line.endswith(" MIDI PCM submitted") for line in trace):
                    raise RuntimeError("Game did not submit any non-silent audio")
                if (copy / "data/timgm.dat").is_file() and not any(
                    line.endswith(" TimGM6mb PCM submitted") for line in trace
                ):
                    raise RuntimeError("Game did not use the TimGM6mb cache")
                if trace[-1].endswith(" exit"):
                    raise RuntimeError("Game exited before the end of the test")
                if not (copy / "saves/rec2.bin").is_file():
                    raise RuntimeError("No save file was created during startup")
            exc = copy / "exceptions.log"
            if exc.exists() and exc.stat().st_size:
                raise RuntimeError(exc.read_text())
        finally:
            process.terminate()
            process.wait(timeout=5)
    print(boot_log.read_text().strip())
    print(f"Smoke test captures: {output}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("emulator", type=Path)
    p.add_argument("game_folder", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--stage", choices=("boot", "help", "story"), default="story")
    p.add_argument("--layout", choices=("memstick", "portable"), default="memstick")
    p.add_argument("--skip-intro", action="store_true")
    p.add_argument("--language", type=int, choices=range(8), default=0)
    a = p.parse_args()
    run(a.emulator.resolve(), a.game_folder.resolve(), a.output.resolve(), a.stage,
        a.layout, a.skip_intro, a.language)
