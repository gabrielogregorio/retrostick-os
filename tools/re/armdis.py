#!/usr/bin/env python3
"""Disassembles functions of an ARM ELF (not stripped) and annotates strings and calls.

Usage: venv/bin/python tools/armdis.py <elf> <func> [func...]
       venv/bin/python tools/armdis.py <elf> --list [filter]
Read-only.
"""
import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB

path = sys.argv[1]
f = open(path, 'rb')
elf = ELFFile(f)
data = open(path, 'rb').read()

segs = [(s['p_vaddr'], s['p_offset'], s['p_filesz']) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']


def rd(addr, n):
    for va, off, sz in segs:
        if va <= addr < va + sz:
            o = off + addr - va
            return data[o:o + n]
    return None


def cstr(addr):
    b = rd(addr, 200)
    if not b:
        return None
    s = b.split(b'\0')[0]
    if len(s) >= 3 and all(32 <= c < 127 or c in (9, 10) for c in s):
        return s.decode()
    return None


syms = {}
byaddr = {}
for sec in elf.iter_sections():
    if sec.header['sh_type'] in ('SHT_SYMTAB', 'SHT_DYNSYM'):
        for s in sec.iter_symbols():
            if s['st_info']['type'] == 'STT_FUNC' and s['st_value']:
                syms[s.name] = (s['st_value'], s['st_size'])
                byaddr[s['st_value'] & ~1] = s.name

# PLT: resolved through .rel.plt (each PLT entry = 12 bytes, after a 20-byte header)
plt = elf.get_section_by_name('.plt')
relplt = elf.get_section_by_name('.rel.plt')
if plt and relplt:
    dsym = elf.get_section(relplt['sh_link'])
    for i, r in enumerate(relplt.iter_relocations()):
        byaddr[plt['sh_addr'] + 20 + 12 * i] = dsym.get_symbol(r['r_info_sym']).name + '@plt'

if sys.argv[2] == '--list':
    flt = sys.argv[3] if len(sys.argv) > 3 else ''
    for n, (a, s) in sorted(syms.items(), key=lambda x: x[1]):
        if flt in n:
            print(f'{a:08x} {s:6d} {n}')
    sys.exit()

for name in sys.argv[2:]:
    a, size = syms[name]
    thumb = a & 1
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    a &= ~1
    print(f'==== {name} @ {a:08x} ({size} bytes, {"thumb" if thumb else "arm"})')
    for ins in md.disasm(rd(a, size), a):
        note = ''
        if ins.mnemonic.startswith('ldr') and '[pc' in ins.op_str:
            try:
                imm = int(ins.op_str.split('#')[-1].rstrip(']'), 0)
            except ValueError:
                imm = 0
            lit = ((ins.address + (4 if thumb else 8)) & ~3) + imm
            b = rd(lit, 4)
            if b:
                v = int.from_bytes(b, 'little')
                s = cstr(v)
                note = f'  ; ={v:#x}' + (f' "{s}"' if s else '') + (f' <{byaddr[v]}>' if v in byaddr else '')
        if ins.mnemonic in ('bl', 'blx', 'b') and ins.op_str.startswith('#'):
            t = int(ins.op_str[1:], 0)
            if t in byaddr:
                note = f'  ; <{byaddr[t]}>'
        print(f'{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}{note}')
