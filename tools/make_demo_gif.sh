#!/usr/bin/env bash
# Records the README demo from the real menu on mock games: no real title, cover or ROM.
# Needs ffmpeg. Usage: make demo   (writes docs/demo.gif)
set -euo pipefail
cd "$(dirname "$0")/.."

work_dir=$(mktemp -d)
project=$PWD
key_script=.,D,D,D,Y,.,.,.,.,R,.,D,D,.,R,.,D,.,S,.,.,.,.,.
scale_filter="fps=12,scale=400:-1:flags=lanczos"
palette_gen="palettegen=max_colors=64:stats_mode=diff"
palette_use="paletteuse=dither=none:diff_mode=rectangle"

mkdir -p "$work_dir/retro/saves" "$work_dir/frames"
python3 tests/mock/mock_roms.py "$work_dir/retro/roms"

(cd "$work_dir/retro" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    STICK_ROMS="$work_dir/retro/roms" STICK_FRAMES="$work_dir/frames" STICK_MUSIC=0 \
    STICK_KEYS=$key_script "$project/out/test/menu-sdl")

ffmpeg -v error -y -framerate 30 -i "$work_dir/frames/%05d.ppm" \
    -vf "${scale_filter},split[a][b];[a]${palette_gen}[p];[b][p]${palette_use}" docs/demo.gif
ls -la docs/demo.gif
