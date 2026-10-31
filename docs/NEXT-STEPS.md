# Next steps

In priority order. Every change must hold for any game on any system.

## 1. Hardware scaler

**Goal:** remove software scaling from each game frame and free CPU for the heavier systems.

**What must change:**
- Work out how `/dev/scalar` and `/dev/scale2` are driven, and add them to `docs/HARDWARE.md`.
- Add a scaled output path in `src/stick_frames.c`, with the software path kept as a fallback.
- Check whether the primary layer accepts a bitmap smaller than the panel and scales it, which would make the scaler unnecessary.

## 2. Screen rotation for vertical games

**Goal:** make vertical WonderSwan games playable.

**What must change:**
- Rotate the frame in `src/runner.c` or the scaling path in `src/stick_frames.c`, driven by the rotation the core reports through libretro.
- Rotate the D-pad mapping to match.
- Use a fast path, so rotation does not break the frame budget.

## 3. Retrace wait in the menu

**Goal:** remove tearing during menu animation.

**What must change:**
- Use the swap, `0x6402`, `0x6407` sequence in the menu's frame path in `src/stick_video.c`.
- Keep time-based animation, so a slower frame rate does not slow the animation.

## 4. Settings screen

**Goal:** let the player change volume, scale (integer or full screen 4:3) and filter without rebuilding.

**What must change:**
- Add a screen in `src/main.c` and `src/menu_view.c`.
- Store the values in a file in `retro/`, written only in the menu.
- Read them in `src/runner.c` and the `src/stick_*.c` files.

## 5. Swap controllers

**Goal:** let the players choose who is player 1 after a game has started.

**What must change:**
- Add a pause menu item in `src/pause.c` and `src/runner_pause.c` that exchanges the player assignment.

## 6. Covers without a collection

**Goal:** generate covers and game details from a plain `roms/` folder.

**What must change:**
- Make `--collection` optional in `tools/gen_covers.py`, using the libretro `.rdb` and libretro-thumbnails as the main sources.

## 7. Clock

**Goal:** give saves and logs a usable date, since the stick goes back to 1970 on every boot.

**What must change:**
- Read a file date from the card at boot and use it as the base time in logs and save metadata.
- Keep `.last` as the source of truth for which save is newest.

## 8. One-step card assembly

**Goal:** assemble a complete card from scratch with one command.

**What must change:**
- Add a script or `make` target that copies `out/card/retro/`, the games and `.meta/` folders to a mounted card and verifies them with `cmp`.
- It must refuse to run on anything but a mounted FAT32 partition, and it must never format. Formatting stays a manual step.
- It must refuse to copy a file named `SPI_ROM.bin`.
