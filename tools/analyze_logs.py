#!/usr/bin/env python3
"""Summarizes the logs the stick leaves on the SD card (log-steps.txt and log-stick.txt).

Usage: python3 tools/analyze_logs.py <folder with the logs>
"""
import re, sys, os
from collections import OrderedDict

d = sys.argv[1]
steps_log = os.path.join(d, 'log-steps.txt')
stick_log = os.path.join(d, 'log-stick.txt')
print('== BOOT STEPS (log-steps.txt)')
if os.path.exists(steps_log):
    for l in open(steps_log, errors='replace'):
        if re.search(r'insmod|fb0|/proc/fb|^\d+ |virtual|bpp|mknod|frozen|ended|LAUNCH|QUIT|=== |error|fail', l, re.I):
            print('  ' + l.rstrip())
else:
    print('  (missing: did the menu even run?)')

print('\n== MENU AND CONTROLLERS (log-stick.txt)')
if not os.path.exists(stick_log):
    print('  (missing)'); sys.exit()
for l in open(stick_log):
    if l.startswith('step:'): print('  ' + l.rstrip())
packets = [l.split() for l in open(stick_log) if l.startswith('t=')]
for player in ('P1', 'P2'):
    lines = [p for p in packets if p[1] == player]
    print(f'  {player}: {len(lines)} packets logged')
    if not lines:
        continue
    b345 = sorted({' '.join(p[5:8]) for p in lines})
    print(f'     bytes 3-5 seen ({len(b345)}): {", ".join(b345[:12])}{" ..." if len(b345) > 12 else ""}')
    seq = OrderedDict()
    prev = None
    order = []
    for p in lines:
        b = (p[8], p[9])
        if b != prev:
            order.append(b)
            prev = b
    print('     button sequence (B6 B7), in the order they changed:')
    labels = []
    for b6, b7 in order:
        v6, v7 = int(b6, 16), int(b7, 16)
        bits = [f'B6.{i}' for i in range(8) if v6 >> i & 1] + [f'B7.{i}' for i in range(8) if v7 >> i & 1]
        labels.append(f'{b6} {b7}' + (f' ({"+".join(bits)})' if bits else ' (released)'))
    for i in range(0, len(labels), 4):
        print('       ' + '  |  '.join(labels[i:i + 4]))
