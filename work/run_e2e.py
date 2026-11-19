#!/usr/bin/env python3
"""End-to-end run of SPU.COM in the emulator, from the COM entry point.

Usage: run_e2e.py [--keys 'nkf'] [--steps N] [--stop 0xADDR] [--dump] [--trace N]
  --keys:  characters to inject when the game blocks on a key read
  --steps: max emulator steps
  --stop:  stop when IP reaches this address (hex)
  --dump:  dump game state (segments, key reals, map) at stop
  --trace: print last N IPs before stop
"""
import sys
sys.path.insert(0, '/home/neuro/reverse/spurgux/work')
from emu16 import CPU, EmuError

SPU = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM', 'rb').read()
SEG = 0x0025


def real3(c, off):
    ds = c.ds
    return tuple(c.rw(ds, off + i) for i in range(3))


def dump_state(c):
    ds = c.ds
    print(f"--- state dump ---")
    print(f"ds={c.ds:04X} ss={c.ss:04X} sp={c.sp:04X} bp={c.bp:04X}")
    r308 = real3(c, 0x308)
    r30E = real3(c, 0x1132)
    print(f"[0x308] r308 = {r308[0]:04X} {r308[1]:04X} {r308[2]:04X}")
    print(f"[0x1132] r30E = {r30E[0]:04X} {r30E[1]:04X} {r30E[2]:04X}")
    print(f"[0x2EA] Mk = {c.rw(ds, 0x2EA)}")
    v = real3(c, 0x2FC)
    print(f"[0x2FC] Voima = {v[0]:04X} {v[1]:04X} {v[2]:04X}")
    p = real3(c, 0x302)
    print(f"[0x302] Prom = {p[0]:04X} {p[1]:04X} {p[2]:04X}")
    print(f"[0x2CC] keyflag = {c.rb(ds, 0x2CC):02X}  [0x2D7]={c.rw(ds,0x2D7):04X} [0x2D8]={c.rw(ds,0x2D8):04X}")
    # map dump: x 14..79, y 3..22 ; Map[x,y] @ [0x146 + x*0x28 + y*2]
    print("--- map (x=14..79, y=3..22; . = 0) ---")
    for y in range(3, 23):
        row = []
        for x in range(14, 80):
            v = c.rw(ds, 0x146 + x * 0x28 + y * 2)
            row.append('.' if v == 0 else f'{v:02X}')
        print(f"y{y:2d}: " + ' '.join(row))


def main():
    keys = []
    steps = 5_000_000
    stop = None
    dump = False
    trace = 0
    args = sys.argv[1:]
    if '--keys' in args:
        i = args.index('--keys')
        keys = [ord(ch) for ch in args[i + 1]]
    if '--steps' in args:
        i = args.index('--steps')
        steps = int(args[i + 1])
    if '--stop' in args:
        i = args.index('--stop')
        stop = int(args[i + 1], 16)
    if '--dump' in args:
        dump = True
    if '--trace' in args:
        i = args.index('--trace')
        trace = int(args[i + 1])

    c = CPU(SPU, com_seg=SEG)
    if stop is not None:
        c.stop_ip = stop
    ips = []
    n = 0
    try:
        while not c.halt and n < steps:
            ip = c.ip
            if trace:
                ips.append(ip)
                if len(ips) > trace:
                    ips.pop(0)
            ah = c.ah
            is_int16_read = (c.rb(c.cs, ip) == 0xCD and c.rb(c.cs, ip + 1) == 0x16 and ah == 0)
            is_int21_read = (c.rb(c.cs, ip) == 0xCD and c.rb(c.cs, ip + 1) == 0x21 and ah == 1)
            if (is_int16_read or is_int21_read) and not c.keys and keys:
                c.keys.append(keys.pop(0))
            c.step()
            n += 1
    except EmuError as e:
        print(f"EmuError after {n} steps (ip={c.ip:04X}): {e}")
        if trace:
            print(f"last ips: {[f'{a:04X}' for a in ips]}")
        return

    print(f"done: {n} steps, halt={c.halt}, ip={c.ip:04X}")
    if trace:
        print(f"last ips: {[f'{a:04X}' for a in ips]}")
    if c.out:
        print("console out:", bytes(c.out).decode('latin1', 'replace'))
    if dump:
        dump_state(c)


if __name__ == '__main__':
    main()
