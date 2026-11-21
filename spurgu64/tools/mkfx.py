#!/usr/bin/env python3
"""Sound effect definitions for the C64 port of SpurguX.

    mkfx.py OUT.inc OUT.h

The effects are defined in EFFECTS below.  The script writes

    OUT.inc   the tables sound.s includes (priority, ADSR, step lists)
    OUT.h     the SND_* ids for C (included by spurgux.h)

The Makefile runs it (build/sfx.inc, build/sfx.h), so after editing this
file `make` rebuilds the game with the new sounds, and `make sfxtest`
builds sfxtest.prg, where the keys 1-9 play the effects one by one.

An effect is played on SID voice 1, one step after another, one frame
(1/60 s) at a time.  Each step is

    step(frames, freq, slide, wave, gate=True)

    frames  how long the step lasts (1-255 frames)
    freq    SID frequency register value; hz(440) converts from hertz
    slide   added to the frequency every frame (negative = pitch falls)
    wave    TRI, SAW, PUL (50 % pulse) or NOI (noise)
    gate    False starts the release: use it for a gap between notes
            (the next gated step restarts the envelope)

The envelope is set once per effect: ad = attack/decay, sr =
sustain/release, one hex digit each as in the SID registers ($D405 and
$D406).  Attack 0 = 2 ms ... F = 8 s; decay and release 0 = 6 ms ... F =
24 s; sustain 0-F is the level held while the gate is on.

prio: an effect with a higher priority than the one playing cuts it off;
one of equal or lower priority waits until it ends (one is kept waiting).
The game plays SND_DOT, SND_CASH, ... from game.c; a new effect needs a
SndPlay(SND_NAME) call there.

Frequencies are for PAL machines (985 248 Hz clock); on NTSC the effects
play about 4 % higher.
"""
import sys

PAL_CLOCK = 985248


def hz(f):
    """SID frequency register value for f hertz."""
    return int(round(f * 16777216 / PAL_CLOCK))


TRI, SAW, PUL, NOI = 0x10, 0x20, 0x40, 0x80


def step(frames, freq, slide, wave, gate=True):
    assert 1 <= frames <= 255 and 0 <= freq <= 0xFFFF
    slide &= 0xFFFF
    return [frames, freq & 255, freq >> 8, slide & 255, slide >> 8,
            wave | (1 if gate else 0)]


def wobble(start, n, up, down, frames, wave):
    """n steps alternating a rising and a falling slide: a vibrato that
    drifts by (up + down) * frames every two steps."""
    out, f = [], start
    for i in range(n):
        d = up if i % 2 == 0 else down
        out.append(step(frames, f, d, wave))
        f += d * frames
    return out, f


