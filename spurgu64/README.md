# SpurguX v1.17 — Commodore 64 port

A Commodore 64 version of the C89 port in `../cspurgux`, built with cc65.
The plan and the memory estimates are in `../SPURGU64PLAN.md`.

    make            # spurgu64.prg + spurgu64.d64 (needs cl65, python3, c1541)
    make run        # x64sc -autostart spurgu64.d64
    make size       # segment sizes from the linker map
    make sfxtest    # sfxtest.prg: listen to the sound effects (see Sound)

On a real C64 or in an emulator: `LOAD"SPURGU64",8` and `RUN` (or
`LOAD"*",8,1`). The data files are read from, and the high scores and the
save game written to, the drive the game was loaded from.

## Screen

```
rows 0-1   message line (the original's 80 columns, wrapped over two rows)
row  2     frame, turn counter
rows 3-22  the 66x20 map through a 40-column window that scrolls sideways
row  23    frame, state text (Voit pahoin!, Horjahtelet, ...)
row  24    Taso Mk Voima Kok Prom
```

The map is kept at its full size. The window follows the player with a
12-column margin, and arrows on the frame show when there is more street to
the left or right. Buildings and frame use PETSCII graphics. The Finnish
letters and the CP437 item symbols PETSCII does not have (Σ ß Θ Φ ⌂ ∞ ¥ æ Ω
Ñ ° ¿ ≈) are drawn over the unused reverse-video letters of a RAM copy of
the character set.

## Keys

| key                         | action                                  |
|-----------------------------|-----------------------------------------|
| h j k l y u b n             | move                                    |
| SHIFT + h j k l y u b n     | run until something is in the way       |
| cursor keys, joystick port 2 | move; joystick fire = `>` (descend)    |
| q e t i . > / ? v p c S Q   | as in the original (see `?`)            |
| F1 / F3                     | help / inventory                        |
| CLR/HOME                    | redraw the screen                       |
| RUN/STOP                    | cancel (ESC)                            |

Name entry: `[` = ä, `]` = ö, `£` = å.

## Files on the disk

| file      | type | contents                                                    |
|-----------|------|-------------------------------------------------------------|
| `spurgu64`| PRG  | the game                                                    |
| `spu.hlp` | SEQ  | help text, re-written for 40 columns (`data/spu_hlp.txt`)    |
| `spu.spe` | SEQ  | `/` explanations (original `data/SPU.SPE`, CP437)           |
| `spu.tip` | SEQ  | hints (original `data/SPU.TIP`, CP437)                      |
| `spu.top` | SEQ  | high scores, created by the game; same 560-byte format as the DOS `SPU.TOP` (CP437 names) |
| `spu.sav` | SEQ  | save game (`S`), deleted when it is loaded                   |

The text files are converted from CP437 to the program's PETSCII encoding
by `tools/mkdata.py` when the disk image is built.
`data/SPU.TIP` and `data/SPU.SPE` are copies of the original v1.17 files
(© 1987 Petri Niska), so the port builds on its own.

## Memory

| area          | address       | size                         |
|---------------|---------------|------------------------------|
| program       | $0801-$B228   | 43.6 KB: code 35.7 KB, strings/tables 7.5 KB |
| BSS           | $B229-$BF6B   | 3.3 KB (map 1320 B, high scores 560 B, tables) |
| free          | $BF6C-$C7FF   | 2.1 KB                       |
| C stack       | $C800-$CBFF   | 1 KB                         |
| screen RAM    | $CC00-$CFE7   | VIC bank 3                   |
| character set | $F000-$F7FF   | RAM under the KERNAL ROM     |

`make size` prints the exact numbers from `spurgu64.map`.

## Speed

Measured in VICE (x64sc, PAL):

* a turn: about 0.15 s (map redraw: a 6502 assembler inner loop in
  `drawrow.s`; map scans with `memchr`)
* a new level: a few seconds — most of it is the reachability flood fill,
  rewritten as row sweeps (identical result to the C89 port's cell-by-cell
  fill, checked on 20 000 random maps)

## Sound

SID voice 1 plays short effects from the KERNAL jiffy interrupt
(`sound.s`), so the game never waits for a sound:

