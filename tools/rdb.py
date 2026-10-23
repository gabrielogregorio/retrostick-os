"""Minimal reader for libretro .rdb databases (simplified msgpack). Read-only."""
import struct


def _read(b, i):
    t = b[i]
    if t <= 0x7f: return t, i + 1
    if 0x80 <= t <= 0x8f: return _read_map(b, i + 1, t & 0x0f)
    if 0xa0 <= t <= 0xbf: n = t & 0x1f; return b[i + 1:i + 1 + n].decode('utf-8', 'replace'), i + 1 + n
    if t == 0xc0: return None, i + 1
    if t in (0xc4, 0xd9): n = b[i + 1]; v = b[i + 2:i + 2 + n]; return (v if t == 0xc4 else v.decode('utf-8', 'replace')), i + 2 + n
    if t in (0xc5, 0xda): n = struct.unpack('>H', b[i + 1:i + 3])[0]; v = b[i + 3:i + 3 + n]; return (v if t == 0xc5 else v.decode('utf-8', 'replace')), i + 3 + n
    if t in (0xc6, 0xdb): n = struct.unpack('>I', b[i + 1:i + 5])[0]; v = b[i + 5:i + 5 + n]; return (v if t == 0xc6 else v.decode('utf-8', 'replace')), i + 5 + n
    if t == 0xcc: return b[i + 1], i + 2
    if t == 0xcd: return struct.unpack('>H', b[i + 1:i + 3])[0], i + 3
    if t == 0xce: return struct.unpack('>I', b[i + 1:i + 5])[0], i + 5
    if t == 0xcf: return struct.unpack('>Q', b[i + 1:i + 9])[0], i + 9
    if t == 0xde: return _read_map(b, i + 3, struct.unpack('>H', b[i + 1:i + 3])[0])
    if t == 0xdf: return _read_map(b, i + 5, struct.unpack('>I', b[i + 1:i + 5])[0])
    raise ValueError(f'msgpack type {t:#x} at {i}')


def _read_map(b, i, n):
    m = {}
    for _ in range(n):
        k, i = _read(b, i)
        v, i = _read(b, i)
        m[k] = v
    return m, i


def read_rdb(path):
    b = open(path, 'rb').read()
    assert b[:7] == b'RARCHDB'
    i, items = 16, []
    while i < len(b):
        v, i = _read(b, i)
        if not isinstance(v, dict): break
        items.append(v)
    return items
