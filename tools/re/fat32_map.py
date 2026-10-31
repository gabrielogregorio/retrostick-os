#!/usr/bin/env python3
"""FAT32 map of a partial image (read-only). Lists files with their offset in the image.
usage: fat32_map.py image [part_offset] > map.tsv  (path, size, start_offset, max_end_offset, fragments)"""
import struct, sys
img = open(sys.argv[1], 'rb'); size = img.seek(0, 2)
P = int(sys.argv[2]) if len(sys.argv) > 2 else 1048576
def rd(o, n): img.seek(o); return img.read(n)
bs = rd(P, 512)
bps, spc, rsv, nf = struct.unpack_from('<HBHB', bs, 11)
fatsz = struct.unpack_from('<I', bs, 36)[0]; rootc = struct.unpack_from('<I', bs, 44)[0]
cs = bps * spc; fat0 = P + rsv * bps; data0 = fat0 + nf * fatsz * bps
fat = rd(fat0, fatsz * bps)
def nxt(c): return struct.unpack_from('<I', fat, c * 4)[0] & 0x0FFFFFFF
def chain(c):
    out = []
    while 2 <= c < 0x0FFFFFF7 and len(out) < 2_000_000: out.append(c); c = nxt(c)
    return out
def coff(c): return data0 + (c - 2) * cs
def readchain(c):
    b = b''
    for x in chain(c):
        o = coff(x)
        if o + cs > size: raise EOFError(o)
        b += rd(o, cs)
    return b
def walk(c, path):
    try: d = readchain(c)
    except EOFError as e:
        print(f'{path}/\tDIR-OUTSIDE\t{e}', file=sys.stderr); return
    lfn = []
    for i in range(0, len(d), 32):
        e = d[i:i+32]
        if e[0] == 0: break
        if e[0] == 0xE5: lfn = []; continue
        if e[11] == 0x0F:
            lfn.insert(0, e[1:11] + e[14:26] + e[28:32]); continue
        if lfn:
            name = b''.join(lfn).decode('utf-16le', 'ignore').split('\x00')[0]
        else:
            n, x = e[0:8].decode('latin1').strip(), e[8:11].decode('latin1').strip()
            name = n + ('.' + x if x else '')
        lfn = []
        if name in ('.', '..') or e[11] & 0x08: continue
        cl = (struct.unpack_from('<H', e, 20)[0] << 16) | struct.unpack_from('<H', e, 26)[0]
        fsz = struct.unpack_from('<I', e, 28)[0]
        p = f'{path}/{name}' if path else name
        if e[11] & 0x10:
            print(f'{p}/\tDIR\t{coff(cl)}'); walk(cl, p)
        else:
            ch = chain(cl) if cl else []
            frag = 1 + sum(1 for a, b in zip(ch, ch[1:], strict=False) if b != a + 1)
            start = coff(ch[0]) if ch else 0; end = max(coff(x) for x in ch) + cs if ch else 0
            print(f'{p}\t{fsz}\t{start}\t{end}\t{frag}')
print(f'# cs={cs} data0={data0} root={rootc} imgsize_partial={size}', file=sys.stderr)
walk(rootc, '')