| event                         | sound                               |
|-------------------------------|-------------------------------------|
| a dot (`.`) collected         | short two-note blip                 |
| an item picked up (overalls, weapon, boots, chocolate, sausage, a good apple) | quick rising "pling" |
| beer, Lonkka or cognac        | glug-glug + happy rising arpeggio   |
| stepping on a hint (`¿`)      | bizarre warble with a hiss          |
| wading into water             | splash + "blub"                     |
| slipping in poop (`æ`)        | low fluttering "bbrrrp"             |
| money collected               | cash register: clack + bell         |
| you hit someone (or throw)    | noise whack                         |
| someone hurts you             | thud                                |
| *Voit pahoin!* comes on       | a falling, wobbling "bouuuu"        |
| death                         | descending "wah wah wah waaah"      |

A more important sound cuts in; an equal or less important one waits
until the current one ends (one is kept waiting).

### Changing the sounds: `tools/mkfx.py`

The effects are defined in `tools/mkfx.py`. `make` runs it to generate
`build/sfx.inc` (the tables `sound.s` includes) and `build/sfx.h` (the
`SND_*` ids), so there is nothing to copy by hand: edit the script and run
`make` again.

Each effect in the `EFFECTS` list is

    ("NAME", priority, attack/decay, sustain/release, [steps])

and each step is `step(frames, freq, slide, wave, gate=True)`:

| argument | meaning                                                              |
|----------|----------------------------------------------------------------------|
| `frames` | length of the step in 1/60 s (1-255)                                 |
| `freq`   | SID frequency register value; `hz(440)` converts from hertz (PAL)    |
| `slide`  | added to the frequency every frame; negative = pitch falls           |
| `wave`   | `TRI`, `SAW`, `PUL` (50 % pulse) or `NOI` (noise)                    |
| `gate`   | `False` starts the release, e.g. for a gap between notes             |

`attack/decay` and `sustain/release` are the SID's `$D405`/`$D406` values,
one hex digit each (attack 0 = 2 ms ... F = 8 s; decay/release 0 = 6 ms
... F = 24 s; sustain 0-F is the level held while the gate is on). The
helpers `wobble()`, `sick()` and `death()` show how to build vibrato and
melodies with a loop.

To listen to the effects without playing the game:

    make sfxtest    # builds sfxtest.prg and starts it in VICE

In `sfxtest` the keys 1-9 and then a, b, ... play one effect each (the
screen lists them), RETURN plays them all in turn and Q quits.

A new effect is added to the end of `EFFECTS` (its id is `SND_NAME`, in
list order) and played from the game with `SndPlay(SND_NAME);` in
`game.c`. Each effect may have at most 42 steps.

## Differences from the C89 port

* RNG seed: the jiffy clock when the name has been typed.
* `c` shows the time since the C64 was switched on (there is no clock).
* No command-line options (`--seed`, `--level`, `--mk`, `--dumpmap`).
* After a game the program asks `Uusi peli (k/e)?` instead of exiting.
* Save files are not compatible with the Linux build (`SPURGU64SAVE1`).

## Source files

| file          | contents                                               |
|---------------|--------------------------------------------------------|
| `spurgux.h`   | types, tile codes, screen layout, custom character codes |
| `c64.c`       | VIC bank / charset set-up, drawing, keyboard, joystick, timing |
| `drawrow.s`   | map row renderer (assembler)                            |
| `flood.s`     | reachability flood fill row sweeps (assembler)          |
| `sound.s`     | SID sound effects, interrupt driven (assembler)         |
| `data.c`      | disk I/O with the KERNAL (`cbm_*`), high scores, save/load |
| `game.c`      | the rules (from `../cspurgux/game.c`), 40-column drawing |
| `main.c`      | start-up and the play-again loop                        |
| `glyphs.h`    | custom characters, generated by `tools/mkglyphs.py`     |
| `tools/mkfx.py` | sound effect definitions; generates `build/sfx.inc`, `build/sfx.h` |
| `tools/sfxtest.c` | `make sfxtest`: plays the effects one key each        |
| `spurgu64.cfg`| cc65 linker config (program below $CC00, 1 KB stack)    |
