import subprocess
import sys
from pathlib import Path

TOOL = Path(__file__).parents[2] / 'tools' / 'analyze_logs.py'


def run(folder: Path) -> str:
    return subprocess.run([sys.executable, str(TOOL), str(folder)], capture_output=True, text=True, check=True).stdout


def packet(player: str, stick: str, buttons: str) -> str:
    return f't=10 {player} 0A 09 02 {stick} {buttons} 7F\n'


def test_reports_steps_and_controller_bits(tmp_path: Path) -> None:
    (tmp_path / 'log-steps.txt').write_text('12.3 4.0 LAUNCH nes demo\n')
    (tmp_path / 'log-stick.txt').write_text(
        'step: disp: panel 720x480\n'
        't=10 P1 0A 09 02 32 31 82 10 00 7F\n'
        't=20 P1 0A 09 02 32 31 82 00 00 6F\n'
    )
    out = run(tmp_path)
    assert 'LAUNCH nes demo' in out
    assert 'step: disp: panel 720x480' in out
    assert 'P1: 2 ' in out
    assert 'B6.4' in out


def test_handles_a_card_without_logs(tmp_path: Path) -> None:
    out = run(tmp_path)
    assert 'missing' in out.lower() or 'does not exist' in out.lower()


def test_ignores_boot_lines_that_match_no_pattern(tmp_path: Path) -> None:
    (tmp_path / 'log-steps.txt').write_text('nothing interesting\n')
    assert 'nothing interesting' not in run(tmp_path)


def test_labels_a_packet_with_no_buttons_as_released(tmp_path: Path) -> None:
    (tmp_path / 'log-stick.txt').write_text(packet('P2', '32 31 82', '00 00'))
    out = run(tmp_path)
    assert 'P2: 1 packets logged' in out
    assert '00 00 (released)' in out


def test_lists_only_the_button_changes(tmp_path: Path) -> None:
    repeated = packet('P1', '32 31 82', '10 00')
    (tmp_path / 'log-stick.txt').write_text(repeated + repeated + packet('P1', '32 31 82', '00 80'))
    assert '10 00 (B6.4)  |  00 80 (B7.7)' in run(tmp_path)


def test_keeps_exactly_twelve_stick_patterns_without_an_ellipsis(tmp_path: Path) -> None:
    (tmp_path / 'log-stick.txt').write_text(''.join(packet('P1', f'{index:02X} 31 82', '00 00') for index in range(12)))
    out = run(tmp_path)
    assert 'bytes 3-5 seen (12)' in out
    assert '...' not in out


def test_cuts_the_stick_patterns_past_twelve_with_an_ellipsis(tmp_path: Path) -> None:
    (tmp_path / 'log-stick.txt').write_text(''.join(packet('P1', f'{index:02X} 31 82', '00 00') for index in range(13)))
    out = run(tmp_path)
    assert 'bytes 3-5 seen (13)' in out
    assert '...' in out
