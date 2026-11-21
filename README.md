# SpurguX — reverse engineered and brought back to life

**SpurguX** is a Finnish roguelike written in Turbo Pascal for DOS in 1987
by **Petri Niska** ("P.N."). You play an anti-hero staggering through the
streets of Helsinki with nothing but a terrible thirst, looking for the
bottle of cognac that is rumoured to wait on level 50. On every level you
collect all the dots, avoid (or fight) a colourful cast of street
characters, keep your strength up and your blood alcohol at a level where
the game still "flows", and take the exit down.

The original source code is lost. This repository rebuilds the game from
the DOS binaries alone, documents how it works, and ports it to new
platforms: a modern Linux terminal and the Commodore 64.

> The game is satirical and deliberately crude, a product of 1987 Helsinki
> street culture. Its texts and characters are kept as they were.

## What is here

| path | contents |
|------|----------|
| [`spurgudesign.md`](spurgudesign.md) | clean-room design specification: screen, map generator, creatures, items, combat, drinking, scoring, file formats, all reverse engineered from the binaries |
| [`spurgux117.pas`](spurgux117.pas) | clean-room re-implementation of v1.17 in Free Pascal |
| [`cspurgux/`](cspurgux/README.md) | ANSI C89 port for Linux terminals, sized for low-end machines (no floating point, ~8 KB RAM) |
| [`spurgu64/`](spurgu64/README.md) | Commodore 64 port (cc65): PETSCII graphics, scrolling playfield, SID sound effects, joystick, disk save and high scores |
| [`SPURGU64PLAN.md`](SPURGU64PLAN.md) | the plan and memory budget for the C64 port, with the measured results |
| `work/` | reverse-engineering tools and notes: a 16-bit x86 emulator, a disassembler, Turbo Pascal 6-byte `real` arithmetic, string extraction, annotated disassembly listings |

## Playing

**Linux (C89 port)**

    cd cspurgux
    make
    ./spurgux

**Free Pascal version**

    fpc spurgux117.pas
    ./spurgux117

**Commodore 64** (needs cc65, Python 3 and VICE's `c1541`)

    cd spurgu64
    make            # spurgu64.prg and spurgu64.d64
    make run        # start it in the VICE emulator

On a real C64: `LOAD"SPURGU64",8` and `RUN`.

Movement follows *Hack*: `h j k l y u b n`, with SHIFT to run. `?` shows
the help, `i` the inventory, `>` takes the exit once every dot is
collected. The C64 version also takes the cursor keys and a joystick in
port 2.

The high-score file `SPU.TOP` has the same format in every version,
including the DOS original.

## How it was made

1. **Reverse engineering.** The v1.17 `SPU.COM` was disassembled and its
   behaviour traced with a purpose-built 16-bit emulator, and the original
   was observed running under QEMU. Everything was written down in
   `spurgudesign.md`. No original source code was used.
2. **Re-implementation.** `spurgux117.pas` rebuilds the game rules from
   that specification, keeping the original level generator, creatures,
   turn handling and file formats, and adding a few conveniences from v2.24
   (save game, message repeat, clock).
3. **C89 port.** `cspurgux` turns the Pascal code into portable C with
   fixed-point arithmetic and a small memory footprint, the stepping stone
   to 8-bit machines.
4. **Commodore 64.** `spurgu64` fits the full 66×20 map into a 40-column
   screen with a scrolling view, uses PETSCII and redefined characters for
   the graphics, moves the hot loops into 6502 assembler, and adds SID
   sound effects.

## Credits

* **Petri Niska** — original author of SpurguX (© 1987), the game design,
  the characters and the texts. The game and its data files remain his
  work. All the fun is his.
* **Henri Koskela** — directed the project: led the AI (Anthropic's Claude)
  through reverse engineering the original binaries, re-implementing the
  game, and revitalising this great game on new platforms, the Linux
  terminal and the Commodore 64.

## License

The re-implementations, ports, tools and documentation in this repository
are under the MIT license (see [`LICENSE`](LICENSE)). The original SpurguX
binaries and data files (`SPU.COM`, `SPU.EXE`, `SPU.HLP`, `SPU.SPE`,
`SPU.TIP`) and the game's texts, including the copies of `SPU.TIP` and
`SPU.SPE` in `spurgu64/data/` and the texts embedded in
`cspurgux/spudata.h`, are © 1987 Petri Niska and are not covered by that
license.
