from pathlib import Path

import mock_roms
import pytest


def test_every_system_has_a_rom_extension() -> None:
    assert set(mock_roms.GAMES) == set(mock_roms.EXT)


@pytest.mark.parametrize('rgb, packed', [
    ((0, 0, 0), 0x0000),
    ((255, 255, 255), 0xffff),
    ((7, 3, 7), 0x0000),
    ((8, 4, 8), 0x0821),
])
def test_rgb565_truncates_each_channel(rgb: tuple[int, int, int], packed: int) -> None:
    assert mock_roms.rgb565(*rgb) == packed


def test_cover_has_the_exact_cap_size() -> None:
    assert len(mock_roms.cover(1)) == 8 + 32 + mock_roms.COVER_W * mock_roms.COVER_H // 2


def test_covers_differ_per_game() -> None:
    assert mock_roms.cover(1) != mock_roms.cover(2)


def test_build_writes_rom_details_and_cover_for_each_game(tmp_path: Path) -> None:
    mock_roms.build(str(tmp_path))
    for system, games in mock_roms.GAMES.items():
        for title, year, genre, developer, text in games:
            assert (tmp_path / system / (title + mock_roms.EXT[system])).read_bytes() == b'MOCK' + title.encode()
            details = (tmp_path / system / '.meta' / (title + '.txt')).read_text(encoding='utf-8')
            assert details == f'{year}\n{genre}\n{developer}\n{text}\n'
            assert (tmp_path / system / '.meta' / (title + '.cap')).exists()
