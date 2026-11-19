#!/usr/bin/env python3
"""Extract inline strings from SPU.COM (v1.17).

The Turbo Pascal RTL writes inline strings via:
    call 0x295D        ; WriteInlineStr  (mem)
    <len><chars...>
    <code resumes>
The call is E8 rel16; mem addr = file off + 0x100, so call at file F
targets file F+3+rel16... target_mem = (F+3+0x100 + rel16) => file target = F+3+rel16.
We also record calls to 0x2975 (WriteLn).
"""
import struct, sys

data = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM','rb').read()
N = len(data)
MEM = 0x100

def s16(x):
    return x - 0x10000 if x >= 0x8000 else x

# find all E8 calls
out = []
i = 0
while i < N-2:
    if data[i] == 0xE8:
        rel = s16(struct.unpack_from('<H', data, i+1)[0])
        tgt = i + 3 + rel
        if tgt == 0x295D - MEM:  # WriteInlineStr (file offset)
            # wait: file offset of 0x295D mem = 0x295D-0x100 = 0x285D
            pass
    i += 1

# simpler: file offset of target mem 0x295D is 0x285D, of 0x2975 is 0x2875
for name, tgt_file in (("STR", 0x285D), ("LN", 0x2875)):
    for i in range(N-2):
        if data[i] != 0xE8:
            continue
        rel = s16(struct.unpack_from('<H', data, i+1)[0])
        tgt = i + 3 + rel
        if tgt == tgt_file:
            p = i + 3
            if p >= N: continue
            ln = data[p]
            body = data[p+1:p+1+ln]
            if ln == 0 or ln > 200: continue
            # printable-ish check: allow >=0x20 and our two special 0x84/0x94
            ok = all((0x20 <= b < 0x7F) or b in (0x84, 0x94) for b in body)
            out.append((i + MEM, name, ln, body, ok))

# dedupe, keep order
out.sort()
print(f"total call-sites: {len(out)}", file=sys.stderr)
def dec(b):
    return b.decode('cp437').replace('\x84','ä').replace('\x94','ö')
for mem, name, ln, body, ok in out:
    tag = "" if ok else "  !!NOT-OK"
    print(f"0x{mem:04X} [{name}] len={ln}{tag}: {dec(body)!r}")
