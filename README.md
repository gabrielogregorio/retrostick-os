<div align="center">

# RetroStick OS

**A custom retro gaming system for the GPL32900B Game Stick 4K. Runs entirely from the SD card, nothing is written to the flash.**

![C](https://img.shields.io/badge/C-11-00599C?style=for-the-badge&logo=c&logoColor=white)
![libretro](https://img.shields.io/badge/libretro-cores-555555?style=for-the-badge)
![ARMv6](https://img.shields.io/badge/ARMv6-GPL32900B-0091BD?style=for-the-badge&logo=arm&logoColor=white)
![SDL2](https://img.shields.io/badge/SDL2-preview-1D4ED8?style=for-the-badge)
![pytest](https://img.shields.io/badge/pytest-e2e-0A9EDC?style=for-the-badge&logo=pytest&logoColor=white)
![Safe](https://img.shields.io/badge/flash-never%20written-2E7D32?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-blue?style=for-the-badge)

<br>

<img src="docs/demo.gif" alt="The menu running on mock games: carousel of systems, game list with cover and details, favorites and the controller diagnostics screen" width="480">

<sub>Recorded from the real menu with invented games (`make demo`).</sub>

</div>

A retro gaming system (menu plus emulators) for the **Game Stick 4K** built on a **GeneralPlus GPL32900B** board.

It runs **entirely from the SD card** and writes nothing to the device's SPI flash. Remove the card and the stick goes back to its factory menu.

It provides an Aero-themed menu with glass, bubbles and 8-bit music, a game list with cover, year, genre and description, favorites, a controller diagnostics screen, and a pause menu with save states.

## Is this your stick?

RetroStick OS targets one specific board, and sticks sold under similar names use other chips. Check the system and the hardware below before you start.

| | Compatible stick |
|---|---|
| Product | **Game Stick 4K Ultra HD**, HDMI stick with 2 wireless controllers and a USB receiver (the LITE version and other boards are not the target) |
| Factory system | Own **"10 in 1"** menu on Linux 2.6.32, loaded from the SPI flash, no Android and no EmuELEC |
| SoC | **GeneralPlus GPL32900B**, ARM1176 (ARMv6 + VFP); the marking on the chip may be scratched or re-marked |
| Memory | 64 MB RAM, 8 MB SPI flash (XMC XM25QH64A) |
| Video | HDMI, logical panel 720x480 at 59.94 Hz, even when the box says 4K |
| Card | SD with MBR and one FAT32 partition; exFAT does not mount |

**Does not apply:** sticks with Hi3798 or Rockchip chips, or any system based on Android, EmuELEC, GStickOS or similar. Their images do not boot here, and this project does not boot there.

**Safe check, no opening and no flashing:** prepare a card as described in [Preparing the card](#preparing-the-card) and boot the stick with it. On a compatible stick the menu comes up, and `retro/log-steps.txt` appears on the card. Without the card, the stick returns to its factory menu. If nothing changes and that file is not created, the board is a different one.

**Sure check:** with the stick open, read the SPI flash in read-only mode (`tools/re/read_spi.sh`). A compatible dump contains the strings `GPCV2149 SPI 1st bootloader` and `GPL329xxB linux bootloader`. See [docs/HARDWARE.md](docs/HARDWARE.md).

## Requirements

**Hardware**

- A Game Stick 4K with a GeneralPlus GPL32900B board. Images made for Hi3798 or Rockchip sticks do not run on it, and this project does not run on those. Nothing is written to the device, so trying a card is a safe way to find out which board you have. See [docs/HARDWARE.md](docs/HARDWARE.md).
- An SD card with an MBR partition table and one FAT32 partition. exFAT does not mount on the stick.

**PC (Debian or Ubuntu package names)**

```bash
sudo apt install build-essential make curl libsdl2-dev zlib1g-dev \
    qemu-user-static python3 python3-pil fonts-noto
```

`src/font_data.h` is not in the repo. `tools/gen_font.py` generates it during the build from the system Noto Sans font, which is why `python3-pil` and `fonts-noto` are required.

## Getting the toolchain and cores

Neither is stored in the repo. Fetch them into the project folder.

**ARM toolchain** (GCC 11.2 with musl):

```bash
mkdir -p toolchain && cd toolchain
curl -LO https://musl.cc/arm-linux-musleabi-cross.tgz
tar xzf arm-linux-musleabi-cross.tgz
cd ..
```

**Emulator cores** (each folder name must match exactly). The build also takes `libretro.h` from `cores/fceumm`, so that core is required even if you never use it.

```bash
cd cores
git clone https://github.com/libretro/QuickNES_Core quicknes
git clone https://github.com/libretro/libretro-fceumm fceumm
git clone https://github.com/libretro/snes9x2002 snes9x2002
git clone --recursive https://github.com/libretro/picodrive picodrive
git clone https://github.com/libretro/gpsp gpsp
git clone https://github.com/libretro/gambatte-libretro gambatte
git clone https://github.com/libretro/stella2014-libretro stella2014
git clone https://github.com/libretro/RACE race
git clone https://github.com/libretro/beetle-wswan-libretro wswan
cd ..
```

The result looks like this:

```
retrostick-os/
├── Makefile
├── src/
├── tools/
├── card-base/retro/init
├── toolchain/
│   └── arm-linux-musleabi-cross/bin/arm-linux-musleabi-gcc
├── cores/
│   ├── core.mk  quicknes.mk  fceumm.mk  picodrive.mk
│   ├── quicknes/  fceumm/  snes9x2002/  picodrive/  gpsp/
│   └── gambatte/  stella2014/  race/  wswan/
└── roms/
    ├── nes/your-game.nes
    └── snes/your-game.sfc
```

## Building

```bash
make run     # builds for the PC and opens the menu in a 1280x720 window
make arm     # static binaries for the stick (menu-arm, nes-arm, snes-arm, ...)
make all     # PC and ARM
make smoke-arm  # runs the ARM menu in qemu-arm-static, headless, saves out/stick-shot.png
make card    # assembles out/card/retro/
```

To rebuild from scratch, use `make -B`.

To run one emulator on the PC without the menu:

```bash
./out/nes-pc roms/nes/your-game.nes
```

| Key | Button | Key | Button |
|---|---|---|---|
| Arrows | D-pad | Q / W | L / R |
| Z | A | Right Shift | SELECT |
| X | B | Enter | START |
| A | X | M | MODE |
| S | Y | Esc | Quit |
| F2 | Pretend controller 2 connected | | |

Any USB or Bluetooth controller also works on the PC, and its GUIDE button acts as MODE.

## Tests

No test opens a window or needs an emulator core or a ROM: the e2e layer runs the real menu and runner on SDL's dummy drivers, with a mock core (`tests/mock/mock_core.c`) and invented games (`tests/mock/mock_roms.py`).

```bash
python3 -m venv --system-site-packages .venv
.venv/bin/pip install -r requirements-dev.txt
./run-tests.sh        # lint, unit and e2e, ends with ALL GREEN
```

| Layer | Command | What it covers |
|---|---|---|
| Unit, C | `make test-unit` | drawing, menu helpers, favorites, state file, controller packets, audio, with ASan and UBSan |
| Unit, Python | `.venv/bin/python -m pytest tests/python` | `.rdb` reader, cover title matching, log summary |
| E2E | `make test-bins`, then `.venv/bin/python -m pytest tests/e2e` | menu navigation, favorites, launching a game, pause menu, save states, restart, quit |
| ARM | `make out/menu-arm`, then the same pytest | the stick build under qemu; skipped when the binary is missing |

Any scripted run works by hand too:

```bash
SDL_VIDEODRIVER=dummy STICK_ROMS=roms STICK_KEYS=R,D,D,A STICK_SHOT=out/shot.ppm out/test/menu-sdl
```

`STICK_KEYS` takes `U D L R A B X Y S T M`, one key every 15 input reads, and `.` for a pause. `make demo` records `docs/demo.gif` the same way.

## CI

GitHub Actions runs on `dev`, `main` and pull requests into `main`:

- **Tests:** C and Python unit tests, e2e on SDL, and the ARM menu built with `gcc-arm-linux-gnueabi` and run under qemu.
- **Lint:** ruff, shellcheck, C without warnings, pinned actions, valid YAML, a light GIF, and no ROM, firmware or core in the repo.
- **CodeQL:** C, Python and the workflows.
- **Release:** merging a pull request into `main` creates the tag `vX.Y.Z` from `VERSION` and a release with the source tarball. Each pull request must bump `VERSION`.

## Preparing the card

1. Use a card that already has an MBR partition table and one FAT32 partition. This project never formats anything. Do that yourself with your usual tools if the card needs it.
2. Run `make card` and copy `out/card/retro/` to the root of the partition.
3. Put your games in `retro/roms/<system>/`, in the folders listed under [Supported systems](#supported-systems).
4. Optionally add covers (see [Covers and game details](#covers-and-game-details-optional)).
5. Run `sync`, eject the card, insert it in the stick and power on. The menu comes up directly.

The card looks like this:

```
/ (root of the FAT32 partition)
└── retro/
    ├── init                  boot script
    ├── menu-arm              menu
    ├── nes-arm               emulators, one binary per system
    ├── snes-arm
    ├── md-arm  sms-arm  gba-arm  gb-arm  gg-arm  a26-arm  ngp-arm  ws-arm
    ├── audio/                prebuilt menu music and effects
    ├── roms/
    │   ├── nes/
    │   │   ├── your-game.nes
    │   │   └── .meta/        covers and game details (optional)
    │   └── snes/
    │       └── your-game.sfc
    └── saves/                battery saves and save states
```

The stick creates `log-*.txt`, `favorites.txt`, `menu-state.txt` and `launch.txt` inside `retro/` as it runs.

**Safety rules for the card:**

- Never put a file named `SPI_ROM.bin` on the card. The factory firmware rewrites the flash from it.
- Keep the partition. On a card with no partition, the factory firmware deletes the card's contents when it falls through to the factory menu.

Use uncompressed ROMs. The runner passes the file to the core as it is, and the cover tool identifies a game by the checksum of the raw file. Prefer NTSC versions with no translation patches: the TV runs at 59.94 Hz and 50 Hz versions lose the smooth retrace pacing.

## Controls on the stick

| Where | Button | Action |
|---|---|---|
| Menu | D-pad | Navigate (the list wraps around) |
| Menu | L / R | Switch system |
| Menu | A | Open the game |
| Menu | Y | Add or remove a favorite |
| Menu | SELECT | Controller diagnostics |
| Menu | START held for 3 s | Close the menu and open the factory menu |
| Game | MODE | Pause menu |
| Game | SELECT + START held for 1 s | Return to the menu, with an automatic save |

The pause menu offers: Resume, Save to slot 1, Save to slot 2, Load slot 1, Load slot 2, Back to last save, Restart game, Quit game. B or MODE closes it. Restart asks for confirmation and deletes no saves.

Controllers can be turned on and off at any time. Player 1 is the first controller to press a button in the game.

## Covers and game details (optional)

`tools/gen_covers.py` writes one compact cover and one details file per game into `roms/<system>/.meta/`.

```bash
python3 tools/gen_covers.py --collection <folder> [--rdb <folder>] [--preview sheet.png] \
    [--download] [--fixes <file>]
```

| Option | Meaning |
|---|---|
| `--collection` | `retro/` folder of a game collection in the RetroStation layout (`cache/games.json` and `assets/games/`). Read only |
| `--rdb` | Folder with the libretro `.rdb` databases, used for year, genre and developer. Defaults to a flatpak RetroArch install |
| `--preview` | Saves a sheet with every generated cover, for review by eye |
| `--download` | Fetches missing covers from libretro-thumbnails, one request at a time, and stops at the first block. Without it, only the local cache in `cache/libretro-covers/` is used |
| `--fixes` | Optional manual fixes file, owned by you and not part of the repo. Defaults to `cache/cover-fixes.json` |

The fixes file maps a ROM file name to the fields to override:

```json
{
  "your-game.nes": {
    "description": "A short description.",
    "year": 1990,
    "source": "libretro",
    "cover_name": "Your Game (USA)"
  }
}
```

Keys: `description`, `year`, `image` (a path inside the collection's `assets/games/`), `cover_name` (catalog name in libretro-thumbnails), `source` (`libretro` ignores the collection's cover), `no_cover`, `reason` (a note for yourself).

Copy each `.meta/` folder along with the games: `retro/roms/<system>/.meta/`.

## Project layout

```
src/main.c          menu process: startup, input loop, launching
src/catalog.c       systems, game scan, favorites, menu state
src/details.c       game details and covers
src/menu_view.c     menu drawing
src/menu_audio.c    music and effects playback
src/aero.c          Aero theme: sky, bubbles, glass
src/icons.c         glass bubbles with the console drawn inside
src/gfx.c           software drawing: rectangles, gradients, text with alpha
src/music.c         synthesized music and effects (PC only)
src/chip.c          8-bit synthesizer (PC only)
src/runner.c        game process: libretro core, main loop
src/runner_audio.c  resampling, rate control, fade-in
src/runner_saves.c  SRAM and save states
src/runner_pause.c  pause menu logic
src/pause.c         pause menu drawing
src/plat.h          platform layer: the menu and the runner talk only to it
src/plat_stick.c    stick platform: init, log, clock
src/stick_*.c       stick video, game frames, controllers, audio
src/plat_sdl.c      PC platform: SDL2
src/plat_exec.c     launches a game
src/font_data.h     generated at build time, not in the repo
card-base/retro/    boot script for the card
cores/              build recipes; core sources are fetched here, not in the repo
roms/               your games, not in the repo
tools/              covers, font and audio generators, log analysis, re/
tests/              unit, Python and e2e tests, mock core and mock games
docs/               hardware, troubleshooting, techniques, card testing, next steps
```

Full documentation:

- [docs/HARDWARE.md](docs/HARDWARE.md): board, boot flow, video, audio and controller interfaces.
- [docs/PROBLEMS-AND-SOLUTIONS.md](docs/PROBLEMS-AND-SOLUTIONS.md): troubleshooting guide.
- [docs/OPTIMIZATIONS.md](docs/OPTIMIZATIONS.md): performance techniques and the principle behind each.
- [docs/CARD-TESTING.md](docs/CARD-TESTING.md): how to test a build on a card and read the logs.
- [docs/NEXT-STEPS.md](docs/NEXT-STEPS.md): what should change next, with the goal of each change.
- [CLAUDE.md](CLAUDE.md): rules and working notes for agents.

## Third-party components

This repo contains only original code. It documents hardware interfaces (ioctl numbers, packet format) for interoperability. Everything else stays outside it:

| Component | Where it comes from |
|---|---|
| Emulator cores | Fetched by you into `cores/<name>/`, each under its own license |
| `libretro.h` | Comes with the cores |
| Noto Sans | SIL OFL; used at build time from the system font, not stored in the repo |
| Factory firmware | Never stored in the repo |
| ROMs | Supplied by you, never included |

## License

The code in this repository is under the [MIT License](LICENSE). It covers only this repository. Each emulator core keeps its own license: a binary linked with a GPL core must follow the GPL, and cores under non-commercial terms, such as snes9x2002 and picodrive, cannot be sold. ROMs and the factory firmware are not covered.

## Supported systems

| System | Folder | Extensions | Core |
|---|---|---|---|
| NES | `nes` | `.nes` | QuickNES |
| Super Nintendo | `snes` | `.sfc` `.smc` | snes9x2002 |
| Mega Drive | `md` | `.md` `.gen` `.bin` | picodrive |
| Master System | `sms` | `.sms` | picodrive |
| Game Gear | `gg` | `.gg` | picodrive |
| Game Boy Advance | `gba` | `.gba` | gpsp |
| Game Boy / Color | `gb` | `.gb` `.gbc` | gambatte |
| Atari 2600 | `a26` | `.a26` `.bin` | stella2014 |
| Neo Geo Pocket / Color | `ngp` | `.ngp` `.ngc` | RACE |
| WonderSwan / Color | `ws` | `.ws` `.wsc` | beetle-wswan |
