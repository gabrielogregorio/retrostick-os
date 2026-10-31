#!/usr/bin/env python3
"""Summarizes the logs the stick leaves on the SD card (log-steps.txt and log-stick.txt).

Usage: python3 tools/analyze_logs.py <folder with the logs>
"""
import os
import re
import sys

BOOT_LINE_RE = re.compile(r'insmod|fb0|/proc/fb|^\d+ |virtual|bpp|mknod|frozen|ended|LAUNCH|QUIT|=== |error|fail',
                          re.IGNORECASE)
PLAYERS = ('P1', 'P2')
PLAYER_FIELD = 1
FIRST_STICK_BYTE_FIELD = 5
BUTTON_BYTE_6_FIELD = 8
BUTTON_BYTE_7_FIELD = 9
STICK_PATTERNS_SHOWN = 12
BITS_PER_BYTE = 8
BUTTON_LABELS_PER_LINE = 4


def print_boot_steps(path: str) -> None:
    print('== BOOT STEPS (log-steps.txt)')
    if not os.path.exists(path):
        print('  (missing: did the menu even run?)')

        return
    with open(path, errors='replace') as log:
        for line in log:
            if BOOT_LINE_RE.search(line):
                print('  ' + line.rstrip())


def button_changes(packets: list[list[str]]) -> list[tuple[str, str]]:
    changes = []
    previous = None
    for packet in packets:
        buttons = (packet[BUTTON_BYTE_6_FIELD], packet[BUTTON_BYTE_7_FIELD])
        if buttons != previous:
            changes.append(buttons)
            previous = buttons

    return changes


def button_label(byte_6: str, byte_7: str) -> str:
    value_6 = int(byte_6, 16)
    value_7 = int(byte_7, 16)
    bits = [f'B6.{bit}' for bit in range(BITS_PER_BYTE) if value_6 >> bit & 1]
    bits += [f'B7.{bit}' for bit in range(BITS_PER_BYTE) if value_7 >> bit & 1]

    return f'{byte_6} {byte_7}' + (f' ({"+".join(bits)})' if bits else ' (released)')


def print_player(player: str, packets: list[list[str]]) -> None:
    player_packets = [packet for packet in packets if packet[PLAYER_FIELD] == player]
    print(f'  {player}: {len(player_packets)} packets logged')
    if not player_packets:
        return
    last_stick_field = FIRST_STICK_BYTE_FIELD + 3
    stick_bytes = sorted({' '.join(packet[FIRST_STICK_BYTE_FIELD:last_stick_field]) for packet in player_packets})
    ellipsis = ' ...' if len(stick_bytes) > STICK_PATTERNS_SHOWN else ''
    print(f'     bytes 3-5 seen ({len(stick_bytes)}): {", ".join(stick_bytes[:STICK_PATTERNS_SHOWN])}{ellipsis}')
    print('     button sequence (B6 B7), in the order they changed:')
    labels = [button_label(byte_6, byte_7) for byte_6, byte_7 in button_changes(player_packets)]
    for start in range(0, len(labels), BUTTON_LABELS_PER_LINE):
        print('       ' + '  |  '.join(labels[start:start + BUTTON_LABELS_PER_LINE]))


def print_menu_and_controllers(path: str) -> None:
    print('\n== MENU AND CONTROLLERS (log-stick.txt)')
    if not os.path.exists(path):
        print('  (missing)')

        return
    with open(path) as log:
        lines = log.readlines()
    for line in lines:
        if line.startswith('step:'):
            print('  ' + line.rstrip())
    packets = [line.split() for line in lines if line.startswith('t=')]
    for player in PLAYERS:
        print_player(player, packets)


def main() -> None:
    folder = sys.argv[1]
    print_boot_steps(os.path.join(folder, 'log-steps.txt'))
    print_menu_and_controllers(os.path.join(folder, 'log-stick.txt'))


if __name__ == '__main__':
    main()
