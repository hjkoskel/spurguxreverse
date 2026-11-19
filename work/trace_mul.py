#!/usr/bin/env python3
"""Trace emulator execution of MUL(3,4) instruction by instruction."""
import sys
sys.path.insert(0, '/home/neuro/reverse/spurgux/work')
from emu16 import CPU

SPU = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM', 'rb').read()
c = CPU(SPU)

def conv(c, n):
    c.ax = n & 0xFFFF
    c.call_proc(0x1B2F)
    return (c.rw(0, c.sp), c.rw(0, c.sp + 2), c.rw(0, c.sp + 4))

wa = conv(c, 3)
wb = conv(c, 4)
print(f"A={wa} B={wb}", flush=True)
c.push(wa[2]); c.push(wa[1]); c.push(wa[0])
c.push(wb[2]); c.push(wb[1]); c.push(wb[0])
c.push(0x0000)   # return address for wrapper's pop [0x186]
c.ip = 0x19D0  # MUL wrapper

import re
lab = {}
for ln in open('/home/neuro/reverse/spurgux/work/full_disasm.txt'):
    m = re.match(r'^(0000[0-9A-Fa-f]{4})\s+([0-9A-Fa-f ]{2,14})\s+(.*)$', ln)
    if m:
        lab[int(m.group(1), 16)] = m.group(3).strip()

END = {0x19B8}   # tail: about to `jmp [0x186]`; result words already pushed
count = 0
out = 0
while count < 3000:
    ip = c.ip
    if ip in END:
        print(f"REACHED TAIL at ip={ip:04X} after {count} steps", flush=True)
        break
    if 0x1745 <= ip <= 0x188B or 0x19D0 <= ip <= 0x19F5:
        m221 = c.rw(0, 0x221)
        print(f"ip={ip:04X} ax={c.ax:04X} bx={c.bx:04X} cx={c.cx:04X} dx={c.dx:04X} di={c.di:04X} si={c.si:04X} [221]={m221:02X}  {lab.get(ip, '?')[:44]}", flush=True)
        out += 1
        if out > 600:
            print("... too many in-range")
            break
    elif not (0x1658 <= ip <= 0x18DE):
        print(f"!! OUT-OF-RANGE ip={ip:04X}  {lab.get(ip, '?')[:44]}", flush=True)
        if out > 600:
            break
    c.step()
    count += 1
else:
    print(f"STEP BUDGET EXHAUSTED at ip={c.ip:04X} after {count} steps", flush=True)
    print(f"  {lab.get(c.ip, '?')}")
print("final ip = %04X" % c.ip)
w1 = c.pop(); w2 = c.pop(); w3 = c.pop()
print(f"result = ({w1:04X} {w2:04X} {w3:04X})")
