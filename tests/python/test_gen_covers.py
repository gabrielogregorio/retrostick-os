import struct
import zlib
from pathlib import Path

import gen_covers
import pytest
import rdb
from PIL import Image

CAP_HEADER_SIZE = 8
CAP_PALETTE_SIZE = 32
CAP_PIXELS_SIZE = gen_covers.COVER_W * gen_covers.COVER_H // 2


@pytest.mark.parametrize('name, key', [
    ('Mock Hero, The (USA).sfc', 'mockhero'),
    ('Mock & Friends (Europe) [!].nes', 'mockandfriends'),
    ('mock_hero.gba', 'mockhero'),
])
def test_title_key_ignores_region_article_and_punctuation(name: str, key: str) -> None:
    assert gen_covers.title_key(name) == key


def test_the_same_game_gets_the_same_key_across_regions() -> None:
    assert gen_covers.title_key('Mock Hero (USA).nes') == gen_covers.title_key('Mock Hero (Japan) (Rev 1).nes')


def test_every_system_has_collection_folders() -> None:
    assert set(gen_covers.FOLDERS) <= set(gen_covers.SYSTEMS)


@pytest.mark.parametrize('name', ['', None])
def test_title_key_of_nothing_is_empty(name: str | None) -> None:
    assert gen_covers.title_key(name) == ''


def test_base_title_keeps_the_words_and_drops_the_region() -> None:
    assert rdb.base_title('Mock Hero: Part 2 (USA) [!]') == 'mock hero: part 2'


@pytest.mark.parametrize('rgb, packed', [
    ((0, 0, 0), 0x0000),
    ((255, 255, 255), 0xffff),
    ((255, 0, 0), 0xf800),
    ((0, 255, 0), 0x07e0),
    ((0, 0, 255), 0x001f),
])
def test_rgb565_packs_each_channel_in_its_bits(rgb: tuple[int, int, int], packed: int) -> None:
    assert gen_covers.rgb565(*rgb) == packed


@pytest.mark.parametrize('size', [(gen_covers.COVER_W, gen_covers.COVER_H), (300, 100), (100, 300)])
def test_make_cover_writes_the_cap_format_for_any_aspect(tmp_path: Path, size: tuple[int, int]) -> None:
    source = tmp_path / 'cover.png'
    Image.new('RGB', size, (200, 40, 40)).save(source)
    data, preview = gen_covers.make_cover(str(source))
    assert data[:4] == b'CAP1'
    assert struct.unpack('<HH', data[4:CAP_HEADER_SIZE]) == (gen_covers.COVER_W, gen_covers.COVER_H)
    assert len(data) == CAP_HEADER_SIZE + CAP_PALETTE_SIZE + CAP_PIXELS_SIZE
    assert preview.size == (gen_covers.COVER_W, gen_covers.COVER_H)


def test_rdb_details_finds_a_rom_by_crc_without_its_ines_header(tmp_path: Path) -> None:
    body = b'demo rom body'
    rom = tmp_path / 'demo.nes'
    rom.write_bytes(b'NES\x1a' + b'\0' * 12 + body)
    catalog = rdb.RdbCatalog(
        by_crc={format(zlib.crc32(body), '08x'): ('Demo (USA)', 'nes-db')},
        by_name={'Demo (USA)': {'genre': 'Action'}},
        by_base={'demo': 1990},
    )
    name, details = rdb.rdb_details(str(rom), 16, catalog)
    assert name == 'Demo (USA)'
    assert details == {'genre': 'Action', 'releaseyear': 1990, '_base': 'nes-db'}


def test_rdb_details_of_an_unknown_rom_is_empty(tmp_path: Path) -> None:
    rom = tmp_path / 'unknown.gba'
    rom.write_bytes(b'nothing')
    assert rdb.rdb_details(str(rom), 0, rdb.RdbCatalog()) == (None, {})


def test_resolve_year_prefers_the_fix_then_the_catalog_then_the_description() -> None:
    assert gen_covers.resolve_year({'year': 1991}, {'releaseyear': 1990}, 'From 1989.') == 1991
    assert gen_covers.resolve_year({}, {'releaseyear': 1990}, 'From 1989.') == 1990
    assert gen_covers.resolve_year({}, {}, 'From 1989.') == 1989
    assert gen_covers.resolve_year({}, {}, 'No date here.') == ''
