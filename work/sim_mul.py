#!/usr/bin/env python3
"""Pure-Python reference simulation of SPU.COM's MUL body (0x1745..0x17BE),
helper 0x1856, and shared tail 0x17AF.., per the ndisasm listing.

8086 carry semantics (verified against nasm encodings):
  RCR v,1 : new = (cf_in << (n-1)) | (v >> 1) ; cf_out = v.bit0      (LSB -> CF)
  RCL v,1 : new = ((v << 1) | cf_in) & mask  ; cf_out = v.bit(n-1)   (MSB -> CF)

Entry (from wrapper 0x19D0):
  A = (dx:bx:ax) first-pushed real  (w3:w2:w1); al = A exp byte
  B = (di:si:cx) last-pushed real   (w3:w2:w1); cl = B exp byte
Exit: (dx:bx:ax) result.
"""

def conv_words(n):
    """Reference conv for integers (verified against emulator table)."""
    if n == 0:
        return (0, 0, 0)
    neg = n < 0
    m = abs(n)
    e = m.bit_length() - 1
    if e <= 22:
        M = (m << (23 - e)) - (1 << 23)
    else:
        M = m - (1 << e)
        e = e - 23
    al = 0x81 + e
    w1 = al                      # ah (guard) = 0 for integers
    w2 = M & 0xFFFF
    w3 = (M >> 16) & 0x7F
    if neg:
        w3 |= 0x80
    return (w1, w2, w3)


def mul_body(A, B, mem221=0x00):
    """A,B = (w1,w2,w3). Returns (result (w1,w2,w3), mem221)."""
    ax, bx, dx = A
    cx, si, di = B
    al, ah = ax & 0xFF, (ax >> 8) & 0xFF
    cl, ch = cx & 0xFF, (cx >> 8) & 0xFF

    if cl == 0:                  # 0x1745 or cl,cl; jz 0x17B8
        return (0, 0, 0), mem221
    if al == 0:                  # 0x1749 or al,al; jz 0x17BE
        return (ax, bx, dx), mem221

    s = al + cl                  # 0x174D add al,cl
    cf = 1 if s > 0xFF else 0
    al = s & 0xFF

    # ---- call 0x1856 (helper) ----
    if cf == 0:
        s2 = al + 0x80           # 0x1858
        if s2 <= 0xFF:
            return (0, 0, 0), mem221    # underflow -> 0
        al = s2 & 0xFF
    else:
        s2 = al + 0x80           # 0x1864
        if s2 > 0xFF:
            return (0, 0, 0), mem221    # overflow -> 0, CF=1
        al = s2 & 0xFF
    # 0x186B:
    chx = (((dx ^ di) >> 8) & 0xFF) ^ 0xFF
    mem221 = chx & 0x80          # 0x1878 mov [0x221],ch
    dx |= 0x8000                 # 0x187C or dh,0x80
    di |= 0x8000                 # 0x187F or di,0x8000
    # [0x214]=cx ; [0x216]=si ; [0x218]=di

    # ---- back in body ----
    exp_byte = al                # 0x1752 [0x20E]=al
    a20f = ah                    # [0x20F] = A guard
    a210 = bx                    # [0x210] = A.w2
    a212 = dx                    # [0x212] = A.w3 | 0x8000
    agroup = (a212 << 24) | (a210 << 8) | a20f
    ax = (exp_byte & 0xFF)       # 0x175D-0x1761: keep al, clear rest
    bx = 0
    dx = 0
    ah = 0

    bbytes = [                   # loop order: di = 0x214; inc di -> 0x215..0x219
        (cx >> 8) & 0xFF,        # 0x215 = B.w1 hi (guard)
        (si >> 8) & 0xFF,        # 0x216 = B.w2 hi
        si & 0xFF,               # 0x217 = B.w2 lo
        (di >> 8) & 0xFF,        # 0x218 = B.w3 hi (| 0x80)
        di & 0xFF,               # 0x219 = B.w3 lo
    ]

    def rcr(v, cf_in, bits):
        new = (cf_in << (bits - 1)) | (v >> 1)
        return new, (v & 1)

    def rcl(v, cf_in, bits):
        mask = (1 << bits) - 1
        new = ((v << 1) | cf_in) & mask
        return new, (v >> (bits - 1)) & 1

    cf = 0                       # `or ch,ch` cleared CF
    chb = 0
    for byte in bbytes:
        chb = byte
        cf = 0
        if byte == 0:
            acc = (dx << 24) | (bx << 8) | ah
            acc >>= 8
            dx = (acc >> 24) & 0xFFFF
            bx = (acc >> 8) & 0xFFFF
            ah = acc & 0xFF
            cf = 0               # xor dh,dh
        else:
            for _ in range(8):
                chb, b = rcr(chb, cf, 8)
                cf = b
                if b:
                    acc = ((dx << 24) | (bx << 8) | ah) + agroup
                    dx = (acc >> 24) & 0xFFFF
                    bx = (acc >> 8) & 0xFFFF
                    ah = acc & 0xFF
                    cf = (acc >> 40) & 1    # CF3 from adc dx
                dx, c2 = rcr(dx, cf, 16)
                bx, c2 = rcr(bx, c2, 16)
                ah, c2 = rcr(ah, c2, 8)
                cf = c2
    # cl = 0

    # ---- tail 0x179B.. ----
    cx2 = ax                     # xchg ax,cx
    ax = 0                       # cl
    ah = 0x40                    # lahf (dec cl: ZF=1, CF=0)
    ax = (ah << 8) | (ax & 0xFF)
    if ((dx >> 8) & 0x80) == 0:  # test dh,0x80 ; jnz 0x17AF
        c = 0                    # sahf
        chb, c2 = rcl(chb, c, 8)
        bx, c2 = rcl(bx, c2, 16)
        dx, c2 = rcl(dx, c2, 16)
        # (ah NOT in the rcl chain)
    ax = cx2                     # xchg ax,cx
    dh = (((dx >> 8) & 0xFF) ^ mem221) & 0xFF   # xor dh,[0x221]
    dx = (dx & 0x00FF) | (dh << 8)
    if (ax & 0xFF) == 0:         # or al,al; jnz
        return (0, 0, 0), mem221
    return (ax, bx, dx), mem221


def fmt(w):
    return '(%04X %04X %04X)' % w


def decode(w):
    w1, w2, w3 = w
    al = w1 & 0xFF
    if al == 0:
        return 0.0
    e = al - 0x81
    sign = -1 if (w3 >> 7) & 1 else 1
    M = ((w3 & 0x7F) << 16) | w2
    return sign * (1 + M / 2 ** 23) * (2 ** e)


def main():
    print("=== MUL reference simulation (fresh [0x221]=0) ===")
    for a, b in [(3, 4), (10, 7), (100, 100), (0, 99)]:
        wa = conv_words(a)
        wb = conv_words(b)
        (r, m221) = mul_body(wa, wb, 0x00)
        print(f"{a} * {b}: A={fmt(wa)} B={fmt(wb)} -> {fmt(r)}  [0x221]=0x{m221:02X}  value={decode(r)!r}")
    print()
    for n in [12, 70, 10000]:
        print(f"conv({n}) = {fmt(conv_words(n))}")


if __name__ == '__main__':
    main()
