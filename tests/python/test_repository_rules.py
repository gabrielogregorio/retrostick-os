import re
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).parents[2]
MAX_C_LINES = 500
C_ROOTS = ('src/', 'tools/', 'tests/')
GENERATED = {'src/font_data.h'}
STRING_OR_COMMENT = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', re.S)
INCREMENT = re.compile(r'\+\+|--')
ONE_LINE_BLOCK = re.compile(r'^\s*(?:if|for|while|else|\} else)\b.*[^{\s]\s*;\s*$')
RETURN_LINE = re.compile(r'^\s*return\b')
BLOCK_START = re.compile(r'[{:]\s*$')
FIRMWARE_NAMES = re.compile(r'wiseemu|dispCreate|dispFlip|GetSERJoystick|InitSound|InitRFJoystick', re.I)
DATE = re.compile(r'\b20\d\d-\d\d(?:-\d\d)?\b')


def tracked() -> list[str]:
    output = subprocess.run(['git', 'ls-files'], cwd=ROOT, capture_output=True, text=True, check=True).stdout

    return output.split()


def tracked_text() -> list[Path]:
    paths = [ROOT / name for name in tracked() if not name.endswith(('.gif', '.png'))]

    return [path for path in paths if path.is_file()]


def c_files() -> list[str]:
    names = [name for name in tracked() if name.endswith(('.c', '.h')) and name.startswith(C_ROOTS)]

    return [name for name in names if name not in GENERATED]


def code_of(name: str) -> str:
    source = (ROOT / name).read_text()

    return STRING_OR_COMMENT.sub(lambda found: re.sub(r'[^\n]', ' ', found.group()), source)


@pytest.mark.parametrize('path', tracked_text(), ids=lambda path: str(path.relative_to(ROOT)))
def test_text_files_have_no_em_dash(path: Path) -> None:
    assert '\u2014' not in path.read_text(errors='replace')


@pytest.mark.parametrize('name', c_files())
def test_c_files_stay_small(name: str) -> None:
    assert len((ROOT / name).read_text().splitlines()) <= MAX_C_LINES


@pytest.mark.parametrize('name', c_files())
def test_c_uses_plus_equals_instead_of_increment(name: str) -> None:
    assert INCREMENT.findall(code_of(name)) == []


@pytest.mark.parametrize('name', c_files())
def test_c_blocks_are_never_on_one_line(name: str) -> None:
    assert [line for line in code_of(name).splitlines() if ONE_LINE_BLOCK.match(line)] == []


@pytest.mark.parametrize('name', c_files())
def test_c_has_a_blank_line_before_return(name: str) -> None:
    lines = code_of(name).splitlines()
    pairs = zip(lines, lines[1:], strict=False)
    crowded = [line for previous, line in pairs if RETURN_LINE.match(line) and previous.strip()
               and not BLOCK_START.search(previous)]
    assert crowded == []


@pytest.mark.parametrize('name', [name for name in tracked() if name.endswith(('.c', '.h', '.py', '.md', '.sh'))
                                  and name not in GENERATED and not name.startswith('tests/python/test_repository')])
def test_firmware_internals_are_not_named(name: str) -> None:
    assert FIRMWARE_NAMES.findall((ROOT / name).read_text()) == []


@pytest.mark.parametrize('name', c_files())
def test_c_comments_carry_no_dates(name: str) -> None:
    assert DATE.findall((ROOT / name).read_text()) == []
