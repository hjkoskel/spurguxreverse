#!/usr/bin/env python3
"""Scan the data region for TP string descriptors [len, ptrlo, ptrhi, ...]
whose pointer targets printable chars. Data region: file 0x03 .. 0x2C7C.
"""
import struct

data = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM','rb').read()
N = len(data)

def dec(b):
    return b.decode('cp437', errors='replace').replace('\x84','ä').replace('\x94','ö')

def printable(b):
    return all((0x20 <= x < 0x7F) or x in (0x84,0x94,0x0D,0x0A,0x09) for x in b)

rows = []
for d in range(0x03, 0x2C7C-5):
    ln = data[d]
    if not (1 <= ln <= 120): continue
    ptr = struct.unpack_from('<H', data, d+1)[0]
    pf = ptr - 0x100
    if not (0x03 <= pf <= 0x2C7C-ln): continue  # chars must be in data region
    body = data[pf:pf+ln]
    if not printable(body): continue
    rows.append((d+0x100, ptr, ln, dec(body)))

print(f"candidates: {len(rows)}")
for mem, ptr, ln, txt in rows:
    print(f"desc@0x{mem:04X} ptr=0x{ptr:04X} len={ln:3d} {txt!r}")
