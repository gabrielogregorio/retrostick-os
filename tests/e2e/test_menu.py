import stat
from pathlib import Path

from conftest import BinaryRun
from PIL import Image, ImageChops

SCREEN_SIZE = (640, 360)
MIN_COLORS_IN_MENU = 200
ALL_COLORS = 1 << 20
CAROUSEL_AREA = (0, 170, 640, 330)
BODY_AREA = (0, 40, 640, 330)


def screen(card: Path) -> Image.Image:
    return Image.open(card / 'shot.ppm').convert('RGB')


def test_menu_starts_draws_and_quits(menu: BinaryRun, card: Path) -> None:
    result = menu('.')

    assert result.returncode == 0, result.stderr
    image = screen(card)
    assert image.size == SCREEN_SIZE
    assert len(image.getcolors(ALL_COLORS)) > MIN_COLORS_IN_MENU


def test_changing_system_changes_the_screen(menu: BinaryRun, card: Path) -> None:
    menu('.')
    before = screen(card).crop(CAROUSEL_AREA)

    menu('R')
    after = screen(card).crop(CAROUSEL_AREA)

    assert ImageChops.difference(before, after).getbbox() is not None


def test_y_saves_the_game_as_favorite(menu: BinaryRun, card: Path) -> None:
    assert menu('Y,.').returncode == 0
    assert (card / 'favorites.txt').read_text().strip() == 'nes/Comet Patrol.nes'


def test_y_on_a_favorite_removes_it(menu: BinaryRun, card: Path) -> None:
    menu('Y,.')

    assert menu('Y,.').returncode == 0
    assert (card / 'favorites.txt').read_text().strip() == ''


def test_a_launches_the_runner_with_game_and_saves(menu: BinaryRun, card: Path) -> None:
    runners = card / 'runners'
    runners.mkdir()
    record = card / 'launched.txt'
    runner = runners / 'nes-pc'
    runner.write_text(f'#!/bin/sh\nprintf "%s\\n" "$1" "$2" > "{record}"\n')
    runner.chmod(runner.stat().st_mode | stat.S_IEXEC)

    result = menu('D,A,.,.,.,.', STICK_RUNNERS=str(runners))

    rom = card / 'roms' / 'nes' / 'Pixel Plumber.nes'
    assert result.returncode == 0, result.stderr
    assert f'LAUNCH nes {rom}' in result.stdout.splitlines()
    assert record.read_text().splitlines() == [str(rom), f'{card}/roms/../saves']
    assert 'nes 1' in (card / 'menu-state.txt').read_text().splitlines()


def test_missing_runner_keeps_the_menu_alive(menu: BinaryRun, card: Path) -> None:
    result = menu('A,.,.,.,.', STICK_RUNNERS=str(card / 'nothing'))

    assert result.returncode == 0, result.stderr


def test_diagnostics_screen_opens(menu: BinaryRun, card: Path) -> None:
    menu('.')
    plain = screen(card).crop(BODY_AREA)

    menu('S,.')
    diagnostics = screen(card).crop(BODY_AREA)

    assert ImageChops.difference(plain, diagnostics).getbbox() is not None
