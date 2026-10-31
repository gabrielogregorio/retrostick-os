import struct
from pathlib import Path

import pytest
from conftest import BinaryRun

RESUME, SAVE_1, SAVE_2, LOAD_1, LOAD_2, LAST_SAVE, RESTART, QUIT = range(8)

# Scripted keys last 15 input reads each and the mock core reads input once per frame. So the frame
# numbers below are exact: the first '.' covers frames 1 to 15 and MODE lands on frame 16.
FRAME_WHEN_PAUSED_AFTER_ONE_KEY = 16
LAST_FRAME_OF_TWO_IDLE_KEYS = 62
LAST_FRAME_AFTER_FAILED_LOAD = 62
SAVE_RAM_SIZE = 16
BOTTOM_LINES_CHANNELS = {0, 32, 104, 112, 248, 252}
BOTTOM_LINES_BYTES = 3000


def pause_and_choose(item: int) -> str:
    return ','.join(['M'] + ['D'] * item + ['A'])


def save_then_choose(save_item: int, next_item: int) -> str:
    return f'.,{pause_and_choose(save_item)},.,.,.,{pause_and_choose(next_item)},.'


def restart_keys(answer: str) -> str:
    return ','.join(['M'] + ['D'] * RESTART + ['A', answer])


def saves(card: Path) -> dict[str, Path]:
    return {path.suffix: path for path in (card / 'saves').iterdir()}


def saved_frame(path: Path) -> int:
    return struct.unpack('<I', path.read_bytes())[0]


def trace_file(card: Path) -> str:
    return str(card / 'trace.txt')


def trace(card: Path) -> list[str]:
    path = card / 'trace.txt'

    return path.read_text().splitlines() if path.exists() else []


def pause_log(card: Path) -> list[str]:
    return [line for line in (card / 'log.txt').read_text().splitlines() if line.startswith('pause:')]


def test_quit_writes_the_last_frame_to_save_ram(game: BinaryRun, card: Path) -> None:
    result = game('.,.')

    assert result.returncode == 0, result.stderr
    assert saves(card)['.srm'].read_bytes() == bytes([LAST_FRAME_OF_TWO_IDLE_KEYS]) + bytes(SAVE_RAM_SIZE - 1)


def test_quit_writes_the_automatic_save(game: BinaryRun, card: Path) -> None:
    game('.,.')

    assert saved_frame(saves(card)['.sta']) == LAST_FRAME_OF_TWO_IDLE_KEYS


def test_quit_marks_the_automatic_save_as_the_latest(game: BinaryRun, card: Path) -> None:
    game('.,.')

    assert saves(card)['.last'].read_text() == 'a'


def test_game_picture_reaches_the_screen(game: BinaryRun, card: Path) -> None:
    game('.,.')

    assert set((card / 'shot.ppm').read_bytes()[-BOTTOM_LINES_BYTES:]) == BOTTOM_LINES_CHANNELS


@pytest.mark.parametrize(('save_item', 'extension'), [(SAVE_1, '.st1'), (SAVE_2, '.st2')])
def test_saving_writes_the_frame_to_that_slot_only(game: BinaryRun, card: Path, save_item: int, extension: str) -> None:
    result = game(f'.,{pause_and_choose(save_item)},.,.')

    assert result.returncode == 0, result.stderr
    assert set(saves(card)) == {'.srm', '.sta', '.last', extension}
    assert saved_frame(saves(card)[extension]) == FRAME_WHEN_PAUSED_AFTER_ONE_KEY


@pytest.mark.parametrize(('save_item', 'load_item'), [(SAVE_1, LOAD_1), (SAVE_2, LOAD_2)])
def test_loading_a_slot_restores_its_frame(game: BinaryRun, card: Path, save_item: int, load_item: int) -> None:
    result = game(save_then_choose(save_item, load_item), MOCK_CORE_TRACE=trace_file(card))

    assert result.returncode == 0, result.stderr
    assert trace(card) == [f'loaded {FRAME_WHEN_PAUSED_AFTER_ONE_KEY}']


def test_last_save_loads_the_most_recent_slot(game: BinaryRun, card: Path) -> None:
    result = game(save_then_choose(SAVE_2, LAST_SAVE), MOCK_CORE_TRACE=trace_file(card))

    assert result.returncode == 0, result.stderr
    assert trace(card) == [f'loaded {FRAME_WHEN_PAUSED_AFTER_ONE_KEY}']


def test_last_save_after_quitting_loads_the_automatic_save(game: BinaryRun, card: Path) -> None:
    game(f'.,{pause_and_choose(SAVE_2)},.,.')
    automatic_frame = saved_frame(saves(card)['.sta'])

    result = game(f'{pause_and_choose(LAST_SAVE)},.', MOCK_CORE_TRACE=trace_file(card))

    assert result.returncode == 0, result.stderr
    assert trace(card) == [f'loaded {automatic_frame}']


def test_restart_cancelled_with_b_does_not_reset(game: BinaryRun, card: Path) -> None:
    result = game(f'{restart_keys("B")},B', MOCK_CORE_TRACE=trace_file(card))

    assert result.returncode == 0, result.stderr
    assert trace(card) == []


def test_restart_confirmed_with_a_resets_the_game(game: BinaryRun, card: Path) -> None:
    result = game(f'{restart_keys("A")},.', MOCK_CORE_TRACE=trace_file(card))

    assert result.returncode == 0, result.stderr
    assert trace(card) == ['reset 0']


def test_mode_closes_the_pause_menu(game: BinaryRun, card: Path) -> None:
    assert game('M,.,M,.').returncode == 0
    assert pause_log(card)[:2] == ['pause: menu opened', 'pause: menu closed, quit 0']


def test_quit_from_the_pause_menu_saves_the_paused_frame(game: BinaryRun, card: Path) -> None:
    result = game(pause_and_choose(QUIT))

    assert result.returncode == 0, result.stderr
    assert saved_frame(saves(card)['.sta']) == 1


def test_resume_logs_the_menu_opening_and_closing(game: BinaryRun, card: Path) -> None:
    game(f'{pause_and_choose(RESUME)},.')

    expected = ['pause: menu opened', 'pause: menu closed, quit 0', f'pause: state saved, slot {ord("a")}']
    assert pause_log(card) == expected


def test_load_with_no_save_keeps_the_game_running(game: BinaryRun, card: Path) -> None:
    result = game(f'{pause_and_choose(LOAD_1)},B,.', MOCK_CORE_TRACE=trace_file(card))

    assert result.returncode == 0, result.stderr
    assert trace(card) == []
    assert saved_frame(saves(card)['.sta']) == LAST_FRAME_AFTER_FAILED_LOAD
