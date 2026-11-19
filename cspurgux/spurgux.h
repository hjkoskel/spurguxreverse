/* ===========================================================================
   SpurguX v1.17 -- clean-room re-implementation, C89 port.

   Ported from spurgux117.pas (Free Pascal).  Rules:
     * ANSI C89, no floating point anywhere (fixed-point integers instead)
     * 8- and 16-bit values preferred; 32-bit only where the range or the
       fixed-point precision demands it (Voima, Promille, turn counter)
     * global variables, fixed-size buffers, no malloc
     * sized for low-end machines: all text is kept as single-byte CP437
       (like the DOS original) and converted to UTF-8 only on output;
       data files are streamed line by line; stdio uses one static 128-byte
       buffer; about 6 KB of static RAM in total

   Build options:
     -DOUT_CP437   write CP437 bytes to the terminal as-is (DOS/ANSI.SYS,
                   retro terminals) instead of converting them to UTF-8

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

/* --- CP437 letters used in the Finnish texts ----------------------------- */
#define AE  "\204"    /* ä */
#define OE  "\224"    /* ö */
#define AA  "\206"    /* å */
#define AEU "\216"    /* Ä */
#define OEU "\231"    /* Ö */

/* --- screen geometry ----------------------------------------------------- */
#define SCRW 80
#define SCRH 25

#define MSG_ROW   1
#define FRAME_TOP 2
#define FRAME_BOT 23
#define FRAME_L   13
#define FRAME_R   80
#define STAT_ROW  25
#define MAP_X0 14
#define MAP_X1 79
#define MAP_Y0 3
#define MAP_Y1 22
#define MAPW   (MAP_X1 - MAP_X0 + 1)
#define MAPH   (MAP_Y1 - MAP_Y0 + 1)

/* only the 66x20 play area is stored; every access is InMap-checked */
#define M(x, y) Map[((y) - MAP_Y0) * MAPW + ((x) - MAP_X0)]

/* random-placement rectangle of the original placement helper (0x2DF3) */
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

/* colour attributes */
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

/* --- buffer sizes -------------------------------------------------------- */
#define MSGMAX   (SCRW + 1)   /* the message row is 80 columns */
#define NAMEMAX  10           /* 9 CP437 characters (SPU.TOP field) */
#define PATHMAX  128

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

/* ===========================================================================
   Global game state (defined in game.c)
   =========================================================================== */
extern u8  Map[MAPW * MAPH];
extern u8  PX, PY;
extern u8  Level;
extern s16 Mk;
extern s32 Voima;          /* fixed point, VS */
extern s32 Prom;           /* fixed point, PS */
extern u32 Turn;
extern s16 Score;          /* hidden score (shown only in SPU.TOP) */
extern u8  DotsNeeded, DotsGot;
extern u8  InvKalja, InvLonkka, InvKiekura;
extern u8  Equip[E_COUNT]; /* 1 = carried */
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
extern char PlayerName[NAMEMAX];   /* CP437 */
extern u8  UseColor;
extern u8  StartLevel;
extern s16 StartMk;

extern const char *const EquipName[E_COUNT];
extern const char *const Causes[K_COUNT];

/* ===========================================================================
   term.c -- raw terminal, 80x25 screen buffer with diff output, keyboard
   =========================================================================== */
extern u8 ForceRedraw, CursorOn;
extern u8 CurCol, CurRow;

void SetRaw(void);
void RestoreTerm(void);
void ScreenInit(void);
void InstallSignals(void);
void OutStr(const char *s);
void OutText(const char *s);
void SPut(int col, int row, const char *s, u8 at);
void SFill(int c0, int r0, int c1, int r1);
void SClear(void);
void SFlush(void);
int  ReadKey(void);
int  ReadRawByte(void);
int  ReadKeyAfterPause(u16 ms);
void SleepMs(u16 ms);

/* ===========================================================================
   data.c -- data files, high scores, save/load
   =========================================================================== */
void InitDataDirs(const char *argv0);
u8   TextOpen(const char *name);
u8   TextLine(char *line);     /* next line, max SCRW chars; 0 at the end */
void TextRewind(void);
void TextClose(void);
void ShowHiscore(void);
u8   SaveFileExists(void);
u8   SaveGame(void);
u8   LoadGame(void);
void DeleteSave(void);
const char *SavePathStr(void);

/* ===========================================================================
   game.c -- rules
   =========================================================================== */
int  Random(s32 n);
void Randomize(u32 seed);
void RecomputeWeapon(void);
void MainLoop(void);
void DumpMapToFile(const char *fn);
void StrCopy(char *dst, const char *src, u16 size);
void StrCat(char *dst, const char *src, u16 size);
const char *NumStr(s32 v, u8 width);

#endif
