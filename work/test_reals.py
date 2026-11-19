#!/usr/bin/env python3
"""Run SPU.COM's real-arithmetic thunks in the emulator to pin down
the 6-byte REAL format and all operation semantics (ground truth).

Thunk map (emulator-verified):
  conv   0x1B2F: ax=int in -> pushes (w1,w2,w3), w1 on top
  i2r    0x1B38: pops 3w real (discarded) + 1w int, pushes conv(int); net -1w
  load   0x1973: di=addr (push ds before call) -> pushes 3w real
  store  0x1993: 3w real below di (push ds, di before call); consumes them
  ADD    0x19A6 (body 0x164A), SUB 0x19C1 (body 0x1652), A first pushed
  MUL    0x19D0 (body 0x1745), DIV 0x19DF (body 0x17BF)
  CMP    0x1A0E ==  0x1A28 <>  0x1A42 >=  0x1A5C <=  0x1A76 >  0x1A90 <
  Trunc  0x1AFA: pops 3w real, result int in AX (e>14 -> err 0x92, e<-1 -> 0)
  Round  0x1AF6: same + round-half-away-from-zero (never called in binary)

Format: value = (-1)^(w3.15) * (1 + M/2^23) * 2^(w1.lo - 0x81)
        M = ((w3>>8)&0x7F)<<16 | (w3&0xFF)<<8 | (w2>>8)
        w1.hi = guard/extra byte (not part of value)
"""
import sys
sys.path.insert(0, '/home/neuro/reverse/spurgux/work')
from emu16 import CPU

SPU = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM', 'rb').read()

def fresh():
    return CPU(SPU)

def conv(c, n):
    c.ax = n & 0xFFFF
    c.call_proc(0x1B2F)
    return (c.rw(0, c.sp), c.rw(0, c.sp + 2), c.rw(0, c.sp + 4))

def push_real(c, w):
    c.push(w[2]); c.push(w[1]); c.push(w[0])

def pop_real(c):
    w1 = c.pop(); w2 = c.pop(); w3 = c.pop()
    return (w1, w2, w3)

def binop(c, a, b, thunk):
    """A = a (pushed first/bottom), B = b (pushed last/top)."""
    push_real(c, a)
    push_real(c, b)
    c.call_proc(thunk)
    return pop_real(c)

def cmpop(c, a, b, thunk):
    push_real(c, a)
    push_real(c, b)
    c.call_proc(thunk)
    return c.ax

def truncop(c, a, b, thunk=0x1AFA):
    """Trunc/Round of (a/b); returns signed int from AX."""
    push_real(c, a)
    push_real(c, b)
    c.call_proc(0x19DF)
    c.call_proc(thunk)
    v = c.ax
    return v - 0x10000 if v & 0x8000 else v

def fmt(w):
    return '(%04X %04X %04X)' % w

def decode(w):
    w1, w2, w3 = w
    al = w1 & 0xFF
    if al == 0:
        return 0.0
    e = al - 0x81
    sign = -1 if (w3 >> 15) & 1 else 1   # sign = dh.7 = w3 bit 15
    M = (((w3 >> 8) & 0x7F) << 16) | ((w3 & 0xFF) << 8) | (w2 >> 8)
    return sign * (1 + M / 2 ** 23) * (2 ** e)