def sick():
    """'bouuuu': a short rise, then a slow wobbling fall."""
    s = [step(4, hz(170), hz(10) // 4, SAW)]
    w, f = wobble(hz(180), 10, +12, -40, 5, SAW)
    return s + w + [step(20, f, -8, SAW)]


def death():
    """'wah wah wah waaaah'."""
    s = []
    for f in (392, 370, 349):                    # G4 F#4 F4
        s += [step(16, hz(f), -1, PUL), step(4, hz(f) - 16, 0, PUL, gate=False)]
    w, f = wobble(hz(330), 12, +8, -12, 5, PUL)  # E4 with vibrato
    return s + w + [step(30, f, -4, PUL, gate=False)]


def booze():
    """glug-glug, then a happy rising C major arpeggio."""
    s = []
    for f in (0x0900, 0x0700):                   # two low gulps, pitch rising
        s += [step(4, f, +0x60, PUL), step(2, f, 0, PUL, gate=False)]
    for f in (523, 659, 784):                    # C5 E5 G5
        s.append(step(3, hz(f), 0, TRI))
    return s + [step(14, hz(1047), 0, TRI)]      # C6, held


def hint():
    """a bizarre short warble: pitch jumping up and down every frame
    while the whole thing sweeps upwards."""
    s, f = [], hz(300)
    for i in range(14):
        s.append(step(1, f if i % 2 == 0 else f * 3, +0x80, PUL))
        f += hz(70)
    return s + [step(6, hz(3000), -0x400, NOI, gate=False)]


def water():
    """splash: falling noise, then a rising 'blub'."""
    return [step(2, 0x3000, 0, NOI),
            step(6, 0x2000, -0x300, NOI),
            step(1, 0x0600, 0, TRI, gate=False),
            step(5, hz(150), +0x60, TRI),
            step(4, hz(300), +0xA0, TRI, gate=False)]


def poop():
    """'bbrrrp': a low sawtooth fluttering on and off (2 frames on, 1 off)
    while its pitch sags, then a short rising 'p'."""
    s = []
    for i in range(7):
        f = hz(95 - 5 * i)
        s += [step(2, f, -4, SAW), step(1, f, 0, SAW, gate=False)]
    return s + [step(3, hz(110), +0x40, PUL), step(2, hz(160), 0, PUL, gate=False)]


# name, priority, attack/decay, sustain/release, steps -- the order gives
# the SND_* numbers
EFFECTS = [
    ("DOT", 1, 0x00, 0xA0, [        # blip: a dot collected
        step(2, hz(1760), 0, PUL),
        step(3, hz(2637), 0, PUL)]),
    ("CASH", 2, 0x0A, 0x06, [       # cash register: clack-clack, bell
        step(2, 0x2800, 0, NOI), step(1, 0x2800, 0, NOI, gate=False),
        step(2, 0x1800, 0, NOI), step(2, 0x1800, 0, NOI, gate=False),
        step(3, hz(2093), 0, TRI),
        step(40, hz(2637), 0, TRI)]),
    ("HIT", 2, 0x00, 0xF2, [        # you hit someone
        step(1, 0x4000, 0, NOI),
        step(6, 0x1C00, -0x280, NOI)]),
    ("OUCH", 2, 0x00, 0xF3, [       # someone hits you
        step(2, 0x0C00, 0, NOI),
        step(10, hz(140), -6, SAW)]),
    ("SICK", 3, 0x48, 0xC9, sick()),    # Voit pahoin!
    ("DEATH", 4, 0x26, 0xAA, death()),
    ("ITEM", 2, 0x00, 0xA4, [       # an item picked up: quick rising "pling"
        step(2, hz(880), 0, TRI),
        step(2, hz(1319), 0, TRI),
        step(5, hz(1760), 0, TRI)]),
    ("BOOZE", 2, 0x02, 0xA6, booze()),  # beer, Lonkka, cognac
    ("HINT", 2, 0x00, 0xA3, hint()),    # stepping on a hint
    ("WATER", 2, 0x00, 0xA4, water()),  # wading through water
    ("POOP", 2, 0x00, 0xF0, poop()),    # slipping in poop
]


def main():
    inc, hdr = sys.argv[1:3]
    names = [e[0].lower() for e in EFFECTS]
    out = ["; generated by tools/mkfx.py -- edit that file, not this one", ""]
    for label, col in (("prio", 1), ("ad", 2), ("sr", 3)):
        out.append("%-7s .byte   %s" % (label + ":",
                   ", ".join("$%02X" % e[col] for e in EFFECTS)))
    out.append("lo:     .byte   " + ", ".join("<fx_" + n for n in names))
    out.append("hi:     .byte   " + ", ".join(">fx_" + n for n in names))
    for n, e in zip(names, EFFECTS):
        assert len(e[4]) * 6 < 256, n    # steps are read with LDA abs,Y
        out.append("fx_%s:" % n)
        for st in e[4]:
            out.append("        .byte " + ",".join("$%02X" % b for b in st))
        out.append("        .byte 0")
    open(inc, "w").write("\n".join(out) + "\n")

    h = ["/* generated by tools/mkfx.py -- edit that file, not this one */"]
    for i, e in enumerate(EFFECTS):
        h.append("#define SND_%-8s %d" % (e[0], i))
    h.append("#define SND_COUNT    %d" % len(EFFECTS))
    h.append("#define SND_NAMES    " +
             ", ".join('"%s"' % e[0].lower() for e in EFFECTS))
    open(hdr, "w").write("\n".join(h) + "\n")


if __name__ == "__main__":
    main()
