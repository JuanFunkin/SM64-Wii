# Super Mario 64 for Wii (homebrew)

A native **Nintendo Wii** homebrew port of Super Mario 64, built on top of the
[sm64-port](https://github.com/sm64-port/sm64-port) project (itself based on the
[n64decomp/sm64](https://github.com/n64decomp/sm64) decompilation).

> **Status: experimental / work in progress.** Expect bugs. Bug reports and pull requests are welcome.

## Features

- Runs natively on the Wii (PowerPC, GX video, AESND audio) as a Homebrew Channel app.
- **2-player co-op** on one screen: Player 2 is a green Mario.
  - There is a single camera that follows Player 1.
  - If Player 2 gets too far away, falls into a pit or dies, they are teleported back to Player 1.
  - Player 2 can collect stars (the level ends for both players), open doors, use warps/pipes and paintings, just like Player 1.
  - Stars, coins, keys and lives are shared.
- **Full-screen widescreen**: fills 16:9 TVs without black bars and without stretching (the field of view is widened instead).
- **No assets are included.** On first launch the app reads *your own* ROM and extracts what it needs into the `extracted` folder, showing a progress bar. Later launches just load the extracted data.
- Save files are stored in the `saves` folder.
- English and Spanish interface (follows the Wii system language, see [Language](#language)).

## Legal notice

This repository contains **no Nintendo ROM, assets or copyrighted game data**. You must provide
your own legally obtained copy of the **US (NTSC-U)** version of Super Mario 64.
This project is not affiliated with or endorsed by Nintendo.

## Installation

1. Download `sm64wii.zip` from the [latest release](../../releases/latest) (the **Releases** section of this repository).
2. Unzip it and copy the `apps/sm64wii` folder to the `apps` folder at the root of your SD card (or USB drive).
3. Put your US ROM in `apps/sm64wii/sm64/`. Any file name works, and `.z64`, `.v64` and `.n64` are all accepted (the ROM must be 8 MB).
4. Launch **Super Mario 64 Wii** from the Homebrew Channel.

Resulting layout:

```
sd:/apps/sm64wii/
├── boot.dol
├── assets.tbl
├── meta.xml
├── icon.png
├── sm64/          <- put your US ROM here
├── extracted/     <- created automatically on first launch
└── saves/         <- save files
```

First launch: *Reading ROM* → *Building* (extracts textures, skyboxes and sound) → *Loading* → game.
Later launches only show *Loading*.

## Controls

| Controller | Layout |
|---|---|
| Wiimote + Nunchuk | Stick: move · A: jump · B: punch/dive · Z: crouch/ground pound · C: R button (camera) · D-pad: C buttons · +: Start · −: L |
| Classic Controller | Left stick: move · A: jump · B: punch · ZL/ZR: Z · R: R · L: L · Right stick: C buttons · +: Start |
| GameCube controller | Control stick: move · A: jump · B/X: punch · L/R: Z · Z: R · C-stick: C buttons · Start |
| Wiimote alone (sideways) | D-pad: move · 2: jump · 1: punch · A: Z · B: R · +: Start · −: C-up |

Player 1 uses controller 1 and Player 2 uses controller 2.
Press **HOME** (or Start + L + R on a GameCube controller) to exit.

## Language

The interface is in English by default and switches to Spanish when the Wii system language is Spanish.
To force a language, create a `lang.txt` file next to `boot.dol` containing `en` or `es`.

## Building

### With GitHub Actions (no local setup)

The workflow in `.github/workflows` builds the app inside the `devkitpro/devkitppc` container and
publishes `sm64wii.zip` in the `latest` release (it is also attached to the workflow run as an artifact).
No ROM is needed to build.

### Locally

1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `wii-dev` group (devkitPPC, libogc, libfat) and make sure
   `powerpc-eabi-gcc` and `elf2dol` are in your `PATH`.
2. Install `python3`, `gcc`, `g++` and `make` for the host tools.
3. Run:

   ```
   make -j"$(nproc)" TARGET_WII=1 VERSION=us
   ```

   The output is `build/us_wii/boot.dol` and `build/us_wii/assets.tbl`. Both must be placed in the same folder on the SD card.

### How it works

- The game code is compiled without any ROM. Textures, skyboxes, sound banks and the ending cake are
  compiled as zero-filled placeholders of the correct size (see `tools/wii_asset_tool.py`).
- After linking, the tool writes `assets.tbl`, which maps every placeholder in RAM to its location in the ROM.
- At runtime the launcher (`src/pc/wii/wii_main.c`) reads your ROM, writes the data to `extracted/`,
  then copies it over the placeholders before the game starts.
- Wii-specific code lives in `src/pc/wii/` (GX renderer, AESND audio, WPAD/PAD input, launcher and ROM loader).
- The 2-player logic is in `src/game/mario.c` and a few small hooks in the camera, rendering and level code.

## Troubleshooting

- **"assets.tbl not found next to boot.dol"**: `boot.dol` and `assets.tbl` must be in the same folder, inside `apps/` on the SD card
  (for example `sd:/apps/sm64wii/`). The error screen lists where the app looked.
- **"ROM not found"**: put the ROM in the `sm64` folder next to `boot.dol`. It must be the US version and 8 MB.
- **Extraction looks broken or incomplete**: delete the `extracted` folder to force a rebuild on the next launch.
- **Dolphin**: when you open a `.dol` directly, Dolphin only sees its emulated SD card, so the app folder must be inside that virtual SD.

## Known limitations

- Only the US (NTSC-U) ROM is supported.
- Title-screen attract demos are disabled.
- Player 2 camera effects are suppressed, and a few edge cases (for example some dialogs while Player 2 triggers them) may look odd.

## Credits

- **Super Mario 64** © Nintendo. All rights to the original game belong to Nintendo.
- **[n64decomp/sm64](https://github.com/n64decomp/sm64)**: the Super Mario 64 decompilation project and all of its contributors.
- **[sm64-port](https://github.com/sm64-port/sm64-port)**: the native PC port this project is based on, and all of its contributors.
- **[devkitPro](https://devkitpro.org/)**: devkitPPC, libogc and libfat, used to build and run on the Wii.
- Wii port (GX renderer, audio, input, launcher, 2-player mode): this repository's contributors.

The original sm64-port README is kept in [`README.sm64-port.md`](README.sm64-port.md).
