#!/usr/bin/env python3
"""Builds a mock card: invented games with details and covers, no real ROM, title or artwork.

Usage: python3 tests/mock/mock_roms.py <roms folder>
"""
import colorsys
import os
import struct
import sys

GAMES = {
    'nes': [('Comet Patrol', '1987', 'Shooter', 'Mock Works', 'Pilot a tiny ship across a demo galaxy.'),
            ('Pixel Plumber', '1988', 'Platform', 'Mock Works', 'Jump over sample pipes in this invented title.'),
            ('Star Runner', '1989', 'Action', 'Sample Soft', 'Dash through placeholder levels at full speed.'),
            ('Tiny Dungeon', '1990', 'RPG', 'Sample Soft', 'A fake quest used to show the game details.'),
            ('Turbo Karts', '1991', 'Racing', 'Demo Labs', 'Race imaginary karts on made-up tracks.')],
    'snes': [('Crystal Saga', '1993', 'RPG', 'Demo Labs', 'An invented epic with sixteen-color covers.'),
             ('Neon Drift', '1994', 'Racing', 'Demo Labs', 'Sample racing with no real cars at all.'),
             ('Robo Rumble', '1995', 'Fighting', 'Mock Works', 'Two mock robots settle a pretend score.')],
    'md': [('Blast Hedge', '1991', 'Platform', 'Sample Soft', 'A fast mock hero with a borrowed attitude.'),
           ('Street Fury', '1992', 'Beat \'em up', 'Sample Soft', 'Clean up a city that does not exist.')],
    'gba': [('Pocket Quest', '2002', 'Adventure', 'Demo Labs', 'A portable tale made for this demo.')],
}
EXT = {'nes': '.nes', 'snes': '.sfc', 'md': '.md', 'gba': '.gba'}
COVER_W = 60
COVER_H = 86
PALETTE_SIZE = 16
BAND_COLOR = PALETTE_SIZE - 1
LAST_GRADIENT_COLOR = PALETTE_SIZE - 2
BAND_PERIOD = 60
BAND_HALF_WIDTH = 5
SEED_HUE_STEP = 0.13


def rgb565(red: int, green: int, blue: int) -> int:
    return (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3


def cover_palette(seed: int) -> list[int]:
    palette = []
    for level in range(PALETTE_SIZE):
        red, green, blue = colorsys.hsv_to_rgb((seed * SEED_HUE_STEP) % 1, 0.75 - level * 0.03, 0.25 + level * 0.05)
        palette.append(rgb565(int(red * 255), int(green * 255), int(blue * 255)))

    return palette


def cover_pixel(x: int, y: int) -> int:
    in_band = abs((x + y) % BAND_PERIOD - BAND_PERIOD // 2) < BAND_HALF_WIDTH

    return BAND_COLOR if in_band else min(LAST_GRADIENT_COLOR, y * BAND_COLOR // COVER_H)


def cover(seed: int) -> bytes:
    """CAP1 cover: a gradient plus a light diagonal band, unique per game."""
    data = bytearray(b'CAP1' + struct.pack('<HH', COVER_W, COVER_H))
    for color in cover_palette(seed):
        data += struct.pack('<H', color)
    pixels = [cover_pixel(x, y) for y in range(COVER_H) for x in range(COVER_W)]
    for index in range(0, len(pixels), 2):
        data.append(pixels[index] << 4 | pixels[index + 1])

    return bytes(data)


def build(root: str) -> None:
    seed = 0
    for system, games in GAMES.items():
        meta = os.path.join(root, system, '.meta')
        os.makedirs(meta, exist_ok=True)
        for title, year, genre, developer, text in games:
            seed += 1
            with open(os.path.join(root, system, title + EXT[system]), 'wb') as file:
                file.write(b'MOCK' + title.encode())
            with open(os.path.join(meta, title + '.txt'), 'w', encoding='utf-8') as file:
                file.write(f'{year}\n{genre}\n{developer}\n{text}\n')
            with open(os.path.join(meta, title + '.cap'), 'wb') as file:
                file.write(cover(seed))


if __name__ == '__main__':
    build(sys.argv[1])
