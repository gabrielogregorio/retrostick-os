"""End-to-end: the real menu and the real runner on the SDL platform with dummy video and audio.

No window, no emulator core (tests/mock/mock_core.c), no ROM (tests/mock/mock_roms.py).
Build first: make test-bins
"""
import os
import subprocess
from pathlib import Path

import mock_roms
import pytest

ROOT = Path(__file__).parents[2]
BIN = ROOT / 'out' / 'test'


@pytest.fixture
def card(tmp_path):
    roms = tmp_path / 'retro' / 'roms'
    mock_roms.build(str(roms))
    (tmp_path / 'retro' / 'saves').mkdir()
    return tmp_path / 'retro'


def sdl_env(card, keys, **extra):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software',
               STICK_ROMS=str(card / 'roms'), STICK_KEYS=keys, STICK_LOG=str(card / 'log.txt'),
               STICK_SHOT=str(card / 'shot.ppm'), STICK_MUSIC='1')
    env.update(extra)
    return env


@pytest.fixture
def menu(card):
    def run(keys, **extra):
        env = sdl_env(card, keys, **extra)
        return subprocess.run([str(BIN / 'menu-sdl')], env=env, cwd=card, capture_output=True, text=True, timeout=60)
    return run


@pytest.fixture
def game(card):
    def run(keys, **extra):
        rom = card / 'roms' / 'nes' / 'Star Runner.nes'
        env = sdl_env(card, keys, **extra)
        return subprocess.run([str(BIN / 'nes-pc'), str(rom), str(card / 'saves')], env=env, cwd=card,
                              capture_output=True, text=True, timeout=60)
    return run
