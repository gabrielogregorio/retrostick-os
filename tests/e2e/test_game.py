"""Pause menu items, top to bottom: continue, save 1, save 2, load 1, load 2, last save, restart, quit."""
CONTINUE, SAVE1, SAVE2, LOAD1, LOAD2, LAST, RESTART, QUIT = range(8)


def pick(item):
    return ','.join(['M'] + ['D'] * item + ['A'])


def saves(card):
    return {path.suffix: path for path in (card / 'saves').iterdir()}


def trace(card):
    path = card / 'trace.txt'
    return path.read_text().splitlines() if path.exists() else []


def test_game_runs_and_quit_writes_sram_and_autosave(game, card):
    result = game('.,.')
    assert result.returncode == 0, result.stderr
    files = saves(card)
    assert files['.srm'].stat().st_size == 16
    assert files['.sta'].stat().st_size == 4
    assert files['.last'].read_text() == 'a'
    pixels = (card / 'shot.ppm').read_bytes()
    assert len(set(pixels[-3000:])) > 1


def test_save_slot_then_load_it_back(game, card):
    result = game(f'.,{pick(SAVE1)},.,.,.,{pick(LOAD1)},.', MOCK_CORE_TRACE=str(card / 'trace.txt'))
    assert result.returncode == 0, result.stderr
    files = saves(card)
    assert '.st1' in files and '.st2' not in files
    assert any(line.startswith('loaded') for line in trace(card))


def test_last_save_uses_the_most_recent_slot(game, card):
    game(f'.,{pick(SAVE2)},.,.')
    assert game(f'{pick(LAST)},.', MOCK_CORE_TRACE=str(card / 'trace.txt')).returncode == 0
    assert any(line.startswith('loaded') for line in trace(card))


def test_restart_needs_confirmation(game, card):
    one_press = f'M,{",".join("D" * RESTART)},A,B,B'
    assert game(one_press, MOCK_CORE_TRACE=str(card / 'trace.txt')).returncode == 0
    assert 'reset 0' not in trace(card)
    two_presses = f'M,{",".join("D" * RESTART)},A,A,.'
    assert game(two_presses, MOCK_CORE_TRACE=str(card / 'trace.txt')).returncode == 0
    assert 'reset 0' in trace(card)


def test_quit_from_the_pause_menu(game, card):
    result = game(pick(QUIT))
    assert result.returncode == 0, result.stderr
    assert '.sta' in saves(card)


def test_pause_log_lines(game, card):
    game(f'{pick(CONTINUE)},.')
    assert 'pause: menu' in (card / 'log.txt').read_text()


def test_load_with_no_save_keeps_the_game_running(game, card):
    assert game(f'{pick(LOAD1)},B,.').returncode == 0
    assert '.st1' not in saves(card)
