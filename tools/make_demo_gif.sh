#!/usr/bin/env bash
# Records the README demo from the real menu on mock games: no real title, cover or ROM.
# Needs ffmpeg. Usage: make demo   (writes docs/demo.gif)
set -euo pipefail
cd "$(dirname "$0")/.."

WORK=$(mktemp -d)
mkdir -p "$WORK/retro/saves" "$WORK/frames"
python3 tests/mock/mock_roms.py "$WORK/retro/roms"
ROOT=$PWD

(cd "$WORK/retro" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    STICK_ROMS="$WORK/retro/roms" STICK_FRAMES="$WORK/frames" STICK_MUSIC=0 \
    STICK_KEYS=.,D,D,D,Y,.,.,.,.,R,.,D,D,.,R,.,D,.,S,.,.,.,.,. "$ROOT/out/test/menu-sdl")

ffmpeg -v error -y -framerate 30 -i "$WORK/frames/%05d.ppm" \
    -vf "fps=12,scale=400:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
    docs/demo.gif
ls -la docs/demo.gif
