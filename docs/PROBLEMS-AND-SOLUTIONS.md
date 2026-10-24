# Troubleshooting

Each entry gives the symptom, the cause and the fix. Log files are described in [CARD-TESTING.md](CARD-TESTING.md).

## Identifying the device

**A card image does not boot at all**
- Cause: the image was built for another SoC (Hi3798 or Rockchip).
- Fix: identify the board by reading the SPI flash strings (see HARDWARE.md).

**flashrom does not recognize the chip**
- Cause: the XM25QH64A is only known to flashrom 1.6 and newer.
- Fix: build flashrom from source with only `ch341a_spi`.

**CH341 fails with `LIBUSB_TRANSFER_TIMED_OUT`**
- Cause: flashrom was killed midway and left the programmer stuck.
- Fix: replug the USB.

**The programmer may damage the board**
- Cause: the black CH341B sends 5 V on its data lines and has no switch.
- Fix: use a green CH341A v1.7 with a voltage switch or a Raspberry Pi Pico with pico-serprog.

## Boot and card

**The factory menu appears instead of ours**
- Cause: the card is exFAT, the card has no partition, or `retro/` is not at the root of the partition.
- Fix: use MBR with one FAT32 partition and copy `retro/` to its root. Formatting is your own step.

**The card's contents disappeared**
- Cause: on a card with no partition, the factory firmware runs `rm -rf /media/sdcarda` when it falls through to the factory menu.
- Fix: always use a partition.

**A boot script fails with "not found"**
- Cause: the stick's busybox has no `head`, `grep`, `dmesg`, `uname`, `stty`, `sed` or `awk`.
- Fix: use only `cat`, `ls`, `ps` and `ash` builtins in scripts. Write the rest in C.

**The stick hangs with no log**
- Cause: `insmod gp_fb.ko` hangs the system.
- Fix: never load it. Draw through `/dev/disp0` and chunkmem.
- To find any hang: write a step mark with `sync` before each risky command. The last line of `log-steps.txt` shows where it stopped.

## Video

**The factory screen shows over the menu**
- Cause: the factory OSD layer stays on above the primary layer.
- Fix: `ioctl 0x40046480` with 0 on open.

**The screen flickers between our menu and the factory one, and the factory menu opens games**
- Cause: the factory menu supervises its emulator process and restarts it.
- Fix: do not bring up the factory menu. Our menu does not need it.

**Slow animation in the menu**
- Cause: full redraw with per-pixel alpha, animation tied to frame count.
- Fix: time-based animation, cached background, a fast path for rectangles, redraw only on change, and `-mcpu=arm1176jzf-s`.

**Slow menu and choppy menu audio**
- Cause: live synthesis in the loop and a background computed at boot.
- Fix: generate music, effects and background on the PC (`tools/gen_audio.c`, `retro/audio/`), and cache the static screen.

**A frame costs far more than expected**
- Cause: code reads from chunkmem.
- Fix: build in normal memory and copy with `memcpy`.

## Audio

**No sound at all, even though `/dev/dsp` accepts everything**
- Cause: the driver sends audio to the analog output by default, and the mixer volume is not set.
- Fix: `ioctl(dsp, 0xc0045007, &1)` before `SETFRAGMENT`, then `SOUND_MIXER_WRITE_VOLUME`. `STICK_AUDIO_OUTPUT=0` selects analog.

**`could not open /dev/dsp` in the log**
- Cause: another process, such as the factory software, holds the device.
- Fix: do not start the factory menu before ours.

**A pop when opening a game**
- Cause: an abrupt cut of the music, plus DMA repeating leftovers from an empty queue.
- Fix: fade the music, fill the whole ring with silence before closing (8192 + 1024 frames), and fade the game's audio in.

**A second loud sound after a click**
- Cause: an echo on the effect.
- Fix: keep effects dry. Use low square waves and bubbles.

**Menu music is much louder or quieter than games**
- Cause: the music was leveled by ear in isolation.
- Fix: level it by the RMS of the loudest console.

**Pops while paused**
- Cause: the audio queue runs dry.
- Fix: keep sending zeros during pause, with the queue near 4000 frames.

**Wrong audio speed or pitch on some core**
- Cause: each core has its own rate (SNES 32040, Mega Drive and Master System 44100, GBA 65536, Game Boy 32768).
- Fix: always open the device at 48 kHz and resample in the runner (base ratio times rate control).

## Pacing

**The game feels half stuck even at full frame rate**
- Cause: irregular cadence. `usleep` has 10 ms resolution, the blocking audio `write` sleeps in steps of 10 to 20 ms, and a swap alone does not wait for the TV.
- Fix: swap, `0x6402`, `0x6407`, with audio non-blocking (`SNDCTL_DSP_NONBLOCK`) and rate control from the queue.

**Smooth, but a freeze every few seconds**
- Cause: statistics written mid-game with `fsync`.
- Fix: no card writes during a game. Use the buffered log call and flush in `plat_shutdown`.

