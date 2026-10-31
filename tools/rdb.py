"""Minimal reader for libretro .rdb databases (simplified msgpack) and the game lookup built on them. Read-only."""
import os
import re
import struct
import zlib
from dataclasses import dataclass, field
from typing import Any

MAGIC = b'RARCHDB'
HEADER_SIZE = 16

POSITIVE_FIXINT_MAX = 0x7f
FIXMAP_FIRST = 0x80
FIXMAP_LAST = 0x8f
FIXMAP_LENGTH_MASK = 0x0f
FIXSTR_FIRST = 0xa0
FIXSTR_LAST = 0xbf
FIXSTR_LENGTH_MASK = 0x1f
NIL = 0xc0

BINARY_LENGTH_FORMATS = {0xc4: '>B', 0xc5: '>H', 0xc6: '>I'}
STRING_LENGTH_FORMATS = {0xd9: '>B', 0xda: '>H', 0xdb: '>I'}
UINT_FORMATS = {0xcc: '>B', 0xcd: '>H', 0xce: '>I', 0xcf: '>Q'}
MAP_LENGTH_FORMATS = {0xde: '>H', 0xdf: '>I'}

HEADER_KB_MODULO = 1024
INES_MAGIC = b'NES\x1a'
INES_HEADER_SIZE = 16
NO_YEAR = 9999
ROM_DETAIL_KEYS = ('releaseyear', 'genre', 'developer', 'publisher')
REGION_TAIL_RE = re.compile(r'\s*[\(\[].*$')


@dataclass
class RdbCatalog:
    by_crc: dict[str, tuple[str, str]] = field(default_factory=dict)  # crc -> (catalog name, database)
    by_name: dict[str, dict[str, Any]] = field(default_factory=dict)
    by_base: dict[str, int] = field(default_factory=dict)  # base title -> earliest year


def _unpack(fmt: str, data: bytes, offset: int) -> tuple[int, int]:
    return struct.unpack_from(fmt, data, offset)[0], offset + struct.calcsize(fmt)


def _read_text(data: bytes, offset: int, length: int) -> tuple[str, int]:
    end = offset + length

    return data[offset:end].decode('utf-8', 'replace'), end


def _read(data: bytes, offset: int) -> tuple[Any, int]:
    kind = data[offset]
    payload = offset + 1
    if kind <= POSITIVE_FIXINT_MAX:
        return kind, payload
    if FIXMAP_FIRST <= kind <= FIXMAP_LAST:
        return _read_map(data, payload, kind & FIXMAP_LENGTH_MASK)
    if FIXSTR_FIRST <= kind <= FIXSTR_LAST:
        return _read_text(data, payload, kind & FIXSTR_LENGTH_MASK)
    if kind == NIL:
        return None, payload
    if kind in BINARY_LENGTH_FORMATS:
        length, start = _unpack(BINARY_LENGTH_FORMATS[kind], data, payload)

        return data[start:start + length], start + length
    if kind in STRING_LENGTH_FORMATS:
        length, start = _unpack(STRING_LENGTH_FORMATS[kind], data, payload)

        return _read_text(data, start, length)
    if kind in UINT_FORMATS:
        return _unpack(UINT_FORMATS[kind], data, payload)
    if kind in MAP_LENGTH_FORMATS:
        length, start = _unpack(MAP_LENGTH_FORMATS[kind], data, payload)

        return _read_map(data, start, length)
    raise ValueError(f'msgpack type {kind:#x} at {offset}')


def _read_map(data: bytes, offset: int, length: int) -> tuple[dict[Any, Any], int]:
    result = {}
    for _ in range(length):
        key, offset = _read(data, offset)
        value, offset = _read(data, offset)
        result[key] = value

    return result, offset


def read_rdb(path: str) -> list[dict[Any, Any]]:
    with open(path, 'rb') as file:
        data = file.read()
    assert data[:len(MAGIC)] == MAGIC
    offset = HEADER_SIZE
    items = []
    while offset < len(data):
        value, offset = _read(data, offset)
        if not isinstance(value, dict):
            break
        items.append(value)

    return items


def base_title(name: str | None) -> str:
    return REGION_TAIL_RE.sub('', name or '').strip().lower()


def add_rdb_entry(catalog: RdbCatalog, entry: dict[str, Any], base: str) -> None:
    name = entry.get('name')
    if not name:
        return
    if isinstance(entry.get('crc'), bytes):
        catalog.by_crc.setdefault(entry['crc'].hex(), (name, base))
    details = catalog.by_name.setdefault(name, {})
    for key in ROM_DETAIL_KEYS:
        if key in entry and key not in details:
            details[key] = entry[key]
    if 'releaseyear' in entry:
        title = base_title(name)
        catalog.by_base[title] = min(catalog.by_base.get(title, NO_YEAR), entry['releaseyear'])


def load_rdb(folder: str, bases: list[str]) -> RdbCatalog:
    catalog = RdbCatalog()
    for base in bases:
        path = os.path.join(folder, base + '.rdb')
        if not os.path.exists(path):
            print(f'  warning: missing {path}')
            continue
        for entry in read_rdb(path):
            add_rdb_entry(catalog, entry, base)

    return catalog


def rom_payloads(rom: str, header: int) -> list[bytes]:
    """The ROM as it is, and without its header when it seems to have one: the catalog CRC excludes it."""
    with open(rom, 'rb') as file:
        content = file.read()
    payloads = [content]
    if header and len(content) % HEADER_KB_MODULO == header % HEADER_KB_MODULO and len(content) > header:
        payloads.append(content[header:])
    elif header == INES_HEADER_SIZE and content[:len(INES_MAGIC)] == INES_MAGIC:
        payloads.append(content[INES_HEADER_SIZE:])

    return payloads


def rdb_details(rom: str, header: int, catalog: RdbCatalog) -> tuple[str | None, dict[str, Any]]:
    for payload in rom_payloads(rom, header):
        found = catalog.by_crc.get(format(zlib.crc32(payload), '08x'))
        if not found:
            continue
        name, base = found
        details = dict(catalog.by_name.get(name, {}))
        if 'releaseyear' not in details and base_title(name) in catalog.by_base:
            details['releaseyear'] = catalog.by_base[base_title(name)]
        details['_base'] = base

        return name, details

    return None, {}
