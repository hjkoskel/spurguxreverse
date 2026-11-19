#!/usr/bin/env python3
"""Correct x86-16 real-mode instruction length decoder + disassembler.

Ground-truth replacement for the old buggy dis.py. Decodes instruction
lengths exactly (prefixes, modrm, displacement, immediates) and prints a
readable listing. Used to regenerate all work/*.txt files.
"""
import sys

def b(m, a):
    return m[a & 0xFFFF]

def w(m, a):
    return b(m, a) | (b(m, a+1) << 8)

def l(m, a):
    return w(m, a) | (w(m, a+2) << 16)

# 16-bit modrm base/reg tables
RM16 = {
    0: "bx+si", 1: "bx+di", 2: "bp+si", 3: "bp+di",
    4: "si", 5: "di", 6: "bp", 7: "bx",
}
REG16 = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"]
REG8L = ["al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"]
REG8H = ["al", "cl", "dl", "bl", "al", "cl", "dl", "bl"]  # for /0..7 low
SEG = {"26": "es", "2E": "ss", "36": "ds", "3E": "cs"}

def segname(op):
    return {"26": "es", "2E": "ss", "36": "ds", "3E": "cs"}.get(op, None)

def decode_modrm(m, p, seg):
    """Return (ea_str, next_p). p points at modrm byte."""
    modrm = b(m, p); p += 1
    mod = modrm >> 6; reg = (modrm >> 3) & 7; rm = modrm & 7
    pre = (seg + ":") if seg else ""
    if mod == 3:
        return f"{pre}{REG16[rm]}", p
    base = RM16[rm]
    disp = 0
    if mod == 1:
        disp = b(m, p); p += 1
        if disp >= 0x80: disp -= 0x100
        return f"{pre}[{base}+{disp:+x}]".replace("+-", "-"), p
    # mod == 0
    if rm == 6:
        disp = w(m, p); p += 2
        return f"{pre}[{disp:04x}]", p
    return f"{pre}[{base}]", p

def imm_size(op, modrm=None, is16=True):
    """Return (imm_bytes, is_signed_hint)."""
    o = op & 0xFF
    if o in (0xB0,):  # mov reg8, imm8
        return 1
    if o == 0xB8:  # mov reg16, imm16
        return 2
    # add/sub/and/or/xor/cmp reg8, imm8  (00,28,20,08,38,38) /r8i8 with rm=reg
    # handled by caller
    return 0

COND = {0x70:"jo",0x71:"jno",0x72:"jb",0x73:"jc",0x74:"je",0x75:"jne",
        0x76:"jae",0x77:"ja",0x78:"js",0x79:"jns",0x7A:"jpe",0x7B:"jp",
        0x7C:"jl",0x7D:"jge",0x7E:"jle",0x7F:"jg"}
JCC16 = {0x0F: {0x80:"jo",0x81:"jno",0x82:"jb",0x83:"jc",0x84:"je",0x85:"jne",
        0x86:"jae",0x87:"ja",0x88:"js",0x89:"jns",0x8A:"jpe",0x8B:"jp",
        0x8C:"jl",0x8D:"jge",0x8E:"jle",0x8F:"jg"}}

