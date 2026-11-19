#!/usr/bin/env python3
"""Scan data region for length-prefixed strings: data[d]=len, data[d+1..d+1+len] printable."""
data = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM','rb').read()
N = len(data)
def dec(b): return b.decode('cp437', errors='replace').replace('\x84','ä').replace('\x94','ö')
def isp(x): return (0x20 <= x < 0x7F) or x in (0x84,0x94)

hits=[]
for d in range(0x03, 0x2C7C):
    if 0x46 <= d <= 0xCF5: continue  # map region
    ln = data[d]
    if not (4 <= ln <= 150): continue
    if d+1+ln > N: continue
    body = data[d+1:d+1+ln]
    if all(isp(x) for x in body):
        hits.append((d, ln, dec(body)))
print(len(hits))
for d, ln, txt in hits:
    print(f"mem 0x{d+0x100:04X} file 0x{d:04X} len={ln:3d} {txt!r}")
