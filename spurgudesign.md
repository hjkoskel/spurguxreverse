# SpurguX — Clean-Room Re-implementation Design Specification

**Derived exclusively from reverse engineering** of the original DOS binaries
(`spurg117/SPU.COM` v1.17, `spurg224/SPU.EXE` v2.24, Turbo Pascal, 1987,
author "P.N." / Petri Niska, Clinet handle `pate`), the shipped data files
(`SPU.HLP`, `SPU.SPE`, `SPU.TIP`, `SPU.TOP`), disassembly of the executables,
and live observation of v1.17 running under QEMU (an automated play-bot plus
targeted probes). No original source code was used.

Where a value is inferred rather than directly observed, it is marked
**(inferred)**; where it is directly read from the disassembly or a data file,
it is stated as fact.

---

## 1. Overview

SpurguX is a Finnish "drunk anti-hero" roguelike. The player is an
anti-hero ("Spurgu") staggering through the streets of Helsinki, driven only
by a fierce thirst, searching for a bottle of **cognac (konjakki, `è`)**
rumoured to be on **level 50**.

* The game presents itself as "based on real events"
  (`tositapahtumiin perustuva`), in deliberate contrast to *Hack*
  ("SpurguX is based on real life, whereas Hack is pure fiction").
* The tone is dark, satirical and heavily profane (1987 Helsinki street
  culture: punks, skinheads, teekkari, juppi, police, and a great many
  offensive character types — see §9).
* **Core loop:** on each level, collect **all the dots (`.`)** scattered on
  the map, then stand on the **exit (`>`, bottom-right)** and press `>` to
  descend to the next level. Repeat until level 50 and the cognac.
* **Resources:** the player tracks **Voima** (strength/stamina),
  **Kokemus** (experience/level), **Promille** (blood-alcohol level), and
  **Mk** (money). Drinking raises Promille; Promille and Voima interact with
  survival (see §5).
