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
COVER_W, COVER_H = 60, 86


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def cover(seed):
    """CAP1 cover: a gradient plus a light diagonal band, unique per game."""
    palette = []
    for i in range(16):
        r, g, b = colorsys.hsv_to_rgb((seed * 0.13) % 1, 0.75 - i * 0.03, 0.25 + i * 0.05)
        palette.append(rgb565(int(r * 255), int(g * 255), int(b * 255)))
    data = bytearray(b'CAP1' + struct.pack('<HH', COVER_W, COVER_H))
    for color in palette:
        data += struct.pack('<H', color)
    index = []
    for y in range(COVER_H):
        for x in range(COVER_W):
            band = abs((x + y) % 60 - 30) < 5
            index.append(15 if band else min(14, y * 15 // COVER_H))
    for i in range(0, len(index), 2):
        data.append(index[i] << 4 | index[i + 1])

    return bytes(data)


def build(root):
    seed = 0
    for system, games in GAMES.items():
        meta = os.path.join(root, system, '.meta')
        os.makedirs(meta, exist_ok=True)
        for title, year, genre, dev, text in games:
            seed += 1
            with open(os.path.join(root, system, title + EXT[system]), 'wb') as f:
                f.write(b'MOCK' + title.encode())
            with open(os.path.join(meta, title + '.txt'), 'w', encoding='utf-8') as f:
                f.write(f'{year}\n{genre}\n{dev}\n{text}\n')
            with open(os.path.join(meta, title + '.cap'), 'wb') as f:
                f.write(cover(seed))


if __name__ == '__main__':
    build(sys.argv[1])
