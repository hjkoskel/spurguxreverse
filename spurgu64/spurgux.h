/* ===========================================================================
   SpurguX v1.17 -- Commodore 64 port (cc65), from the C89 port in
   ../cspurgux.  See ../SPURGU64PLAN.md.

   Text inside the program is PETSCII as cc65 produces it from string
   literals ('h' = $48, 'H' = $C8).  Codes that never occur in printed
   PETSCII are used for the extra glyphs:
     $01-$0F  PETSCII graphics used in strings (blocks, frame lines)
     $80-$93  custom characters: Finnish letters and the CP437 item symbols
              (redefined reverse-video glyphs of the RAM character set)
   c64.c turns these into screen codes when drawing.

   Original game: (C) 1987 Petri Niska.  This is an independent port.
   =========================================================================== */
#ifndef SPURGUX_H
#define SPURGUX_H

typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned short u16;
typedef short          s16;
typedef long           s32;
typedef unsigned long  u32;

/* --- custom characters ($80-$93, octal escapes are not translated) ------ */
#define AE  "\200"    /* ä */
#define OE  "\201"    /* ö */
#define AA  "\202"    /* å */
#define AEU "\203"    /* Ä */
#define OEU "\204"    /* Ö */
#define AAU "\205"    /* Å */
#define C_AE  0x80
#define C_OE  0x81
#define C_AA  0x82
#define C_SIGMA   0x86   /* Σ overalls  */
#define C_BSLASH  0x87   /* \ stiletto  */
#define C_SZ      0x88   /* ß chains    */
#define C_THETA   0x89   /* Θ Camel     */
#define C_PHI     0x8A   /* Φ cognac    */
#define C_HOUSE   0x8B   /* ⌂ milk      */
#define C_INF     0x8C   /* ∞ chocolate */
#define C_YEN     0x8D   /* ¥ herring   */
#define C_AE_LIG  0x8E   /* æ poop      */
#define C_OMEGA   0x8F   /* Ω bouncer   */
#define C_NTILDE  0x90   /* Ñ punk king */
#define C_DEGREE  0x91   /* ° ghost     */
#define C_IQUEST  0x92   /* ¿ hint      */
#define C_APPROX  0x93   /* ≈ (help text only) */

/* --- PETSCII graphics in strings ($01-$10) ------------------------------
   cc65 runs escapes through its character map too: $08 and $0A-$0D become
   other codes, so they are not used in strings ($80-$92 pass unchanged,
   $93 does not -- C_APPROX is only used as a number). */
#define G_FULL  "\001"   /* █ */
#define G_HALF  "\002"   /* ▐ */
#define G_UPPR  "\003"   /* ▀ */
#define G_LOWR  "\004"   /* ▄ */
#define G_SHAD  "\005"   /* ▒ */
#define G_HLINE "\006"   /* ─ */
#define G_VLINE "\007"   /* │ */
#define G_TEE   "\011"   /* ┬ */
#define G_CROSS "\016"   /* ┼ */
#define G_LARR  "\017"   /* ◄ more map this way */
#define G_RARR  "\020"   /* ► */

/* --- screen -------------------------------------------------------------- */
#define SCRW 40
#define SCRH 25

/* message: the original 80-column line, wrapped over rows 0-1 */
#define MSGW      80
#define FRAME_TOP 2
#define FRAME_BOT 23
#define STAT_ROW  24

/* world coordinates are the original 80x25 screen's (1-based): the frame
   is at x=13 and x=80, the map at x=14..79, y=3..22.  Screen row = y,
   screen column = x - ViewX. */
#define WORLD_X0 13
#define WORLD_X1 80
#define MAP_X0 14
#define MAP_X1 79
#define MAP_Y0 3
#define MAP_Y1 22
#define MAPW   (MAP_X1 - MAP_X0 + 1)
#define MAPH   (MAP_Y1 - MAP_Y0 + 1)
#define VIEW_MAX (WORLD_X1 - SCRW + 1)   /* 41 */

/* a row table instead of a multiplication by 66 (slow on the 6502) */
extern const u16 MapRow[MAPH];
#define M(x, y) Map[MapRow[(y) - MAP_Y0] + ((x) - MAP_X0)]

#define PLACE_X0 23
#define PLACE_Y0 7

