# dist/EBOOT.PBP

Prebuilt PSP executable. It contains **no game data** — only the port's code and optional XMB menu artwork.

## Install

1. Create `PSP/GAME/GOWBETRAYAL/` on the memory stick (or on internal storage for a PSP Go).
2. Copy `EBOOT.PBP` there.
3. From the repository root, extract your own Sony Ericsson K800i 1.4.20 JAR:

   ```sh
   python3 psp/prepare_data.py God-of-War-Betrayal_K800i_v1420.jar /path/to/PSP/GAME/GOWBETRAYAL
   ```

4. Optionally copy `INTRO.BIN` next to `EBOOT.PBP`.
5. Launch from the PSP menu, or open `EBOOT.PBP` with **File → Load** in PPSSPP.

A firmware that allows unsigned homebrew is required. Saves appear in `saves/` next to the executable. Full documentation, controls and build instructions are in the repository [README](../README.md).
