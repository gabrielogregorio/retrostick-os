# The binaries come from `make test-bins`.
import os
import subprocess
from collections.abc import Callable
from pathlib import Path

import mock_roms
import pytest

ROOT = Path(__file__).parents[2]
BIN = ROOT / 'out' / 'test'

BinaryRun = Callable[..., subprocess.CompletedProcess]


@pytest.fixture
def card(tmp_path: Path) -> Path:
    roms = tmp_path / 'retro' / 'roms'
    mock_roms.build(str(roms))
    (tmp_path / 'retro' / 'saves').mkdir()

    return tmp_path / 'retro'


def sdl_env(card: Path, keys: str, **extra: str) -> dict[str, str]:
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software',
               STICK_ROMS=str(card / 'roms'), STICK_KEYS=keys, STICK_LOG=str(card / 'log.txt'),
               STICK_SHOT=str(card / 'shot.ppm'), STICK_MUSIC='1')
    env.update(extra)

    return env


@pytest.fixture
def menu(card: Path) -> BinaryRun:
    def run(keys: str, **extra: str) -> subprocess.CompletedProcess:
        env = sdl_env(card, keys, **extra)

        return subprocess.run([str(BIN / 'menu-sdl')], env=env, cwd=card, capture_output=True, text=True, timeout=60)

    return run


@pytest.fixture
def game(card: Path) -> BinaryRun:
    def run(keys: str, **extra: str) -> subprocess.CompletedProcess:
        rom = card / 'roms' / 'nes' / 'Star Runner.nes'
        env = sdl_env(card, keys, **extra)

        return subprocess.run([str(BIN / 'nes-pc'), str(rom), str(card / 'saves')], env=env, cwd=card,
                              capture_output=True, text=True, timeout=60)

    return run
