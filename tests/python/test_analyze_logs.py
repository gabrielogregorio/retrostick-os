import subprocess
import sys
from pathlib import Path

TOOL = Path(__file__).parents[2] / 'tools' / 'analyze_logs.py'


def run(folder):
    return subprocess.run([sys.executable, str(TOOL), str(folder)], capture_output=True, text=True, check=True).stdout


def test_reports_steps_and_controller_bits(tmp_path):
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


def test_handles_a_card_without_logs(tmp_path):
    out = run(tmp_path)
    assert 'missing' in out.lower() or 'does not exist' in out.lower()
