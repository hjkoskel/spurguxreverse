#!/usr/bin/env python3
"""Scan game code region (file 0x2C7C..0xE7E5) for length-prefixed printable
runs: byte L (3..120) followed by L printable bytes.
"""
data = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM','rb').read()
N = len(data)
def dec(b): return b.decode('cp437', errors='replace').replace('\x84','ä').replace('\x94','ö')
def isp(x): return (0x20 <= x < 0x7F) or x in (0x84,0x94)

hits=[]
i = 0x2C7C
while i < 0xE7E5:
    L = data[i]
    if 4 <= L <= 120 and i+1+L <= 0xE7E5:
        body = data[i+1:i+1+L]
        if all(isp(x) for x in body):
            # sanity: next byte after string should look like code or another len
            hits.append((i+0x100, L, dec(body)))
    i += 1
print(len(hits))
for mem, L, txt in hits:
    print(f"0x{mem:04X} len={L:3d} {txt!r}")