def main():
    print("=== conv table ===")
    for n in list(range(0, 17)) + [31, 32, 63, 64, 100, 1000, 10000, 32767, 32768, 65535, 65534, 65533]:
        c = fresh()
        w = conv(c, n)
        signed = n - 0x10000 if n >= 0x8000 else n
        ok = 'OK' if abs(decode(w) - signed) < max(1e-9, abs(signed) * 2 ** -23) else 'MISMATCH'
        print(f"conv({signed:5d}) = {fmt(w)}  dec={decode(w):.6f}  {ok}")

    print()
    print("=== ADD (0x19A6 wrapper; body 0x164A): A + B, A=first pushed ===")
    fails = 0
    for a, b in [(10, 3), (7, 7), (100, 23), (0, 5), (-5, 5), (-3, -4), (10, -3)]:
        c = fresh()
        r = binop(c, conv(c, a & 0xFFFF), conv(c, b & 0xFFFF), 0x19A6)
        exp = conv(c, (a + b) & 0xFFFF)
        ok = r == exp
        fails += 0 if ok else 1
        print(f"{a} + {b} = {fmt(r)}  (conv({a+b}) = {fmt(exp)})  {'OK' if ok else 'MISMATCH'}")
    # 32767+1 = 32768: not representable as int16, but the real format can hold +32768.0
    c = fresh()
    r = binop(c, conv(c, 32767), conv(c, 1), 0x19A6)
    ok = abs(decode(r) - 32768.0) < 1e-9
    fails += 0 if ok else 1
    print(f"32767 + 1 = {fmt(r)}  dec={decode(r)}  (exp 32768.0, int16 oracle wraps)  {'OK' if ok else 'MISMATCH'}")
    print(f"ADD: {'ALL OK' if not fails else str(fails) + ' FAIL'}")

    print()
    print("=== SUB (0x19C1 wrapper; body 0x1652): A - B, A=first pushed ===")
    fails = 0
    for a, b in [(10, 3), (3, 10), (5, 5), (100, 23), (-5, 5), (-3, -4), (0, -7)]:
        c = fresh()
        r = binop(c, conv(c, a & 0xFFFF), conv(c, b & 0xFFFF), 0x19C1)
        exp = conv(c, (a - b) & 0xFFFF)
        ok = r == exp
        fails += 0 if ok else 1
        print(f"{a} - {b} = {fmt(r)}  (conv({a-b}) = {fmt(exp)})  {'OK' if ok else 'MISMATCH'}")
    print(f"SUB: {'ALL OK' if not fails else str(fails) + ' FAIL'}")

    print()
    print("=== MUL (0x19D0 wrapper; body 0x1745): A * B ===")
    fails = 0
    for a, b in [(3, 4), (10, 7), (100, 100), (0, 99), (-3, 4), (3, -4), (-3, -4), (-100, 7)]:
        c = fresh()
        r = binop(c, conv(c, a & 0xFFFF), conv(c, b & 0xFFFF), 0x19D0)
        exp = conv(c, (a * b) & 0xFFFF)
        ok = r == exp
        fails += 0 if ok else 1
        print(f"{a} * {b} = {fmt(r)}  (conv({a*b}) = {fmt(exp)})  {'OK' if ok else 'MISMATCH'}")
    print(f"MUL: {'ALL OK' if not fails else str(fails) + ' FAIL'}")

    print()
    print("=== DIV (0x19DF wrapper; body 0x17BF): A / B ===")
    fails = 0
    for a, b in [(12, 3), (10, 2), (7, 2), (1, 3), (-12, 3), (12, -3), (-12, -3), (-7, 2), (5, 5)]:
        c = fresh()
        r = binop(c, conv(c, a & 0xFFFF), conv(c, b & 0xFFFF), 0x19DF)
        d = decode(r)
        expd = a / b
        ok = abs(d - expd) < max(1e-9, abs(expd) * 2 ** -23)
        fails += 0 if ok else 1
        print(f"{a} / {b} = {fmt(r)}  dec={d:.9f}  (exp {expd:.9f})  {'OK' if ok else 'MISMATCH'}")
    print(f"DIV: {'ALL OK' if not fails else str(fails) + ' FAIL'}")

    print()
    print("=== CMP (0x1A0E ==, 0x1A28 <>, 0x1A42 >=, 0x1A5C <=, 0x1A76 >, 0x1A90 <) ===")
    # Body 0x18B4: same-sign -> unsigned lex compare of (exp, w3, w2, w1.hi):
    #   CF = A<B, ZF = A==B (exact game semantics).
    # Different signs -> CF = signA (correct), but ZF = ((A.w3 << 1) == 0), i.e. a
    # SPURIOUS ZF when A.w3 in {0x0000, 0x8000} (|A| is a power of two). Game quirk.
    def oracle(op, x, y):
        if (x[2] >> 15) != (y[2] >> 15):
            # different-signs fast path: CF = signA (correct);
            # ZF = ((A.w3 << 1) == 0) -- spurious when A.w3 in {0x0000, 0x8000}
            cf = x[2] >> 15
            zf = 1 if x[2] in (0x0000, 0x8000) else 0
        else:
            a = (x[0] & 0xFF, x[2], x[1], x[0] >> 8)
            b = (y[0] & 0xFF, y[2], y[1], y[0] >> 8)
            if a[0] == 0 and b[0] == 0:
                cf, zf = 0, 1
            elif x[2] >> 15:  # both negative: body does cmc -> reversed order
                cf, zf = (1 if a > b else 0), (1 if a == b else 0)
            else:
                cf, zf = (1 if a < b else 0), (1 if a == b else 0)
        return {"=": zf, "<>": 1 - zf, ">=": 1 - cf, "<=": cf | zf, ">": (1 - cf) & (1 - zf), "<": cf}[op]
    fails = 0
    for name, thunk in [("=", 0x1A0E), ("<>", 0x1A28), (">=", 0x1A42), ("<=", 0x1A5C), (">", 0x1A76), ("<", 0x1A90)]:
        c = fresh()
        r5 = conv(c, 5); r6 = conv(c, 6); rn1 = conv(c, 0xFFFF); r4 = conv(c, 4); r0 = conv(c, 0)
        rn4 = conv(c, 0xFFFC); rn5 = conv(c, 0xFFFB)
        res = []
        okall = True
        for (x, y) in [(r5, r6), (r6, r5), (r5, r5), (rn1, r5), (r4, rn1), (rn1, r4),
                       (r0, r0), (r0, r5), (r5, r0), (rn4, rn5), (rn5, rn4), (rn1, r0)]:
            c2 = fresh()
            got = cmpop(c2, x, y, thunk)
            exp = oracle(name, x, y)
            okall = okall and (got == exp)
            res.append(f"({decode(x):.0f},{decode(y):.0f})={got}")
        fails += 0 if okall else 1
        print(f"0x{thunk:04X} {name}: {', '.join(res)}  {'OK' if okall else 'MISMATCH'}")
    print(f"CMP: {'ALL OK' if not fails else str(fails) + ' FAIL (game ==/>= quirks reproduced)'}")

    print()
    print("=== Trunc (0x1AFA): Trunc(A/B) -> AX ===")
    fails = 0
    for a, b, exp in [(7, 2, 3), (5, 2, 2), (11, 2, 5), (1, 3, 0), (100, 10, 10),
                      (99, 10, 9), (-7, 2, -3), (7, -2, -3), (3, 2, 1), (16, 1, 16),
                      (1, 1, 1), (32767, 1, 32767), (65535, 1, -1)]:
        c = fresh()
        got = truncop(c, conv(c, a & 0xFFFF), conv(c, b & 0xFFFF))
        ok = got == exp
        fails += 0 if ok else 1
        print(f"Trunc({a}/{b}) = {got}  (exp {exp})  {'OK' if ok else 'MISMATCH'}")
    print(f"Trunc: {'ALL OK' if not fails else str(fails) + ' FAIL'}")

    print()
    print("=== Round (0x1AF6): Round(A/B) -> AX (half away from zero) ===")
    fails = 0
    for a, b, exp in [(7, 2, 4), (5, 2, 3), (11, 2, 6), (1, 3, 0), (100, 10, 10),
                      (99, 10, 10), (-7, 2, -4), (7, -2, -4), (3, 2, 2), (1, 1, 1)]:
        c = fresh()
        got = truncop(c, conv(c, a & 0xFFFF), conv(c, b & 0xFFFF), thunk=0x1AF6)
        ok = got == exp
        fails += 0 if ok else 1
        print(f"Round({a}/{b}) = {got}  (exp {exp})  {'OK' if ok else 'MISMATCH'}")
    print(f"Round: {'ALL OK' if not fails else str(fails) + ' FAIL'}")

if __name__ == '__main__':
    main()