* **Combat** is initiated by the player walking *into* an entity
  ("if the player has aggressive tendencies, he may beat up the aforementioned
  creatures — or even a wall — by walking into them; what follows is another
  matter…").

Win condition (inferred from HLP + level structure): reach the cognac on
level 50. Loss condition: Voima is exhausted (collapse/death), or the player
chooses suicide (`Q`).

---

## 2. Technical basis (as implemented by the original)

| Item | v1.17 | v2.24 |
|---|---|---|
| Binary | `SPU.COM` (TPA, loaded at `0x100`) | `SPU.EXE` (NE/overlaid DOS EXE) |
| Size (code) | game code region file `0x2C7C`–`0xE7E5` | larger; adds save/load, fines, wanted, APTEEKKI, new entities |
| Compiler | Borland Turbo Pascal (c) 1985 (RTL signature present) | same |
| Video | VGA **mode 0x10** (720×400), 80×25 text, **9×16 font**, direct writes to the `0xB8000` text buffer (screendump size + buffer read confirm) | same |
| Data files | `SPU.HLP`, `SPU.SPE`, `SPU.TIP`, `SPU.TOP` | same + save file |
| Author string | `(C) 1987 P.N.`, "Clinetissä on pate" | adds "Nixulassa niskap." |

* **Text encoding:** the in-memory/message strings use a custom encoding where
  `0x84` = **ä** and `0x94` = **ö** (translated at display time). Map/object
  glyphs are **CP437** characters (see §9, §15).
* **Screen:** 80 columns × 25 rows. The map is drawn inside a framed window;
  a **message line** is row 0; a **status line** is the bottom row (row 24,
  0-based).
* **Randomness:** a dedicated RTL random routine (`@Random`, disassembly
  `0x10DA`: `push ax; call seed; shr ax,1; cwd; div bx; xchg ax,dx; ret`)
  is used throughout (dot/NPC placement, combat outcomes, Promille decay).

### 2.1 Binary layout (v1.17 `SPU.COM`, file offsets)
* `0x0000`–`0x0002` TPA header; entry jumps to `0x2D7C` (mem).
* `0x0003`–`0x2C7B` Turbo Pascal **RTL** + some game data/globals.
* `0x2C7C`–`0xE7E5` **game code**, with **string literals and INT-3 padding
  interleaved inline** (see §15.2).
* `0x146` (file) start of the **map tile array** (§8.4).
* Game **globals** live at mem `0x2xx`/`0x3xx` (file `0x1xx`/`0x2xx`).

---

## 3. Screen layout

```
row 0   : message line (single line, left-justified; cleared each turn)
row 1   : ╔══════… top border of map frame
rows 2-21: map area (20 rows × 66 visible columns, cols 13-78)
row 22  : ╚══════… bottom border
rows 18 (left margin, cols 0-11): transient state text, e.g. "Voit pahoin!"
row 24  : status line:
          "Taso: <n>   Mk: <n>   Voima: <n>   Kokemus: <n>   Prom: <x.x>   <turn>"
```

* The map frame bytes (verified from the original's in-memory text buffer,
  rendered through the mode-0x10 9×16 font; 1-based (col,row)): top
  (13,2)=`0xC9` ╔, (14..79,2)=`0xCD` ═, (80,2)=`0xBB` µ; sides
  (13|80, 3..22)=`0xBA` ║; bottom (13,23)=`0xC8` ╚, (14..79,23)=`0xCD` ═,
  (80,23)=`0xBC` ╝. In the 9×16 font the 0xC0–0xDF box set is laid out
  differently from CP437 (e.g. `0xBB` renders as µ, not ╗).
* The **status line** is produced by a dedicated renderer (procedure at file
  `0x328D`) that writes the labels `Taso: `, `Mk: `, `Voima: `, `Kokemus: `,
  `Prom: ` (all confirmed inline in the disassembly) followed by the numeric
  globals, at fixed column offsets, on the bottom row.
* The **turn counter** is the last token on the status line and increments
  once per player action.

---

## 4. Player state

| Stat | Global (mem) | Start | Notes |
|---|---|---|---|
| **Taso** (level) | — | 1 | 1 … 50. Cognac on 50. |
| **Mk** (money) | — | 0 | Gained from `(` money tiles / some NPCs; spent on items. |
| **Voima** (strength) | `0x2FC` (6-byte `real`) | 10 | Consumed by wall-bumps, combat; regenerates slowly. Exhaustion ⇒ death. |
| **Kokemus** (experience) | — | 1 | +1 per defeated enemy (observed 1→2 after a dog fight). |
| **Promille** (`Prom`) | `0x302` (6-byte `real`) | 0.2 | `+0.2` per beer/lonkka; no visible natural change at low values. Drives stagger, shop-refusal, blackout, friendly-police. |
| **X / Y** (position) | `0x2EE` / `0x2F0` | (11,14) on levels 1 & 3 | Map coordinates. |
| **turn** | — | 0 | Action counter. |

### 4.1 Voima (strength/stamina) — best current model
* Starts at **10**.
* **Wall-bump** ("Isket päätäsi seinään, mielenkiintoista." = "You hit your
  head against the wall, interesting.") depletes Voima; a long streak of
  bumps (117) killed the player (turn 177).
* **Combat** depletes Voima (each dog bite costs 1–4, observed).
* **Regenerates** slowly over time (observed +1 over several turns; exact
  rate is **(inferred / not yet pinned down)** — the turn-update Voima code
  has not been fully extracted).
* **Death:** when Voima is fully exhausted the game prints
  "Voimasi ehtyvät, lyhistyt maahan …" ("Your strength runs out, you collapse
  to the ground …") and proceeds to the death sequence (§12). A single turn
  at exactly 0 did **not** kill the player in one observed dog fight (it
  recovered to 1), so the lethal threshold is likely **Voima < 0 after a
  hit** or a separate exhaustion check **(inferred — needs the turn-update
  disassembly or more live testing)**.

### 4.2 Promille (drunkenness) — traced
`Prom` is a 6-byte Turbo-Pascal `real` at **`0x302`** (Voima at `0x2FC`);
the status line (bottom row) prints it with 3 integer / 1 decimal digit
(`mov di,0x302; push ds; call 0x1873` → print helper `0x27EE`).
Ground-truth values were read from the on-screen status line under QEMU.

* **Starts at 0.2** (screen-confirmed; not 0.0).
* **Drinking:** `q` opens the drink/shop menu (shared procedure file
  `0x4F1C`, also used by the shopkeeper — "Tervetuloa, \<name\>, mitä
  myisit? >"). Selecting an item runs the consume routine (file `0x7E50`):
  it decrements the inventory counter at `[0x374 + digit*2]`
  (`1`=kalja `0x376`, `2`=lonkka `0x378`, `3`=kiekura `0x37A`) and then
  updates `Prom`. Measured on-screen:
  * **kalja (beer) `1` → Prom +0.2**
  * **lonkka `2` → Prom +0.2**
  * **kiekura `3` → no Prom change** (it is food; it affects Voima).
* **Decay (CONFIRMED, file `0xE346..0xE35B`):** a **fixed −0.00054** per
  turn while `Prom >= 0` (`Prom := Prom − <inline real 77 8D 97 6E 12 03>`
  = 0.000540128…; the real-op thunk `0x18C1`→`0x154A` is subtraction). Very
  slow — below the 0.1 status-line precision for many turns, which is exactly
  why the displayed Prom looked pinned over 14 wait-turns at Prom 0.2
  (14 × 0.00054 ≈ 0.008 < 0.1). It takes ~370 turns to sober fully from 0.2.
* **Kankkunen / sobering death (CONFIRMED, file `0xE33E..0xE36A` → `0x9D9A`):**
  the decay guard is `if Prom >= 0 then Prom := Prom − 0.00054 else <Kankkunen>`.
  Once `Prom` dips **below 0** the decay stops and the handler calls the
  Kankkunen routine (file `0x9D9A..0x9DF2`), which prints
  **"Kankkunen saapuu ja katkaisee taipaleesi …"** (inline string, len 44,
  file `0x9DAB`) and sets the death flag `[0x2CC] = 1`. There is exactly one
  caller (`0xE36A`), so this is the *only* path to Kankkunen and there is no
  turn-counter — the trigger is purely `Prom < 0`. Net effect: stay at
  Prom 0.0 too long and Kankkunen arrives and ends the run. From a starting
  Prom 0.2 this is ~372 turns; the last ~93 turns (Prom < 0.05) also show
  "Voit pahoin!" while the status line reads 0.0. The port now reproduces this
  (no clamp; `Prom < 0 → Die('Kankkunen …')`).
* **Drunk-state machine (CONFIRMED, file `0xE1C5..0xE311`) — the per-turn
  state-text + effect, in order:**
  * **`Prom < 0.05`** → prints **"Voit pahoin!"** (you feel sick — the
    sobering/hangover onset). Threshold is an inline real `7D CC CC CC CC 4C`
    = 0.04999… No Voima change.
  * **`Prom > 4`** → prints **"Oksennat!"** (you vomit) **and drops Voima by 1**
    (file `0xE241..0xE253`, `Voima := Voima − 1`), then paints a vomit mark
    (tile `0x17`) on the floor cell ahead. So heavy drinking (Prom > 4)
    drains 1 Voima **every turn** you keep puking.
  * **`3 < Prom ≤ 4`** → prints **"Horjahtelet"** (you stagger). No Voima
    change (movement is separately randomized at Prom ≥ 3, §above).
  * **otherwise (0.05 ≤ Prom ≤ 3)** → blank state line.
  * The real-compare thunks: `0x1873`=load real, `0x1882`=load inline real
    constant, `0x1A2F`=push int-as-real, `0x1990`=`first<second`,
    `0x1976`=`first>second`.
* **Drunk-stagger (movement):** the turn-processor (file `0x54A0`) compares
  `Prom` with **3**; when drunk the intended direction is overwritten with
  `dx:=Random(3)-1, dy:=Random(3)-1` (each in −1..1; 0,0 = stumble in
  place). Ported (threshold 3.0).
* **Shop refusal:** while drunk the shopkeeper refuses — "Teille ei myydä,
  te olette humalassa!" ("We don't sell to you, you're drunk!", file
  `0x7697`). The hospital likewise rejects drunks ("Emme ota juoppoja!").
* **Blackout / doze-off:** at high Prom the player "dozes off" — file
  `0x4C67` prints "Sinua alkaa väsyttää ja nukahdat." ("You start to get
  tired and you doze off.") and fades to black; the caller (file `~0x99F6`)
  then resets state (clears the friendly flag `0x2CE`, re-rolls the level,
  resets Voima/Prom). The player **survives** the blackout (it is not the
  Voima-exhaustion death). Exact triggering Prom **not yet pinned down**.
  *(Port deviation: the port currently treats a very high Prom as fatal via
  `Die('Sinua alkaa vasytaa ja nukahdat.')` — a simplification, since the
  original's exact post-blackout state/level is not yet extracted.)*
* **Friendly-police trigger:** with 50% odds (`Random(2)`) a check on
  Voima and Prom (file `~0xC1CE`) can set the friendly flag `0x2CE=1`,
  changing police/NPC behaviour.
* In v2.24 a hangover state (**krapula**) is added — a **post-blackout**
  condition: drink heavily → high Prom → blackout/doze-off → wake hungover.
  The HLP warns "krapula on kettumainen tauti!" ("a hangover is a hellish
  disease!") and notes the game doesn't really progress without alcohol but
  you shouldn't drink recklessly; a TIP adds "Krapula on köyhien tauti"
  ("a hangover is a disease of the poor"). **Absent from v1.17** (the string
  is nowhere in `SPU.COM`). In the v2.24 `SPU.EXE` the 7-char label `krapula`
  sits at file `0xEF4F` (data offset `0x4F`), immediately after a `'%s'` slot
  (`0xEF4C`); a status cluster `'Prom: %3.1f' + '%s' + ESC[1m(bold) + '%s' +
  'tuntematon'` (data `0x1E86..0x1E9D`) is where Promille + hangover state are
  rendered, and the related strings are 'Hän sammuu!' (data `0x17D`), 'Ukko
  sammuu ja kolehtihaavi aukeaa' (data `0x0FEA`), 'Sinua alkaa väsyttää ja
  nukahdat' (file `0xED7C`). The exact thresholds/duration/effects could **not**
  be pinned down statically: the `krapula` label is never loaded as a string
  (its only code hits, data `0x4F`, are the number 79 used as a screen column
  in a print-at-position routine), and the packed v2.24 binary fails to unpack
  in QEMU ("Packed file is corrupt"), so the behaviour couldn't be observed
  live. **Out of scope for the v1.17-faithful port.**

### 4.3 Kokemus (experience)
* +1 per defeated enemy (observed). Level/character progression is
  **(not fully mapped)**; the exact use of Kok is to be confirmed.

---

## 5. Controls

Primary mapping is **Hack-like** (HJKL), confirmed by the in-game key
dispatcher (procedure at file `0x9291`, which compares the read key against
each code below) and by `SPU.HLP`:

| Key(s) | Action | Key(s) | Action |
|---|---|---|---|
| `h` | move left, 1 step | `H` | move left **repeat-until-blocked** |
| `j` | move down, 1 step | `J` | move down **repeat-until-blocked** |
| `k` | move up, 1 step | `K` | move up **repeat-until-blocked** |
| `l` | move right, 1 step | `L` | move right **repeat-until-blocked** |
| `y` | move NW, 1 step | `Y` | move NW **repeat-until-blocked** |
| `u` | move NE, 1 step | `U` | move NE **repeat-until-blocked** |
| `n` | move SE, 1 step | `N` | move SE **repeat-until-blocked** |
| `b` | move SW, 1 step | `B` | move SW **repeat-until-blocked** |
| `q` | **juo** — drink | `i` | **inventaario** — inventory |
| `e` | **syö** — eat | `v` | **version** |
| `.` | **odota** — wait (pass turn) | `?` | **help** |
| `Q` | **luovuta** — quit (asks "Todella luovutat (k/e)?") | `>` | **tasonvaihto** — descend (only on exit, all dots) |
| `t` | **heitä** — throw (item prompt `1..9`/`a..c`, then direction; `0`/invalid = cancel, no turn) | `/` | **sepustus** (see note) |

* **Arrow keys** also move single steps; **Del** = `.` (wait), **Ins** = `i`
  (inventory).
* **Uppercase repeat moves** (v1.17, RE'd from dispatcher file `0x9291`):
  the dispatcher sets the mode flag `[0x2F8]` := 1 and re-queues the
  corresponding **lowercase** key into `[0x31A]`, so the same direction is
  retried every turn until the move is blocked; the blocking bump ends the
  repeat (the key becomes the `0x69` "no turn consumed" sentinel). A repeat
  run therefore consumes one turn per step it actually makes. The drunk-stagger
  path (Prom ≥ 3, file `0x54B5..0x54D0`) also clears `[0x2F8]`, so while drunk a
  repeat run collapses to a single randomized step.
* **ESC + key remap** (v1.17, RE'd from the dispatcher file `0x92D0..0x9393`):
  an **inline two-key read**, not a persistent armed state — if the key is
  ESC the dispatcher immediately reads the *next* key and, if it is in the
  second cluster, rewrites it before dispatching:
  `G`→`y` (NW), `H`→`k` (U), `I`→`u` (NE), `K`→`h` (L), `M`→`l` (R),
  `O`→`b` (SW), `P`→`j` (D), `Q`→`n` (SE), `R`→`i` (inventory),
  `S`→`.` (wait). Any other second key falls through to `W`, which is not a
  v1.17 command and so dispatches as an unknown command. (The port approximates
  the blocking second read with a short timeout so a bare ESC is a no-op.)
* **`/` = sepustus** is listed in HLP but its exact effect is not yet
  confirmed live **(open item)**.

Movement is **8-directional** (4 orthogonal + 4 diagonal). Each single-step
move is one action/turn; a repeat run takes one turn per step it makes.

---

## 6. Turn system

One **turn** = one player action (a movement, wait, drink, eat, inventory
peek that advances time, etc.). Per turn, in approximate order **(inferred
from structure + observation)**:

1. **Read key** (RTL `ReadKey` → global at mem `0x31A`), apply the (optional)
   remap, dispatch (§5).
2. **Player action:** for movement, compute the target cell `(x+dx, y+dy)`
   from the direction code and process the tile/entity there (§8).
3. **NPC/entity updates** (dogs & dot-eaters move; police behaviour; Kake, etc.).
4. **Resource updates:** Voima regen, Promille decay (§4).
5. **Death check** (Voima exhaustion, etc.).
6. **Redraw** the changed cells + status line + message line.
7. **Increment** the turn counter.

The turn processor is the large procedure at file `0x5473` (disassembly
`0x5573`–`0x7D54`, ~257 instructions in its main body plus per-entity
handlers). It:
* sets `dx`,`dy` from the key (8-way),
* computes the target tile index `map[x*40 + y*2 + 0x146]` (2-byte cells),
* runs a big **case switch** on the target tile code (§8.5) to decide the
  outcome,
* on success places the player (tile code `0x6B`, drawn `@`) and redraws.

---

## 7. Message / I/O system

* **Messages** are single-line text on row 0, one per turn, cleared on the
  next turn. All player-facing strings come from a **string table** in the
  binary (438 entries in v1.17, 721 in v2.24 — see the extracted catalogs
  `str117.txt` / `str224.txt`).
* **Inline-string output idiom (key to the disassembly):** the RTL writes a
  string by a *return-address trick* —
  ```
  call 0x295D            ; WriteInlineStr
  <len><chars…>          ; the literal, immediately after the call
  <next code>            ; execution resumes here
  ```
  `0x295D` reads the length byte from the return address, writes each
  character via `0x2928` (WriteChar), and `jmp`s back past the string.
  `0x2975` = WriteLn (`CR LF`). This is why string literals sit **inline in
  the code stream** and why a naive linear disassembly "falls into" them.
* Other RTL output helpers seen: `0x0DAa` = GotoXY(x,y); `0x10DA` = Random;
  `0x2458`/`0x27EE`/`0x279D` = cursor/width/flush helpers; `0x1022` = flush.
* **Help/credits/tips** are stored in external files and displayed by
  dedicated procedures: `SPU.HLP` (procedure at `0x7FBC`), `SPU.SPE`
  (speakers/credits, `0x8090`), `SPU.TIP` (tips, used by the hint pager
  `0x5086`).
* **High scores** use `SPU.TOP` (20 records × 28 bytes; I/O at file
  `0xE4BB`–`0xE820`, buffer at `0xEDC`; RTL file ops `ReadRec`/`WriteRec`/
  `EOF`/`CloseFile`).

---

## 8. Map, levels & movement

### 8.1 Level structure
* 50 levels, descending. Level `n` is a grid of tiles; the player starts
  around **(11,14)** (observed on levels 1 and 3; **(assumed** constant).
* Each level has a number of **dots (`.`)** that must **all** be collected
  before descending. Attempting `>` with dots remaining shows
  "Ensin pisteet …" ("First the dots …"); pressing `>` **off** the exit tile
  shows "Ei ei, vain portaita pitkin" ("No no, only along the stairs").
* The **exit (`>`)** is at a fixed position — the **bottom-right** of the map
  (internal coords **x=78, y=21**, per the exit check at file `0x3F5F`).
* **Buildings** occupy blocks of the map and have **names that change per
  level** while the geometry stays the same. Observed names: `ALCO`,
  `HOTELLI`, `SAIRAALA` (hospital), `ISKU`.
* **Building-door handler (CONFIRMED, file `0x7BC9..0x7D0B`):** the buildings
  are sealed (collision), but each has a **door cell** — the walkable cell
  directly against its wall. Bumping the wall from that cell prints a
  building-specific line with **no Voima change**; any other wall bump is the
  generic head-bump (`Isket päätäsi seinää, mielenkiintoista.`, **plus** a
  Voima hit, file `0x7CF3`). The check is purely positional (player `[0x2EE]`/
  `[0x2F0]`), **not** gated on Prom — the "juoppoja" wording is flavor. Doors:
  * **SAIRAALA/HOTELLI** (body cols 34..55 rows 3..6) → door **(44,7)/(45,7)**
    (the set test is `X in {44,45} AND Y=7`) → **"Emme ota juoppoja!"**
  * **bottom-mid** (cols 43..51 row 19) → door **(47,18)** → **"Ei ketään kotona."**
  * **bottom-right** (cols 54..62 row 19) → door **(58,18)** → **"Valitan, ei vapaita huoneita."**
  * **fallback** (any other wall) → **"Isket päätäsi seinään, mielenkiintoista."** + Voima
  A blocked door-bump, like any edge-bump, consumes **no turn**.
* The map frame and the bottom row of buildings are **sealed/decorative**
  (cannot be entered) in the observed levels.

### 8.2 Tile codes
Tiles are stored as **16-bit codes**. The map is a grid of words; each word is a
**tile code**. The *tile code is also the index into the entity position table*
(§8.7) — i.e. `table[tile]` tracks the creature whose glyph is that tile.

The **map renderer** (file `0x3405..0x3F4F`; the full-map pass is gated by
`[0x2CD]`) is authoritative for what each tile *draws* (ndisasm-verified).
It pushes **one byte per tile code**, which is rendered through the VGA
mode-0x10 **9×16 font**: for 0x80–0xBF and 0xE0–0xFF those glyphs match
CP437, for 0xC0–0xDF the box-drawing layout differs (§3 frame bytes).
(An earlier draft of this doc assumed the code was the ASCII glyph / that
`0x24`=money, `0x25`=beer, etc. — that is **wrong**; the real mapping is
below, extracted from the renderer switch; the *Glyph* columns are the 9×16
rendering of the renderer byte, equal to CP437 for the bytes involved.)

Floor / wall / control tiles (CP437 glyphs):

| Code | Glyph | Meaning |
|---|---|---|
| `0x01`–`0x13` | `% & Σ ! \ ß Θ … ¿ … ( ` | **pickups/items**: `0x01` kalja `%`, `0x02` lonkka `&`, `0x03` haalarit `Σ`, `0x04` pamppu `!`, `0x05` stiletti `\`, `0x06` konjakki `ß`, `0x07`/`0x0B` camel `Θ`, `0x0D` vihje `¿`, `0x0E` maito, `0x10` omena `o`, `0x11` silakka `¥`, `0x12` makkara `(` (see live L1: `%`@(59,10), `\`@(51,18), `¿`@(61,14), `(`@(26,15)) |
| `0x14` | `.` | **Piste** — dot (must collect) |
| `0x15` | `>` | **exit / stairs** (fixed at map (78,21); permanent, §17.11) |
| `0x16` | `≈` | vesialue (water; renderer byte 0xF7) |
| `0x17` | `æ` | hauta (grave; renderer byte 0x91) |
| `0x0C` | `$`* | **rahaa** (money; SPU.SPE keys `$` as "rahaa"). The renderer itself has no case for it (→ space); the `'$'` is the one-shot draw the blackout handler makes when it drops this item (§17). *whether stepping on it adds Mk was not separately verified* |
| `0x1D` | (space) | **unknown** — walkable (≤ `0x1D`), drawn as space (no renderer case); identity unresolved (§17) |
| `0x5B` | `±` | structure (renderer byte 0xF1) |
| `0x5C` | `Ç` | structure (renderer byte 0x80; *not* the stiletto — that is `0x05`) |

Creature / object tiles (glyph = the renderer output):

| Code | Glyph | Entity (Finnish) | Notes |
|---|---|---|---|
| `0x1E`–`0x22` | `C` | **Poliisi** (Lainvartija) | 5 variants; **dead AI loop → static** (§10.3) |
| `0x23` | `M` | **Mummo** (old woman) | no chase block; static |
| `0x24` | `K` | **Kake** | chase block @ `0xB…`; flag `0x2D1`, cache `0xDF2/0xDF4` |
| `0x25` | `V` | **Taskuvaras** (pickpocket) | chase block @ `0xB0A0`; flag `0x2D2`, cache `0xDF8/0xDFA` |
| `0x26` | `T` | **Vankikarkuri** | chase block @ `0xB3C5`; flag `0x2D5`, cache `0xE5E/0xE60` |
| `0x27` | `Ω` | **Portinvartija** (SPU.SPE: "Ω: Portinvartija"; renderer byte 0xEA) | no chase block; throw hit: "Osut portsariin." (throw handler file `0x81A7..0x923E`) |
| `0x28` | `N` | **Neekeri** | no chase block (ghost *eats* it, `0xA591`) |
| `0x29` | `H` | **Hinta** | chase block; ghost eats it (`0xA5C7`) |
| `0x2A` | `Q` | **Trokari** | chase block; ghost kills it (`0xA5FB`) |
| `0x2B` | `R` | (variant) | |
| `0x2C` | `I` | **Hippi** | chase block; ghost kills it (`0xA637`) |
| `0x2D` | `B` | **Pistepirko** (dot-eater) | **no chase block of its own** — passive, moved by others (§10.2) |
| `0x2E` | `D` | **Koira** (dog) | chase block; ghost kills it (`0xA6BA`) |
| `0x2F` | `G` | (variant) | |
| `0x30` | `O` | **O** (skinhead?) | chase block; ghost removes it (`0xA6F9`) |
| `0x31` | `S` | (variant) | chase block @ `0xD40E` |
| `0x32` | `A` | (variant) | chase block |
| `0x33` | `P` | **Punkkari** (punk) | sprite; flag `0x2E3`, pos `0xE52/0xE54` |
| `0x34` | `Ñ` | punk sprite (2nd frame) | |
| `0x35` | `J` | (variant) | chase block |
| `0x36` | `E` | (variant) | chase block |
| `0x4B` | (space) | **Aave** (ghost) | **invisible** (drawn as blank); flag `0x2DD`, cache `0xEDC/0xEDE`, HP `0xEDA` |
| `0x6B` | `@` | **player** | drawn at `[0x2EE]/[0x2F0]` |

**Renderer byte mapping** (ndisasm-verified switch, file
`0x3405..0x3F4F`): tile → byte pushed: `0x01`→0x25, `0x02`→0x26,
`0x03`→0xE4, `0x04`→0x21, `0x05`→0x5C, `0x06`→0xE1, `0x07`/`0x0B`→0xE9,
`0x0D`→0xA8, `0x0E`→0x7F, `0x0F`→0xEC, `0x10`→0x6F, `0x11`→0x9D,
`0x12`→0x28, `0x14`→0x2E, `0x15`→0x3E, `0x16`→0xF7, `0x17`→0x91,
`0x1E..0x22`→0x43, `0x23`→0x4D, `0x24`→0x4B, `0x25`→0x56, `0x26`→0x54,
`0x27`→0xEA, `0x28`→0x4E, `0x29`→0x48, `0x2A`→0x51, `0x2B`→0x52,
`0x2C`→0x49, `0x2D`→0x42, `0x2E`→0x44, `0x2F`→0x47, `0x30`→0x4F,
`0x31`→0x53, `0x32`→0x41, `0x33`→0x50, `0x34`→0xA5, `0x35`→0x4A,
`0x36`→0x45, `0x5B`→0xF1, `0x5C`→0x80, `0x6B`→0x40;
**any other code → `0x20` (space)** — this covers the wall `0x63`, the
unknown `0x1D`, the invisible ghost `0x4B`, and `0x0C`.

The wall/building tile is **`0x63`** — the renderer draws it as a space, and
the cityscape pass draws the building glyphs on top. In the verified live L1
dump, all 180 wall cells are `0x63` (§8.4 wall template); the low codes
`0x10..0x17` are pickups/decoration, not walls (L1: `0x12` = makkara at
(26,15), `0x05` = stiletti at (51,18)). The port's `IsWall` = outside the
visible range or `0x63`.

### 8.3 Entities vs. tiles
* **Everything is a tile.** Every creature (police `C`, mummo `M`, Kake `K`,
  pickpocket `V`, …, dog `D`, dot-eater `B`, even the invisible ghost) is a
  **word in the map array**. There is no separate entity list — the "entity"
  is the tile code at a cell, plus a **position-table entry** (`table[tile]`,
  §8.7) and a per-creature **flag** that gates its per-turn AI block.
* **Movement is not a property of the tile; it is a per-turn code block.** The
  NPC-update routine (file `0x9EC4`, called once per player turn) runs a chain
  of ~18 flag-gated blocks, one per creature. Most blocks are *chasers*
  (invisible table drifts toward the player; the visible glyph is re-pinned to
  a fixed anchor cell every turn → the creature looks **static**). Two blocks
  produce **visible** motion: the dot-eater `B` (its own block **teleports B to
  a random cell every turn**, §8.7.4) and the **police** (their block, gated by
  the activation flag `[0x2D0]`, **walks every officer one cell toward the
  player each turn** and draws them at the moving table position, §8.7.6/§10.3).
  The police are **asleep** (static) until the player commits a crime —
  attacking the old lady `M` (when knocked down), a cop, the neekeri `N`, the
  hintti `H`, or the priest `R` — which sets `[0x2D0]` and wakes them (§8.7.6).
* **Player-vs-creature** interaction happens when the player's move target
  cell holds a creature tile: the turn-processor `case` (file `0x6F91`..)
  dispatches on the tile code (dog → fight, `B` → milk/kill, police →
  whack, …).
* The **exit (`>`)** and **hint (`¿`/`¨`)** are special: the exit is a fixed
  permanent tile checked directly (never deleted by player movement); the hint
  is a two-page pager (§11).

### 8.4 Map storage (v1.17)
* Array at **file `0x146`**, **2-byte (word) cells**, indexed
  `index = x*40 + y*2`: **80 x-major rows** (x = 0..79, stride 40 bytes)
  × **20 y sub-rows** (y = 0..19, 2 bytes each). Confirmed against the
  live memory dump (180-cell wall template below; the port's level 1
  now diffs clean against the dump cell-for-cell).
* **Screen ↔ physical mapping.** The visible map is screen (x, y) with
  x = 14..79, y = 3..22 (1-based). For y = 3..19, screen (x, y) is the
  physical cell of the same (x, y). The bottom three visible rows alias
  upward: screen (x, 20..22) reads/writes physical **(x+1, 0..2)**.
  Screen (79, 20..22) would address physical (80, 0..2) — past the end
  of the array in the original as well; it is floor in both.
* Clearing/redrawing a cell = writing its word; the player is placed by
  writing `0x6B` and drawing `@` (the port tracks the player separately
  as PX/PY and leaves the cell floor).
* **Wall template — identical on every level, 180 cells, verified
  cell-for-cell against the live dump (screen coords):**

      y = 3..5:   x = 20, 34..55
      y = 6:      x = 14..20, 34..55
      y = 19:     x = 14..20, 43..51, 54..62
      y = 20..22: x = 20, 43..51, 54..62

  i.e. in physical coords the alias rows are y = 0..2: x = 21, 44..52,
  55..63. Rows 3..6 are the two top buildings (left = ISKU/ALCO: right
  edge col 20 + roof row 6; middle = SAIRAALA/HOTELLI: body cols 34..55
  rows 3..6); row 19 the three bottom roofs; rows 20..22 the bottom
  building bodies (solid). The row directly BELOW the top buildings
  (y = 7) is open — that is the approach row to ISKU and SAIRAALA.
* **Off-by-one fix (2026-11-02).** An earlier port revision indexed the
  physical array with a 1-based offset (`Map[x-1, y-1]`) while the wall
  template was written with mixed conventions; net effect: every wall
  rendered one screen row lower (the y = 7 approach row became wall,
  the y = 6 roof row became walkable, and a ghost wall fragment leaked
  into row 3 from the y = 20..22 alias), which made ISKU/SAIRAALA
  unreachable. Fixed by adopting the (x, y) / (x+1, 0..2) convention
  above in `MapTile`, `SetCell` and a rewritten `BuildWalls`; verified
  by diffing the port's L1 (`--dumpmap`) against the original's live
  dump — 1320/1320 cells match (only the player's own `0x6B` cell
  differs, as the port does not store the player in the map).

### 8.5 Movement & collision
* 8-way. The player attempts to step onto the target cell; the turn
  processor (file `0x5652`) splits on the target code:
  * **code ≤ `0x1D`** → the cell is **walkable** (floor or a
    pickup/control tile):
    * **empty / floor decoration** → move.
    * **dot `0x14` `.`** → collected ("Sokea kana löysi jyvän!"); the
      "dots left" counter decrements.
    * **haalarit `0x03` `Σ`** → equipped ("Löydät haalarit.", inventory #3).
    * **pamppu `0x04` `!`** → weapon slot 4 (weapon level 2; +1 with Camel).
    * **beer `0x01` `%` / lonkka `0x02` `&`** → picked up into inventory
      (drink later with `q`).
    * **exit `0x15` `>`** → walk onto it; descend with `>` (§9).
  * **code > `0x1D`** → **bump handler** (file `0x5F7B`), a per-tile case
    switch: police `0x1E..0x22` (attacking is a crime; combat §10), mummo
    `0x23` (knocking her down is a crime), Kake `0x24` (beer prompt §13),
    varas `0x25`, teekkari `0x26`, portinvartija `0x27`, …, wall `0x63`
    → the **building-door handler** (§8.1, file `0x7BC9..0x7D0B`): a known
    door cell prints its building line (SAIRAALA "Emme ota juoppoja!", etc.)
    with **no** Voima change; any other wall is the generic head-bump
    ("Isket päätäsi seinään, mielenkiintoista." + Voima drain).
* A wall/edge bump consumes **no turn** (the dispatcher returns the `0x69`
  "no turn consumed" sentinel and the main loop skips the turn advance);
  combat and pickup interactions do consume a turn.

### 8.6 Player movement — reverse-engineered (v1.17, VA `0x5573`)
The turn-processor body was recovered with a control-flow-correct `ndisasm`
pass (the linear `capstone` dump derails at the prologue `jmp`). Confirmed:
* **Prologue** clears the player's **old** cell: `Map[PX,PY] := 0`
  (`PX`=`[0x2ee]`, `PY`=`[0x2f0]`; cell = `base + 0x146 + X*40 + Y*2`).
* **Direction** is decoded from the key byte `[0x31a]` (lower-case), grouped:
  * `dx = -1` if key ∈ {`h`,`b`,`y`}; `dx = +1` if key ∈ {`u`,`l`,`n`}; else 0.
  * `dy = -1` if key ∈ {`y`,`u`,`k`}; `dy = +1` if key ∈ {`b`,`n`,`j`}; else 0.
  (So `y`=UL, `u`=UR, `k`=U, `h`=L, `l`=R, `b`=DL, `n`=DR, `j`=D; `.`/`i`=wait.)
* **Drunk-stumble variant:** a gate (`call 0x1a76`) selects, instead of the
  key, a random step `dx=Random(3)-1`, `dy=Random(3)-1` (each −1..1).
* **Boundary clamp:** the *destination* is clamped to X∈[13,79], Y∈[3,22]; an
  out-of-range delta is zeroed (the step is cancelled, not clamped).
* The main input loop (file `0x9291`) **pre-checks** each direction against the
  map edge; at an edge the key is silently turned into a wait (`i`) and the
  turn-processor is skipped.

### 8.7 Creature movement — reverse-engineering + ground truth

#### 8.7.1 The RNG is deterministic with a fixed baked-in seed
*(supersedes the earlier "time-seeded" hypothesis)*

* 32-bit state at **file `0x1FC`** (high word, initial `0x0272`) /
  **file `0x1FE`** (low word, initial `0x03B0`). The seed is the file bytes
  themselves (the region overlaps dead code bytes `72 02 b0 03`); it is never
  re-initialised.
* Update routine **file `0x0FE6`**: byte-rotate the 4 seed bytes, `rcr 1`
  through them, add the state to itself, add constant `0x351961E9`, write the
  state back. Returns the low word.
* `Random(n)` is **file `0x0FDA`**: calls the update, `shr 1`, `cwd`,
  `div n`, returns the remainder. **178** call sites in the binary.
* **No clock reads anywhere.** The only `INT 21h` in the whole binary is the
  program-exit at file `0x86C`. There is no `INT 21h AH=2Ch/2Ah` (DOS time),
  no `INT 1Ah` (BIOS RTC), and no Turbo-Pascal `Randomize`-style re-seed.
  ⇒ The entire run is a pure function of the seed (constant) + the sequence
  of `Random` draws, i.e. of the **exact key/action sequence**.
* Consequence: **L1 is byte-identical every run** (fixed number of draws
  before L1 generation). **L2+ = f(RNG state at descent)** = f(the exact
  L1 key/action sequence, including dot-detours that change the draw count).
  Two runs with the *same* route reproduce the *same* L2; a route that
  collects a different dot set lands on a different RNG state → a different
  L2. (This is why an earlier "same route, different L2" was seen: the routes
  differed by one dot detour.)

#### 8.7.2 The position table and the per-turn NPC blocks
* **Position table** at **file `0xD18`** — **6-byte entries**: `flag at +0`,
  `X at +2`, `Y at +4`. The **table index is the tile code** (`table[tile]`),
  so the dot-eater's position is `table[0x2D]`: X at `0xD18+0x2D*6+2 = 0xE28`,
  Y at `0xE2A`. The per-tile `flag` (+0, e.g. `0xE26` for `0x2D`) is a
  "present on this map" marker, **distinct** from the creature's global AI flag
  (`[0x2D1..0x2DF]`). (Earlier note of base `0xD1A`, X+0/Y+2/flag+4 gave the
  right X/Y addresses but the wrong flag offset — the police block reads the
  per-tile flag at `0xD18+tile*6`.)
* **NPC-update** entry **file `0x9EC4`**, called **once per player turn** from
  the main loop (file `0xE3D9`). It runs a **chain of ~18 flag-gated blocks**
  (header pattern `mov al,[0x2Dx]; xor ah,ah; or ax,ax; jnz …`), one per
  creature, in a fixed order. A block runs only if its creature's flag
  (`[0x2D1..0x2DF]`, plus ghost `[0x2DD]`, punk `[0x2E3]`) is set.
* **Blocked move = no turn.** The key dispatcher (file `0x9291`) sets the key
  to `0x69` on an edge-bump; the main loop (`0xE3CB: cmp ax,0x69`) then
  **skips** the NPC update. So a wall-bump consumes no turn and no creature
  (including `B`) moves.

#### 8.7.3 The "cache-pinned" chaser model (why most creatures are STATIC)
Each **pinned** chaser block (e.g. pickpocket `0x25` @ `0xB0A0`, ghost `0x4B` @
`0xA377`, …) has the same shape, per turn. (The police and the dog are NOT
pinned — they draw at their moving table position and visibly walk, §8.7.6/§10.1.)
1. `StepTowardPlayer(tile)` (file `0x9ECB`): stashes the old table X/Y in the
   block frame, **clears the old table cell**, steps `table[tile]` one cell
   toward the player, then finds a **random empty cell** (`X=Random(60)+20`,
   `Y=Random(20)+3`, retry if occupied) into the frame. This "chase" position
   is **internal/invisible** — it is never the cell that gets drawn.
2. Reads **`Map[anchor]`**, where `anchor` is the creature's **fixed cache
   cell** (a per-creature global, e.g. pickpocket `[0xDF8]/[0xDFA]`,
   ghost `[0xEDC]/[0xEDE]`, set at level-init/spawn).
3. `case Map[anchor]`: `0x14` dot → relocate the dot to the random cell;
   `0x15` exit → retire the creature; `0x2D` B → **teleport B** to the random
   cell + update B's position; `0x23/0x24/…` other creatures → messages /
   kills; default → a small vertical-centre drift of the invisible table.
4. **Draw section (always):** re-writes the creature's **own tile at the
   anchor cell** (`Map[anchor] := tile`) and prints the glyph there.

Because step 4 re-pins the glyph to the anchor **every turn**, the creature's
visible position is **frozen** at the anchor forever; the invisible table chase
is a no-op visually. The `case` therefore only "sees" something other than the
creature's own tile if another entity (a dot, `B`, the exit) happens to sit on
the anchor cell — a rare coincidence. **Net: chasers look and act static.**

* The **police are NOT pinned** (and the earlier "police block is dead code"
  verdict was wrong — it mis-traced the loop header `sub cx,ax / jnl`; the
  `xchg` means the loop runs 5 times over tiles `0x1E..0x22`). Their block
  (file `0xA7EC..0xB09F`) is **live**, gated by the activation flag `[0x2D0]`,
  and it **visibly chases** the player. See §8.7.6.

#### 8.7.4 The dot-eater `B` has its OWN mover (teleport, not walk)
B's block (file **`0xC943..0xCA4B`**, gated by flag **`[0x2D6]`**) is
fundamentally different from the chasers — it does **not** use
`StepTowardPlayer` and does **not** pin to an anchor. Each turn it:
1. Picks a random cell `X = Random(58)+22` (∈ 22..79),
   `Y = Random(20)+3` (∈ 3..22), **retrying until the cell is empty (`0`) or a
   dot (`0x14`)**.
2. If the cell holds a dot → **B eats it**: the dot is consumed and
   **`life [0xE26]` increases** (B "banks" the dot).
3. **Teleports B there**: erases B's old cell, sets
   **B's position `table[0x2D]` = `[0xE28]/[0xE2A]`** to the new cell,
   writes tile `0x2D` there, draws `B`.

⇒ **B jumps to a new random cell on every player turn.** This is the single
mechanism that makes B "the only thing moving" (besides the player). B's
`life` starts at **0** (set at generation, file `0x4875`), so a freshly
spawned B is a one-hit-kill until it has banked dots by eating them.

* B globals: flag **`[0x2D6]`**, life **`[0xE26]`**, position
  **`[0xE28]/[0xE2A]`** (= `table[0x2D]`).
* Secondary (rare) relocation: a chaser's `case 0x2D` (10 sites, e.g.
  `0xA6A7,0xACFA,0xB2A8,0xB54F,0xC5C5,0xCA21,0xCD6F,0xD218,0xD684,0xDB57`)
  also teleports B when B happens to sit on that chaser's anchor cell — but
  this is a coincidence-trigger, not B's primary motion.
* **Player steps on B** (file `0x6F99..0x7098`): `life>0` → spawn a DOT at a
  random empty cell (`X=Random(50)+25`, `Y=Random(20)+3`), `life--`, no
  message; `life==0` → "Tuhoat pistepirkon.", flag `[0x2D6]` off, B erased at
  `[0xE28]/[0xE2A]`.

#### 8.7.5 Level generators
* L1: fixed (byte-identical every run — see §8.7.1).
* L2+: random walls/dots/creatures; creature **spawn flags** and positions come
  from `Random` draws, so they depend on the RNG state at descent.

  Both dynamic creatures are **roll-to-spawn** (not guaranteed), each gated by
  a level test AND-ed with a `Random` draw (the `Random` is always consumed):
  * **Dot-eater `B`** (file `0x483F..0x487B`): spawns when
    `(Level > 1) AND (Random(100) > 77)` → **level ≥ 2, ~22 %** (values
    78..99 of 0..99). Placed by the placement helper (file `0x2CF3`, called
    from `0x486F`) with `life := 0` and flag `[0x2D6] := 1`.
  * **Dog `D`** (file `0x4A11..0x4A6D`): spawns when
    `(Level > 2) AND (Random(5) = 3)` → **level ≥ 3, ~20 %**. So there is
    **never** a dog on level 1 or 2. Placed with HP in `[0xE2C]` and flag
    `[0x2D7] := 1`; its table slot is `0xD18 + 0x2E*6` (HP `0xE2C`, X `0xE2E`,
    Y `0xE30`).
  * `Random(n)` (file `0xFDA`) = `(LCG_state >> 1) mod n` → returns `0..n-1`
    (same as Pascal), so the percentages above are exact.
   * **Random-placement rectangle** (placement helper file `0x2CF3`, body
     `0x2CFD..0x2D12`): EVERY random tile/creature placement (all 31 call
     sites `0x4491..0x4BF7`: dots `0x14`, beer `0x0C`, woman, the police
     types, `B` `0x2D`, dog `0x2E`, …) first draws
     **`X = 23 + Random(56)` → 23..78, `Y = 7 + Random(12)` → 7..18**, then
     retries until the cell is empty (`== 0`). The rectangle sits entirely in
     the open street: it excludes every building AND the two **sealed hollow
     pockets** the wall template creates — the ISKU interior
     (`x=14..19, y=3..5`) and the bottom-left building interior
     (`x=14..19, y=20..22`), each enclosed by the map border + building walls
     and unreachable from the player start. A dot generated in a pocket can
     never be collected → "Ensin pisteet …" soft-lock, so the original simply
     never rolls those cells. Cross-check: all hand-placed L1 entities
     `(52,10) (59,10) (77,11) (65,13) (61,14) (64,14) (26,15) (46,18) (51,18)`
     fall inside the rectangle. The port replicates the rectangle exactly
     (`PLACE_X0..PLACE_Y1` in `spurgux117.pas`, used by `PlaceRandomFloor`);
     the other dot paths keep their own faithful ranges (B-step spawn
     `X=25..74`, cop "ate dot" relocation `X=20..79` — both `X ≥ 20`, so they
     also miss the pockets; B's teleport `X=22..79` likewise).

#### 8.7.6 The police — a gated VISIBLE chaser (why the cops chase after a crime)
The police block (file **`0xA7EC..0xB09F`**) is the one chaser that draws at
its **moving table position** rather than a pinned anchor, so the officers
**walk**. It is gated by the activation flag **`[0x2D0]`**:
* **`[0x2D0]` = 0 → the whole block is skipped** (`A7EC: mov al,[0x2D0]; …
  jnz A7F8 / jmp B09F`). The police are asleep (static tiles).
* **`[0x2D0]` is cleared to 0 at level-init** (file `0x425F`), so every level
  starts with the police asleep.
* **`[0x2D0]` is set to 1 by "crime" handlers** — i.e. when the player
  attacks/harms certain creatures. There are exactly **8 write sites**, in
  **two** creature case-chains. The complete set of crimes (creatures whose
  attack alerts the police) is five: **the police `C`, the old lady `M`, the
  neekeri `N` (0x28), the hintti `H` (0x29), and the pappi/priest `R` (0x2B)**.

  **Chain 1 — the turn processor** (the main "player steps onto tile" path,
  file `0x54D9..`; full combat with health pools):
  * **Police `C` (0x1E..0x22)** @ `0x5F8C` — "Huitaiset skoudea!" (You throw a
    fist!). A cop has a **health pool** (`[0xD18+T*6]`); each hit does
    `1+[0x2F6]` (weapon level) + 1 damage. When a cop's health drops below 1 it
    is defeated: "Hän väsähtää!", its cell becomes tile `0x04` (a corpse), and
    the player takes a hit. `[0x2D0]` is set on **every** hit, even if the cop
    survives.
  * **Old lady `M` (0x23)** @ `0x63BF` — **only when M is knocked down** (her
    health `[0xDEA]` drops below `1+[0x2F6]`): "Isket mummon kumoon!" and her
    cell becomes tile `0x0C`. If M merely survives the hit she whacks you back
    ("Eläkeläinen lyö sinua käsilaukullaan!", 1 dmg) and does **not** alert the
    police.
  * **Neekeri `N` (0x28)** @ `0x679A` — **unconditional**. Message depends on
    the global `[0x2CE]` flag: set → "Tapaat vanhan isäsi!"; clear → "Hakkaat
    neekeriä." There is a **1/4 chance** the neekeri dies ("Hän taintuu.", its
    flag `[0x2DA]` cleared, cell → tile `0x0C`).
  * **Hintti `H` (0x29)** @ `0x69B6` — **only when `[0x2CE]` is clear**:
    "Hakkaat homoa, joka luhistuu kadulle." (When `[0x2CE]` is set it instead
    prints "Tapaat mukavan pojan!" and does **not** alert the police.)
  * **Pappi/priest `R` (0x2B)** @ `0x6E9A` — **unconditional**: "Lyöt pappia
    mutta Jeesus suojelee häntä!" (the priest is protected and survives).

  **Chain 2 — a separate creature-interaction routine** (file `0x87CD..0x9150`;
  takes `(X,Y)` + `(dx,dy)` in its frame; common exit `jmp 0x923E`). It handles
  a subset of the same creatures with **simpler, immediate** effects (no health
  pools):
  * **`M` (0x23)** @ `0x8816` — "Kolkkaat mummelin brutaalisti." (M's cell →
    `0x0C` immediately).
  * **Priest `R` (0x2B)** @ `0x8C91` — "Osut pappismieheen."
  * **Police `C` (0x1E..0x22)** @ `0x8F44` — "Heität skoudea!"

  ⇒ The five crime types are **police, M, neekeri, hintti, priest**. `[0x2CE]`
  is a global flag (the "friendly" variant) that changes the message flavour
  and gates the hintti's police activation. **Port:** sets `PoliceActive` on
  the `M` knockdown and on attacking a cop (the two crimes its level builders
  can produce); the neekeri/hintti/priest are not generated, so those crimes
  are not reachable there.

When active, the block **loops over the five police tiles `0x1E..0x22`**
(ascending, counter 5). For each officer whose per-tile flag
(`table[i].flag` = `0xD18+i*6`) is `> 0`:
1. **Adjacent?** if `|offX−PX|<2 and |offY−PY|<2` → **ATTACK**: print
   "Poliisi pamputtaa!" and deal **`1+Random(3)`** Voima damage (a rare
   `[0x2CE]` "friendly" variant instead prints "Ystävällinen herra ojentaa
   sinulle kukkasen!"). **No movement** this turn; next officer.
2. **Else CHASE:** call `StepTowardPlayer(i)` (`0x9ECB`) — clears the officer's
   old cell and steps `table[i]` **one cell toward the player on each axis**
   (a diagonal step when offset on both axes; **no wall check**), stashing the
   old position and a random empty cell in the frame. Then it reads
   **`Map[table[i]]`** (the officer's *new* cell) and `case`s on it:
   * `0x14` dot → the dot is **relocated to the random empty cell** (the
     officer "eats" it; the dot count is unchanged);
   * `0x15` exit → the officer **teleports to the random empty cell** (the
     stairs cell is overwritten with tile `0x1D+i`);
   * `0x23` M → "Poliisit vievät eukon turvaan." (the M's cell becomes the
     officer's);
   * `0x24/0x25/0x27/0x29/0x2A/0x2C/0x2F/0x36` → the corresponding creature's
     flag is cleared + an "arrested/removed" message;
   * `0x2D` B → **B is teleported** to the random empty cell;
   * `0x2E` dog → the dog's flag is cleared ("Virkavalta poistaa vesikauhuisen
     hurtun.");
   * `0x26` T → the officer **swaps places** with the T (`[0xDFE]/[0xE00]`);
   * `0..0x13` (floor/terrain) → nothing (the officer simply stands on it);
   * other `>0x13` (wall/building/another officer) → **push-off**: move the
     officer to an adjacent empty cell (right/left if it shares the player's
     column, else down/up), reverting to its old cell if blocked.
3. **Draw (always):** write the officer's tile `i` to `Map[table[i]]` and print
   `C` there — at the **new (moved)** cell, so the officer visibly advances.

⇒ **Once `[0x2D0]` is set, every officer walks one cell toward the player each
turn and beats the player when adjacent.** This is the "police start when the
player attacks M" behaviour.

* The port models this in `UpdatePolice` (called each acted turn): it collects
  the `C` tiles and, for each — if adjacent, attack
  (`DamagePlayer(1+Random(3))`); else step one cell/axis toward the player and
  handle the landed tile (dot relocated, exit → teleport, M taken, floor →
  move, wall/building → push-off).

---

## 9. Entity legend (from `SPU.HLP`, both versions)

Object glyphs are CP437. `ÿ`-style diacritics are the in-game font.

> **Note:** this is the game's *own* help text (`SPU.HLP`). Several of its
> glyph↔meaning pairs do **not** match what the binary actually draws — the
> **map renderer** (§8.2) is authoritative (e.g. the HLP's `# = Sähköaita`,
> `% = Kalja`, `0x30..0x36 = buildings 0–6` are wrong; the real creature
> glyphs are `M K V T N H Q R I B D G O S A P J E`, etc.). Use §8.2 for what
> is on screen; use this section only for the Finnish entity names.

### v1.17
```
& = Lonkka (cheap liquor)     @ = Pelaaja (player)      C = Lainvartija (police)
% = Kalja (beer)              # = Sähköaita (electric fence)  M = Vanhahko nainen (older woman)
è = Konjakki (cognac — goal)  █(0xDB) = Seinä (wall)    N = Neekeri (slur)
( = Makkara (sausage)         ñ = Hauta (grave)         T = Teekkari (techie)
! = Pamppu (pad/rag)          ä = Haalarit (overalls)   P = Punkkari (punk) [¥ = Punkkingi (punk king)]
\ = Stiletti (stiletto)       ÷ = Vesialue (water)      K = Kake
á = Ketjut (chains)            = Kökköä                V = Taskuvaras (pickpocket)
$ = Rahaa (money, mk)         E = Vankikarkuri (prison guard)  D = Koira (DOG)
(0x7F) = Maito (milk)          (space 0x20) = Aave (ghost, invisible)  B = Pisteennielijä (DOT-EATER)
o = Omena (apple)             ê = Portsari (doorman)    H = Hintti (character)
ì = Suklaata (chocolate)      G = Gorilla               O = Skinhead
¥(0x9D) = Silakka (gull)      Q = Trokari               S = Käärme (snake)
é = Camel-Boots               I = Hippi (hippie)        > = Portti (exit to next level)
¨ = Vihje (hint)              A = Aidsinkantaja (AIDS carrier)  ^ = Metroasema (metro station)
. = Piste (dot)               J = Juppi (yuppie)
```

### v2.24 (adds; removes Maito/Silakka)
New entities:
```
± = Ovi (door)        R = Pappi (priest)        L = Sosiaaliavustaja (social worker)
h = Lumppu-Leena       X = Poliitikko (politician)  Y = Mustalainen (slur)
Z = Alokas (rookie)    U = Juntti (jerk)
```
(2.24 also relocates a few legend entries; the full 2.24 legend is in
`hlp224.txt`.)

**Notes for the re-implementation:**
* The character roster is deliberately offensive (racial slurs, an AIDS
  "carrier", etc.) — it is part of the 1987 satire. A clean-room port must
  decide how faithfully to reproduce it.
* The **dog** is glyph `D`; the **dot-eater** is glyph `B`. (Do not
  conflate them.) The dog has **names** — observed **Piski** and **Hauveli**
  (Finnish dog names/slang). The dog-eating/dot-eating distinction matters
  for gameplay: the dog is an **aggressive combat** entity; the dot-eater
  **wanders and eats dots** (killing it removes a threat to level completion:
  "Tuhoat pistepirkon." = "You destroy the dot-thief.").
* `Aave` (ghost) is drawn as a **blank/space** — an invisible entity.

---

## 10. Combat

Combat is **player-initiated by walking into** an entity. Outcomes observed
live (v1.17, level 3):

### 10.1 Dogs (`D`, Koira) — named (Piski, Hauveli, …)
* Walking into a dog: **"Hakkaat koiraa."** ("You beat up the dog.") and the
  dog **bites back**: **"… Piski puraisee sinua!"** ("… Piski bites you!").
* Each exchange costs the player **Voima** (observed bite damage 1–4, e.g.
  15→11→7→3→1→0 across five hits).
* The dog has limited "health": after ~5 player hits it is defeated and
  replaced/retires (a second dog, **Hauveli**, then appears, initially
  non-aggressive: "Hauveli uinahtaa." = "Hauveli snores.").
* Defeating a dog grants **+1 Kokemus** (observed 1→2).
* **Dog AI:** the dog is a **visible chaser** — the same shape as the police
  block (file `0xCA4B..0xCEA1`, gated by flag `[0x2D7]`, active from the level
  it spawns). Each turn:
  * **adjacent to the player** (`|dx|<2 and |dy|<2`) → **attack**:
    "Piski puraisee sinua!" + bite damage (1–4); no movement.
  * **else → chase**: `StepTowardPlayer(0x2E)` moves the dog one cell toward
    the player on each axis; the dog is drawn at that new (moving) table cell,
    so it **visibly walks toward the player**.
  * If the dog steps on another creature it "infects" it with blood poisoning
    (clears that creature's flag + "…saa verenmyrkytyksen."), **teleports `B`**,
    **retires at the exit** (`[0x2D7]` off), and **dies on a ghost**
    ("Haamut tappavat rakin.").
* *(Correction: an earlier note claimed the dog was a "cache-pinned chaser"
  with a frozen glyph — that was the same loop-header mis-trace as the police.
  The dog's block reads `Map[table[0x2E]]` and draws the glyph there, so it is a
  **visible chaser** like the police; the port's visibly-walking dog was
  correct.)*
* **Port:** `UpdateEntities` walks the dog one cell/axis toward the player and,
  when adjacent, bites (`DamagePlayer(RandRange(1,4))` + "Piski puraisee
  sinua!").

### 10.2 Dot-eaters (`B`, Pisteennielijä / pistepirko)
* **B visibly moves, and it does not walk — it teleports.** (The other visible
  mover is the police, which *walks* — §8.7.6/§10.3.) B has its own per-turn AI
  block (file `0xC943..0xCA4B`, gated by flag `[0x2D6]`), distinct from the
  cache-pinned chasers (§8.7.4). Every
  player turn B:
  1. picks a random cell `X=Random(58)+22` (22..79), `Y=Random(20)+3` (3..22),
     **retrying until the cell is empty or a dot**;
  2. if the cell holds a **dot**, B **eats it** — the dot is consumed and B's
     `life` (`[0xE26]`) **increases by 1** (B "banks" the dot);
  3. **teleports** to that cell (position `table[0x2D]` = `[0xE28]/[0xE2A]`).
* B spawns with **`life = 0`** (file `0x4875`), so a fresh B is a
  one-hit-kill until it has banked dots.
* **Stepping on B** (file `0x6F99..0x7098`):
  * `life > 0` → a **DOT is spawned** at a random empty cell
    (`X=Random(50)+25`, `Y=Random(20)+3`), `life--`, **no message** (the player
    "milks" one banked dot back);
  * `life == 0` → **"Tuhoat pistepirkon."** ("You destroy the dot-thief."),
    flag `[0x2D6]` cleared, B erased at its position. No counter-damage.
* **Threat:** it eats dots it lands on (reducing the collectable set) and can
  re-spawn them when milked — the dot count on a level is dynamic, not fixed.
* The port models exactly this: `UpdateEntities` teleports B to a random
  empty-or-dot cell each turn (eating dots → `HP`/life++), and the step-on
  handler spawns a dot + `life--` (or kills at `life==0`).

### 10.3 Police (`C`, Lainvartija)
* **The police are gated chasers — they wake and chase once the player
  commits a crime.** Their per-turn block (file `0xA7EC..0xB09F`, §8.7.6) is
  controlled by the activation flag **`[0x2D0]`**, which is **cleared at
  level-init** (`0x425F`) and **set by criminal acts**. The crimes (creatures
  whose attack sets `[0x2D0]`) are: **the police `C`**, **the old lady `M`**
  (only when she is knocked down), **the neekeri `N`**, **the hintti `H`**
  (only when `[0x2CE]` is clear), and **the priest `R`** — full detail in
  §8.7.6. Until then the police are **asleep (static tiles)**.
* **Once awake, every officer (`0x1E..0x22`) acts each turn:**
  * **adjacent to the player** (`|dx|<2 and |dy|<2`) → **attack**:
    "Poliisi pamputtaa!" + **`1+Random(3)` Voima** damage; no movement.
  * **else → chase**: `StepTowardPlayer` moves the officer **one cell toward
    the player on each axis** (no wall check); the officer is drawn at that
    new cell, so it **visibly walks toward the player**.
  * What the officer lands on is then handled: a **dot** is relocated to a
    random empty cell; the **exit** makes the officer teleport away; **M** is
    "taken to safety" ("Poliisit vievät eukon turvaan."); a **creature** is
    arrested/removed (its flag cleared + message); a **wall/building** pushes
    the officer to an adjacent empty cell (or reverts).
* **The player can also step onto an officer** (the player's turn-processor
  `case`): this is itself a crime — `[0x2D0]` is set. An officer has a
  **health pool** (reduced by `1+[0x2F6]` + 1 per hit); a defeated officer
  becomes a corpse (tile `0x04`) with "Hän väsähtää!" and the player takes a
  hit ("Huitaiset skoudea!" style).
* **Port:** `UpdatePolice` (called each acted turn) implements exactly this —
  asleep until `PoliceActive` is set (by attacking `M` **or** a cop), then
  each `C` tile either attacks when adjacent or walks one cell/axis toward the
  player and handles the tile it landed on. (The port simplifies the officer's
  health pool to a one-hit removal.)

### 10.4 General
* Hitting a **wall** also "counts" as aggression and depletes Voima.
* Punks and others may **flee** when attacked ("Punkkarit luikkivat
  pakoon." = "The punks slink away." — procedure at file `0x5195`).

**Damage/health constants for non-dog entities are not yet fully extracted
(open item).**

---

## 11. Hint system (`¿` / `¨` — Vihje)

* Stepping on a **hint tile** (`¿`) costs **1 turn** and opens a **two-page
  pager**:
  * Page 1: "Ruumiisi alkaa vavahdella ja kuulet taivaallisen äänen!"
    ("Your body begins to tremble and you hear a heavenly voice!") +
    `<lisää>` (more) — **any key** advances.
  * Page 2: "Se sanoo: <TIP from SPU.TIP>" ("It says: <tip>") — the tip text
    is read from **`SPU.TIP`** (procedure at file `0x5086`).
* While the pager is open, the map/player is not advanced and the turn does
  not progress until it closes.

---

## 12. Death & high scores

* **Cause line** (e.g. "Voimasi ehtyvät, lyhistyt maahan …" for exhaustion;
  combat deaths have their own lines) is shown, then a **`<lisää>` pager**
  (any key).
* Then the **high-score table** "20 PARASTA SPURGUA" (20 best Spurgus) is
  shown and the player's run is inserted into **`SPU.TOP`** (20 records × 28
  bytes).
* The game then exits to the DOS prompt.
* **`SPU.TOP` must exist** or the death sequence aborts (a missing file was a
  crash trigger in early testing).
* **Suicide** (`Q`): "Todella luovutat (k/e)?" ("Do you really give up?") —
  answering `k` ends the run.

### 12.1 `SPU.TOP` record layout (byte-exact, 28 bytes/record)

| offset | field | notes |
|---|---|---|
| 0 | name length | 0..9 (field capped at 9 chars; byte 10 is the key) |
| 1..9 | name | the typed name, no case change |
| 10 | **key** | score base (see 12.2); also part of the TEIT SEN keystr |
| 11..14 | (0) | |
| 15 | **b0** | score fraction; for v1.17 deaths = `64*(Level-1)` byte-wrapped |
| 16..17 | **level** | little-endian word |
| 18..27 | (0) | |

### 12.2 Score formula (cracked, byte-exact — 24 data points)

The displayed score is **not stored**; it is computed from the record bytes:

```
key = r[10];  b0 = r[15]
key <  0x81            -> 0                              (empty slot)
0x81 <= key <  0x88    -> 2^(key-0x81) + floor(b0/2^(0x88-key))
key =  0x88            -> 128 + b0
key >  0x88            -> unobserved (port caps at High(Integer) div 4)
```

i.e. `score = 2^(key-0x81) + b0 · 2^(key-0x88)` (the fractional `b0` term
right-shifts below key 0x88). Verified against **all 20 shipped records**
(134, 97, 88, 77, 52, 48, 47, 45, 43, 42×5, 41×2, 40×4) and 3 fresh live
deaths. A v1.17 **death record** uses `key = 0x82` (constant, *not*
level-based) and `b0 = 64*(Level-1)` (byte-wrapped: level 5 → 256 → 0), so
`score = 2 + (Level-1) = Level + 1`. Older shipped records carry b0 values
(6, 26, 44..80, non-multiples of 4) from earlier-version semantics; the
formula renders them correctly.

### 12.3 Insertion & display algorithm (verified live)

```
N   = records loaded from SPU.TOP (0..20)
pos = 1
while (pos <= 20) and (Score(New) <= Score(Slots[pos])) do pos++
        ^ ties sink *below* the existing record
if pos <= 20:                      // made the top 20
    upper = min(N+1, 20)           // full table drops its 20th record
    shift Slots[upper..pos+1] down one slot
    Slots[pos] := New
    count = upper                  // = min(N+1, 20)
else:                              // overflow: missed the top 20
    count = 20
SaveTop(Slots, count)              // file becomes count*28 bytes
```

* **Rows shown / records saved = `min(N+1, 20)`** (N = records *loaded*),
  **not** the insertion position. Proven by the mid-insertion test: a full
  table (10×32 + 10×1) with a new score 2 landing at pos = 11 still shows
  **20 rows**, writes a **560-byte** file, ranks the newcomer at 11, and
  leaves row 23 **blank**.
* **Row 23** (the death record, unranked) is shown **only on overflow**
  (`pos > 20`); otherwise it stays blank.
* **`*** TEIT SEN ! ***`** ("it's you!") is printed at **col 55** on the row
  whose record matches the new one (5-byte name-eq **and** 6-byte
  keystr-eq, `r[10..15]`); no `inserted` guard (faithful to the original).
  On overflow it never prints (the new record is not in the table).
* **Screen layout**: row 1 col 30 banner `20 PARASTA SPURGUA :`; rank *i* on
  row *i*+2 — col 2 `i.`, col 7 score right-justified width 6 (ends col 12),
  col 20 `<name> menehtyi tasolle <level>` (name is *not* fixed-width);
  row 25 col 30 footer `SpurguX Vs. 1.17, (C) 1987 P.N.`.
* The player presses any key, then the game exits.

---

## 13. k/e (kyllä/ei = yes/no) prompts

Exactly three in v1.17 (all confirmed, two live):
1. **"Todella luovutat (k/e)?"** — suicide confirmation (key `Q`).
2. **"Kamusi Kake pummaa kaljaa, annatko (k/e)?"** — "Your buddy Kake is
   begging for beer, do you give it?" Triggered by **Kake** (`K`). Answering
   `k`: **"Kake kiittää ja katoaa."** ("Kake thanks you and disappears.") —
   Kake is removed. (Answering `e` presumably angers him — **(not yet
   observed)**.)
3. **"Hämärä veikko tarjoaa taikajuomaa pullostaan, 50 mk (k/e)?"** — "A
   shady dude offers a magic potion from his bottle, 50 mk." (Costs Mk.)

Prompt handling: the prompt text is on the message line; once answered the
**result is appended to the same line** (so the line still contains `(k/e)`
— a re-implementation must detect the *answered* state by what follows the
marker, not by the marker's presence).

---

## 14. Drinking / eating & inventory

* **Drinks:** `q` opens the drink/shop menu (procedure file `0x4F1C`,
  inline labels `Kaljaa`/`Lonkaa`/`Kiekuraa`); it is **shared with the
  shopkeeper** ("Tervetuloa, \<name\>, mitä myisit? >"). Counts come from
  mem `0x376` = Kaljaa/beer, `0x378` = Lonkkaa, `0x37A` = Kiekuraa.
* **Consume routine** (file `0x7E50`): reads the selected digit `d`,
  decrements `[0x374 + d*2]`, then updates `Prom`.
  * `1` kalja → **Prom +0.2** (measured)
  * `2` lonkka → **Prom +0.2** (measured)
  * `3` kiekura → no Prom change (food)
* **Food:** `e` eats (makkara, omena, suklaata, maito, …).
* **Inventory:** `i` lists carried items; displayed #n = entry n at
  `0x2FE+n*10` (9-char string; entry 0 unused). Weapon slots are entries
  4–7 (Pamppu/Stiletti/Ketjut/Camel). Throwing via `t` uses a digit `d`
  (→ entry `d+2`, §14.1) or the drink letters `a`=kalja, `b`=lonkka,
  `c`=kiekura; `0` cancels (§14.1).

### 14.1 Throw (`t`) — complete v1.17 spec (RE'd from file `0x81A7..0x9290`)

1. **Item prompt** — "Mitä viskaat >" (file `0x81CF`):
   * accepts a digit `d` (`1..9`), which acts on **entry `d+2`** — the item
     at `0x2FE+(key−0x2E)*10`; since each item's tile code equals its entry
     index, digit `d` throws the item with code `d+2` (displayed #(d+2)) —
     or `a`/`b`/`c` (drinks); any other key is re-prompted silently;
   * `0` = **cancel**: a short delay loop, then exit with key := `0x69`
     (no turn), no message;
   * on a digit: the inventory entry at `0x2FE+(key−0x2E)*10` is cleared
     (9 spaces) and the weapon level `[0x2F6]` is recomputed from the
     remaining weapon slots — entry 6 (`0x33A`) "Ketjut" → 5, entry 5
     "Stiletti" → 3, entry 4 "Pamppu" → 2, +1 while "Camel" is present
     (string compares at file `0x8569..0x85D7`);
   * on a letter: drink count `[0x374+(key−0x60)*2]` −= 1 (i.e.
     `0x376`/`0x378`/`0x37A`);
   * **damage** of the throw (file `0x8306..0x8396`): `1`→1, `2`→2,
     `3`→3, `4`→4, `5`→2, `8`→10, `9`→3, `a`→1, `b`→1, `c`→0;
     `6`/`7` are unmapped (no damage set).
2. **Direction prompt** — "Suunta >" (file `0x83AA`); the chosen key is
   stored in `[0x2AC]`. Accepts `h j k l y u b n` and `.`; other keys are
   re-prompted silently. `.` — or any direction whose target leaves the
   map (X∉[14..79], Y∉[3..22]) — exits with key := `0x69`, message
   "Hassu suunta!", **no turn**.
3. **Flight** — the projectile steps one cell per iteration in the chosen
   direction, drawing `*` (byte `0x2A`) into each **empty** cell it
   crosses; it **stops** at the first cell whose tile code is `> 0x1D`
   (building/creature) or when the step would leave the map.
4. **Landing** (file `0x8725..0x923D`):
   * **empty cell (tile 0):** the thrown item is written into the cell —
     digit `d` → tile `key−0x2E` = **d+2**, which *is* the thrown item's
     own code (it is entry d+2 and tile code = entry index); letter → tile
     `key−0x60` (`a`→1, `b`→2, `c`→3; note `c`=kiekura lands as tile
     `0x03`, the haalarit glyph — an original quirk); the cell is then
     drawn as byte `0x7E` (√) when `[0x2CD]` is set;
   * **M `0x23`:** the cell becomes **`0x0C`** (money; drawn `$`) —
     "Kolkkaat mummelin brutaalisti." — and `[0x2D0]`:=1 (crime: the
     police wake);
   * **K `0x24`:** "Kiusaat Kakea.";
   * **V `0x25`:** "Osut varkaaseen.", HP `[0xDF6]` −= damage
     (down: "Hän tuupertuu ketoon.");
   * **T `0x26`:** with a drink (`a`/`b`) — "Teekkari nappaa pullon
     lennosta ja katoaa.";
   * **portsari `0x27`:** "Osut portsariin.", `[0xE02]` −= damage
     (down: "Hän sammahtaa.");
   * **G `0x2F` / O `0x30` / S `0x31` / A `0x32`:** "Viskaat gorillaa." /
     "Viskaat klania." / "Viskaat käärmettä." / "Osut potilaaseen.";
   * **H `0x29` / Q `0x2A`:** "Hintti väistää." / "Trokari väistää.";
   * **R `0x2B`:** "Osut pappismieheen.", `[0xE1A]` −= damage;
   * **I `0x2C`:** "Hän sammahtaa ja kolehtihaavi aukeaa." (throws of
     entry `3` additionally clear `[0x2DF]` and stamp tile 5 at
     `[0xE22]/[0xE24]`); likewise **Q `0x2A`** with entry `3` clears
     `[0x2DC]` and stamps tile 1 at `[0xE16]/[0xE18]`;
   * **D `0x2E` (dog):** "Viskaat rakkia.", `[0xE2C]` −= damage;
   * **P `0x33`:** "Heittosi osuu punkkariin.";
   * **Ñ `0x34` (kingi):** "Heittosi osuu kingiin.", `[0xE50]` −= damage
     (down → the blackout handler, §17);
   * **N `0x28` / B `0x2D` / J `0x35`:** "Ohi meni!";
   * **E `0x36`:** "Et osunut";
   * **ghost `0x4B` / default (wall, …):** "Heittosi suhahtaa ilmaan!" or
     lost silently;
   * **cops `0x1E..0x22`:** "Heität skoudea!", `[0x2D0]`:=1, cop HP
     `[0xD18+T*6]` −= 1+damage; a downed cop drops `0x04` (corpse) when
     no Pamppu is held, else `Random(10)==9` → `0x0C` (`$`) or vanishes —
     "Poliisi pökertyy!".
5. **Turn accounting** — a completed throw consumes the turn and exits
   with `[0x2F8]` := 0 (file `0x9282`); the cancel and "Hassu suunta!"
   paths set the key to `0x69` so the main loop skips the turn advance.
* Drinking raises **Promille +0.2** per beer/lonkka and (in v2.24) can
  trigger hangover states; eating affects Voima **(inferred)**. High Prom
  then drives the stagger / shop-refusal / blackout / friendly-police
  effects in §4.2.

---

## 15. Re-implementation architecture notes

### 15.1 Suggested modules (clean room)
1. **Core/turn loop** — key read → dispatch → player action → NPC updates →
   resource updates → death check → render → tick.
2. **Map** — 2D grid of tile codes; level generator placing buildings
   (fixed geometry, rotating names), dots (random), exit (fixed bottom-right),
   and spawning entities.
3. **Entities** — tiles (static) vs. dynamic entities (dogs, dot-eaters) with
   simple pursuit/wander AI.
4. **Combat** — resolve-on-contact; per-type damage/health tables.
5. **Stats** — Voima (regen + exhaustion), Promille (drink + stochastic
   decay), Kokemus (+1/kill), Mk.
6. **I/O** — message line, status-line renderer, pager (`<lisää>`), k/e
   prompts.
7. **Data** — external `HLP`/`SPE`/`TIP`/`TOP` equivalents; save file (2.24).
8. **Random** — seeded RNG for placement/outcomes.

### 15.2 Disassembly gotchas (if re-deriving from the binary)
* **Inline strings** use the return-address idiom (§7) — a linear
  disassembler will fall into them. Treat a `call 0x295D` followed by
  `<len><chars>` as a single "write string" unit.
* **INT-3 (`CC`) padding** separates code blocks.
* **TP prologue** (game unit): `push bp; mov bp,sp; push bp; jmp +0`
  (the extra `push bp` + 3-byte no-op `jmp`), present in all 29 game
  procedures but absent in the 2 RTL prologues. A stack check
  `mov cx,N; call 0xFD1` appears in bodies with locals.
* **Globals** are at fixed mem addresses (`0x2xx`/`0x3xx`); the map at file
  `0x146`.
* **String table** is indirect (message-index system) — the code passes
  `ds:di` references (thunk at `0x1973`) rather than `push imm16`.

### 15.3 Key globals (v1.17, memory addresses)
| Addr | Meaning |
|---|---|
| `0x2EE` / `0x2F0` | player X / Y |
| `0x2F4` | current level |
| `0x2F8` | mode flag (0/1/2; gates the alternate key remap) |
| `0x2FC` | Voima (6-byte `real`) |
| `0x302` | **Promille** (6-byte `real`; printed 3 int / 1 dec digit on the status line) |
| `0x31A` | last read key |
| `0x376`/`0x378`/`0x37A` | inventory counts (beer / lonkka / kiekura) |
| `0x146` (file) | map tile array base (word cells, `x*40 + y*2`) |
| `0xDA0` | (set to 0x15 after a player move — **(purpose inferred: redraw/animation counter)**) |
| **`0x1FC` / `0x1FE`** | **RNG 32-bit seed (high / low word); fixed `0x0272`/`0x03B0`, never re-seeded** |
| **`0x2D1..0x2DF`** | per-creature **spawn flags** (byte each; gates the NPC blocks) |
| **`0x2DD`** | ghost flag · **`0x2E3`** punk flag · **`0x2CC`** skip-NPC · **`0x2D0`** police-activation (set on a crime — attacking M / a cop / neekeri / hintti / priest, §8.7.6; cleared at level-init) |
| **`0xD18`** | **position table** base — 6-byte entries `(flag+0, X+2, Y+4)`, index = tile code |
| **B (pistepirko, `0x2D`)** | flag **`0x2D6`** · life **`0xE26`** · position **`0xE28`/`0xE2A`** (= `table[0x2D]`) |
| **Ghost (`0x4B`)** | flag `0x2DD` · HP `0xEDA` · anchor `0xEDC`/`0xEDE` (= spawn cell) |
| **Punk (`0x33`)** | flag `0x2E3` · pos `0xE52`/`0xE54` |
| **Kake (`0x24`)** | flag `0x2D1` · anchor `0xDF2`/`0xDF4` |
| **Pickpocket (`0x25`)** | flag `0x2D2` · anchor `0xDF8`/`0xDFA` |
| **Vankikarkuri (`0x26`)** | flag `0x2D5` · anchor `0xE5E`/`0xE60` |
| **Dog (`0x2E`)** | flag `0x2D7` · HP `0xE2C` |

> **Address note:** an earlier draft listed B's life/position at `0xD26`/
> `0xD28`/`0xD2A` — those are **wrong** (off by `0x100`; the `0xD2x` range is
> unreferenced by any instruction). The correct B block is `0xE26` (life),
> `0xE28`/`0xE2A` (position), confirmed by every writer/reader in the binary.

---

## 16. Version differences (v1.17 `SPU.COM` → v2.24 `SPU.EXE`)

From the string catalogs and HLP deltas:

| Feature | 1.17 | 2.24 |
|---|---|---|
| Doors | — | **`± = Ovi`** (doors; entry logic added) |
| Save/Load | — | **`S = talletus`** (save) + load; save file |
| Last message | — | **`p = viimeisin viesti`** (re-show last message) |
| Clock | — | **`c = kello`** (in-game clock) |
| Weapon switch | — | **`w = aseenvaihto`** |
| Redraw | — | **`^-L`** redraw screen |
| Stiletto/chains | pickable (`\`, `á`) | **not pickable** (removed from pickup) |
| Kake | gives beer, disappears | **extortion** variant ("Kake taputtaa palleaasi…") |
| Fines / wanted | — | **fines & wanted status** added |
| Prostitute | — | **prostitute (50 mk)** added |
| APTEEKKI (pharmacy) | — | **AIDS vaccine (69 mk)** at APTEEKKI |
| Hangover | — | **krapula** post-blackout state (label `SPU.EXE` `0xEF4F`); exact effects untraced |
| Suicide | `Q` prompt | still present (TIP: "Se ei pelaa, joka sammuu!" — the blackout is what kills) |
| New entities | — | Pappi (R), Sosiaaliavustaja (L), Poliitikko (X), Mustalainen (Y), Alokas (Z), Juntti (U), Lumppu-Leena (h), Ovi (±) |
| Removed entities | Maito, Silakka | — |
| Tips | `SPU.TIP` | includes a **Poke** tip |
| High scores | `SPU.TOP` | `SPU.TOP` |

(Exact per-feature mechanics for the 2.24 additions require a live v2.24
session — **pending**.)

---

## 17. Open items / to verify
1. **Exact Voima regen rate** (the per-turn state handler has **no** Voima
   regen — all Voima *adds* found are event-driven, e.g. +1/+2/+3/+5 after
   kills/events; the port's `+1 if Random(4)=0` regen is still **(inferred)**).
   **Death threshold CONFIRMED:** `Voima < 0` strictly (file
   `0xE0E8..0xE0F8`: load Voima, cmp 0, `0x1990` = first<second → die).
   **Voima −1 per vomit turn** at `Prom > 4` is confirmed (file `0xE241`).
2. **Promille** — **largely resolved** (§4.2, §14): start = **0.2** and
    **+0.2 per beer/lonkka** are screen-confirmed; the drink/shop menu
    (`q` → `0x4F1C`), the consume routine (`0x7E50`), the drunk-stagger
    (turn-proc `0x54A0`, threshold **3**), the shop-refusal ("Teille ei myydä,
    te olette humalassa!", `0x7697`) and the doze-off blackout (`0x4C67` +
    `~0x99F6`) are all traced. **NOW CONFIRMED:** the **decay law**
    (fixed **−0.00054/turn** while `Prom >= 0`, file `0xE346`) and the
    **drunk-state machine** (file `0xE1C5..0xE311`): "Voit pahoin!" at
    `Prom < 0.05`, vomit "Oksennat!" **plus Voima −1** at `Prom > 4`,
    "Horjahtelet" at `3 < Prom ≤ 4`, else blank. **The 6-byte TP-`real`
    encoding is now cracked** (byte0 = sign+7-bit exponent biased −129;
    bytes1–5 = big-endian 40-bit mantissa; value = sign·(M/2⁴⁰)·2^(exp−129)),
    so the inline real constants (decay, sick-threshold) decode exactly.
    **RESOLVED — "Voima decreases when Prom → 0":** there is **no** Voima
    drain at `Prom = 0`. The per-turn handler's only Prom-coupled Voima change
    is the vomit drain at `Prom > 4`. What actually happens at low Prom is the
    **Kankkunen sobering death**: `Prom` decays below 0 and the handler (file
    `0xE33E..0xE36A` → `0x9D9A`) prints "Kankkunen saapuu ja katkaisee
    taipaleesi …" and sets the death flag `[0x2CC]`. That is the "stays at 0.0
    too long → Kankkunen → die" the user observed (§4.2). **Still open:** the
    exact **blackout triggering Prom** and the exact **friendly-police**
    (`0x2CE`) Voima/Prom condition.
3. **Combat damage/health** for non-dog, non-police entities (punks,
   skinheads, etc.). *(Police damage is now resolved: `1+Random(3)` Voima per
   turn per adjacent officer, §8.7.6/§10.3.)*
4. **`/` = sepustus** effect.
5. **Kake refusal** (`e`) consequence.
6. **Level generator** exact rules (dot counts, building rotation, entity
   spawn rates per level). **RESOLVED — RNG seed source (§8.7.1):** the RNG is
   a 32-bit LCG/LFSR with a **fixed baked-in seed** (`0x0272`/`0x03B0` at file
   `0x1FC`/`0x1FE`), with **no clock read** (the binary's only `INT 21h` is
   the exit). ⇒ generation is deterministic in the **key/action sequence**,
   not wall-clock time. L1 is byte-identical every run; L2+ = f(RNG state at
   descent) = f(exact L1 route). **RESOLVED (dog & dot-eater):** the two dynamic
    creatures are roll-to-spawn — dog `D`: `(Level>2) AND (Random(5)=3)`
    (level ≥ 3, ~20 %); dot-eater `B`: `(Level>1) AND (Random(100)>77)`
    (level ≥ 2, ~22 %) — see §8.7.5.
    **RESOLVED (random placement, 2026-11-16):** ALL random placements go
    through the helper `0x2CF3`, which rolls a fixed rectangle
    **`X = 23+Random(56)` = 23..78, `Y = 7+Random(12)` = 7..18** and retries
    until the cell is empty. That rectangle avoids every building — including
    the two sealed hollow pockets the wall template encloses
    (x=14..19, y=3..5 under ISKU and x=14..19, y=20..22 under the
    bottom-left roof), where a dot would be uncollectable and the level
    soft-locks ("Ensin pisteet …"). The port previously rolled the whole map
    range (≈17 % of L2+ levels got a pocket dot); it now replicates the
    original rectangle exactly (`PLACE_X0..PLACE_Y1`, verified by pty test:
    8/8 L2 maps, 4–8 dots, all inside 23..78×7..18, zero pocket hits).
    **Still open:** the exact per-level spawn table (which flag/dot/wall each
    draw selects), the exact per-level dot total (original loop runs
    `[0x2F4]` = Level times → N dots on level N; port currently uses
    `RandRange(4,8)+Level div 4`), and the pending decision on whether to
    reproduce the original's dot-eater softlock (B eating a dot leaves the
    required count unchanged → level can become unfinishable).
   **Correction (2026-11-02):** an earlier draft identified the L2+ generator
   at file `0x5129` (VA `0x5229..0x770E`) — that is **wrong**: the region
   file `0x5195..0x5472` is the **blackout handler** (see Appendix A), and
   the real level-2+ generator is still **unlocated**.
7. **Kokemus** use (does it gate progression or change stats?).
8. ~~**Map orientation/dimensions** final confirmation.~~ — **RESOLVED**
   (2026-11-02): 80×20 word array, `x*40+y*2`, screen↔physical aliasing and
   the 180-cell wall template are confirmed against the live dump; see §8.4.
9. **v2.24 live session** for all 2.24-only features (doors, save/load,
   APTEEKKI, fines/wanted, prostitute, hangover).
10. ~~`SPU.TOP` **score-field mapping**~~ — **RESOLVED**: formula cracked and
    verified byte-exact (24 data points); see §12.1–12.3.
11. ~~**Stairs (`>`) deleted when the player walks over them**~~ — **RESOLVED
    (port)**: the exit is a permanent, walkable tile; the player may stand on
    it (`@` drawn over `>`) and leave without erasing it. Port fix: the
    player's old cell is restored via `ClearFrom`, which skips `T_EXIT`
    (verified live — the `>` survives the player walking onto and off it).
    **Open discrepancy (original):** the RE'd turn prologue (file `0x549C`)
    clears the player's *old* cell to 0 *unconditionally*, which on its face
    would delete the exit when the player leaves it — yet in live play the
    exit is never deleted. Some mechanism (possibly the `[0x2CD]`-gated full
    redraw or a re-stamp of fixed tiles) must preserve it; unresolved. The
    port keeps its `T_EXIT` guard because it matches the observed behavior.
12. ~~**NPCs / characters don't move**~~ — **RESOLVED (RE + port)**: the reason
    most creatures are static is now fully extracted (§8.7): chasers are
    **cache-pinned** (glyph re-written at a fixed anchor every turn; the table
    chase is invisible). Two blocks produce **visible** motion: the **dot-eater
    `B`** has its own block (`0xC943..0xCA4B`) that **teleports it to a random
    empty-or-dot cell every turn**, eating dots it lands on (`life [0xE26]++`),
    and the **police** (`0xA7EC..0xB09F`, gated by `[0x2D0]`) **walk one cell
    toward the player each turn and attack when adjacent** — asleep until the
    player commits a crime (e.g. attacking the old lady `M`), which sets
    `[0x2D0]` (§8.7.6/§10.3). Port: `UpdateEntities` teleports B each turn;
    step-on spawns a dot + `life--` or kills at `life==0`; `UpdatePolice`
    wakes the cops on the M-attack and then chases/attacks. Verified live — B
    changed cell every turn over 10 wait-turns (test_l2.py); the cops were
    static before the crime and walked toward the player after it
    (test_police.py).
13. **Tile `0x1D` identity** — walkable (≤ `0x1D`) and drawn as a space
    (no renderer case); it appears in some dumps but no pickup/creature
    handler has been attributed to it yet.
14. **9×16 font full-grid confirmation** — the mode-0x10 font matches CP437
    for 0x80–0xBF and 0xE0–0xFF (verified via rendered glyphs) and differs
    for 0xC0–0xDF (frame bytes, §3). A full 0..255 glyph-grid capture from
    the original is still pending (the FONT.COM grid attempt produced a
    blank decode); only needed if a future tile/entity uses an unverified
    9×16 glyph.

---

## Appendix A — Procedure map (v1.17, file offsets)
Identified by their inline strings / role (from the `procs.py` splitter):

| File addr | Role (evidence) |
|---|---|
| `0x328D` | **Status-line renderer** (inline: `Taso: `, `Mk: `, `Voima: `, `Kokemus: `) |
| `0x3F5F` | **Exit/descent** (inline: "Ei ei, vain portaita pitkin"; exit at x=78,y=21) |
| `0x4C67` | Drunkenness ("ja nukahdat." = "and you doze off.") |
| `0x4F1C` | **Drink menu** (inline: `Kaljaa`, `Lonkkaa`, `Kiekuraa`) |
| `0x5086` | **Hint pager** (inline: "Se sanoo: ", `SPU.TIP`) |
| `0x5195..0x5472` | **Blackout / doze-off handler** ("Hän sammuu!"; Voima check → "Pojat lähtevät nukkumaan." / "Punkkarit luikkivat pakoon."; `[0x2E3]`:=0, full redraw, drop at (PX+1,PY): Ketjut-slot empty → `0x06`, else `Random(2)==1` → `0x0C` drawn `$`, else nothing). *Earlier drafts mislabeled nearby code as the L2+ generator.* |
| `0x5473` | **Turn processor / player move** (VA `0x5573`; 8-way from `[0x31a]`, drunk-stumble variant, old-cell clear, dest clamp X13-79/Y3-22, tile switch) |
| `0x7EE7` | Voima display ("Voima: ") |
| `0x7FBC` | **SPU.HLP** display |
| `0x8090` | **SPU.SPE** display |
| `0x9291` | **Main input loop / key dispatch** (inline: "Tuntematon komento ") |
| `0x9A6D` | Voima/status variant ("Voima: ") |
| `0x2D00` | **Level-1 generator** (VA `0x2E00`; fixed C/M/dot/hint/exit layout) |
| ~~`0x5129`~~ | ~~**Level-2+ generator** (VA `0x5229..0x770E`)~~ — **misidentification**: that region is the tail of the blackout handler (`0x5195..0x5472`); the real level-2+ generator is still **unlocated** (§17.6) |
| `0xA80C` | **Entity draw loop** (VA `0xA90C..0xB19C`; iterates position table `0xD1A`, plots each `(Y,X)`) |
| `0x9D9A` | "…saapuu ja katkaisee taipaleesi" ("…arrives and cuts off your path") |
| `0x2C9F`,`0x2CC9` | small helpers (MAX/MIN-like) |
| `0x2CF3` | random-based (calls `@Random`) |
| `0x2EC0` | globals init (writes `0x2F8`) |

## Appendix B — RTL routine references (mem addresses)
| Addr | Function |
|---|---|
| `0x0DAa` | GotoXY(x,y) |
| `0x0FD1` | stack check (`mov cx,N; call`) |
| `0x1022` | flush/write |
| `0x10DA` | @Random(n) |
| `0x1973` | string-ref thunk (expands `ds:di` 6-byte string onto stack) |
| `0x285D` (file) / `0x295D` (mem) | **WriteInlineStr** (return-address idiom) |
| `0x2875` / `0x2975` | WriteLn |
| `0x2828` / `0x2928` | WriteChar |
| `0x29C9`,`0x2A20`,`0x29CD`,`0x2AD0`,`0x2A0E` | CloseFile, ReadRec, WriteRec, EOF, flush (SPU.TOP I/O) |

## Appendix C — Extraction tooling (this work)
* `decodemod.py` — PPM → 80×25 grid decoder (PIL + `exact_font.json` +
  CP437 fallback).
* `play.py` — headless QEMU/HMP driver (sendkey/screendump/typestr).
* `bot.py` — adaptive play-bot (reaches level 3+, handles k/e prompts).
* `step3.py` / `probe3.py` — targeted combat probes (dog + dot-eater).
* `ndiswin.py` — ndisasm-backed 16-bit window decoder.
* `procs.py` — procedure splitter (byte-scans `55 8B EC` prologues,
  delimiter-aware CFG walk over INT-3 + inline strings, annotates strings &
  calls).
* `com_code.txt` — full linear disassembly of `SPU.COM`.
* `str117.txt` / `str224.txt` — message catalogs (file offsets).
* `hlp117.txt` / `hlp224.txt` (+ spe/tip variants) — decoded HLP/SPE/TIP.

---

## Appendix D — Findings from the 2026-11-19 port rewrite (v1.17 disassembly)

These were extracted while rewriting `spurgux117.pas`; several **correct**
earlier sections of this document.

* **Level-2+ generator located:** file `0x4216..0x4D20` (inside the descent
  procedure `0x405F`). It is *not* unlocated (§17.6). Per level:
  `Level+1`, hidden score `[0x1132]+1`, `Voima+1`, player at (15,12), all
  creature flags cleared. Building flags: ALCO `[0x2E4]` = `Random(100)+Level>90`;
  DIVARI `[0x2E5]` = `Random(100)-Level<0`; POLIISI/VANKILA `[0x2E6]` =
  `Random(Level)>2` (not on special levels); alcove wall x=76,y=3..5 `[0x2E9]` =
  `Random(Level)>3`. Levels 13/31 = graveyard (`0x2F32`: graves `0x5B`, ghost,
  priest), 17/25 = zoo (`0x308A`: fences `0x5C`, gorilla, hippie), level ≥ 50 =
  cognac tile `0x0B` at (78,5) + ghost (`0x322F`).
  Police slots `0x1E..0x1E+Random(5)`; **dots = Level** (one per level number);
  item/creature roll table is in the port's `BuildLevel`.
* **Kokemus on the status line is `[0x2EC]`** (word), the experience/kill-bonus
  counter — *not* the real at `[0x1132]`. `[0x1132]` is a hidden score real
  (+1 per level, ± item/kill events) and is what `SPU.TOP` stores at r[10..15]
  (a 6-byte TP real; the §12.2 formula is just its decoding).
* **Wall bump costs 0.1 Voima** (inline real at `0x7E02`), not 1.
* **Inventory shows entries 3..11 as items 1..9** (`0x501C`), so the digit d in
  throw/sell always means entry d+2. Entry 10 = "Äpy", 11 = "Hennessy".
* **Tile `0x06` = Ketjut, `0x0B` = Konjakki** (not `0x06` konjakki). With the
  correct CP437 decoding SPU.HLP's legend matches the renderer.
* **Shops:** ALCO door (17,7): refused if Prom > 1, closed if Mk < 15 or not
  ALCO, else kalja 10 mk / lonkka 15 mk. (16,7) "Toinen ovi, tollo!". DIVARI
  door (17,18) buys items (Haalarit 100, Pamppu R50, Stiletti R80, Ketjut R100,
  Camel R101, Äpy 10, Hennessy 250).
* **Drinking:** Prom += 0.1 + d/10 (kalja 0.2, lonkka 0.3). Eating: Voima +1.
* **Voima regeneration** (`0x9B6D`, per turn): `Random/87 + Level/300`; AIDS
  `[0x2CF]` drains 0.2 per turn and is never cleared in v1.17.
* **Water `0x16`** drowns you unless you wear Haalarit; with them −0.3 Voima.
* **Portsari `0x27`** stands at (79,21) beside the exit; descending asks a toll
  `min(5*Level, Mk)`. **Win:** descend while entry 11 = "Hennessy".
* There is **no Prom-triggered blackout** in v1.17; `0x4D67` (doze-off) is only
  called from the hippie trip and its return.
* **Cognac condition (`0x449D..0x44CB`):** the cognac (and a ghost) is placed
  only when `Level >= 50` **and** equipment slot 8 (`0x34E`) equals "Äpy"
  (`0x127A` = string equal). Slot 8 is initialised to spaces and never
  written; the Teekkari sells Äpy into slot 10 (`0x362`, shown as item 8).
  So in v1.17 the cognac never appears — an off-by-two bug. The port uses
  the intended rule: carry Äpy (slot 10) and the cognac is on level ≥ 50.
* **Starting inventory (`0x4FAB..0x4FBA`):** 1 kalja, 0 lonkka, 1 kiekura,
  Haalarit in slot 3, slots 4..11 blank.
* **Dot pickup message (0x57BA):** "Sokea kana löysi jyvän!" is printed only
  while the player is blind (`[0x2CD]` = 0); normal pickups are silent.
* **Punk gang movement (0xA1AC..0xA476):** the king Ñ at (`[0xE52]`,`[0xE54]`)
  with punks at (x-1,y±1) and (x,y±1) forms a "<" whose mouth is at x-1.
  Each turn: if PX = king x-1 the gang mugs ("Punkkarit muiluttavat sinua!",
  Voima −2; while hallucinating "Tanssit jonkun mukavan pojan kanssa.",
  Voima −1). Otherwise the gang blanks its old cells, sets y := PY, x := x-1
  and redraws. The original overwrote walls/dots/exit and could walk off the
  left edge; the port restores the covered tiles and stops at the map edge.
* **Death messages:** there is no random death-message table in v1.17 or
  v2.24. Each death has a fixed message: "Voimasi ehtyvät, lyhistyt maahan
  ..." (Voima < 0, 0xE1FD), "Et osaa uida, hukut!" (water, 0x5FEF),
  "Valitettavasti purema oli myrkyllinen ..." (snake, 0xD4B8: sets Voima := -1,
  so the collapse message follows) and "Kankkunen saapuu ..." (Prom < 0 only,
  single call site 0xE46A). v2.24 additionally records a per-killer
  "kuolinsyy" (krapula, myrkytys, poliisi, ...) in its high-score list.
* **Port: v2.24-style cause of death.** The port records a "kuolinsyy" for
  each game in bytes 18..19 of the 28-byte SPU.TOP record (`$C5` marker +
  index into the cause list), bytes the DOS program ignores, so the file
  stays compatible. Causes use the v2.24 vocabulary where one fits (krapula,
  myrkytys, poliisi, haamu, rakkikoira, käärme, gorilla, juppi, piikki,
  hinttailu, masinol, mummo, neekeri, murha, väkivalta, ilkeys,
  kallonmurtuma, itsemurha, tuntematon) plus a few of the port's own
  (punkkarit, hukkuminen, taskuvaras, vilustuminen, AIDS, alkoholimyrkytys,
  Kake). The killer is whatever reduced Voima last. Old records without the
  marker still print "menehtyi tasolle N".
