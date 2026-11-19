#!/usr/bin/env python3
"""Find printable runs >=4 chars in the file; for each, look for a
descriptor [len, ptr] in the data region (file 0x03..0x2C7B, excl. map
0x46..0xCF5) pointing to it. Print those that have a descriptor.
"""
import struct

data = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM','rb').read()
N = len(data)

def dec(b):
    return b.decode('cp437', errors='replace').replace('\x84','ä').replace('\x94','ö')

def isprint(x):
    return (0x20 <= x < 0x7F) or x in (0x84, 0x94)

# find maximal printable runs
runs = []
i = 0
while i < N:
    if isprint(data[i]):
        j = i
        while j < N and isprint(data[j]):
            j += 1
        if j - i >= 4:
            runs.append((i, j))
        i = j
    else:
        i += 1
print(f"runs: {len(runs)}")

# index: for each run start, map file offset -> (start,end)
import bisect
starts = [r[0] for r in runs]

def find_run(pf):
    k = bisect.bisect_right(starts, pf) - 1
    if k >= 0 and starts[k] <= pf <= runs[k][1]-1:
        return runs[k]
    return None

# build ptr->run start lookup for runs
ptr2run = {}
for (s, e) in runs:
    for off in range(s, e-3):
        ptr2run[off + 0x100] = (s, e)

results = []
for d in range(0x03, 0x2C7C):
    if 0x46 <= d <= 0xCF5:  # map region
        continue
    if d + 3 > N: break
    ln = data[d]
    if not (4 <= ln <= 150):
        continue
    ptr = struct.unpack_from('<H', data, d+1)[0]
    r = ptr2run.get(ptr)
    if not r:
        continue
    s, e = r
    if e - s < ln:
        continue
    results.append((d + 0x100, ptr, ln, dec(data[ptr-0x100:ptr-0x100+ln]), s))

# dedupe by (ptr,ln)
seen = set()
uniq = []
for mem, ptr, ln, txt, s in results:
    key = (ptr, ln)
    if key in seen: continue
    seen.add(key)
    uniq.append((mem, ptr, ln, txt, s))
uniq.sort(key=lambda r: r[4])
print(f"described strings: {len(uniq)}")
for mem, ptr, ln, txt, s in uniq:
    print(f"desc@0x{mem:04X} data@0x{ptr:04X}(file 0x{s:04X}) len={ln:3d} {txt!r}")
