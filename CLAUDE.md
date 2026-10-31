# CLAUDE.md - RetroStick OS

A retro gaming system for the **Game Stick 4K** with a **GeneralPlus GPL32900B** board (not Rockchip, not Hi3798). It has an Aero-themed menu with 8-bit music and a pause menu with saves. It runs **entirely from the SD card** through the `retro/init` hook in the factory firmware, and **nothing is written to the SPI flash**. Remove the card and the stick goes back to its factory menu.

This file is the starting point for any agent. User instructions are in `README.md`, details are in `docs/`.

---

## 1. Absolute rules

1. **Never write to the stick's SPI flash.** No `flashrom -w` or `-E`.
2. **Never put `SPI_ROM.bin` on the card.** The factory firmware rewrites the flash with that file after "Press START".
3. **Never format or repartition** a card, USB drive or hard drive. On a test card, touch only what is inside `retro/`.
4. **Never load `gp_fb.ko`** on the stick. It hangs the whole system at `insmod`.
5. **Never use a strategy that only works in some games.** Everything must hold for any game on any system.
6. **One heavy process at a time** on the development machine. Parallel builds, qemu and card copies together overload it. No heavy test batteries, and copies to the card use `nice`.
7. **Git: commit is allowed, push never.** Commit messages are one simple line in English, with no body and no co-author. Out of git: `roms/`, `cores/*/`, `toolchain/`, `out/`, `firmware/`, `trash/`, `cache/`, `src/font_data.h`.
8. **No `rm`.** Whatever goes away moves to `trash/` with one line in `trash/README.txt`, because `rm` with an empty variable destroys a system and `mv` can be undone. To rebuild from scratch use `make -B`.
9. **An external game collection is read and copy only.** Nothing is moved, renamed or deleted there.
10. **English everywhere:** code, identifiers, UI text, docs and commit messages. No em dashes.

---

## 2. How to get it working

### 2.1 PC setup

Packages: `build-essential`, `make`, `curl`, `libsdl2-dev`, `zlib1g-dev`, `qemu-user-static`, `python3`, `python3-pil`, `fonts-noto`.

`src/font_data.h` is generated at build time by `tools/gen_font.py` from the system Noto Sans font.

The ARM compiler (GCC 11.2 with musl) goes in `toolchain/`:

```bash
mkdir -p toolchain && cd toolchain
curl -LO https://musl.cc/arm-linux-musleabi-cross.tgz && tar xzf arm-linux-musleabi-cross.tgz
```

The libretro cores go in `cores/<name>/`: `quicknes`, `fceumm`, `snes9x2002`, `picodrive`, `gpsp`, `gambatte`, `stella2014`, `race`, `wswan`. The clone commands are in `README.md`. Each one builds through `cores/core.mk` (variables `EMU_CORE` and `ARCH`) or its own `.mk` (`quicknes.mk`, `fceumm.mk`, `picodrive.mk`) into a static `.a` linked into the runner. `fceumm` is always required: the build takes `libretro.h` from it.

### 2.2 Build

```bash
make run        # menu in a window on the PC (1280x720), games through out/<system>-pc
make arm        # static binaries for the stick (menu-arm, nes-arm, snes-arm, ...)
make all        # PC and ARM
make smoke-arm  # ARM menu in qemu-arm-static, headless, saves out/stick-shot.png
./run-tests.sh  # lint, unit and e2e, no window, no core, no ROM
make card       # assembles out/card/retro/ (binaries, audio/, init, saves/)
```

`out/nes-fceumm-arm` is an optional alternative NES runner, more accurate and heavier. It is not part of `make card`.

