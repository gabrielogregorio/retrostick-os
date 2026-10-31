import struct
from pathlib import Path

import pytest
from rdb import read_rdb


def text(value: str) -> bytes:
    raw = value.encode()

    return bytes([0xa0 | len(raw)]) + raw


def record(**fields: str | int) -> bytes:
    body = bytes([0x80 | len(fields)])
    for key, value in fields.items():
        body += text(key)
        body += text(value) if isinstance(value, str) else struct.pack('>BI', 0xce, value)

    return body


def single_field(raw_value: bytes) -> bytes:
    return bytes([0x81]) + text('value') + raw_value


def write_rdb(path: Path, *records: bytes) -> None:
    path.write_bytes(b'RARCHDB\0' + b'\0' * 8 + b''.join(records) + b'\xc0')


def read_value(tmp_path: Path, raw_value: bytes) -> object:
    database = tmp_path / 'value.rdb'
    write_rdb(database, single_field(raw_value))

    return read_rdb(str(database))[0]['value']


def test_reads_every_record(tmp_path: Path) -> None:
    db = tmp_path / 'x.rdb'
    write_rdb(db, record(name='Demo', releaseyear=1990), record(name='Other'))
    items = read_rdb(str(db))
    assert items == [{'name': 'Demo', 'releaseyear': 1990}, {'name': 'Other'}]


def test_rejects_a_file_with_the_wrong_magic(tmp_path: Path) -> None:
    db = tmp_path / 'x.rdb'
    db.write_bytes(b'NOTADB\0\0' + b'\0' * 8)
    with pytest.raises(AssertionError):
        read_rdb(str(db))


def test_an_empty_database_has_no_records(tmp_path: Path) -> None:
    db = tmp_path / 'x.rdb'
    write_rdb(db)
    assert read_rdb(str(db)) == []


@pytest.mark.parametrize('raw, value', [
    (b'\x7f', 0x7f),
    (b'\xcc\xff', 0xff),
    (b'\xcd\xff\xff', 0xffff),
    (b'\xce\xff\xff\xff\xff', 0xffffffff),
    (b'\xcf\xff\xff\xff\xff\xff\xff\xff\xff', 0xffffffffffffffff),
    (b'\xc0', None),
])
def test_reads_numbers_up_to_the_limit_of_each_type(tmp_path: Path, raw: bytes, value: int | None) -> None:
    assert read_value(tmp_path, raw) == value


@pytest.mark.parametrize('prefix', [b'\xd9\x03', b'\xda\x00\x03', b'\xdb\x00\x00\x00\x03'])
def test_reads_strings_with_every_length_size(tmp_path: Path, prefix: bytes) -> None:
    assert read_value(tmp_path, prefix + b'abc') == 'abc'


@pytest.mark.parametrize('prefix', [b'\xc4\x03', b'\xc5\x00\x03', b'\xc6\x00\x00\x00\x03'])
def test_keeps_binary_values_as_bytes(tmp_path: Path, prefix: bytes) -> None:
    assert read_value(tmp_path, prefix + b'\x01\x02\x03') == b'\x01\x02\x03'


@pytest.mark.parametrize('prefix', [b'\xde\x00\x01', b'\xdf\x00\x00\x00\x01'])
def test_reads_maps_with_a_long_length(tmp_path: Path, prefix: bytes) -> None:
    assert read_value(tmp_path, prefix + text('key') + text('value')) == {'key': 'value'}


def test_replaces_invalid_utf8_in_strings(tmp_path: Path) -> None:
    assert read_value(tmp_path, b'\xa1\xff') == '�'


def test_rejects_an_unknown_type_naming_its_offset(tmp_path: Path) -> None:
    db = tmp_path / 'x.rdb'
    db.write_bytes(b'RARCHDB\0' + b'\0' * 8 + b'\xc1')
    with pytest.raises(ValueError, match='msgpack type 0xc1 at 16'):
        read_rdb(str(db))
