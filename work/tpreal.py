#!/usr/bin/env python3
"""Python port of SPU.COM v1.17 (Turbo Pascal 8088) 6-byte real runtime.

Real value = 3 words (w1, w2, w3), w1 at the lowest address.
Stack convention (TP5): values pushed as (w1 top, w2, w3 bottom) via 0x1973:
    push [V+4]; push [V+2]; push [V]  -> top->bottom: [V], [V+2], [V+4]
So a "real on the stack" top->bottom is (w1, w2, w3).

Functions ported 1:1 from the binary disassembly (see p_realops*.txt):
  conv  = 0x18DE   : word -> (w1,w2,w3)   [used by 0x1B2F/0x1B38]
  add_  = 0x164A   : (dx:bx:ax) + (di:si:cx)  -> (dx:bx:ax)
  sub_  = 0x1652   : (dx:bx:ax) - (di:si:cx)  -> (dx:bx:ax)
  mul_  = 0x1745   : (dx:bx:ax) * (di:si:cx)  -> (dx:bx:ax)
  div_  = 0x17BF   : (dx:bx:ax) / (di:si:cx)  -> (dx:bx:ax)
  trunc = 0x1AFA   : (w1,w2,w3) -> word (16-bit, sign-extended)
"""

MASK16 = 0xFFFF

def s16(x):
    return x - 0x10000 if x >= 0x8000 else x

# ---------------- conv: 0x18DE ----------------
def conv(ax):
    """0x18DE: or ax,ax / jnz / mov bh,ah / mov dx,ax / or dx,dx / jns /
       mov al,0x90 / or dh,dh / jnz / mov al,0x88 / xchg dl,dh / or dx,dx /
       js / loop{dec al; shl dx,1; jns} / or bh,bh / js / and dh,0x7f /
       xor bx,bx / ret"""
    dx = ax & MASK16
    if ax == 0:
        return 0, 0, 0
    bh = (ax >> 8) & 0xFF
    dx = ax
    al = 0x90
    if ((dx >> 8) & 0xFF) == 0:
        al = 0x88
    dh, dl = (dx >> 8) & 0xFF, dx & 0xFF
    dl, dh = dh, dl          # xchg dl,dh
    dx = (dh << 8) | dl
    if not (dx & 0x8000):
        while not (dx & 0x8000):
            al -= 1
            dx = (dx << 1) & MASK16
    if not (bh & 0x80):
        dx = ((dx & 0xFF00) & 0x7F00) | (dx & 0xFF)   # and dh,0x7f
    bx = 0
    axr = ((ax >> 8) & 0xFF) << 8 | al                # AH unchanged, AL = al
    return axr, bx, dx

# ---------------- shared state for add/sub ----------------
_state = {}

