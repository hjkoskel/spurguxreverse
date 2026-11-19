#!/usr/bin/env python3
"""x86-16 real-mode micro-emulator for SPU.COM (v2, clean).

Runs the actual binary code (real-arithmetic thunks, generator, string
runtime) with INT 0x21 stubbed. Ground truth for all semantics.
"""

class EmuError(Exception):
    pass

class CPU:
    def __init__(self, image=None, base=0x100, com_seg=None):
        if com_seg is not None:
            # Full real-mode model: 1MB, COM program at com_seg:0x100 with PSP.
            self.mem = bytearray(0x100000)
            off = com_seg * 16
            self.mem[off] = 0xCD
            self.mem[off + 1] = 0x20
            self.mem[off + 2] = com_seg & 0xFF        # PSP[2] program segment
            self.mem[off + 3] = (com_seg >> 8) & 0xFF
            self.mem[off + 8] = com_seg & 0xFF        # PSP SSSP
            self.mem[off + 9] = (com_seg >> 8) & 0xFF
            self.mem[off + 10] = 0xFE
            self.mem[off + 11] = 0xFF
            if image:
                self.mem[off + 0x100:off + 0x100 + len(image)] = image
            self.cs = self.ds = self.es = self.ss = com_seg
            self.sp = 0xFFFE
            self.ip = 0x100
        else:
            self.mem = bytearray(0x10000)
            if image:
                self.mem[base:base+len(image)] = image
            self.cs = self.ds = self.es = self.ss = 0
            self.sp = 0xFFFE
            self.ip = base
        self.ax = self.cx = self.dx = self.bx = 0
        self.bp = self.si = self.di = 0
        self.cf = self.pf = self.af = self.zf = self.sf = self.of = 0
        self.dflag = 0
        self.halt = False
        self.keys = []
        self.out = bytearray()
        self.cursor = []
        self.int21_log = []
        self.int10_log = []
        self.rng_state = 0x1234
        self.max_steps = 30_000_000
        self.steps = 0
        self.stop_ip = None   # if set: stop when ip == stop_ip (checked before step)

    # ---------- memory (real mode: absolute = seg*16 + off, 20-bit wrap) ----------
    def _abs(self, seg, a): return (seg * 16 + (a & 0xFFFF)) & 0xFFFFF
    def rb(self, seg, a): return self.mem[self._abs(seg, a)]
    def rw(self, seg, a): return self.mem[self._abs(seg, a)] | (self.mem[self._abs(seg, a + 1)] << 8)
    def lwb(self, seg, a, v): self.mem[self._abs(seg, a)] = v & 0xFF
    def lww(self, seg, a, v):
        self.mem[self._abs(seg, a)] = v & 0xFF
        self.mem[self._abs(seg, a + 1)] = (v >> 8) & 0xFF
    def push(self, v):
        self.sp = (self.sp - 2) & 0xFFFF
        self.lww(self.ss, self.sp, v)
    def pop(self):
        v = self.rw(self.ss, self.sp)
        self.sp = (self.sp + 2) & 0xFFFF
        return v
    def regv(self, r):
        return (self.ax, self.cx, self.dx, self.bx, self.sp, self.bp, self.si, self.di)[r]
    def setreg(self, r, v):
        v &= 0xFFFF
        if r == 0: self.ax = v
        elif r == 1: self.cx = v
        elif r == 2: self.dx = v
        elif r == 3: self.bx = v
        elif r == 4: self.sp = v
        elif r == 5: self.bp = v
        elif r == 6: self.si = v
        else: self.di = v
    def reg8(self, r, hi):
        v = self.regv(r & 3)
        return (v >> 8) & 0xFF if hi else v & 0xFF
    def setreg8(self, r, hi, v):
        base = r & 3
        cur = self.regv(base)
        if hi: cur = (cur & 0x00FF) | ((v & 0xFF) << 8)
        else: cur = (cur & 0xFF00) | (v & 0xFF)
        self.setreg(base, cur)
    def get8reg(self, r):
        """8-bit reg-encoding: 0=AL 1=CL 2=DL 3=BL 4=AH 5=CH 6=DH 7=BH"""
        base = r & 3
        v = self.regv(base)
        return (v >> 8) & 0xFF if r >= 4 else v & 0xFF
    def set8reg(self, r, v):
        base = r & 3
        cur = self.regv(base)
        if r >= 4: cur = (cur & 0x00FF) | ((v & 0xFF) << 8)
        else: cur = (cur & 0xFF00) | (v & 0xFF)
        self.setreg(base, cur)

    # ---------- flags ----------
    def _szp(self, v, bits):
        mask = (1 << bits) - 1
        v &= mask
        self.zf = 1 if v == 0 else 0
        self.sf = (v >> (bits - 1)) & 1
        b = v & 0xFF
        p = 0
        while b:
            p ^= 1
            b &= b - 1
        self.pf = p ^ 1  # PF = 1 when even number of set bits

    def f_add(self, a, b, bits):
        mask = (1 << bits) - 1
        s = a + b
        r = s & mask
        self.cf = 1 if s > mask else 0
        self.af = 1 if ((a ^ b ^ r) & 0x10) else 0
        self.of = 1 if (~(a ^ b) & (a ^ r) & (1 << (bits - 1))) >> (bits - 1) & 1 else 0
        self._szp(r, bits)
        return r

    def f_sub(self, a, b, bits):
        mask = (1 << bits) - 1
        r = (a - b) & mask
        self.cf = 1 if a < b else 0
        self.af = 1 if ((a ^ b ^ r) & 0x10) else 0
        self.of = 1 if ((a ^ b) & (~(a ^ r)) & (1 << (bits - 1))) >> (bits - 1) & 1 else 0
        self._szp(r, bits)
        return r

    def f_adc(self, a, b, bits):
        mask = (1 << bits) - 1
        s = a + b + self.cf
        r = s & mask
        self.cf = 1 if s > mask else 0
        self.af = 1 if ((a ^ b ^ r) & 0x10) else 0
        self.of = 1 if ((a ^ r) & (~(a ^ b) & mask)) >> (bits - 1) & 1 else 0
        self._szp(r, bits)
        return r

    def f_sbb(self, a, b, bits):
        mask = (1 << bits) - 1
        r = (a - b - self.cf) & mask
        self.cf = 1 if a < b + self.cf else 0
        self.af = 1 if ((a ^ b ^ r) & 0x10) else 0
        self.of = 1 if ((a ^ r) & ((a ^ b) & mask)) >> (bits - 1) & 1 else 0
        self._szp(r, bits)
        return r

    def f_logic(self, r, bits):
        self.cf = 0
        self.of = 0
        self._szp(r, bits)

    def f_shl(self, v, n, bits):
        mask = (1 << bits) - 1
        if n == 0: return v
        if n >= bits:
            self.cf = (v >> (bits - 1)) & 1
            v = 0
        else:
            self.cf = (v >> (bits - n)) & 1
            v = (v << n) & mask
        self._szp(v, bits)
        return v

    def f_shr(self, v, n, bits):
        if n == 0: return v
        if n >= bits:
            self.cf = v & 1
            v = 0
        else:
            self.cf = (v >> (n - 1)) & 1
            v >>= n
        self._szp(v, bits)
        return v

    def f_sar(self, v, n, bits):
        mask = (1 << bits) - 1
        v &= mask
        if n == 0: return v
        sign = v & (1 << (bits - 1))
        if n >= bits:
            self.cf = v & 1
            v = 0xFFFF if sign else 0 if bits == 16 else 0xFF
        else:
            self.cf = (v >> (n - 1)) & 1
            v >>= n
            if sign:
                v |= mask ^ ((1 << (bits - n)) - 1)
        self._szp(v & mask, bits)
        return v & mask

    def f_rol(self, v, n, bits):
        mask = (1 << bits) - 1
        v &= mask
        n &= (bits - 1)
        if n == 0:
            self.of = (v >> (bits - 1)) & 1
            return v
        self.cf = (v >> (bits - n)) & 1
        v = ((v << n) | (v >> (bits - n))) & mask
        self.of = ((v >> (bits - 1)) ^ self.cf) & 1
        self._szp(v, bits)
        return v

    def f_ror(self, v, n, bits):
        mask = (1 << bits) - 1
        v &= mask
        n &= (bits - 1)
        if n == 0:
            self.of = (v >> (bits - 1)) & 1
            return v
        self.cf = v & 1
        v = ((v >> n) | (v << (bits - n))) & mask
        self.of = ((v >> (bits - 1)) ^ ((v >> (bits - 2)) & 1)) & 1
        self._szp(v, bits)
        return v

    def f_rcl(self, v, n, bits):
        mask = (1 << bits) - 1
        v &= mask
        for _ in range(n & (bits - 1)):
            c = self.cf
            self.cf = (v >> (bits - 1)) & 1
            v = ((v << 1) | c) & mask
        self._szp(v, bits)
        return v

    def f_rcr(self, v, n, bits):
        mask = (1 << bits) - 1
        v &= mask
        for _ in range(n & (bits - 1)):
            c = self.cf
            self.cf = v & 1
            v = (v >> 1) | (c << (bits - 1))
        self._szp(v, bits)
        return v

    # ---------- operand fetch ----------
    def _modrm(self, a, seg_override):
        modrm = self.rb(self.cs, a)
        mod = modrm >> 6
        reg = (modrm >> 3) & 7
        rm = modrm & 7
        p = a + 1
        if mod == 3:
            return 3, reg, rm, None, None, p
        base = 0
        if rm == 0: base = self.bx + self.si
        elif rm == 1: base = self.bx + self.di
        elif rm == 2: base = self.bp + self.si
        elif rm == 3: base = self.bp + self.di
        elif rm == 4: base = self.si
        elif rm == 5: base = self.di
        elif rm == 6: base = self.bp
        else: base = self.bx
        disp = 0
        if mod == 0 and rm == 6:
            disp = self.rw(self.cs, p)
            p += 2
            return 0, reg, rm, disp, self.ds, p
        if mod == 1:
            d = self.rb(self.cs, p)
            if d & 0x80: d -= 0x100
            disp = d
            p += 1
        elif mod == 3:
            disp = self.rw(self.cs, p)
            p += 2
        ea = (base + disp) & 0xFFFF
        seg = seg_override if seg_override is not None else (self.ss if rm == 6 else self.ds)
        return mod, reg, rm, ea, seg, p

    # ---------- stepping ----------
    def step(self):
        if self.halt:
            return False
        if self.stop_ip is not None and self.ip == self.stop_ip:
            self.halt = True
            return False
        # 0x213 = CPU-spin delay (mov cx,ax; jcxz; call 0x21d; loop; ret):
        # a pure no-op spin (0x21d reads [0x12] and loops). Skip to its ret.
        if self.ip == 0x213:
            ret = self.rw(self.ss, self.sp)
            if 0x100 <= ret <= 0xE8E6:
                self.ip = ret
                self.sp = (self.sp + 2) & 0xFFFF
                return True
        self.steps += 1
        if self.steps > self.max_steps:
            raise EmuError(f"step limit at ip={self.ip:#06x}")
        ip = self.ip
        seg_override = None
        rep = 0
        while True:
            op = self.rb(self.cs, ip)
            if op in (0x26, 0x2E, 0x36, 0x3E):
                seg_override = {0x26: self.es, 0x2E: self.ss, 0x36: self.ds, 0x3E: self.cs}[op]
                ip += 1
                continue
            if op == 0xF3: rep = 1; ip += 1; continue
            if op == 0xF2: rep = 2; ip += 1; continue
            if op == 0x66:
                raise EmuError(f"opsize prefix at {ip:#06x}")
            break
        o = self.rb(self.cs, ip)
        ip += 1
        if o == 0x0F:
            self._step0f(ip, seg_override, rep)
            return True
        self._step1(ip, o, seg_override, rep)
        return True

    def _get8(self, mod, rm, ea, seg):
        return self.get8reg(rm) if mod == 3 else self.rb(seg, ea)
    def _get16(self, mod, rm, ea, seg):
        return self.regv(rm) if mod == 3 else self.rw(seg, ea)
    def _set8(self, mod, rm, ea, seg, v):
        if mod == 3: self.set8reg(rm, v)
        else: self.lwb(seg, ea, v)
    def _set16(self, mod, rm, ea, seg, v):
        if mod == 3: self.setreg(rm, v)
        else: self.lww(seg, ea, v)

    ALU_BIN = {
        0x00: 'add', 0x08: 'or', 0x10: 'adc', 0x18: 'sbb',
        0x20: 'and', 0x28: 'sub', 0x30: 'xor', 0x38: 'cmp',
    }

    def _step1(self, a, o, seg_override, rep):
        m = self
        # ----- ALU r/m16, r16 / r16, r/m16 -----
        if o in (0x01, 0x03, 0x09, 0x0B, 0x11, 0x13, 0x19, 0x1B,
                 0x21, 0x23, 0x29, 0x2B, 0x31, 0x33, 0x39, 0x3B):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            op = m.ALU_BIN[o & 0x38]
            if not (o & 2):  # bit1=0: dst = r/m, src = reg
                va = m.regv(reg)
                vb = m._get16(mod, rm, ea, seg)
                if op == 'add': r = m.f_add(vb, va, 16)
                elif op == 'or': r = vb | va; m.f_logic(r, 16)
                elif op == 'adc': r = m.f_adc(vb, va, 16)
                elif op == 'sbb': r = m.f_sbb(vb, va, 16)
                elif op == 'and': r = vb & va; m.f_logic(r, 16)
                elif op == 'sub': r = m.f_sub(vb, va, 16)
                elif op == 'xor': r = vb ^ va; m.f_logic(r, 16)
                else: m.f_sub(vb, va, 16)
                if op != 'cmp': m._set16(mod, rm, ea, seg, r)
            else:  # bit1=1: dst = reg, src = r/m
                va = m._get16(mod, rm, ea, seg)
                vb = m.regv(reg)
                if op == 'add': r = m.f_add(vb, va, 16)
                elif op == 'or': r = vb | va; m.f_logic(r, 16)
                elif op == 'adc': r = m.f_adc(vb, va, 16)
                elif op == 'sbb': r = m.f_sbb(vb, va, 16)
                elif op == 'and': r = vb & va; m.f_logic(r, 16)
                elif op == 'sub': r = m.f_sub(vb, va, 16)
                elif op == 'xor': r = vb ^ va; m.f_logic(r, 16)
                else: m.f_sub(vb, va, 16)
                if op != 'cmp': m.setreg(reg, r)
            m.ip = p
            return
        # ----- ALU r/m8, r8 / r8, r/m8 -----
        if o in (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
                 0x02, 0x0A, 0x12, 0x1A, 0x22, 0x2A, 0x32, 0x3A):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            op = m.ALU_BIN[o & 0x38]
            if not (o & 2):  # bit1=0: dst = r/m8, src = reg8
                va = m.get8reg(reg)
                vb = m._get8(mod, rm, ea, seg)
                if op == 'add': r = m.f_add(vb, va, 8)
                elif op == 'or': r = vb | va; m.f_logic(r, 8)
                elif op == 'adc': r = m.f_adc(vb, va, 8)
                elif op == 'sbb': r = m.f_sbb(vb, va, 8)
                elif op == 'and': r = vb & va; m.f_logic(r, 8)
                elif op == 'sub': r = m.f_sub(vb, va, 8)
                elif op == 'xor': r = vb ^ va; m.f_logic(r, 8)
                else: m.f_sub(vb, va, 8)
                if op != 'cmp': m._set8(mod, rm, ea, seg, r)
            else:  # dst = reg8, src = r/m8
                va = m._get8(mod, rm, ea, seg)
                vb = m.get8reg(reg)
                if op == 'add': r = m.f_add(vb, va, 8)
                elif op == 'or': r = vb | va; m.f_logic(r, 8)
                elif op == 'adc': r = m.f_adc(vb, va, 8)
                elif op == 'sbb': r = m.f_sbb(vb, va, 8)
                elif op == 'and': r = vb & va; m.f_logic(r, 8)
                elif op == 'sub': r = m.f_sub(vb, va, 8)
                elif op == 'xor': r = vb ^ va; m.f_logic(r, 8)
                else: m.f_sub(vb, va, 8)
                if op != 'cmp': m.setreg8(reg, reg >= 4, r)
            m.ip = p
            return
        # ----- ALU r/m8, imm8 (80) / r/m16, imm16 (81) / 82 / 83 -----
        if o in (0x80, 0x81, 0x82, 0x83):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if o in (0x80, 0x82):
                imm = m.rb(self.cs, p); p += 1
                bits = 8
                va = m._get8(mod, rm, ea, seg)
            else:
                imm = m.rw(self.cs, p); p += 2
                bits = 16
                va = m._get16(mod, rm, ea, seg)
            if reg == 0: r = m.f_add(va, imm, bits)
            elif reg == 1: r = va | imm; m.f_logic(r, bits)
            elif reg == 2: r = m.f_adc(va, imm, bits)
            elif reg == 3: r = m.f_sbb(va, imm, bits)
            elif reg == 4: r = va & imm; m.f_logic(r, bits)
            elif reg == 5: r = m.f_sub(va, imm, bits)
            elif reg == 6: r = va ^ imm; m.f_logic(r, bits)
            else: m.f_sub(va, imm, bits); r = None
            if r is not None:
                if o in (0x80, 0x82): m._set8(mod, rm, ea, seg, r)
                else: m._set16(mod, rm, ea, seg, r)
            m.ip = p
            return
        # ----- C6/C7: mov r/m, imm -----
        if o in (0xC6, 0xC7):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if o == 0xC6:
                v = m.rb(self.cs, p); p += 1
                m._set8(mod, rm, ea, seg, v)
            else:
                v = m.rw(self.cs, p); p += 2
                m._set16(mod, rm, ea, seg, v)
            m.ip = p
            return
        # ----- B0-BF: mov r, imm -----
        if 0xB0 <= o <= 0xBF:
            r = o & 7
            if o >= 0xB8:
                v = m.rw(self.cs, a); p = a + 2
                m.setreg(r, v)
            else:
                # 8-bit: r=4..7 are AH/CH/DH/BH (high bytes)
                v = m.rb(self.cs, a); p = a + 1
                m.set8reg(r, v)
            m.ip = p
            return
        # ----- 50-5F push/pop r16 -----
        if 0x50 <= o <= 0x5F:
            r = o & 7
            if o <= 0x57: m.push(m.regv(r))
            else: m.setreg(r, m.pop())
            m.ip = a
            return
        # ----- push/pop seg -----
        if o in (0x06, 0x0E, 0x16, 0x1E):
            m.push({0x06: m.es, 0x1E: m.cs, 0x0E: m.ds, 0x16: m.ss}[o])
            m.ip = a
            return
        if o in (0x07, 0x0F, 0x17, 0x1F):
            v = m.pop()
            if o == 0x07: m.es = v
            elif o == 0x1F: m.cs = v
            elif o == 0x0F: m.ds = v
            else: m.ss = v
            m.ip = a
            return
        # ----- 40-4F inc/dec r16 -----
        if 0x40 <= o <= 0x4F:
            r = o & 7
            v = m.regv(r)
            m.setreg(r, m.f_add(v, 1, 16) if o <= 0x47 else m.f_sub(v, 1, 16))
            m.ip = a
            return
        # ----- 68/6A push imm -----
        if o == 0x68:
            m.push(m.rw(self.cs, a))
            m.ip = a + 2
            return
        if o == 0x6A:
            m.push(m.rb(self.cs, a))
            m.ip = a + 1
            return
        # ----- 6B imul r16, r/m16 -----
        if o == 0x6B:
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            va = m.regv(reg)
            vb = m._get16(mod, rm, ea, seg)
            if va & 0x8000: va -= 0x10000
            if vb & 0x8000: vb -= 0x10000
            r = (va * vb) & 0xFFFF
            m.setreg(reg, r)
            m.cf = m.of = 1 if (r & 0x8000) else 0
            m.ip = p
            return
        # ----- 60/61 pusha/popa -----
        if o == 0x60:
            for r in range(7, -1, -1): m.push(m.regv(r))
            m.ip = a
            return
        if o == 0x61:
            for r in range(8): m.setreg(r, m.pop())
            m.ip = a
            return
        # ----- FF group -----
        if o == 0xFF:
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if reg in (0, 1):
                v = m._get16(mod, rm, ea, seg)
                r = m.f_add(v, 1, 16) if reg == 0 else m.f_sub(v, 1, 16)
                m._set16(mod, rm, ea, seg, r)
                m.ip = p
                return
            if reg in (2, 3):
                tgt = m._get16(mod, rm, ea, seg)
                m.push(p)
                m.ip = tgt
                return
            if reg in (4, 5):
                m.ip = m._get16(mod, rm, ea, seg)
                return
            if reg == 6:
                m.push(m._get16(mod, rm, ea, seg))
                m.ip = p
                return
            raise EmuError(f"FF/{reg} at {m.ip:#06x}")
        # ----- FE group (8-bit inc/dec) -----
        if o == 0xFE:
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if reg in (0, 1):
                v = m._get8(mod, rm, ea, seg)
                r = m.f_add(v, 1, 8) if reg == 0 else m.f_sub(v, 1, 8)
                m._set8(mod, rm, ea, seg, r)
            m.ip = p
            return
        # ----- F6/F7 group -----
        if o in (0xF6, 0xF7):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            bits = 8 if o == 0xF6 else 16
            if reg == 0:  # test r/m, imm
                if bits == 8:
                    imm = m.rb(self.cs, p); p += 1
                    r = m._get8(mod, rm, ea, seg) & imm
                    m.f_logic(r, 8)
                else:
                    imm = m.rw(self.cs, p); p += 2
                    r = m._get16(mod, rm, ea, seg) & imm
                    m.f_logic(r, 16)
                m.ip = p
                return
            if reg == 2:  # not
                if bits == 8:
                    r = (~m._get8(mod, rm, ea, seg)) & 0xFF
                    m._set8(mod, rm, ea, seg, r)
                else:
                    r = (~m._get16(mod, rm, ea, seg)) & 0xFFFF
                    m._set16(mod, rm, ea, seg, r)
            elif reg == 3:  # neg
                if bits == 8:
                    r = m.f_sub(0, m._get8(mod, rm, ea, seg), 8)
                    m._set8(mod, rm, ea, seg, r)
                else:
                    r = m.f_sub(0, m._get16(mod, rm, ea, seg), 16)
                    m._set16(mod, rm, ea, seg, r)
            elif reg == 4:  # mul
                if bits == 8:
                    full = m.al * (m._get8(mod, rm, ea, seg) & 0xFF)
                    m.ax = full
                else:
                    full = m.ax * (m._get16(mod, rm, ea, seg) & 0xFFFF)
                    m.dx = full >> 16
                    m.ax = full & 0xFFFF
                m.cf = m.of = 1 if full > 0xFFFF else 0
            elif reg == 5:  # imul
                if bits == 8:
                    b8 = m._get8(mod, rm, ea, seg) & 0xFF
                    a = m.al - 0x100 if m.al & 0x80 else m.al
                    b = b8 - 0x100 if b8 & 0x80 else b8
                    full = a * b
                    lo = full & 0xFF
                    lo_s = lo - 0x100 if lo & 0x80 else lo
                    m.al = lo
                    m.cf = m.of = 1 if lo_s != full else 0
                else:
                    b16 = m._get16(mod, rm, ea, seg) & 0xFFFF
                    a16 = m.ax - 0x10000 if m.ax & 0x8000 else m.ax
                    b = b16 - 0x10000 if b16 & 0x8000 else b16
                    full = a16 * b
                    r = full & 0xFFFF
                    m.ax = r
                    r_ext = r - 0x10000 if r & 0x8000 else r
                    m.cf = m.of = 1 if r_ext != full else 0
            elif reg == 6:  # div
                if bits == 8:
                    d = m._get8(mod, rm, ea, seg) & 0xFF
                    if d == 0:
                        raise EmuError(f"div by zero at {m.ip:#06x}")
                    m.al, m.ah = divmod(m.ax, d)
                else:
                    d = m._get16(mod, rm, ea, seg) & 0xFFFF
                    if d == 0:
                        raise EmuError(f"div by zero at {m.ip:#06x}")
                    full = (m.dx << 16) | m.ax
                    m.ax = full // d
                    m.dx = full % d
            elif reg == 7:  # idiv
                if bits == 8:
                    d8 = m._get8(mod, rm, ea, seg) & 0xFF
                    d = d8 - 0x100 if d8 & 0x80 else d8
                    if d == 0:
                        raise EmuError(f"idiv by zero at {m.ip:#06x}")
                    full = m.ax
                    a16 = full - 0x10000 if full & 0x8000 else full
                    q = abs(a16) // abs(d)
                    if (a16 < 0) != (d < 0):
                        q = -q
                    m.al = q & 0xFF
                    m.ah = (a16 - q * d) & 0xFF
                else:
                    d16 = m._get16(mod, rm, ea, seg) & 0xFFFF
                    v16 = d16 - 0x10000 if d16 & 0x8000 else d16
                    if v16 == 0:
                        raise EmuError(f"idiv by zero at {m.ip:#06x}")
                    full = (m.dx << 16) | m.ax
                    if full & 0x8000: full -= 0x10000
                    q = abs(full) // abs(v16)
                    if (full < 0) != (v16 < 0):
                        q = -q
                    m.ax = q & 0xFFFF
                    m.dx = (full - q * v16) & 0xFFFF
            m.ip = p
            return
        # ----- 8F pop r/m16 -----
        if o == 0x8F:
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            v = m.pop()
            if mod == 3: m.setreg(rm, v)
            else: m.lww(seg, ea, v)
            m.ip = p
            return
        # ----- 8D lea -----
        if o == 0x8D:
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if mod != 3:
                m.setreg(reg, ea)
            m.ip = p
            return
        # ----- 89/8B mov r16 -----
        if o in (0x89, 0x8B):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if o == 0x8B:
                m.setreg(reg, m._get16(mod, rm, ea, seg))
            else:
                m._set16(mod, rm, ea, seg, m.regv(reg))
            m.ip = p
            return
        # ----- 88/8C/8A/8E mov r8 -----
        if o in (0x88, 0x8C, 0x8A, 0x8E):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if o == 0x88:  # r/m8 <- r8 (0-3 low, 4-7 high)
                m._set8(mod, rm, ea, seg, m.reg8(reg, reg >= 4))
            elif o == 0x8A:  # r8 <- r/m8
                m.setreg8(reg, reg >= 4, m._get8(mod, rm, ea, seg))
            elif o == 0x8C:
                if reg <= 3:  # r/m16 <- SReg (0=ES 1=CS 2=SS 3=DS)
                    m._set16(mod, rm, ea, seg, (m.es, m.cs, m.ss, m.ds)[reg])
                else:  # r/m8 <- r8 (AH group)
                    m._set8(mod, rm, ea, seg, m.reg8(reg, True))
            else:  # 0x8E
                if reg <= 3:  # SReg <- r/m16 (0=ES 1=CS 2=SS 3=DS)
                    v = m._get16(mod, rm, ea, seg)
                    if reg == 0: m.es = v
                    elif reg == 1: m.cs = v
                    elif reg == 2: m.ss = v
                    else: m.ds = v
                else:  # r8 (AH group) <- r/m8
                    m.setreg8(reg, True, m._get8(mod, rm, ea, seg))
            m.ip = p
            return
        # ----- 84/85 test -----
        if o in (0x84, 0x85):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if o == 0x84:
                r = m._get8(mod, rm, ea, seg) & m.reg8(reg, reg >= 4)
                m.f_logic(r, 8)
            else:
                r = m._get16(mod, rm, ea, seg) & m.regv(reg)
                m.f_logic(r, 16)
            m.ip = p
            return
        # ----- 86/87 xchg -----
        if o in (0x86, 0x87):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            if o == 0x86:
                va = m.reg8(reg, reg >= 4)
                vb = m._get8(mod, rm, ea, seg)
                m.setreg8(reg, reg >= 4, vb)
                m._set8(mod, rm, ea, seg, va)
            else:
                va = m.regv(reg)
                vb = m._get16(mod, rm, ea, seg)
                m.setreg(reg, vb)
                m._set16(mod, rm, ea, seg, va)
            m.ip = p
            return
        # ----- 90-9F -----
        if o == 0x90:
            m.ip = a; return
        if o == 0x98:  # cbw
            m.ax = (m.ax & 0xFF00) | ((0x100 + (m.al & 0x80)) & 0xFF)
            m.ip = a; return
        if o == 0x99:  # cwd
            m.dx = 0xFFFF if m.ax & 0x8000 else 0
            m.ip = a; return
        if o == 0x9C:  # pushf
            f = m.cf | (m.pf << 2) | (m.af << 4) | (m.zf << 6) | (m.sf << 7) | (0 << 11) | (m.of << 15)
            m.push(f)
            m.ip = a; return
        if o == 0x9D:  # popf
            f = m.pop()
            m.cf = f & 1
            m.pf = (f >> 2) & 1
            m.af = (f >> 4) & 1
            m.zf = (f >> 6) & 1
            m.sf = (f >> 7) & 1
            m.of = (f >> 15) & 1
            m.ip = a; return
        if o == 0x9E:  # sahf
            ah = m.ah
            m.cf = ah & 1
            m.pf = (ah >> 2) & 1
            m.af = (ah >> 4) & 1
            m.zf = (ah >> 6) & 1
            m.sf = (ah >> 7) & 1
            m.ip = a; return
        if o == 0x9F:  # lahf
            m.ah = m.cf | (m.pf << 2) | (m.af << 4) | (m.zf << 6) | (m.sf << 7)
            m.ip = a; return
        if 0x91 <= o <= 0x97:  # xchg ax, r (0x91=CX .. 0x97=DI)
            r = o - 0x90
            v = m.regv(r)
            m.setreg(r, m.ax)
            m.ax = v
            m.ip = a; return
        # ----- F4-FD flags/clear -----
        if o == 0xF4:
            raise EmuError(f"HLT at {m.ip:#06x}")
        if o == 0xF5: m.cf ^= 1; m.ip = a; return
        if o == 0xF8: m.cf = 0; m.ip = a; return
        if o == 0xF9: m.cf = 1; m.ip = a; return
        if o in (0xFA, 0xFB): m.ip = a; return
        if o == 0xFC: m.dflag = 0; m.ip = a; return
        if o == 0xFD: m.dflag = 1; m.ip = a; return
        if o == 0xD7:  # xlat
            m.al = m.rb(self.ds, (m.bx + m.al) & 0xFFFF)
            m.ip = a; return
        # ----- D8-DB fpu (should not appear) -----
        if 0xD8 <= o <= 0xDB:
            raise EmuError(f"FPU opcode at {m.ip:#06x}")
        # ----- D0-D3 shifts by 1/CL (D0/D2 = 8-bit, D1/D3 = 16-bit) -----
        if o in (0xD0, 0xD1, 0xD2, 0xD3):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            cnt = 1 if o in (0xD0, 0xD1) else m.cl
            if o & 1:  # 16-bit
                v = m._get16(mod, rm, ea, seg)
                r = m._do_shift(reg, v, cnt, 16)
                m._set16(mod, rm, ea, seg, r)
            else:  # 8-bit
                v = m._get8(mod, rm, ea, seg)
                r = m._do_shift(reg, v, cnt, 8)
                m._set8(mod, rm, ea, seg, r)
            m.ip = p
            return
        # ----- C0/C1 shifts by imm8 (C0 = 8-bit, C1 = 16-bit) -----
        if o in (0xC0, 0xC1):
            mod, reg, rm, ea, seg, p = m._modrm(a, seg_override)
            cnt = m.rb(self.cs, p); p += 1
            if o == 0xC1:
                v = m._get16(mod, rm, ea, seg)
                r = m._do_shift(reg, v, cnt, 16)
                m._set16(mod, rm, ea, seg, r)
            else:
                v = m._get8(mod, rm, ea, seg)
                r = m._do_shift(reg, v, cnt, 8)
                m._set8(mod, rm, ea, seg, r)
            m.ip = p
            return
        # ----- C2/C3/C8-CC/CE/CF -----
        if o == 0xC3:
            m.ip = m.pop()
            return
        if o == 0xC2:
            m.ip = m.pop()
            m.sp = (m.sp + m.rw(self.cs, a)) & 0xFFFF
            return
        if o == 0xC8:  # enter
            m.push(m.bp)
            m.bp = m.sp
            n = m.rw(self.cs, a)
            lvl = m.rb(self.cs, a + 2)
            for _ in range(lvl):
                m.push(m.rw(self.ss, m.sp + n))
            m.sp = (m.sp - n) & 0xFFFF
            m.ip = a + 3
            return
        if o == 0xC9:  # leave
            m.sp = m.bp
            m.bp = m.pop()
            m.ip = a
            return
        if o == 0xCD:
            # INT nn (2 bytes). Stub handlers never IRET, so no frame is pushed;
            # just advance past the interrupt number.
            m.do_int(m.rb(self.cs, a))
            m.ip = a + 1
            return
        if o == 0xCC:
            raise EmuError(f"INT3 at {m.ip:#06x}")
        if o == 0xCE:
            if m.of:
                m.do_int(1)
            m.ip = a
            return
        if o == 0xCF:  # iret
            m.ip = m.pop()
            m.cs = m.pop()
            f = m.pop()
            m.cf = f & 1; m.pf = (f >> 2) & 1; m.af = (f >> 4) & 1
            m.zf = (f >> 6) & 1; m.sf = (f >> 7) & 1; m.of = (f >> 15) & 1
            return
        # ----- A0-AF -----
        if o in (0xA0, 0xA1, 0xA2, 0xA3):
            d = m.rw(self.cs, a)
            if o == 0xA0: m.al = m.rb(self.ds, d)
            elif o == 0xA1: m.ax = m.rw(self.ds, d)
            elif o == 0xA2: m.lwb(self.ds, d, m.al)
            else: m.lww(self.ds, d, m.ax)
            m.ip = a + 2
            return
        if o == 0xA8:
            r = m.al & m.rb(self.cs, a)
            m.f_logic(r, 8)
            m.ip = a + 1
            return
        if o == 0xA9:
            r = m.ax & m.rw(self.cs, a)
            m.f_logic(r, 16)
            m.ip = a + 2
            return
        if o in (0x04,0x0C,0x14,0x1C,0x24,0x2C,0x34,0x3C):
            imm = m.rb(self.cs, a)
            if o == 0x04: m.al = m.f_add(m.al, imm, 8)
            elif o == 0x0C:
                r = m.al | imm; m.f_logic(r, 8); m.al = r
            elif o == 0x14: m.al = m.f_adc(m.al, imm, 8)
            elif o == 0x1C: m.al = m.f_sbb(m.al, imm, 8)
            elif o == 0x24:
                r = m.al & imm; m.f_logic(r, 8); m.al = r
            elif o == 0x2C: m.al = m.f_sub(m.al, imm, 8)
            elif o == 0x34:
                r = m.al ^ imm; m.f_logic(r, 8); m.al = r
            else: m.f_sub(m.al, imm, 8)
            m.ip = a + 1
            return
        if o in (0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D):
            imm = m.rw(self.cs, a)
            if o == 0x05: m.ax = m.f_add(m.ax, imm, 16)
            elif o == 0x0D:
                r = m.ax | imm; m.f_logic(r, 16); m.ax = r
            elif o == 0x15: m.ax = m.f_adc(m.ax, imm, 16)
            elif o == 0x1D: m.ax = m.f_sbb(m.ax, imm, 16)
            elif o == 0x25:
                r = m.ax & imm; m.f_logic(r, 16); m.ax = r
            elif o == 0x2D: m.ax = m.f_sub(m.ax, imm, 16)
            elif o == 0x35:
                r = m.ax ^ imm; m.f_logic(r, 16); m.ax = r
            else: m.f_sub(m.ax, imm, 16)
            m.ip = a + 2
            return
        # ----- E0-E7 / EC-EF in/out/loops -----
        if o in (0xE0, 0xE1, 0xE2, 0xE3):
            rel = m.rb(self.cs, a)
            if rel & 0x80: rel -= 0x100
            a2 = a + 1
            if o == 0xE3:
                m.ip = (a2 + rel) & 0xFFFF if m.cx == 0 else a2
                return
            m.cx = (m.cx - 1) & 0xFFFF
            if o == 0xE0: m.ip = a2 if m.zf else (a2 + rel) & 0xFFFF
            elif o == 0xE1: m.ip = (a2 + rel) & 0xFFFF if m.zf else a2
            else: m.ip = (a2 + rel) & 0xFFFF if m.cx else a2
            return
        if o in (0xE4, 0xE5, 0xE6, 0xE7, 0xEC, 0xED, 0xEE, 0xEF):
            m.ip = a + (1 if o in (0xE4, 0xE5, 0xE6, 0xE7) else 0)
            return
        # ----- E8/E9/EA/EB -----
        if o == 0xE8:
            rel = m.rw(self.cs, a)
            if rel & 0x8000: rel -= 0x10000
            m.push(a + 2)
            m.ip = (a + 2 + rel) & 0xFFFF
            return
        if o == 0xE9:
            rel = m.rw(self.cs, a)
            if rel & 0x8000: rel -= 0x10000
            m.ip = (a + 2 + rel) & 0xFFFF
            return
        if o == 0xEB:
            rel = m.rb(self.cs, a)
            if rel & 0x80: rel -= 0x100
            m.ip = (a + 1 + rel) & 0xFFFF
            return
        if o == 0xEA:
            off = m.rw(self.cs, a)
            m.push(m.cs)
            m.push(a + 4)
            m.cs = m.rw(self.cs, a + 2)
            m.ip = off
            return
        if 0x70 <= o <= 0x7F:
            rel = m.rb(self.cs, a)
            if rel & 0x80: rel -= 0x100
            taken = m._jcc(o)
            m.ip = (a + 1 + rel) & 0xFFFF if taken else a + 1
            return
        # ----- string ops -----
        if o in (0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF):
            m._string(o, rep)
            return
        raise EmuError(f"undecoded opcode 0x{o:02X} at {m.ip:#06x}")

    def _jcc(self, o):
        cf, zf, sf, of, pf = self.cf, self.zf, self.sf, self.of, self.pf
        t = {
            0x70: of, 0x71: not of,
            0x72: cf,                    # JB/JC
            0x73: not cf,                # JAE/JNC
            0x74: zf, 0x75: not zf,
            0x76: cf or zf,              # JBE
            0x77: (not cf) and (not zf), # JA
            0x78: sf, 0x79: not sf,
            0x7A: pf, 0x7B: not pf,
            0x7C: sf != of, 0x7D: sf == of,
            0x7E: zf or (sf != of),
            0x7F: (not zf) and (sf == of),
        }
        return bool(t[o])

    def _do_shift(self, reg, v, cnt, bits):
        m = self
        if reg == 0: return m.f_rol(v, cnt, bits)
        if reg == 1: return m.f_ror(v, cnt, bits)
        if reg == 2: return m.f_rcl(v, cnt, bits)
        if reg == 3: return m.f_rcr(v, cnt, bits)
        if reg in (4, 6): return m.f_shl(v, cnt, bits)
        if reg == 5: return m.f_shr(v, cnt, bits)
        if reg == 7: return m.f_sar(v, cnt, bits)
        raise EmuError(f"shift {reg}")

    def _string(self, o, rep):
        m = self
        n = m.cx if rep else 1
        byteop = o in (0xA4, 0xA6, 0xAA, 0xAC, 0xAE)
        d = (1 if m.dflag == 0 else -1) if byteop else (2 if m.dflag == 0 else -2)
        if o == 0xA4:
            for _ in range(n):
                m.lwb(m.es, m.di, m.rb(m.ds, m.si))
                m.si = (m.si + d) & 0xFFFF
                m.di = (m.di + d) & 0xFFFF
        elif o == 0xA5:
            for _ in range(n):
                m.lww(m.es, m.di, m.rw(m.ds, m.si))
                m.si = (m.si + d) & 0xFFFF
                m.di = (m.di + d) & 0xFFFF
        elif o == 0xA6:
            for _ in range(n):
                m.f_sub(m.al, m.rb(m.es, m.di), 8)
                m.si = (m.si + d) & 0xFFFF
                m.di = (m.di + d) & 0xFFFF
        elif o == 0xA7:
            for _ in range(n):
                m.f_sub(m.ax, m.rw(m.es, m.di), 16)
                m.si = (m.si + d) & 0xFFFF
                m.di = (m.di + d) & 0xFFFF
        elif o == 0xAA:
            for _ in range(n):
                m.lwb(m.es, m.di, m.al)
                m.di = (m.di + d) & 0xFFFF
        elif o == 0xAB:
            for _ in range(n):
                m.lww(m.es, m.di, m.ax)
                m.di = (m.di + d) & 0xFFFF
        elif o == 0xAC:
            m.al = m.rb(m.ds, m.si)
            m.si = (m.si + d) & 0xFFFF
        elif o == 0xAD:
            m.ax = m.rw(m.ds, m.si)
            m.si = (m.si + d) & 0xFFFF
        elif o == 0xAE:
            for _ in range(n):
                m.f_sub(m.al, m.rb(m.es, m.di), 8)
                m.di = (m.di + d) & 0xFFFF
        else:  # 0xAF
            for _ in range(n):
                m.f_sub(m.ax, m.rw(m.es, m.di), 16)
                m.di = (m.di + d) & 0xFFFF
        m.ip += 2

    def _step0f(self, a, seg_override, rep):
        o = self.rb(self.cs, a)
        if 0x80 <= o <= 0x8F:
            rel = self.rw(self.cs, a + 1)
            if rel & 0x8000: rel -= 0x10000
            taken = self._jcc(o - 0x80)
            self.ip = (a + 3 + rel) & 0xFFFF if taken else a + 3
            return
        raise EmuError(f"0F.{o:02X} at {self.ip:#06x}")

    def do_int(self, n):
        if n == 0x20:
            self.halt = True
            return
        if n == 0x10:
            # BIOS video: no-op (output is stubbed in this build: [0x17E]=0x10D0=ret 4)
            self.int10_log.append((self.ip, self.ax))
            return
        if n == 0x16:
            # BIOS keyboard
            ah = self.ah
            if ah == 0x01:
                self.ah = 0
                self.al = 1 if self.keys else 0
            elif ah == 0x00:
                k = self.keys.pop(0) if self.keys else 0
                self.ax = k & 0xFFFF
            return
        if n == 0x21:
            self.int21_log.append((self.ip, self.ax))
            ah = self.ah
            if ah == 0x00:
                self.halt = True
            elif ah == 0x01:
                self.al = self.keys.pop(0) if self.keys else 0
            elif ah == 0x02:
                self.out.append(self.dl)
            elif ah == 0x09:
                d = self.dx
                while True:
                    c = self.rb(self.ds, d)
                    if c == 0x24:
                        break
                    self.out.append(c)
                    d = (d + 1) & 0xFFFF
            elif ah == 0x03:
                self.cursor.append((self.cx, self.dx))
            elif ah == 0x30:  # get current directory: real DOS errors here
                # (buffer = PSP, size byte 0xCD) -> runtime expects AX != 0
                self.ax = 5
            elif ah == 0x4A:  # allocate memory: first free segment above program
                self.ax = 0x1025
            elif ah == 0x3C:  # create file
                self.ax = 1
            elif ah == 0x3D:  # open file
                self.ax = 1
            elif ah == 0x3E:  # close file
                self.ax = 0
            elif ah == 0x3F:  # get fcb
                self.ax = 0
            elif ah == 0x40:  # write
                self.ax = self.cx
            elif ah == 0x41:  # read
                self.ax = self.cx
            elif ah == 0x42:  # seek
                self.ax = 0
            elif ah == 0x80:  # terminate
                self.halt = True
            else:
                raise EmuError(f"INT21 AH={ah:#04x} at ip={self.ip:#06x}")
            return
        if n == 1:
            raise EmuError(f"INT1 (overflow) at {self.ip:#06x}")
        raise EmuError(f"INT {n:#04x} at {self.ip:#06x}")

    # ---------- helpers ----------
    @property
    def al(self): return self.ax & 0xFF
    @al.setter
    def al(self, v): self.ax = (self.ax & 0xFF00) | (v & 0xFF)
    @property
    def ah(self): return (self.ax >> 8) & 0xFF
    @ah.setter
    def ah(self, v): self.ax = (self.ax & 0x00FF) | ((v & 0xFF) << 8)
    @property
    def cl(self): return self.cx & 0xFF
    @cl.setter
    def cl(self, v): self.cx = (self.cx & 0xFF00) | (v & 0xFF)
    @property
    def ch(self): return (self.cx >> 8) & 0xFF
    @property
    def dl(self): return self.dx & 0xFF
    @dl.setter
    def dl(self, v): self.dx = (self.dx & 0xFF00) | (v & 0xFF)
    @property
    def dh(self): return (self.dx >> 8) & 0xFF
    @property
    def bh(self): return (self.bx >> 8) & 0xFF
    @property
    def bl(self): return self.bx & 0xFF

    def run(self, max_steps=None):
        while not self.halt:
            self.step()

    def run_until(self, cond, max_steps=2_000_000):
        for _ in range(max_steps):
            if self.halt:
                break
            if cond(self):
                return
            self.step()
        else:
            raise EmuError("run_until: max steps")

    def call_proc(self, addr, setup=None):
        """Push ret, run until RET lands back on that ret address.
        setup(self) may push args before the call. Returns self.
        """
        ret = self.ip
        if setup:
            setup(self)
        self.push(ret)
        self.ip = addr & 0xFFFF
        # We stop when a RET pops ret and ip becomes ret.
        depth = 1
        last_push_sp = None
        while not self.halt:
            prev_ip = self.ip
            self.step()
            if self.ip == ret and prev_ip != ret:
                # could be a jump back, but RET is the normal path:
                # verify the word at sp is ret (already popped)
                break
            if self.ip == ret:
                break
        return self

def load():
    data = open('/home/neuro/reverse/spurgux/spurg117/SPU.COM', 'rb').read()
    return CPU(data)
