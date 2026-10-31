#!/usr/bin/env python3
"""Disassembles a function of an ARM .ko with relocations resolved. Usage: kodis.py <module.ko> <function>"""
import re
import sys
from typing import Any

from capstone import CS_ARCH_ARM, CS_MODE_ARM, Cs
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

PC_LITERAL_RE = re.compile(r'(\w+), \[pc, #(-?0x[0-9a-f]+)\]')
ARM_PC_OFFSET = 8  # the pc reads as the current instruction + 8 in ARM mode
WORD_SIZE = 4


def relocation_names(elf: ELFFile, section_name: str, symbols: list[Any]) -> dict[int, str]:
    section_names = {index: section.name for index, section in enumerate(elf.iter_sections())}
    names = {}
    for section in elf.iter_sections():
        if not isinstance(section, RelocationSection) or elf.get_section(section['sh_info']).name != section_name:
            continue
        for relocation in section.iter_relocations():
            symbol = symbols[relocation['r_info_sym']]
            names[relocation['r_offset']] = symbol.name or section_names.get(symbol['st_shndx'], '?')

    return names


def literal_note(instruction: Any, code: bytes, relocations: dict[int, str]) -> str:
    literal = PC_LITERAL_RE.match(instruction.op_str)
    if not instruction.mnemonic.startswith('ldr') or not literal:
        return ''
    address = instruction.address + ARM_PC_OFFSET + int(literal.group(2), 16)
    value = int.from_bytes(code[address:address + WORD_SIZE], 'little')
    symbol_note = '<' + relocations[address] + '>' if address in relocations else ''

    return f'  ={value:#x}' + symbol_note


def branch_note(instruction: Any, local_functions: dict[int, str]) -> str:
    if not instruction.mnemonic.startswith('b'):
        return ''
    try:
        target = int(instruction.op_str.lstrip('#'), 16)
    except ValueError:
        return ''
    if target not in local_functions:
        return ''

    return '  [' + local_functions[target] + ']'


def main() -> None:
    path = sys.argv[1]
    function_name = sys.argv[2]
    with open(path, 'rb') as file:
        elf = ELFFile(file)
        symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
        function = [symbol for symbol in symbols if symbol.name == function_name][0]
        section = elf.get_section(function['st_shndx'])
        code = section.data()
        relocations = relocation_names(elf, section.name, symbols)
    local_functions = {symbol['st_value']: symbol.name for symbol in symbols
                       if symbol['st_shndx'] == function['st_shndx'] and symbol.name
                       and symbol['st_info']['type'] == 'STT_FUNC'}
    disassembler = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    start = function['st_value']
    size = function['st_size']
    for instruction in disassembler.disasm(code[start:start + size], start):
        note = ''
        if instruction.address in relocations:
            note = '  <' + relocations[instruction.address] + '>'
        note += literal_note(instruction, code, relocations)
        note += branch_note(instruction, local_functions)
        print(f'{instruction.address:6x}: {instruction.mnemonic:8} {instruction.op_str}{note}')


if __name__ == '__main__':
    main()