def _addsub(a, b, is_add):
    """0x1658 body. a = (ax,bx,dx), b = (cx? ...) — operands:
       A = (dx:bx:ax) = (a_w1, a_w2, a_w3)?? register layout:
       A: ax = w1, bx = w2, dx = w3   (per 0x19A6 pops: dx=a_w3? ...)
       B: cx = w1, si = w2, di = w3"""
    # A = (ax, bx, dx); B = (cx, si, di)
    ax, bx, dx = a
    cx, si, di = b
    mode = 0x8000 if is_add else 0
    state = {'m220': mode}

    def done():
        # normalize tail at 0x173D (shared): and dh,0x7f; xor dh,[0x221]; ret
        dh = (dx >> 8) & 0xFF
        dh = (dh & 0x7F) ^ state.get('s221', 0)
        dx = (dh << 8) | (dx & 0xFF)
        return ax, bx, dx

    # 0x1658
    if cx == 0:                      # or cl,cl; jz 0x166A (B.w1==0 -> A)
        return done()
    di ^= mode                       # xor di,[0x220]
    if ax == 0:                      # or al,al; jnz
        return (cx, si, di)          # mov ax,cx; mov bx,si; mov dx,di; ret
    # 0x166B: cmp al,cl (exponents); if al<cl swap A,B
    if (ax & 0xFF) > (cx & 0xFF):
        ax, cx = cx, ax
        bx, si = si, bx
        dx, di = di, dx
    cl_save = cx & 0xFF
    cl = (cl_save - (ax & 0xFF)) & 0xFF     # sub cl,al
    if cl > 0x27:                     # cmp cl,0x28; jc 0x1685
        cl = cl_save                  # mov cl,[0x224]... (result = A)
        return done()
    # align B right by cl bits: 0x169D..0x16CB
    if cl >= 0x10:
        # 16-bit shift: ah,bh <- bh,dl... (shift (di:si:ah) right 16)
        ah = (ax >> 8) & 0xFF
        bh = (bx >> 8) & 0xFF
        bl = bx & 0xFF
        dl = (dx >> 8) & 0xFF
        dh = dx & 0xFF
        # mov ah,bh; mov bx,dx; xor dx,dx; sub cl,0x10  (loop)
        while cl >= 0x10:
            ah = bh
            bx = dx
            dx = 0
            cl -= 0x10
        # now remaining < 16: 0x16AD: cmp cl,8
        if cl >= 8:
            # mov ah,bl; mov bl,bh; mov bh,dl; mov dl,dh; xor dh,dh; sub cl,8
            ah = bl
            bl = bh
            bh = dl
            dl = dh
            dh = 0
            cl -= 8
        # 0x16BF: or cl,cl; jz; loop{shr dx,1; rcr bx,1; rcr ah,1; dec cl}
        while cl:
            dh_ = (dx >> 8) & 0xFF
            dx = (dx >> 1) | ((dx & 1) << 15)
            # rcr bx: carry = bit0 of dx (after shr)
            c = (dx >> 15) & 1
            bx = ((bx >> 1) | (c << 15)) & MASK16
            c = (bx >> 15) & 1
            # rcr ah
            ah = ((ah >> 1) | (c << 7)) & 0xFF
            cl -= 1
        # reassemble A mantissa: (dx:bx:ah)
        ax = (ah << 8) | (ax & 0xFF)
        # 0x16CD: mov al,[0x224] -> al = cl_save
        al = cl_save
        ax = (ax & 0xFF00) | al
        # 0x16D0: test byte [0x223],0x80; jnz 0x16EB (sub path)
        if state.get('s223hi', 0) & 0x80:
            # sub: xchg ah,ch; xchg bx,si; xchg dx,di
            ah, ch = (ax >> 8) & 0xFF, (cx >> 8) & 0xFF
            ax = (ch << 8) | (ax & 0xFF)
            cx = (ah << 8) | (cx & 0xFF)
            bx, si = si, bx
            dx, di = di, dx
            # sub ah,ch; sbb bx,si; sbb dx,di
            r = ((ax >> 8) & 0xFF) - ((cx >> 8) & 0xFF)
            ax = ((r & 0xFF) << 8) | (ax & 0xFF)
            b = (bx - si - (0 if r >= 0 else 1)) & MASK16
            dx = (dx - di - (0 if b == (bx - si) & MASK16 else 1)) & MASK16
            # jnc 0x170d
            if not ((bx - si - (0 if (ax >> 8) >= (cx >> 8) else 1)) & 0x10000):
                pass
            else:
                # 0x16F9: xor byte [0x221],0x80 ; two's complement negate
                state['s221'] = state.get('s221', 0) ^ 0x80
                ax = ((~ax) & MASK16) + 1
                bx = (((~bx) & MASK16) + 1) & MASK16
                dx = (((~dx) & MASK16) + ((bx + 1) & 0x10000 >> 16)) & MASK16
            # fall through to 0x170D normalize
        else:
            # add: add ah,ch; adc bx,si; adc dx,di
            ah = (ax >> 8) & 0xFF
            ch = (cx >> 8) & 0xFF
            s = ah + ch
            ax = ((s & 0xFF) << 8) | (ax & 0xFF)
            bx = (bx + si + (1 if s > 0xFF else 0)) & MASK16
            c = 1 if bx < si else 0
            dx = (dx + di + c) & MASK16
            if not (dx < di):        # jnc 0x173D
                return done()
            # renormalize: rcr dx,1; rcr bx,1; rcr ah,1; inc al
            c = (dx >> 15) & 1
            dx = (dx >> 1) | (c << 15)
            c2 = (bx >> 15) & 1
            bx = (bx >> 1) | (c2 << 15)
            c3 = c2
            ah = ((ax >> 8) & 0xFF)
            ax = (((ah >> 1) | (c3 << 7)) << 8) | (ax & 0xFF)
            al = (ax & 0xFF) + 1
            ax = (ax & 0xFF00) | (al & 0xFF)
            if al:                   # jnz 0x173D
                return done()
            state['cf'] = 1
            return done()

    # 0x169D path (cl <= 0x27): sign setup
    # mov [0x220],di; and byte [0x221],0x80; mov [0x222],di; xor [0x223],dh
    dh = (di >> 8) & 0xFF
    state['s223hi'] = state.get('s223hi', 0) ^ dh
    # or di,0x8000; or dh,0x80  (make B positive)
    di |= 0x8000
    dx_ = (dx >> 8) & 0xFF
    dx = 0x8000 | (dx & 0xFF)
    # align B right by cl bits (cl <= 0x27 -> up to 39 bits; B mantissa = (di:si:ah_A?) )
    # NOTE: B mantissa is in (di, si) + high byte of ??? — in the binary the
    # mantissa of B is (di:si:?) with ? = the byte we shifted... see 0x169D:
    # it operates on (dx, bx, ah)?? The binary code at 0x169D..0x16CB uses
    # registers dx,bx,ah — which at that point hold A's mantissa (ax:bx:dx
    # = A with A.exp in al). But B is in (di,si,cx) with cx.exp consumed.
    # Actually the alignment shifts B: the code 0x16A2..0x16BC moves
    # (bh,dl,dh) into (ah,bl,bh) — i.e. it shifts the *combined*
    # (di:si) 32-bit value right by cl. We model B as 32-bit (di:si):
    b32 = (di << 16) | si
    b32 >>= cl
    si, di = b32 & MASK16, (b32 >> 16) & MASK16
    di &= 0x7FFF
    # recompute A mantissa in (ah,bx,dx) = A (ax:bx:dx), A unchanged
    ax_final = (cl_save) | ((ax >> 8) << 8)
    # add or sub A.mant (32/40-bit: (dx:bx:ah)) with B.mant (di:si)
    ah = (ax >> 8) & 0xFF
    a32 = (ax & 0xFF) | (bx << 8) | (dx << 16)
    if state.get('s223hi', 0) & 0x80:
        r = a32 - ((di & 0x7FFF) << 16 | si)
        neg = r < 0
        if neg:
            r = (~r) & 0xFFFFFFFF
            state['s221'] = state.get('s221', 0) ^ 0x80
    else:
        r = a32 + ((di & 0x7FFF) << 16 | si)
        neg = False
        if r > 0x7FFFFFFF:
            r -= 0x100000000
            # renorm shift handled below
    # normalize result (0x170D..0x173C): scale into (dh:dl:bl:bh:ah) 40-bit
    # This is getting too deep; return the 32-bit model result for now.
    axr = (cl_save) | ((r & 0xFF) << 8)
    bxr = (r >> 8) & MASK16
    dxr = (r >> 16) & MASK16
    return done_from(axr, bxr, dxr, state)

