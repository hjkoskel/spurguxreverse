#!/usr/bin/env python3
"""Extract the indirect string catalog: mov di, X; call 0x1973 sites.
Descriptor at file X-0x100: [len, ptrlo, ptrhi, ...]. For long strings
the chars live at mem ds:[ptr]. For short strings (len<=3) chars inline.
"""
import struct

data = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM','rb').read()
N = len(data)
def s16(x): return x-0x10000 if x>=0x8000 else x

def dec(b):
    return b.decode('cp437', errors='replace').replace('\x84','ä').replace('\x94','ö')

rows = []
seen = {}
for i in range(N-5):
    # BF imm16 then E8 rel16 -> 0x1973 (file 0x1873)
    if data[i] != 0xBF: continue
    imm = struct.unpack_from('<H', data, i+1)[0]
    if i+3 >= N-2 or data[i+3] != 0xE8: continue
    rel = s16(struct.unpack_from('<H', data, i+4)[0])
    if i+6+rel != 0x1873: continue
    site = i + 0x100
    d = imm - 0x100  # file offset of descriptor
    if d < 0 or d+6 > N: continue
    ln = data[d]
    ptr = struct.unpack_from('<H', data, d+1)[0]
    if ln <= 3:
        body = data[d+3:d+3+ln]
        src = 'inline'
    else:
        p = ptr - 0x100
        body = data[p:p+ln]
        src = f'@{ptr:04X}'
    txt = dec(body)
    rows.append((site, imm, ln, txt, src))

print(f"total: {len(rows)}")
for site, imm, ln, txt, src in rows:
    print(f"0x{site:04X}  di=0x{imm:04X} len={ln:3d} {txt!r}")
