import stat

from PIL import Image, ImageChops


def screen(card):
    return Image.open(card / 'shot.ppm').convert('RGB')


def test_menu_starts_draws_and_quits(menu, card):
    result = menu('.')
    assert result.returncode == 0, result.stderr
    image = screen(card)
    assert image.size == (640, 360)
    assert len(image.getcolors(1 << 20)) > 200


def test_changing_system_changes_the_screen(menu, card):
    menu('.')
    first = screen(card).crop((0, 170, 640, 330))
    menu('R')
    second = screen(card).crop((0, 170, 640, 330))
    assert ImageChops.difference(first, second).getbbox() is not None


def test_favorite_is_saved_and_removed(menu, card):
    assert menu('Y,.').returncode == 0
    favorites = card / 'favorites.txt'
    assert favorites.read_text().strip() == 'nes/Comet Patrol.nes'
    assert menu('Y,.').returncode == 0
    assert favorites.read_text().strip() == ''


def test_a_launches_the_runner_with_game_and_saves(menu, card):
    runners = card / 'runners'
    runners.mkdir()
    record = card / 'launched.txt'
    runner = runners / 'nes-pc'
    runner.write_text(f'#!/bin/sh\nprintf "%s\\n" "$1" "$2" > "{record}"\n')
    runner.chmod(runner.stat().st_mode | stat.S_IEXEC)
    result = menu('D,A,.,.,.,.', STICK_RUNNERS=str(runners))
    assert result.returncode == 0, result.stderr
    assert 'LAUNCH nes' in result.stdout
    game, saves = record.read_text().splitlines()
    assert game.endswith('roms/nes/Pixel Plumber.nes')
    assert saves.endswith('roms/../saves')
    assert 'nes 1' in (card / 'menu-state.txt').read_text()


def test_missing_runner_keeps_the_menu_alive(menu, card):
    result = menu('A,.,.,.,.', STICK_RUNNERS=str(card / 'nothing'))
    assert result.returncode == 0, result.stderr


def test_diagnostics_screen_opens(menu, card):
    menu('.')
    plain = screen(card).crop((0, 40, 640, 330))
    menu('S,.')
    diag = screen(card).crop((0, 40, 640, 330))
    assert ImageChops.difference(plain, diag).getbbox() is not None
