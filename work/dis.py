#!/usr/bin/env python3
"""CFG-aware disassembler with inline-string skipping (SPU.COM v1.17).
mem addr = file offset + 0x100. After call 0x295D (WriteStr) / 0x2975 (WriteLn)
the next byte is <len> followed by <len> chars; execution resumes after.

Usage: dis.py <startMem> <endMem> [out]
"""
import subprocess, sys

BIN = '/home/neuro/reverse/spurgux/spurg117/SPU.COM'
data = open(BIN, 'rb').read()
N = len(data)

def s16(x): return x - 0x10000 if x >= 0x8000 else x

def decode_chunk(fpos, nbytes):
    """Decode instructions from file offset fpos; returns list of
    (memaddr, bytes, mnemonic, op) until out of range or a non-instruction."""
    chunk = data[fpos:fpos + nbytes]
    out = subprocess.run(['ndisasm', '-b', '16', '-o', hex(fpos + 0x100), '-'],
                         input=chunk, capture_output=True).stdout.decode('utf-8', 'replace')
    insns = []
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            break
        addr = int(parts[0], 16)
        blen = len(parts[1]) // 2
        mn = parts[2].split(None, 1)
        insns.append((addr, blen, mn[0], mn[1] if len(mn) > 1 else ''))
    return insns

def walk(start_mem, end_mem, out):
    pos = start_mem - 0x100  # file offset
    emitted = 0
    while pos < end_mem - 0x100:
        # decode a chunk; keep instructions fully within our window
        chunk = decode_chunk(pos, 64)
        if not chunk:
            out.write(f"0x{pos+0x100:04X}  {data[pos]:02x}  ??\n")
            pos += 1
            continue
        # find first instruction at exactly pos
        first = chunk[0]
        if first[0] != pos + 0x100:
            out.write(f"0x{pos+0x100:04X}  {data[pos]:02x}  ??\n")
            pos += 1
            continue
        addr, blen, mn, op = first
        extra = ''
        if mn == 'call' and op.strip() in ('0x295d', '0x2975', '0x11e5'):
            sp = pos + blen
            L = data[sp] if sp < N else 0
            if 0 < L <= 200:
                body = data[sp+1:sp+1+L]
                try:
                    txt = body.decode('cp437').replace('\x84','ä').replace('\x94','ö')
                except Exception:
                    txt = repr(body)
                extra = f'   ; STR len={L} {txt!r}'
                pos = sp + 1 + L
            else:
                pos = sp + 1
        else:
            pos += blen
        out.write(f"0x{addr:04X}  {mn} {op}{extra}\n")
        emitted += 1
        if emitted > 200000:
            break

if __name__ == '__main__':
    s = int(sys.argv[1], 16)
    e = int(sys.argv[2], 16)
    out = open(sys.argv[3], 'w') if len(sys.argv) > 3 else sys.stdout
    walk(s, e, out)
