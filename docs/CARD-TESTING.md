# Testing a build on a card

Use a regular card with MBR and one FAT32 partition. Touch only what is inside `retro/`. Never format the card and never put `SPI_ROM.bin` on it.

## Step by step

1. Change the code and try it on the PC:
   ```bash
   make run
   ./out/nes-pc roms/nes/your-game.nes
   ```
2. Run the ARM menu in qemu. This uses simulated keys and saves `out/stick-shot.png`:
   ```bash
   make test
   ```
3. To try an ARM emulator in qemu:
   ```bash
   STICK_KEYS="M,U,U,A" STICK_SHOT=out/shot.ppm STICK_SERIAL=/dev/null \
     qemu-arm-static ./out/snes-arm roms/snes/your-game.sfc out
   ```
   Empty items in `STICK_KEYS` wait one slot, which lets a game reach a given screen before the next key:
   ```bash
   K=$(python3 -c "print(','.join(['']*20+['T']+['']*25))")
   STICK_KEYS="$K" STICK_SHOT=out/nes-shot.ppm STICK_SERIAL=/dev/null \
     qemu-arm-static ./out/nes-arm roms/nes/your-game.nes out
   ```
4. Assemble the card files:
   ```bash
   make card
   ```
5. On the mounted card, move the old `retro/log-*.txt` to `retro/old-tests/<round>/`.
6. Copy only what changed, for example `init`, `menu-arm`, `nes-arm`, with `nice`. Verify **each file with `cmp`**, then `sync`:
   ```bash
   nice cp out/card/retro/menu-arm <card>/retro/
   cmp out/card/retro/menu-arm <card>/retro/menu-arm
   sync
   ```
7. Optionally start the log watcher. It polls every 10 s, waits for the card to leave the PC and come back, copies the logs to `out/logs-<time>/` and runs the analysis:
   ```bash
   tools/wait_card.sh <card mount point>
   ```
8. Eject the card and run one test on the stick, with a clear script: a fixed button order and a known way to quit.
9. Bring the card back and read the logs.

**Quit the game through MODE > Quit game or SELECT + START.** Statistics stay in memory and only reach the card on exit, so powering off mid-game loses them.

## Reading the logs

| File in `retro/` | Written by | Content |
|---|---|---|
| `log-steps.txt` | `retro/init` | Every boot and game step, with `sync` |
| `log-stick.txt` | Menu and runner (`STICK_LOG`) | `step:` lines, statistics and controller packets |
| `log-menu.txt` | Standard output of menu and runner | `LAUNCH ...`, error messages |

To summarize the steps and each controller's button sequence:

```bash
python3 tools/analyze_logs.py <folder with the logs>
```

### Where it stopped

Read the last line of `log-steps.txt`, then the last `step:` line of `log-stick.txt`. Steps are written with `fsync` before the risky call runs, so the last line says where a hang happened.

The normal sequence is:
- `log-steps.txt`: `1 start, opening the menu without 10in1`, then `game: <path>` and `game exited (code N)` for each game.
- `log-stick.txt`: `disp: panel 720x480`, `disp: chunkmem ok`, `disp: ready (OSD off)`, `audio: output hdmi 1`, `audio: requested rate 48000, accepted ...`, `audio: mixer volume ...`, `game: first frame`.

### Game statistics in `log-stick.txt`

| Line | How to read it |
|---|---|
| `game: pacing N` | 0 = clock, 1 = TV retrace, 2 = audio. A game near 60 Hz should show 1 |
| `game: core frames per second (x100) N` | The core's own rate. Outside 58 to 62 Hz it is paced by the clock |
| `game: fps N` | Frames per second over the run |
| `game: ms/frame emulation (x10) N` | Core time. Above about 16.7 ms the game cannot hold full speed without skipping |
| `game: ms/frame video (x10) N` | Scaling and swap, and it includes the retrace wait |
| `game: ms/frame audio (x10) N` | Time in the audio path |
| `game: skipped frames N` | Frames skipped by the runner |
| `pacing: interval min N max N us` | Frame interval extremes. A smooth game stays close to 16,683 us |
| `pacing: frames >20ms N, <10ms N (of 300)` | Bad frames per window. Both should be 0 |
| `pacing: average retrace wait N us (vsync N)` | Time left over in the frame |
| `audio: average queue N frames` | Should settle slightly below the 2000 target |
| `audio: average ratio (x100000) N` | Rate correction in use. It stays small |
| `audio: underruns N` | Should be 0. Each one is an audible dropout |
| `pause: ...` | Pause menu events: opened, saved, loaded, restarted, closed |

A max interval of many frames, repeating every few seconds, means something is writing to the card during the game.

### Controller packets

Lines look like `t=<ms> P1 0A 09 02 32 31 ID B6 B7 CK`, and `raw:` marks bytes outside the packet format. Only packets that changed are logged, and not during a game. Decode B6 and B7 with the table in HARDWARE.md.

## Environment variables (stick and qemu)

Set these in `retro/init` on the stick, or on the command line in qemu.

| Variable | Effect |
|---|---|
| `STICK_ROMS` | Games folder: `/usr/retro/roms` on the stick, `roms` on the PC |
| `STICK_LOG` | Log file |
| `STICK_VIDEO` | `disp` (default) or `fb` (never on the stick: `gp_fb.ko` hangs it) |
| `STICK_SERIAL` | Controller port: default `/dev/ttyS1`, `/dev/null` in qemu |
| `STICK_KEYS` | Headless: simulated keys, one every 15 input reads (`U D L R A B X Y`, `S` = SELECT, `T` = START, `M` = MODE) |
| `STICK_SHOT` | Headless: where to save the last screen as PPM |
| `STICK_VSYNC` | `0` turns off the retrace wait in games, moving the pacing to the clock |
| `STICK_AUDIO_OUTPUT` | `0` selects analog audio instead of HDMI |
| `STICK_MUSIC` | `0` turns off the menu audio |
| `STICK_CLOCK_TEST` | Logs the real duration of `usleep` and of 120 swaps with retrace wait when video opens. Adds about 2 s at startup |
| `STICK_GAME_VIA_INIT` | Set by `retro/init`: the menu writes `launch.txt` and exits instead of launching a process |

Simulated keys count input reads, not frames, so they also advance while the game is paused.

## Test scripts

- **Button map:** on the diagnostics screen (SELECT), press one button at a time, always in the same order: up, down, left, right, A, B, X, Y, L1, R1, L2, R2, SELECT, START. Then repeat on the other controller.
- **Separating buttons with no visible effect:** press each one alone with a 5 s pause, and repeat the first one at the end, so each bit in the log can be matched to a press.
- **Analog sticks:** hold each direction for 2 s with a 3 s pause, then diagonals, 2 rotations and half tilt.
- **Performance:** play one minute of a demanding part of a game, then quit through the pause menu and read the statistics.
- **Saves:** save to a slot, keep playing, load the slot, quit, reopen the game and use "Back to last save".