**`pacing: retrace wait does not work, pacing by clock` in the log**
- Cause: the average retrace wait over 300 frames fell outside 0.5 to 30 ms.
- Fix: check the video setup. The game keeps running on the clock meanwhile.

**Choppy audio or audio behind the picture**
- Cause: the queue target does not suit the load.
- Fix: see "Tuning knobs" in OPTIMIZATIONS.md.

## Controllers

**Wrong button map**
- Cause: a map deduced by guessing.
- Fix: press a fixed button sequence on the diagnostics screen, read the log, and update the table in `plat_stick.c`.

**Only some buttons work in a game**
- Cause: the active controller is on the channel the receiver calls player 2.
- Fix: player 1 is the first controller to press a button in the game.

**The system seems to crash when the controllers are off**
- Cause: any logic tied to a controller being present.
- Fix: never depend on a controller. They turn on and off at any time.

**SELECT + START in a game drops to the factory menu**
- Cause: the menu's "START held 3 s" timer counted from before the game.
- Fix: reset the timer on return and ignore START with SELECT held.

**The list runs through several items on a short press**
- Cause: key repeat too fast.
- Fix: repeat starts at 450 ms and steps every 150 ms.

**Empty capture when reading `/dev/ttyS1` with `cat`**
- Cause: `cat` opens the port in canonical mode at the default speed.
- Fix: read it from C, in raw mode at 115200.

**Controller packets flood the log**
- Fix: log only packets that changed. The log has a 1.5 MB cap and is off during a game.

**A button or stick direction cannot be detected**
- Cause: L3 and R3 never arrive, and the right stick is identical to A, B, X and Y.
- Fix: do not design around them. MODE is the only free button.

## Building and cores

**A prebuilt core does not run**
- Cause: those in game collections are x86, those for Rockchip boards are ARMv7 hard-float, and the stick is ARMv6 softfp.
- Fix: build libretro cores from source, statically, with musl, and link them into the runner.

**`libretro.h` not found**
- Cause: `cores/fceumm` is missing. The build takes the header from there.
- Fix: fetch it, even if you do not use that core.

**picodrive builds without CPU options**
- Cause: `CFLAGS` on the command line cancels the `CFLAGS +=` in its Makefile.
- Fix: pass the options inside `CC`, as `cores/picodrive.mk` does.

**Cyclone fails to generate**
- Cause: the generator has to run on the PC.
- Fix: generate `Cyclone.s` first, separately, with the PC compiler.

**Undefined libretro routines when linking picodrive**
- Cause: `STATIC_LINKING=1` drops routines the runner does not have.
- Fix: use `STATIC_LINKING_LINK=1`.

**Broken PC build of picodrive after an ARM build**
- Cause: ARM objects and the `.a` got mixed in.
- Fix: build in a separate copy and move the old `.a` away, as `cores/picodrive.mk` does.

**The development machine overloads**
- Cause: parallel builds, qemu and card copies at the same time.
- Fix: run one heavy task at a time and use `nice` on copies.

## Systems

**GBA runs out of memory**
- Cause: ROMs reach 16 MB and only 55 MB exist.
- Fix: gpsp with `ROM_BUFFER_SIZE=8` and `SMALL_TRANSLATION_CACHE`. The runner does not load the ROM when the core asks for the path (`need_fullpath`).

**Heavy SNES games run slow**
- Cause: emulation exceeds the frame budget.
- Fix: the runner reports the audio queue (`SET_AUDIO_BUFFER_STATUS_CALLBACK`) and the core stops drawing when it runs low (`*_frameskip=auto`).

**WonderSwan runs slow**
- Cause: it runs at 75.47 Hz, and tied to the 59.94 Hz retrace it would lose speed.
- Fix: a core outside 58 to 62 Hz is paced by the clock (`plat_disable_retrace`).

**Vertical games come out sideways**
- Cause: the runner does not rotate the screen.
- Fix: leave them out until it does (see NEXT-STEPS.md).

**A game does not open from a `.zip`**
- Cause: the runner passes the file to the core as it is.
- Fix: use uncompressed ROMs.

## Game data

**Wrong cover or a description of another game**
- Cause: errors in the collection data.
- Fix: review the sheet from `--preview` by eye and add a fix to your fixes file (a libretro cover by catalog name, your own description, and the reason).

**"not in libretro database" in the cover tool output**
- Cause: the ROM's CRC is not catalogued.
- Fix: replace it with a catalogued copy. The old file goes to `trash/`.

**The year is missing**
- Cause: the collection does not have it, and in the `.rdb` it may sit in a separate record with another CRC.
- Fix: `tools/gen_covers.py` joins by catalog name and then by base title. If it is still missing, set `year` in your fixes file.

**A game is translated or runs on the wrong clock**
- Cause: a catalogued copy can be a translation or a 50 Hz European version.
- Fix: look at the catalog name, not just the file name, and use the NTSC version.

**No log file after a game**
- Cause: the stick was powered off mid-game, and statistics stay in memory until exit.
- Fix: quit through MODE > Quit game or SELECT + START.