/* --- tile codes (the original's values) ---------------------------------- */
#define T_FLOOR    0x00
#define T_KALJA    0x01
#define T_LONKKA   0x02
#define T_HAALARI  0x03
#define T_PAMPPU   0x04
#define T_STILETTI 0x05
#define T_KETJUT   0x06
#define T_CAMEL    0x07
#define T_KONJAKKI 0x0B
#define T_MONEY    0x0C
#define T_HINT     0x0D
#define T_MILK     0x0E
#define T_CHOCO    0x0F
#define T_APPLE    0x10
#define T_SILAKKA  0x11
#define T_MAKKARA  0x12
#define T_DOT      0x14
#define T_EXIT     0x15
#define T_WATER    0x16
#define T_POOP     0x17
#define T_POLICE1  0x1E
#define T_POLICE5  0x22
#define T_MUMMO    0x23
#define T_KAKE     0x24
#define T_VARAS    0x25
#define T_TEEKKARI 0x26
#define T_PORTSARI 0x27
#define T_NEEKERI  0x28
#define T_HINTTI   0x29
#define T_TROKARI  0x2A
#define T_PAPPI    0x2B
#define T_HIPPI    0x2C
#define T_PIRKKO   0x2D
#define T_DOG      0x2E
#define T_GORILLA  0x2F
#define T_SKIN     0x30
#define T_SNAKE    0x31
#define T_AIDS     0x32
#define T_PUNK     0x33
#define T_KINGI    0x34
#define T_JUPPI    0x35
#define T_KARKURI  0x36
#define T_GHOST    0x4B
#define T_GRAVE    0x5B
#define T_FENCE    0x5C
#define T_WALL     0x63
#define T_COUNT    0x64
/* creature hit points: one signed byte for $1E..$36 and one for the ghost */
#define HP_COUNT   (T_KARKURI - T_POLICE1 + 2)
#define HP(t)      HPt[(t) == T_GHOST ? HP_COUNT - 1 : (t) - T_POLICE1]

#define START_X 15
#define START_Y 12
#define EXIT_X  78
#define EXIT_Y  21
#define PORTSARI_X 79
#define PORTSARI_Y 21
#define KONJAKKI_X 78
#define KONJAKKI_Y 5
#define MAX_LEVEL 50
/* equipment entries; the inventory shows entries 3..11 as items 1..9 */
#define E_HAALARI  3
#define E_PAMPPU   4
#define E_STILETTI 5
#define E_KETJUT   6
#define E_CAMEL    7
#define E_APY      10
#define E_HENNESSY 11
#define E_COUNT    12
/* --- fixed point --------------------------------------------------------
   Voima is kept in units of 1/26100 (= 1/(87*300)): the per-turn
   regeneration Random/87 + Level/300 and the 0.1 / 0.2 / 0.3 / 0.5 steps
   are then all exact integers.
   Promille is kept in units of 1/100000 so the 0.00054 per-turn decay is
   exact.                                                                  */
#define VS     26100L
#define V_01   2610L
#define V_02   5220L
#define V_03   7830L
#define V_05   13050L
#define VI(n)  ((s32)(n) * VS)

#define PS     100000L
#define P_DECAY 54L
#define PI10(n) ((s32)(n) * (PS / 10))   /* n tenths of a promille */
/* --- causes of death (v2.24 "kuolinsyy"), index stored in SPU.TOP ------- */
#define K_WON          0
#define K_TUNTEMATON   1
#define K_ITSEMURHA    2
#define K_KRAPULA      3
#define K_MYRKYTYS     4
#define K_HUKKUMINEN   5
#define K_KALLO        6
#define K_MUMMO        7
#define K_VAKIVALTA    8
#define K_NEEKERI      9
#define K_PUNKKARIT    10
#define K_ILKEYS       11
#define K_VILUSTUMINEN 12
#define K_POLIISI      13
#define K_RAKKIKOIRA   14
#define K_HAAMU        15
#define K_TASKUVARAS   16
#define K_MURHA        17
#define K_KAKE         18
#define K_HINTTAILU    19
#define K_MASINOL      20
#define K_JUPPI        21
#define K_PIIKKI       22
#define K_KAARME       23
#define K_GORILLA      24
#define K_AIDS         25
#define K_ALKOMYRKYTYS 26
#define K_COUNT        27

