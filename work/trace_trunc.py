#!/usr/bin/env python3
"""Replay 0x48A5..0x48D0 with Level=1, printing every out-of-range jump."""
import sys, re
sys.path.insert(0, '/home/neuro/reverse/spurgux/work')
from emu16 import CPU

SPU = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM', 'rb').read()
c = CPU(SPU)
c.lww(0, 0x2F4, 1)
c.ip = 0x48A5

lab = {}
for ln in open('/home/neuro/reverse/spurgux/work/full_disasm.txt'):
    m = re.match(r'^(0000[0-9A-Fa-f]{4})\s+([0-9A-Fa-f ]{2,14})\s+(.*)$', ln)
    if m:
        lab[int(m.group(1), 16)] = m.group(3).strip()

prev = c.ip
steps = 0
while c.ip != 0x48D3 and steps < 50000:
    c.step()
    steps += 1
    ip = c.ip
    if not (0x400 <= ip <= 0xE8E6) or (0x200 <= ip < 0x400):
        # print a window of context: last 6 in-range steps
        print(f"!! left code range at ip={ip:04X} (step {steps}); prev={prev:04X}")
        break
    if ip >= 0x48B0:
        sp = c.sp
        st = [c.rw(0, (sp + 2 * i) & 0xFFFF) for i in range(7)]
        print(f"ip={ip:04X} ax={c.ax:04X} bx={c.bx:04X} cx={c.cx:04X} dx={c.dx:04X} "
              f"st={[f'{w:04X}' for w in st]}  {lab.get(ip, '?')[:36]}")
    prev = ip
print(f"\nend: ip={c.ip:04X} steps={steps} [0xE38]={c.rw(0, 0xE38)}")
