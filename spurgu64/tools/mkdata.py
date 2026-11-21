#!/usr/bin/env python3
"""Converts the SpurguX data files to the C64 port's text encoding.

    mkdata.py cp437 IN OUT     CP437 file (SPU.TIP, SPU.SPE)
    mkdata.py utf8  IN OUT     UTF-8 file (the 40-column help text)

The output is PETSCII as the program uses it (see spurgux.h): ASCII
letters swap case, the Finnish letters and the CP437 item symbols become
the custom codes $80-$93, block graphics become the $01-$10 codes, and
lines end in a single CR.  A trailing ^Z and blank lines at the end of
the file are dropped."""
import sys

SPECIAL = {
    0x84: 0x80, 0x94: 0x81, 0x86: 0x82, 0x8E: 0x83, 0x99: 0x84, 0x8F: 0x85,
    0xE4: 0x86,              # Σ  overalls
    0x5C: 0x87,              # \  stiletto
    0xE1: 0x88,              # ß  chains
    0xE9: 0x89,              # Θ  Camel boots
    0xE8: 0x8A,              # Φ  cognac
    0x7F: 0x8B,              # ⌂  milk
    0xEC: 0x8C,              # ∞  chocolate
    0x9D: 0x8D,              # ¥  herring
    0x91: 0x8E,              # æ  poop
    0xEA: 0x8F,              # Ω  bouncer
    0xA5: 0x90,              # Ñ  punk king
    0xF8: 0x91,              # °  ghost
    0xA8: 0x92,              # ¿  hint
    0xF7: 0x93,              # ≈  water
    0xDB: 0x01,              # █
    0xDE: 0x02,              # ▐
    0xDF: 0x03,              # ▀
    0xDC: 0x04,              # ▄
    0xB1: 0x05, 0xB0: 0x05, 0xB2: 0x05,   # ░▒▓
    0xC4: 0x06, 0xCD: 0x06,  # ─ ═
    0xB3: 0x07, 0xBA: 0x07,  # │ ║
    0xF1: 0x0E,              # ±  grave (┼ on the C64)
    0x5F: 0xA4,              # _
    0x60: 0x27,              # `
    0x7B: 0x28, 0x7D: 0x29, 0x7C: 0x07, 0x7E: 0x2D,
}


def conv_byte(b):
    if b in SPECIAL:
        return SPECIAL[b]
    if 0x41 <= b <= 0x5A:
        return b + 0x80
    if 0x61 <= b <= 0x7A:
        return b - 0x20
    if 0x20 <= b <= 0x5D:
        return b
    if b == 0x5E:
        return 0x5E          # ^ -> PETSCII up arrow
    if b == 0x09:
        return 0x20
    return 0x3F              # anything else: '?'


def convert(data):
    end = data.find(b"\x1a")
    if end >= 0:
        data = data[:end]
    lines = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n")
    while lines and not lines[-1].strip():
        lines.pop()
    out = bytearray()
    for ln in lines:
        out += bytes(conv_byte(b) for b in ln[:80])
        out.append(13)
    return bytes(out)


def main():
    mode, src, dst = sys.argv[1:4]
    data = open(src, "rb").read()
    if mode == "utf8":
        # Python's cp437 has DEL at $7F, the DOS glyph there is the house
        data = data.decode("utf-8").replace("\u2302", "\x7f").encode("cp437")
    open(dst, "wb").write(convert(data))


if __name__ == "__main__":
    main()