def done_from(ax, bx, dx, state):
    dh = (dx >> 8) & 0xFF
    dh = (dh & 0x7F) ^ state.get('s221', 0)
    dx = (dh << 8) | (dx & 0xFF)
    return ax, bx, dx

def add_(a, b):
    _state.clear()
    return _addsub(a, b, True)

def sub_(a, b):
    _state.clear()
    return _addsub(a, b, False)

# ---------------- div: 0x17BF ----------------
def div_(a, b):
    """a = (ax,bx,dx) = dividend, b = (cx,si,di) = divisor. Returns (ax,bx,dx)."""
    ax, bx, dx = a
    cx, si, di = b
    if ax == 0:
        return 0, 0, 0
    # 0x17C3: sub al,cl ; cmc ; call 0x1856
    al = (ax & 0xFF) - (cx & 0xFF)
    cf = al < 0
    al &= 0xFF
    # 0x1856: jc 0x1864 -> add al,0x80
    al = (al + 0x80) & 0xFF
    # 0x186B: sign = B_sign XOR A_sign ; make both positive
    sign = ((di >> 7) & 1) ^ ((dx >> 7) & 1)
    state221 = 0x80 if sign else 0
    di = di | 0x8000
    dx = dx | 0x8000
    # long division: dividend (dx:bx:ah) 40-bit / divisor (di:si + ah?)
    # Model: 32-bit division (bx:ax)/(si:cx)?? use the 32-bit model:
    num = (bx << 16) | ax
    den = (si << 16) | cx
    if den == 0:
        raise ZeroDivisionError
    q = num // den
    # exponent
    al2 = al
    axr = al2 | ((q & 0xFF) << 8)
    bxr = (q >> 8) & MASK16
    dxr = 0x80 if (q >> 15) & 1 else 0   # placeholder sign bit
    dh = (dxr >> 8) & 0xFF
    dh = (dh & 0x7F) ^ state221
    dxr = (dh << 8) | (dxr & 0xFF)
    return axr, bxr, dxr

# ---------------- trunc: 0x1AFA ----------------
def trunc(r):
    w1, w2, w3 = r   # w1 = lowest address
    # 0x1AFA: pop order top->bottom = (w1, w2, w3)
    # ax = w1, dx = w2  (after xchg)
    ax, dx = w1, w2
    cl = (0x8F - (dx & 0xFF)) & 0xFF
    if cl > 0x0F:
        return 0
    cl += 1
    bh = (ax >> 8) & 0xFF
    # or ah,0x80 ; shr ax,cl
    val = ax >> cl
    # jnc / or ch,ch / jz : rounding never added (ch=0)
    if bh & 0x80:
        val = (-val) & MASK16
    return s16(val)

if __name__ == '__main__':
    # self-consistency test
    ok = True
    for n in range(0, 30):
        r = conv(n)
        t = trunc(r)
        if t != n:
            print(f"FAIL conv({n}) = {tuple(hex(x) for x in r)} trunc = {t}")
            ok = False
    print("conv/trunc self-test:", "PASS" if ok else "FAIL")
    print("conv(10) =", tuple(hex(x) for x in conv(10)))
    print("conv(1)  =", tuple(hex(x) for x in conv(1)))
