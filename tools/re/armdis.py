#!/usr/bin/env python3
"""Disassembles functions of an ARM ELF (not stripped) and annotates strings and calls.

Usage: venv/bin/python tools/armdis.py <elf> <func> [func...]
       venv/bin/python tools/armdis.py <elf> --list [filter]
Read-only.
"""
import sys
from dataclasses import dataclass, field
from typing import Any

from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, Cs
from elftools.elf.elffile import ELFFile

PLT_HEADER_SIZE = 20
PLT_ENTRY_SIZE = 12
MAX_STRING_BYTES = 200
MIN_STRING_LENGTH = 3
PRINTABLE_FIRST = 32
PRINTABLE_LIMIT = 127
STRING_WHITESPACE = (9, 10)
WORD_SIZE = 4
ARM_PC_OFFSET = 8  # the pc reads as the current instruction + 8 in ARM mode, + 4 in Thumb
THUMB_PC_OFFSET = 4
BRANCH_MNEMONICS = ('bl', 'blx', 'b')


@dataclass
class Binary:
    data: bytes
    segments: list[tuple[int, int, int]]  # (virtual address, file offset, file size)
    functions: dict[str, tuple[int, int]] = field(default_factory=dict)  # name -> (address, size)
    names_by_address: dict[int, str] = field(default_factory=dict)

    def read(self, address: int, length: int) -> bytes | None:
        for virtual_address, file_offset, file_size in self.segments:
            if virtual_address <= address < virtual_address + file_size:
                start = file_offset + address - virtual_address

                return self.data[start:start + length]

        return None

    def c_string(self, address: int) -> str | None:
        raw = self.read(address, MAX_STRING_BYTES)
        if not raw:
            return None
        text = raw.split(b'\0')[0]
        printable = all(PRINTABLE_FIRST <= char < PRINTABLE_LIMIT or char in STRING_WHITESPACE for char in text)
        if len(text) >= MIN_STRING_LENGTH and printable:
            return text.decode()

        return None


def load_symbols(elf: ELFFile, binary: Binary) -> None:
    for section in elf.iter_sections():
        if section.header['sh_type'] not in ('SHT_SYMTAB', 'SHT_DYNSYM'):
            continue
        for symbol in section.iter_symbols():
            if symbol['st_info']['type'] == 'STT_FUNC' and symbol['st_value']:
                binary.functions[symbol.name] = (symbol['st_value'], symbol['st_size'])
                binary.names_by_address[symbol['st_value'] & ~1] = symbol.name


def load_plt_names(elf: ELFFile, binary: Binary) -> None:
    """PLT entries resolve through .rel.plt."""
    plt = elf.get_section_by_name('.plt')
    relocations = elf.get_section_by_name('.rel.plt')
    if not plt or not relocations:
        return
    dynamic_symbols = elf.get_section(relocations['sh_link'])
    for index, relocation in enumerate(relocations.iter_relocations()):
        address = plt['sh_addr'] + PLT_HEADER_SIZE + PLT_ENTRY_SIZE * index
        binary.names_by_address[address] = dynamic_symbols.get_symbol(relocation['r_info_sym']).name + '@plt'


def load_binary(path: str) -> Binary:
    with open(path, 'rb') as file:
        elf = ELFFile(file)
        segments = [(segment['p_vaddr'], segment['p_offset'], segment['p_filesz'])
                    for segment in elf.iter_segments() if segment['p_type'] == 'PT_LOAD']
        file.seek(0)
        binary = Binary(file.read(), segments)
        load_symbols(elf, binary)
        load_plt_names(elf, binary)

    return binary


def literal_note(binary: Binary, instruction: Any, thumb: int) -> str:
    try:
        immediate = int(instruction.op_str.split('#')[-1].rstrip(']'), 0)
    except ValueError:
        immediate = 0
    pc_offset = THUMB_PC_OFFSET if thumb else ARM_PC_OFFSET
    literal_address = ((instruction.address + pc_offset) & ~3) + immediate
    literal = binary.read(literal_address, WORD_SIZE)
    if not literal:
        return ''
    value = int.from_bytes(literal, 'little')
    text = binary.c_string(value)
    string_note = f' "{text}"' if text else ''
    symbol_note = f' <{binary.names_by_address[value]}>' if value in binary.names_by_address else ''

    return f'  ; ={value:#x}' + string_note + symbol_note


def instruction_note(binary: Binary, instruction: Any, thumb: int) -> str:
    note = ''
    if instruction.mnemonic.startswith('ldr') and '[pc' in instruction.op_str:
        note = literal_note(binary, instruction, thumb)
    if instruction.mnemonic in BRANCH_MNEMONICS and instruction.op_str.startswith('#'):
        target = int(instruction.op_str[1:], 0)
        if target in binary.names_by_address:
            note = f'  ; <{binary.names_by_address[target]}>'

    return note


def disassemble(binary: Binary, name: str) -> None:
    address, size = binary.functions[name]
    thumb = address & 1
    disassembler = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    address &= ~1
    print(f'==== {name} @ {address:08x} ({size} bytes, {"thumb" if thumb else "arm"})')
    for instruction in disassembler.disasm(binary.read(address, size), address):
        note = instruction_note(binary, instruction, thumb)
        print(f'{instruction.address:08x}: {instruction.mnemonic:8s} {instruction.op_str}{note}')


def list_functions(binary: Binary, name_filter: str) -> None:
    for name, (address, size) in sorted(binary.functions.items(), key=lambda item: item[1]):
        if name_filter in name:
            print(f'{address:08x} {size:6d} {name}')


def main() -> None:
    binary = load_binary(sys.argv[1])
    if sys.argv[2] == '--list':
        list_functions(binary, sys.argv[3] if len(sys.argv) > 3 else '')

        return
    for name in sys.argv[2:]:
        disassemble(binary, name)


if __name__ == '__main__':
    main()