/* colour attributes (mapped to C64 colours in c64.c) */
#define A_NORM    0
#define A_BOLD    1
#define A_RED     2
#define A_GREEN   3
#define A_YELLOW  4
#define A_BLUE    5
#define A_MAGENTA 6
#define A_CYAN    7
#define A_WHITE   8
#define A_DIM     9

/* --- buffer sizes -------------------------------------------------------- */
#define MSGMAX   (MSGW + 1)
#define NAMEMAX  10
#define LINEMAX  (MSGW + 1)

/* --- keys (PETSCII from GETIN) ------------------------------------------ */
#define KEY_RETURN 13
#define KEY_DEL    20
#define KEY_STOP   3
#define KEY_HOME   19
#define KEY_CLR    147
#define KEY_ESC    27      /* RUN/STOP is returned as this */

/* ===========================================================================
   Global game state (game.c)
   =========================================================================== */
extern u8  Map[MAPW * MAPH];
extern u8  PX, PY;
extern u8  Level;
extern s16 Mk;
extern s32 Voima;
extern s32 Prom;
extern u32 Turn;
extern s16 Score;
extern u8  DotsNeeded, DotsGot;
extern u8  InvKalja, InvLonkka, InvKiekura;
extern u8  Equip[E_COUNT];
extern u8  Weapon;
extern u16 Kokemus;
extern s8  HPt[HP_COUNT];
extern u8  PoliceActive, Friendly, Aids, Blind, PortsariOn, PunksOn;
extern s16 FriendlyTurns;
extern u8  KingX, KingY;
extern u8  PunkUnder[5];
extern u8  FlagAlco, FlagDivari, FlagPoliisi, FlagGrave, FlagZoo, FlagAlcove;
extern u8  HotelName;
extern char MsgLine[MSGMAX], LastMsg[MSGMAX];
extern u8  Dead, Won, Saved;
extern u8  Killer;
extern char PlayerName[NAMEMAX];

extern const char *const EquipName[E_COUNT];
extern const char *const Causes[K_COUNT];

/* ===========================================================================
   c64.c -- screen, keyboard, timing
   =========================================================================== */
extern u8 CurCol, CurRow, CursorOn;   /* 0-based screen position */
extern const u8 Colour[10];           /* attribute -> C64 colour */
extern u8 ToScr[256];                 /* PETSCII -> screen code */
extern const u16 Row40[SCRH];         /* row * 40 */

#define SCREEN  ((u8 *)0xCC00)        /* screen RAM, VIC bank 3 */
#define COLRAM  ((u8 *)0xD800)

void ScrInit(void);
void ScrExit(void);
void SPut(u8 col, u8 row, const char *s, u8 at);  /* runs on to the next row */
void SPutN(u8 col, u8 row, const char *s, u8 n, u8 at);  /* at most n chars */
void SPutScr(u8 col, u8 row, u8 scr, u8 at);       /* one screen code */
void SFill(u8 c0, u8 r0, u8 c1, u8 r1);
void SClear(void);
u8   ScrCode(u8 petscii);
int  ReadKey(void);
int  ReadKeyAfterPause(u16 ms);

/* sound.s: SID effects played from the jiffy interrupt; the SND_* ids
   come from tools/mkfx.py */
#include "sfx.h"
void SndInit(void);
void SndExit(void);
void __fastcall__ SndPlay(u8 id);
void SleepMs(u16 ms);
u32  Jiffies(void);

/* ===========================================================================
   data.c -- data files, high scores, save/load
   =========================================================================== */
u8   TextOpen(const char *name);
u8   TextLine(char *line);     /* next line, max MSGW chars; 0 at the end */
void TextRewind(void);
void TextClose(void);
void ShowHiscore(void);
u8   SaveFileExists(void);
u8   SaveGame(void);
u8   LoadGame(void);
void DeleteSave(void);
void DataClose(void);          /* closes the drive command channel */

/* ===========================================================================
   game.c
   =========================================================================== */
int  Random(s32 n);
void Randomize(u32 seed);
void RecomputeWeapon(void);
void MainLoop(void);
void StrCopy(char *dst, const char *src, u16 size);
void StrCat(char *dst, const char *src, u16 size);
const char *NumStr(s32 v, u8 width);

#endif