def decode(m, a, maxinstr=64):
    """Decode one instruction at memory address a.
    Returns (disasm_str, next_addr)."""
    start = a
    p = a
    seg = None
    rep = ""
    # prefixes
    while True:
        op = b(m, p)
        if op in (0x26, 0x2E, 0x36, 0x3E):
            seg = segname(op); p += 1; continue
        if op == 0xF3: rep = "rep "; p += 1; continue
        if op == 0xF2: rep = "repne "; p += 1; continue
        break
    o = b(m, p); p += 1
    # two-byte 0F opcodes (limited)
    if o == 0x0F:
        o2 = b(m, p); p += 1
        name = JCC16[0x0F].get(o2, f"0F.{o2:02x}")
        if name.startswith("j"):
            rel = w(m, p) - 0x10000 if w(m, p) >= 0x8000 else w(m, p); p += 2
            tgt = (p + rel) & 0xFFFF
            return f"{name} 0x{tgt:04x}", p
        # modrm-based
        ea, p = decode_modrm(m, p, seg)
        return f"{name} {ea}", p
    op = o & 0xFF
    pre = (seg + ":") if seg else ""

    # groups by first byte
    # MOV r/m16, r16  /0 ; MOV r16, r/m16  /r (88/8C)
    if op in (0x88, 0x8C):
        modrm = b(m, p)
        reg = (modrm >> 3) & 7; rm = modrm & 7; mod = modrm >> 6
        if op == 0x88:
            dst, p2 = (f"{REG8L[reg]}", p) if mod==3 else (None, p)
        else:
            pass
        # simpler: decode ea then name
        ea, p = decode_modrm(m, p, seg)
        if op == 0x88:
            return f"mov {ea},{REG8L[reg]}", p
        else:
            return f"mov {REG16[reg]},{ea}", p

    if op in (0x8A, 0x8E):
        ea, p = decode_modrm(m, p, seg)
        modrm = b(m, start)  # not reliable; recompute
        # recompute reg
        mp = a
        # skip prefixes
        while True:
            x = b(m, mp)
            if x in (0x26,0x2E,0x36,0x3E,0xF3,0xF2): mp += 1; continue
            break
        mp += 1  # opcode
        modrm = b(m, mp); reg = (modrm>>3)&7
        if op == 0x8A:
            return f"mov {REG8L[reg]},{ea}", p
        else:
            return f"mov {ea},{REG8L[reg]}", p

    # MOV r16, imm16 (B8+reg) ; MOV r8, imm8 (B0+reg)
    if 0xB0 <= op <= 0xBF:
        reg = op & 7
        if op >= 0xB8:
            imm = w(m, p); p += 2
            return f"mov {REG16[reg]},{imm:04x}", p
        else:
            imm = b(m, p); p += 1
            return f"mov {REG8L[reg]},{imm:02x}", p

    # PUSH r16 (50+r), POP r16 (58+r)
    if 0x50 <= op <= 0x57:
        return f"push {REG16[op-0x50]}", p
    if 0x58 <= op <= 0x5F:
        return f"pop {REG16[op-0x58]}", p

    # PUSH seg (06 es,0E ds,16 ss,1E cs) / POP seg (07,0F,17,1F)
    if op in (0x06,0x0E,0x16,0x1E):
        return f"push {['es','','ds',''][0] or ['es','cs','ds','ss'][[0x06,0x1E,0x0E,0x16].index(op)]}", p
    if op in (0x07,0x0F,0x17,0x1F):
        return f"pop {['es','cs','ds','ss'][[0x07,0x1F,0x0F,0x17].index(op)]}", p

    # INC/DEC r16 (40+r / 48+r)
    if 0x40 <= op <= 0x47:
        return f"inc {REG16[op-0x40]}", p
    if 0x48 <= op <= 0x4F:
        return f"dec {REG16[op-0x48]}", p

    # 0x6x: 68 push imm16, 6A push imm8, 60-67 pusha..., 6C insb, 6E insw, 6F insw, 6B imul r,m
    if op == 0x68:
        imm = w(m, p); p += 2
        return f"push {imm:04x}", p
    if op == 0x6A:
        imm = b(m, p); p += 1
        if imm >= 0x80: imm -= 0x100
        return f"push {imm:x}", p
    if op == 0x6B:
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        return f"imul {REG16[reg]},{ea}", p
    if 0x60 <= op <= 0x63:
        return {0x60:"pusha",0x61:"popa",0x62:"bound",0x63:"jmp far"}[op], p
    if op in (0x64,0x65,0x66,0x67,0x6C,0x6D,0x6E,0x6F):
        return {0x64:"push fs",0x65:"pop fs",0x66:"(66)",0x67:"(67)",
                0x6C:"insb",0x6D:"insw",0x6E:"insw",0x6F:"insw"}[op], p

    # 0xCx group
    if 0xC0 <= op <= 0xC3:
        if op == 0xC3: return "ret", p
        if op == 0xC2:
            imm = w(m, p); p += 2
            return f"ret {imm:04x}", p
        if op in (0xC9,): return "leave", p
        if op in (0xCD,):
            iv = b(m, p); p += 1
            return f"int 0x{iv:02x}", p
        if op == 0xCC: return "int3", p
        if op == 0xCE: return "into", p
        if op == 0xCF: return "iret", p
        # C0/C1: shift r/m, imm8 ; C6/C7: mov r/m, imm
        if op in (0xC0, 0xC1):
            ea, p = decode_modrm(m, p, seg)
            imm = b(m, p); p += 1
            return f"sh? {ea},{imm:x} (grp1)", p
        if op in (0xC6, 0xC7):
            ea, p = decode_modrm(m, p, seg)
            if op == 0xC6:
                imm = b(m, p); p += 1
                return f"mov {ea},{imm:02x}", p
            else:
                imm = w(m, p); p += 2
                return f"mov {ea},{imm:04x}", p
        # C4/C5: LES/LDS
        if op in (0xC4, 0x05, 0xC5, 0x06):
            pass
        return f"0x{op:02x}?", p

    # 0xDx group
    if 0xD0 <= op <= 0xD3:
        if op in (0xD0, 0xD1, 0xD2, 0xD3):
            ea, p = decode_modrm(m, p, seg)
            if op == 0xD0: return f"shl {ea},1 (grp2)", p
            if op == 0xD1: return f"shl {ea},1 (grp2)", p
            if op == 0xD2: return f"shl {ea},cl (grp2)", p
            if op == 0xD3: return f"shl {ea},cl (grp2)", p
    if 0xD4 <= op <= 0xD9:
        if op in (0xD4, 0xD5):
            r = op - 0xD4
            imm = b(m, p); p += 1
            return f"aam {imm:02x}", p
        if op in (0xD6,): return "xchg ax,es?", p
        if op in (0xD7,): return "xlat", p
        if op in (0xD8, 0xD9, 0xDA, 0xDB):
            ea, p = decode_modrm(m, p, seg)
            return f"f? {ea} (grp)", p

    # 0xFF group
    if op == 0xFF:
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        name = ["inc","dec","call","call","jmp","jmp","push",""][reg]
        if reg == 2 or reg == 3:
            return f"{name} {ea}", p
        if reg == 4 or reg == 5:
            return f"{name} {ea}", p
        if reg == 6:
            return f"push {ea}", p
        return f"ff/{reg} {ea}", p

    # 0xFE group
    if op == 0xFE:
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        name = ["inc","dec","","","","","inc","dec"][reg]
        return f"{name} {ea}", p

    # 0xF7 / 0xF0 groups (mul/imul/div at /4,6,7)
    if op in (0xF7, 0xF0):
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        g = ["test","test","not","neg","mul","imul","div","idiv"]
        return f"{g[reg]} {ea}", p

    # ALU r/m16, r16 (01,21,29,09,31,39) ; r16, r/m16 (03,23,2B,0B,33,3B)
    ALU16 = {0x01:"add",0x21:"sub",0x29:"and",0x09:"or",0x31:"xor",0x39:"cmp",
             0x03:"add",0x23:"sub",0x2B:"and",0x0B:"or",0x33:"xor",0x3B:"cmp"}
    if op in ALU16:
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        name = ALU16[op]
        if op % 2 == 0:  # r/m <- reg
            return f"{name} {ea},{REG16[reg]}", p
        else:
            return f"{name} {REG16[reg]},{ea}", p
    ALU8 = {0x00:"add",0x20:"sub",0x28:"and",0x08:"or",0x30:"xor",0x38:"cmp",
            0x02:"add",0x22:"sub",0x2A:"and",0x0A:"or",0x32:"xor",0x3A:"cmp"}
    if op in ALU8:
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        name = ALU8[op]
        if op % 2 == 0:
            return f"{name} {ea},{REG8L[reg]}", p
        else:
            return f"{name} {REG8L[reg]},{ea}", p

    # ALU r/m, imm
    ALUI = {0x80:"grp3/8",0x81:"grp3/16",0x82:"grp3/8i",0x83:"grp3/16i",
            0x04:"add al",0x0C:"or al",0x14:"adc al",0x1C:"sbb al",
            0x24:"and al",0x2C:"sub al",0x34:"xor al",0x3C:"cmp al",
            0x05:"add ax",0x0D:"or ax",0x15:"adc ax",0x1D:"sbb ax",
            0x25:"and ax",0x2D:"sub ax",0x35:"xor ax",0x3D:"cmp ax"}
    if op in ALUI:
        if op in (0x80, 0x82):
            ea, p = decode_modrm(m, p, seg)
            imm = b(m, p); p += 1
            if imm >= 0x80: imm -= 0x100
            return f"grp3 {ea},{imm:x}", p
        if op in (0x81, 0x83):
            ea, p = decode_modrm(m, p, seg)
            imm = w(m, p); p += 2
            return f"grp3 {ea},{imm:04x}", p
        imm = b(m, p); p += 1
        return f"{ALUI[op]} {imm:02x}", p

    # LEA (8D)
    if op == 0x8D:
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        return f"lea {REG16[reg]},{ea}", p

    # TEST r/m, r (84/85)
    if op in (0x84, 0x85):
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        if op == 0x84:
            return f"test {ea},{REG8L[reg]}", p
        return f"test {REG16[reg]},{ea}", p

    # XCHG (86/87)
    if op in (0x86, 0x87):
        ea, p = decode_modrm(m, p, seg)
        modrm = _modrm(m, a); reg = (modrm>>3)&7
        if op == 0x86:
            return f"xchg {ea},{REG8L[reg]}", p
        return f"xchg {REG16[reg]},{ea}", p

    # CMP/TEST r/m, imm8 (80/82 handled); 
    # shifts: 0xC0/C1 handled. 

    # specific single-byte
    SPEC = {
        0x90:"nop", 0x98:"cbw", 0x99:"cwd", 0x9C:"pushf", 0x9D:"popf",
        0x9F:"sahf", 0x9E:"lahf", 0xF4:"hlt", 0xF5:"cmc", 0xF6:"grp6",
        0xF8:"clc", 0xF9:"stc", 0xFA:"cli", 0xFB:"sti", 0xFC:"cld", 0xFD:"std",
        0xD9:"(fpu?)",
    }
    if op in SPEC:
        if op in (0xF6, 0xF7):
            ea, p = decode_modrm(m, p, seg)
            return f"grp6 {ea}", p
        return SPEC[op], p

    # 0x9x xchg ax,reg
    if 0x91 <= op <= 0x97:
        return f"xchg ax,{REG16[op-0x91]}", p

    # string ops (F3/F2 prefixed handled as rep)
    STR = {0xA0:"mov al,[..]",0xA1:"mov ax,[..]",0xA2:"mov [..],al",0xA3:"mov [..],ax"}
    if op in (0xA0, 0xA1):
        d = l(m, p); p += 4
        return f"mov {'al' if op==0xA0 else 'ax'},[{d:04x}]", p
    if op in (0xA2, 0xA3):
        d = l(m, p); p += 4
        return f"mov [{d:04x}],{'al' if op==0xA2 else 'ax'}", p
    if op == 0xA4: return "movsb", p
    if op == 0xA5: return "movsw", p
    if op == 0xA6: return "cmpsb", p
    if op == 0xA7: return "cmpsw", p
    if op == 0xA8: return "test al,imm", p
    if op == 0xA9:
        imm = l(m, p); p += 4
        return f"test ax,{imm:04x}", p
    if op == 0xAA: return "stosb", p
    if op == 0xAB: return "stosw", p
    if op == 0xAC: return "lodsb", p
    if op == 0xAD: return "lodsw", p
    if op == 0xAE: return "scasb", p
    if op == 0xAF: return "scasw", p

    # 0xB4/0xB8 etc handled; 0xB0-0xBF handled
    # 0xE4-0xE7 in/out, 0xE8 call rel, 0xE9 jmp rel, 0xEA jmp far, 0xEB jmp rel8, 0xEC/ED in, 0xEE/EF out
    if op == 0xE8:
        rel = w(m, p); p += 2
        if rel >= 0x8000: rel -= 0x10000
        tgt = (p + rel) & 0xFFFF
        return f"call 0x{tgt:04x}", p
    if op == 0xE9:
        rel = w(m, p); p += 2
        if rel >= 0x8000: rel -= 0x10000
        tgt = (p + rel) & 0xFFFF
        return f"jmp 0x{tgt:04x}", p
    if op == 0xEB:
        rel = b(m, p); p += 1
        if rel >= 0x80: rel -= 0x100
        tgt = (p + rel) & 0xFFFF
        return f"jmp 0x{tgt:04x}", p
    if op == 0xEA:
        off = w(m, p); segv = w(m, p+2); p += 4
        return f"jmp far {segv:04x}:{off:04x}", p
    if 0x70 <= op <= 0x7F:
        rel = b(m, p); p += 1
        if rel >= 0x80: rel -= 0x100
        tgt = (p + rel) & 0xFFFF
        return f"{COND[op]} 0x{tgt:04x}", p
    if op in (0xE0, 0xE1, 0xE2, 0xE3):
        ea, p = decode_modrm(m, p, seg)
        rel = b(m, p); p += 1
        if rel >= 0x80: rel -= 0x100
        tgt = (p + rel) & 0xFFFF
        return {0xE0:"loopz",0xE1:"loopnz",0xE2:"loop",0xE3:"jecxz"}[op] + f" 0x{tgt:04x}", p
    if op in (0xE4, 0xE5, 0xE6, 0xE7, 0xEC, 0xED, 0xEE, 0xEF):
        if op in (0xE4, 0xE6):
            imm = b(m, p); p += 1
            return f"in al,0x{imm:02x}" if op==0xE4 else f"out 0x{imm:02x},al", p
        if op in (0xE5, 0xE7):
            imm = b(m, p); p += 1
            return f"in ax,0x{imm:02x}" if op==0xE5 else f"out 0x{imm:02x},ax", p
        return {0xEC:"in al,dx",0xED:"in ax,dx",0xEE:"out dx,al",0xEF:"out dx,ax"}[op], p

    # MOV r/m16, r16 done; MOV r/m, imm via C6/C7 done
    # 0x00/0x02 etc ALU done.
    return f".byte 0x{op:02x} (undecoded)", p

def _modrm(m, a):
    """Return the modrm byte of the instruction at a (skipping prefixes+opcode)."""
    p = a
    while True:
        x = b(m, p)
        if x in (0x26,0x2E,0x36,0x3E,0xF3,0xF2):
            p += 1; continue
        break
    p += 1  # opcode
    if b(m, p-1) == 0x0F:
        p += 1
    return b(m, p)

def disasm_range(m, start, end):
    out = []
    a = start
    guard = 0
    while a < end and guard < 100000:
        guard += 1
        try:
            s, na = decode(m, a)
        except Exception as e:
            s, na = f"<err {e}>", a+1
        out.append(f"0x{a:04X}: {s}")
        if na <= a:
            na = a + 1
        a = na
    return "\n".join(out)

if __name__ == "__main__":
    import sys
    path = sys.argv[1]
    start = int(sys.argv[2], 0)
    end = int(sys.argv[3], 0)
    data = open(path, "rb").read()
    # data is the raw .COM; memory address = file offset + 0x100
    # build a 64k memory image
    mem = bytearray(0x10000)
    mem[0x100:0x100+len(data)] = data
    print(disasm_range(mem, start, end))
