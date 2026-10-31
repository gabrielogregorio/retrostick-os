import struct

import pytest
from rdb import read_rdb


def text(value):
    raw = value.encode()
    return bytes([0xa0 | len(raw)]) + raw


def record(**fields):
    body = bytes([0x80 | len(fields)])
    for key, value in fields.items():
        body += text(key)
        body += text(value) if isinstance(value, str) else struct.pack('>BI', 0xce, value)
    return body


def write_rdb(path, *records):
    path.write_bytes(b'RARCHDB\0' + b'\0' * 8 + b''.join(records) + b'\xc0')


def test_reads_every_record(tmp_path):
    db = tmp_path / 'x.rdb'
    write_rdb(db, record(name='Demo', releaseyear=1990), record(name='Other'))
    items = read_rdb(str(db))
    assert items == [{'name': 'Demo', 'releaseyear': 1990}, {'name': 'Other'}]


def test_rejects_a_file_with_the_wrong_magic(tmp_path):
    db = tmp_path / 'x.rdb'
    db.write_bytes(b'NOTADB\0\0' + b'\0' * 8)
    with pytest.raises(AssertionError):
        read_rdb(str(db))
