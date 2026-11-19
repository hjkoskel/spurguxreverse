#!/usr/bin/env python3
"""Isolated experiment: Trunc thunk (0x1AFA) calling convention.
Manually push retaddr, enter 0x1AFA, step until IP leaves the thunk."""
import sys
sys.path.insert(0, '/home/neuro/reverse/spurgux/work')
from emu16 import CPU

SPU = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM', 'rb').read()

def conv_words(c, n):
    c.ax = n & 0xFFFF
    c.call_proc(0x1B2F)
    return (c.rw(0, c.sp), c.rw(0, c.sp + 2), c.rw(0, c.sp + 4))

def div72(c):
    a = conv_words(c, 7)
    b = conv_words(c, 2)
    c.push(a[2]); c.push(a[1]); c.push(a[0])
    c.push(b[2]); c.push(b[1]); c.push(b[0])
    c.call_proc(0x19DF)
    return (c.rw(0, c.sp), c.rw(0, c.sp + 2), c.rw(0, c.sp + 4))

def decode(w):
    w1, w2, w3 = w
    al = w1 & 0xFF
    if al == 0:
        return 0.0
    e = al - 0x81
    sign = -1 if (w3 >> 7) & 1 else 1
    M = (((w3 >> 8) & 0x7F) << 16) | ((w3 & 0xFF) << 8) | (w2 >> 8)
    return sign * (1 + M / 2 ** 23) * (2 ** e)

def trunc_case(name, extras):
    c = CPU(SPU)
    r = div72(c)
    for x in extras:
        c.push(x)
    fake_ret = 0x0000
    c.push(fake_ret)
    c.ip = 0x1AFA
    steps = 0
    while 0x1AFA <= c.ip <= 0x1B2E and steps < 500:
        c.step()
        steps += 1
    st = [c.rw(0, (c.sp + 2 * i) & 0xFFFF) for i in range(6)]
    print(f"{name}: div=({r[0]:04X} {r[1]:04X} {r[2]:04X}) val={decode(r):.6f}")
    print(f"   landed ip={c.ip:04X} ax={c.ax:04X} bx={c.bx:04X} cx={c.cx:04X} dx={c.dx:04X} "
          f"st={[f'{w:04X}' for w in st]}")
    print()

trunc_case("A: [w1,w2,w3]        ", [])
trunc_case("B: [X,w1,w2,w3]      ", [0x0030])
trunc_case("C: [X,Y,w1,w2,w3]    ", [0x0004, 0x000A])
trunc_case("D: [w1,w2,w3,X]      ", [])  # placeholder
