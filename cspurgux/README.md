# SpurguX v1.17 — C89 port

A Linux terminal port of the clean-room SpurguX re-implementation
(`../spurgux117.pas`), written in ANSI C89 and sized for low-end machines.

    make            # size-optimised ./spurgux (UTF-8 terminal, 80x25+)
    make cp437      # raw CP437 output: DOS ANSI.SYS, retro terminals
    make nofloat    # fails if any floating point is used
    make CC=tcc OPT=-O LDOPT=    # another compiler: drop the gcc flags

The original text files (help `SPU.HLP`, `/` explanations `SPU.SPE` and
hints `SPU.TIP`) are compiled into the program from `spudata.h`, so no data
files are needed to play. The high-score file `SPU.TOP` is looked up in the
current directory, the program directory, `./spurg117/`,
`<program dir>/spurg117/` and `<program dir>/../spurg117/`. If none exists,
it is created in the current directory. The save file `SPU.SAV` is written
next to it.

### Embedded texts: `spudata.h` and `tools/mktexts.py`

`spudata.h` holds each original file as a macro of C89 string literals, one
per line, used as an array initializer in `data.c`:

    #define SPU_HLP_LINES "", "   SpurguX Vs 1.17", ...
    static const char *const HlpLines[] = { SPU_HLP_LINES };

The text stays CP437 (bytes above 127 are octal escapes, such as `\204` for
ä) and every literal is at most 80 characters, under C89's 509-character
limit. The header is generated and kept in the repository. To regenerate
it after changing the texts:

    make texts                  # reads ../spurg117/SPU.HLP, .SPE, .TIP
    make texts TEXTS=some/dir   # the files from another directory

`tools/mktexts.py` splits the lines exactly as the old file reader did: CR
LF, CR or LF end a line, ^Z ends the file, tabs become spaces, other
control characters are dropped and lines are cut at 80 characters.

Options: `--mono`, `--seed N`, `--level N`, `--mk N`, `--dumpmap FILE`.
`NO_COLOR` turns colours off.

## Footprint

|                         | first port | now        |
|-------------------------|-----------:|-----------:|
| static RAM (bss + data) | 77 KB      | 7 KB       |
| of which game buffers   | ~75 KB     | ~6 KB      |
| code + constants (text) | 41 KB      | 37 KB + 6 KB texts |
| Linux x86-64 binary, same flags | 45 KB | 47 KB (texts built in) |
| heap (`malloc`)         | stdio 4 KB/file | none  |

Where the RAM goes:
* Screen: 2000 CP437 bytes, 1000 bytes of packed 4-bit colours and a
  250-byte dirty bitmap. Only changed cells are sent to the terminal.
* Map: 1320 bytes, only the 66×20 play area. The reachability flood fill
  marks cells in bit 7 of the map byte, so it needs no extra array.
* High-score table: 560 bytes. The texts are read straight from the
  compiled-in constants, and saves are written field by field straight to
  the file.
* stdio: one static 128-byte buffer (`setvbuf`), shared because only one
  file is open at a time.
* Creature HP: 26 signed bytes. Messages: 2 × 81 bytes.

## Port notes

* **C89 only**: `-std=c89 -pedantic` builds with no warnings with gcc and
  clang. No `printf` family. `term.c` is the only module with terminal and
  POSIX code (termios, `read`/`write`, `nanosleep`, signals). `data.c` uses
  plain stdio plus `readlink`/`getcwd`.
* **Text is CP437 internally**, one byte per character as in the DOS
  original. Finnish letters in the source are written with the `AE`, `OE`
  and `AA` macros (`"Isket p" AE AE "t" AE "si"`). Output is converted to
  UTF-8 through a 129-entry table, or sent raw with `-DOUT_CP437`. Names
  typed in UTF-8 (ä, ö, å) are converted on input.
* **No floating point.** The Pascal `Double` values are fixed-point integers:
  * `Voima` is in units of 1/26100 (= 1/(87·300)). The per-turn regeneration
    `Random/87 + Level/300` becomes `Random(300) + 87·Level`, and the
    0.1/0.2/0.3/0.5 steps are exact.
  * `Prom` is in units of 1/100000, so the 0.00054 per-turn decay is exact.
  * The 6-byte Turbo Pascal `real` in `SPU.TOP` is encoded and decoded with
    integer shifts, so the file stays compatible with the DOS and Pascal
    versions.
* **8/16-bit types** are used everywhere except Voima, Prom and the turn
  counter, which are 32-bit. The code does not assume a 32-bit `int`.
* The random number generator is a 32-bit LCG, so a given `--seed` produces
  different levels from the Pascal build. Level 1 is fixed and is
  byte-identical (`--dumpmap`).
* The ghost seen while drunk is drawn as `°`, because the Pascal build's `¤`
  is not in CP437.
* The save format (`SPURGUXC89SAVE2`) is portable little-endian. Older saves
  are rejected.

## Files

| file        | contents                                                    |
|-------------|-------------------------------------------------------------|
| `spurgux.h` | types, constants, tile codes, fixed-point scales, globals   |
| `term.c`    | raw terminal, CP437 screen buffer with diff output, keyboard |
| `data.c`    | compiled-in texts, high-score table, save/load              |
| `spudata.h` | the original SPU.HLP, SPU.SPE and SPU.TIP as string literals (generated) |
| `tools/mktexts.py` | makes `spudata.h` from the original files (`make texts`) |
| `game.c`    | level generator, combat, creatures, turn handler, main loop |
| `main.c`    | command line and start-up                                   |