PC keys: arrows = D-pad, Z = A, X = B, A = X, S = Y, Q/W = L/R, Right Shift = SELECT, Enter = START, M = MODE (or the controller's GUIDE), Esc = quit, F2 = pretend controller 2. Any USB or Bluetooth controller works.

### 2.3 Preparing the card

1. The card needs **MBR with one FAT32 partition**. exFAT does not mount on the stick, and a card without a partition gets `rm -rf` from the factory firmware. Formatting is done by the user, never by an agent.
2. Copy `out/card/retro/` to the root of the partition.
3. Games go in `retro/roms/<system>/`, covers and details in `retro/roms/<system>/.meta/`.
4. `sync`, eject, power on the stick. `retro/init` brings up the menu directly.

### 2.4 Test cycle

Full procedure: `docs/CARD-TESTING.md`. In short: test on the PC, `./run-tests.sh`, `make smoke-arm`, `make card`, archive the old logs on the card, copy only what changed, verify each file with `cmp`, `sync`, run a fixed script on the stick, read the logs with `tools/analyze_logs.py`.

Quit a game through MODE > Quit game or SELECT + START. Statistics stay in memory and only reach the card on exit.

---

## 3. How the system works

### 3.1 Boot

The factory firmware mounts the card (vfat only). **If `/media/sdcarda1/retro` exists, it mounts that folder on `/usr/retro` and runs `/usr/retro/init` as root.** Otherwise it runs the factory menu.

`card-base/retro/init` is a loop:

```
menu-arm  --- exits with code 10 ---> reads launch.txt (runner, game, saves folder)
   ^                                    |
   +---- game exits, menu reopens ------+   (returns to the same spot through menu-state.txt)
menu exits with 0 (START held 3 s) ---> brings up the factory menu
```

The menu **exits** to open a game so the game gets all the RAM and CPU, of which there are only 55 MB. On the stick a game **never** runs through a fork from inside the menu. If `launch.txt` cannot be written, the menu logs the errno and opens nothing.

### 3.2 Code map

| File | Role |
|---|---|
| `src/main.c` | Menu: system carousel, list with cover and details, favorites, diagnostics, music fade, launching |
| `src/aero.c`, `src/icons.c` | Aero theme: sky, glass bubbles with the console drawn inside |
| `src/gfx.c`, `src/font_data.h` | Software drawing: rounded rectangle, gradient, text with alpha. The font is generated at build time |
| `src/music.c`, `src/chip.c` | 8-bit synthesizer, used **only on the PC** to generate files |
| `tools/gen_audio.c` | Generates `retro/audio/` on the PC: music, `sfx-*.pcm` effects, prebuilt background. The stick only reads |
| `src/runner.c` | Runner: statically linked libretro core, resampling to 48 kHz, rate control, frame skip, saves, pause menu |
| `src/pause.c` | Draws the pause menu over the game's last frame, darkened |
| `src/plat.h` | Platform layer: the menu and the runner talk only to it |
| `src/plat_stick.c` | Stick: `/dev/disp0` + chunkmem, `/dev/ttyS1`, `/dev/dsp`, log |
| `src/plat_sdl.c` | PC: SDL2 |
| `src/plat_exec.c` | Writes `launch.txt` and exits (stick) or launches the runner as a process (PC) |
| `tools/gen_covers.py`, `tools/rdb.py` | Covers and details. Options: `--collection`, `--rdb`, `--preview`, `--download`, `--fixes`. The fixes file is optional, user-owned, defaults to `cache/cover-fixes.json` and is not in the repo. Keys: `description`, `reason`, `cover_name`, `source`, `image`, `year`, `no_cover` |
| `tools/re/` | Read-only analysis tools |

### 3.3 Systems

| System | Core | Note |
|---|---|---|
| NES | QuickNES | Light enough for full speed; FCEUmm is available as an alternative |
| SNES | snes9x2002 | The heaviest; automatic frame skip |
| Mega Drive / Master System / Game Gear | picodrive | Cyclone 68000 and DrZ80 in ARM assembly |
| GBA | gpsp | ARM dynamic recompiler, ROM paged within 8 MB (`need_fullpath`) |
| Game Boy / Color | gambatte | |
| Atari 2600 | stella2014 | |
| Neo Geo Pocket / Color | RACE | DrZ80 in ARM assembly |
| WonderSwan / Color | beetle-wswan | Runs at 75.47 Hz, so it is paced by the clock, not the retrace |

**Adding a system:**

1. Put the code in `cores/<core>/`.
2. In the Makefile, add `EMU_<sys> := <core>` and the name to `SYSTEMS`.
3. Add the system and its `roms/<sys>/` folder to `SYSTEMS` in `src/main.c`.
4. Add core options to the table in `runner.c`.
5. Add the system to `tools/gen_covers.py`.

A prebuilt core from another board does not work. Those in game collections are x86, those for Rockchip boards are ARMv7 hard-float, and the stick is ARMv6 softfp.

### 3.4 Controllers

The receiver is on UART `/dev/ttyS1`, 115200 8N1. Packet: `0A 09 PP 32 31 ID B6 B7 CK` (PP 02/03 = channel, ID = physical controller, CK = `~(XOR(0..7) ^ 0x5A)`).

- B6: bits 0-7 = up, down, left, right, A, B, Y, X.
- B7: bit 0 START, 1 SELECT, 2 L1 **and** L2, 3 R1 **and** R2, 4 MODE.
- **There are no analog values.** The left stick becomes the D-pad. The right stick sends the same bits as the buttons (up = Y, down = A, left = B, right = X) and cannot be told apart from them.
- **L3 and R3 never arrive.** MODE switches no mode. It is a free button, used for pause.
- Player 1 is the first controller to press a button in the game. Controllers turn on and off at any time, and nothing depends on a controller being on.

| Where | Button | Action |
|---|---|---|
| Menu | D-pad | Navigate (the list wraps) |
| Menu | L / R | Switch system |
| Menu | A (alone) | Open the game |
| Menu | Y | Favorite or unfavorite; favorites tab |
| Menu | SELECT | Controller diagnostics |
| Menu | START 3 s | Close the menu and bring up the factory menu |
| Game | MODE | Pause menu |
| Game | SELECT + START 1 s | Return to the menu, with autosave |

### 3.5 Pause menu and saves

Items: Resume, Save to slot 1, Save to slot 2, Load slot 1, Load slot 2, Back to last save, Restart game, Quit game. B or MODE closes it. After saving it closes by itself in 700 ms. **Restart asks for confirmation** (A again restarts, B or moving the cursor cancels) and deletes no saves.

Files in `retro/saves/`: `<game>.srm` (battery), `.st1`, `.st2`, `.sta` (automatic on exit) and `.last` (which one was written last, because the stick has no clock). Everything goes through `retro_serialize` and `retro_unserialize`, with `fsync` only during pause, never mid-game. While paused the audio receives zeros, with the queue kept near 4000 frames, to avoid pops.

The saves folder is also what the runner reports to cores as the system directory.

### 3.6 Video

- `/dev/disp0` + `/dev/chunkmem`, logical panel **720x480**, TV at **59.94 Hz**. ioctls in `docs/HARDWARE.md`.
- Game pacing comes from the **TV retrace**: buffer swap, then `0x6402`, then `0x6407`.
- The menu fills the screen at 720x480. Games use integer scale k, with a line built once and copied k times.
- **chunkmem is very slow to read.** Build the line in normal memory and copy it with `memcpy`.
- Frame skip: up to 2 in a row, when the clock falls more than 2 ms behind or the audio queue runs low. snes9x2002, gpsp and picodrive use libretro's `frameskip=auto`, driven by the audio queue.
- A core outside 58 to 62 Hz is paced by the clock (`plat_disable_retrace`).

### 3.7 Audio

- `gp_audio.ko` outputs to **analog** by default. For HDMI use `ioctl(dsp, 0xc0045007, &1)` **before** `SETFRAGMENT`, plus volume on `/dev/mixer`.
- Always **non-blocking** (`SNDCTL_DSP_NONBLOCK`; `O_NONBLOCK` on open is ignored), with dynamic rate control from the queue (`GETODELAY`, up to +/-0.8 %, target 2000 audio frames). The device opens at 48 kHz and the runner resamples each core's rate.
- The ring is 32 x 1 KB. **With an empty queue the DMA repeats old leftovers**, so before closing the device fill the whole ring with silence (8192 + 1024 frames).
- Opening a game: the menu music fades down over 0.4 s, the soft enter effect plays, the message shows, the ring fills with silence, the device closes. The game's audio rises from 0 to normal in 0.6 s (`FADE_IN` in `runner.c`).
- Effects have no echo. No high chimes: low square waves and bubbles only.

### 3.8 Logs on the card (`retro/`)

| File | Written by | Content |
|---|---|---|
| `log-steps.txt` | `init` | Every step of the boot and of each game, with `sync` |
| `log-stick.txt` | Menu and runner | Video and audio steps, fps, ms per part, skips, controller packets (`raw:` for bytes outside the format, 1.5 MB cap) |
| `log-menu.txt` | Standard output | Errors, `LAUNCH ...` |

Environment variables are listed in `docs/CARD-TESTING.md`.

---

## 4. Rules learned

### Hardware and firmware

- Identify the SoC before anything else, because images for other processors never boot. The SPI flash strings are the reliable source.
- Build everything on the `retro/init` hook, because it gives root without touching the flash.
- Do not bring up the factory menu alongside ours, because it watches its own emulator process and restarts it.
- Use only `cat`, `ls`, `ps` and `ash` builtins in boot scripts, because the stick's busybox has no `head`, `grep`, `dmesg`, `uname`, `stty`, `sed` or `awk`. Everything else goes in C.
- Build binaries static with musl and `-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp -no-pie`, because that is the only ABI the board runs.

### Pacing and performance

- Never use `usleep` for fine pacing, because with HZ=100 its resolution is 10 ms.
- Never let audio hold the pacing, because the blocking `write` sleeps in steps of 10 to 20 ms (`msleep(1)` in the driver). The retrace holds the pacing.
- Never write to the card during a game, because repeated `fsync` on the SD's FAT32 freezes the picture for many frames. Statistics go to a buffer and are written on exit.
- Never read from chunkmem. Write to normal memory and copy in blocks.
- Precompute on the PC whatever can be precomputed (`gen_audio`), because live synthesis in the menu uses up the frame budget.
- Check the logs before optimizing. The runner already reports ms of emulation, video and audio, skips and frame intervals.

### Audio

- When there is no sound at all, check the output route first (analog versus HDMI), then the mixer volume.
- Avoid pops when opening a game with a fade, a ring full of silence and a soft start, because the cause is an abrupt cut plus DMA repeating leftovers.
- Level the menu music to the loudest console, using the games' RMS, so the volume does not jump between menu and game.

### Controllers

- Map buttons with a fixed sequence and read the log, never deduce. Press one button at a time with a pause, because that is what separates MODE from L3 and R3.
- Do not try to swap B and X on the right stick. It sends no position, so any swap also swaps the physical buttons.

### Games and data

- Do not trust collection data, because it contains swapped covers, descriptions of another game, translations and 50 Hz PAL versions. Check the catalog name in the libretro database and review the cover sheet by eye. Corrections go in the user's fixes file.
- Join the year from the libretro `.rdb` by catalog name, because the year often lives in a separate record with another CRC.
- Fit GBA in 55 MB with gpsp's `ROM_BUFFER_SIZE=8` and the ROM paged by path.
- For picodrive: `CFLAGS` on the command line cancels the `+=` in its Makefile, so pass the CPU options inside `CC`. Generate Cyclone on the PC first, and use `STATIC_LINKING_LINK=1`.
- Leave out vertical games until the runner rotates the screen, because they come out sideways.

### Process

- Log every risky step on the stick **with fsync before** it runs (`step()`), so the last line says where a hang happened.
- Use polling for long tasks: run them in the background and check periodically.
- Run one test at a time on the stick, with a fixed script.
- Simulated keys in qemu count input reads, not frames, because otherwise they would not advance while the game is paused.
- Verify the copy to the card with `cmp` before `sync`.

Troubleshooting: `docs/PROBLEMS-AND-SOLUTIONS.md`. Techniques: `docs/OPTIMIZATIONS.md`. Planned changes: `docs/NEXT-STEPS.md`.
