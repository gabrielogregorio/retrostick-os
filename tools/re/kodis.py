# disassembles a function of an ARM .ko with relocations resolved
import sys, re
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from capstone import *
path, fn = sys.argv[1], sys.argv[2]
e = ELFFile(open(path,'rb'))
st = e.get_section_by_name('.symtab')
syms = list(st.iter_symbols())
secname = {i: s.name for i, s in enumerate(e.iter_sections())}
f = [s for s in syms if s.name == fn][0]
sec = e.get_section(f['st_shndx'])
data = sec.data()
rel = {}
for rs in e.iter_sections():
    if isinstance(rs, RelocationSection) and e.get_section(rs['sh_info']).name == sec.name:
        for r in rs.iter_relocations():
            s = syms[r['r_info_sym']]
            nm = s.name or secname.get(s['st_shndx'], '?')
            rel[r['r_offset']] = nm
# names of local functions in the same section
loc = {s['st_value']: s.name for s in syms if s['st_shndx'] == f['st_shndx'] and s.name and s['st_info']['type']=='STT_FUNC'}
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
a, n = f['st_value'], f['st_size']
for i in md.disasm(data[a:a+n], a):
    x = ''
    if i.address in rel: x = '  <' + rel[i.address] + '>'
    m = re.match(r'(\w+), \[pc, #(-?0x[0-9a-f]+)\]', i.op_str)
    if i.mnemonic.startswith('ldr') and m:
        la = i.address + 8 + int(m.group(2), 16)
        v = int.from_bytes(data[la:la+4], 'little')
        x += f'  ={v:#x}' + ('<' + rel[la] + '>' if la in rel else '')
    if i.mnemonic in ('bl','b') or i.mnemonic.startswith('b'):
        try:
            t = int(i.op_str.lstrip('#'), 16)
            if t in loc: x += '  [' + loc[t] + ']'
        except: pass
    print(f'{i.address:6x}: {i.mnemonic:8} {i.op_str}{x}')
# pool literals with relocations
