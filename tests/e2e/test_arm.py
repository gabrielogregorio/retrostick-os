"""The stick build itself, under qemu-arm, headless. Skipped when the ARM binary or qemu is missing."""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).parents[2]
MENU = ROOT / 'out' / 'menu-arm'
QEMU = shutil.which('qemu-arm-static') or shutil.which('qemu-arm')

pytestmark = pytest.mark.skipif(not MENU.exists() or not QEMU, reason='needs out/menu-arm and qemu-arm')


def run_menu(card, keys):
    env = dict(os.environ, STICK_ROMS=str(card / 'roms'), STICK_SERIAL='/dev/null', STICK_KEYS=keys,
               STICK_SHOT=str(card / 'shot.ppm'), STICK_LOG=str(card / 'log-stick.txt'))
    return subprocess.run([QEMU, str(MENU)], env=env, cwd=card, capture_output=True, text=True, timeout=120)


def test_arm_menu_draws_and_quits(card):
    result = run_menu(card, 'R,L,.')
    assert result.returncode == 0, result.stderr
    assert (card / 'shot.ppm').stat().st_size == 640 * 360 * 3 + len(b'P6\n640 360\n255\n')


def test_arm_menu_saves_a_favorite(card):
    assert run_menu(card, 'Y,.').returncode == 0
    assert (card / 'favorites.txt').read_text().strip() == 'nes/Comet Patrol.nes'
