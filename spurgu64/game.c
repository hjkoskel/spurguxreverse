/* game.c -- SpurguX v1.17 rules, Commodore 64 version.

   The rules are the C89 port's (../cspurgux/game.c) unchanged; the drawing
   is redone for the 40x25 screen: the 66x20 map is seen through a 40-column
   window that scrolls sideways with the player, the 80-column message line
   wraps over the top two rows and the status fits on the bottom row.

   Original notes:

   Game rules follow the v1.17 disassembly:
     * level generator   (file 0x4216..0x4D20)
     * turn processor    (file 0x5473..0x7E50)
     * NPC update chain  (file 0x9EC4..0xE0E0)
     * per-turn handler  (file 0xE0E8..0xE36A)
   Additions merged from v2.24: S = save and quit, p = repeat last message,
   c = clock, Ctrl-L = redraw.

   All strings are CP437 (see AE / OE in spurgux.h).                       */
#include <string.h>
#include "spurgux.h"

/* HP(t) with a constant t compares two constants: harmless */
#pragma warn (const-comparison, off)

/* ===========================================================================
   Global state
   =========================================================================== */
u8  Map[MAPW * MAPH];
const u16 MapRow[MAPH] = {
  0, 66, 132, 198, 264, 330, 396, 462, 528, 594, 660, 726, 792, 858, 924, 990, 1056, 1122, 1188, 1254
};
u8  PX, PY;
u8  Level;
s16 Mk;
s32 Voima;
s32 Prom;
u32 Turn;
s16 Score;
u8  DotsNeeded, DotsGot;
u8  InvKalja, InvLonkka, InvKiekura;
u8  Equip[E_COUNT];
u8  Weapon;
u16 Kokemus;
s8  HPt[HP_COUNT];
u8  PoliceActive, Friendly, Aids, Blind, PortsariOn, PunksOn;
s16 FriendlyTurns;
u8  KingX, KingY;
u8  PunkUnder[5];
u8  FlagAlco, FlagDivari, FlagPoliisi, FlagGrave, FlagZoo, FlagAlcove;
u8  HotelName;
char MsgLine[MSGMAX], LastMsg[MSGMAX];
u8  Dead, Won, Saved;
u8  Killer;
char PlayerName[NAMEMAX];

#define StartLevel 1
#define StartMk    0

static const char *StateText;
static const char *DeathCause;
static const char *DeathPre;

const char *const EquipName[E_COUNT] = {
  "", "", "", "Haalarit", "Pamppu", "Stiletti", "Ketjut", "Camel",
  "", "", AEU "py", "Hennessy"
};

const char *const Causes[K_COUNT] = {
  "", "tuntematon", "itsemurha", "krapula", "myrkytys", "hukkuminen",
  "kallonmurtuma", "mummo", "v" AE "kivalta", "neekeri", "punkkarit",
  "ilkeys", "vilustuminen", "poliisi", "rakkikoira", "haamu", "taskuvaras",
  "murha", "Kake", "hinttailu", "masinol", "juppi", "piikki",
  "k" AE AE "rme", "gorilla", "AIDS", "alkoholimyrkytys"
};

static const char ST_AIDS[]    = "AIDS painaa!";
static const char ST_BLANK[]   = "            ";
static const char ST_SICK[]    = "Voit pahoin!";
static const char ST_VOMIT[]   = "Oksennat!   ";
static const char ST_STAGGER[] = "Horjahtelet ";
static const char KANKKUNEN[]  = "Kankkunen saapuu ja katkaisee taipaleesi ...";
static const char S_UINAHTAA[] = "Otus uinahtaa.";
static const char S_HAN_UIN[]  = "H" AE "n uinahtaa.";
static const char S_YN[]       = "kKeE";
static const char S_HUKUT[]    = "Et osaa uida, hukut!";

/* main loop state (the Pascal nested Move procedure used these) */
static int RepeatCh;
static u8  Acted, WasRepeat;

/* ===========================================================================
   Small helpers
   =========================================================================== */
void StrCopy(char *dst, const char *src, u16 size)
{
  u16 i = 0;
  if (size == 0) return;
  while (src[i] && i + 1 < size) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

void StrCat(char *dst, const char *src, u16 size)
{
  u16 n = (u16)strlen(dst);
  if (n < size) StrCopy(dst + n, src, (u16)(size - n));
}

/* decimal, right-aligned to `width` (static buffer) */
const char *NumStr(s32 v, u8 width)
{
  static char b[14];
  char *p = b + sizeof(b) - 1;
  u8 neg = v < 0, n = 0;
  u32 u = neg ? (u32)-v : (u32)v;
  u16 w;
  *p = 0;
  /* 32-bit division is slow on the 6502: only the high digits use it */
  while (u > 0xFFFFUL) {
    *--p = (char)('0' + (u8)(u % 10));
    u /= 10;
    n++;
  }
  w = (u16)u;
  do {
    *--p = (char)('0' + (u8)(w % 10));
    w /= 10;
    n++;
  } while (w);
  if (neg) { *--p = '-'; n++; }
  while (n < width && p > b) { *--p = ' '; n++; }
  return p;
}

/* C89 leaves signed division rounding implementation-defined: be explicit */
static s32 TruncDiv(s32 a, s32 b)
{
  return a < 0 ? -(-a / b) : a / b;
}

static s32 RoundDiv(s32 a, s32 b)
{
  return a < 0 ? -((-a + b / 2) / b) : (a + b / 2) / b;
}

static int Min(int a, int b) { return a < b ? a : b; }
static int Max(int a, int b) { return a > b ? a : b; }
static int Sgn(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
static int Abs(int v) { return v < 0 ? -v : v; }

static void Inc8(u8 *v) { if (*v < 255) (*v)++; }
static void Dec8(u8 *v) { if (*v) (*v)--; }

static s16 Clamp16(s32 v)
{
  return (s16)(v > 32000 ? 32000 : (v < -32000 ? -32000 : v));
}

static void AddMk(int n)    { Mk = Clamp16((s32)Mk + n); }
static void AddScore(int n) { Score = Clamp16((s32)Score + n); }

static void Hurt(u8 cause, s32 amount)
{
  Killer = cause;
  Voima -= amount;
}

static u8 Has(u8 e) { return Equip[e]; }

/* ===========================================================================
   Random numbers: 32-bit LCG, Random(n) in [0, n) from the high 16 bits
   =========================================================================== */
static u32 RandSeed = 1;

void Randomize(u32 seed)
{
  RandSeed = seed & 0xFFFFFFFFUL;
}

int Random(s32 n)
{
  if (n <= 0) return 0;
  if (n > 32767) n = 32767;
  RandSeed = (RandSeed * 134775813UL + 1UL) & 0xFFFFFFFFUL;
  return (int)(((RandSeed >> 16) * (u32)n) >> 16);
}

/* ===========================================================================
   Messages and prompts
   =========================================================================== */
static void SyncLast(void)
{
  if (MsgLine[0]) StrCopy(LastMsg, MsgLine, MSGMAX);
}

static void DrawMsg(void)
{
  u8 n = (u8)strlen(MsgLine);
  if (n > MSGW - 1) n = MSGW - 1;
  SFill(0, 0, SCRW - 1, 1);
  SPutN(0, 0, MsgLine, MSGW, A_NORM);
  CurRow = (u8)(n / SCRW);
  CurCol = (u8)(n % SCRW);
}

static void SetMsg(const char *s)
{
  StrCopy(MsgLine, s, MSGMAX);
  SyncLast();
}

/* append to the message line as is */
static void MsgCat(const char *s)
{
  StrCat(MsgLine, s, MSGMAX);
  SyncLast();
}

/* adds a message to this turn's message line (joined when it fits) */
static void Msg(const char *s)
{
  if (!s[0]) return;
  if (MsgLine[0] && strlen(MsgLine) + 1 + strlen(s) <= MSGW) {
    MsgCat(" ");
    MsgCat(s);
  } else SetMsg(s);
}

static void Prompt(u8 slow)
{
  /* the original's column 73 of the 80-column message line */
  SFill(32, 1, SCRW - 1, 1);
  SPut(32, 1, "<lis" AE AE ">", A_BOLD);
  CurRow = 1;
  CurCol = SCRW - 1;
  if (slow) ReadKeyAfterPause(1000); else ReadKey();
  SFill(32, 1, SCRW - 1, 1);
}

/* "<lisää>" prompt (original 0x2E82) */
static void PressAny(void) { Prompt(0); }

/* the message line `q` (may be MsgLine itself) and one key */
static int AskKey(const char *q)
{
  int k;
  if (q != MsgLine) StrCopy(MsgLine, q, MSGMAX);
  DrawMsg();
  CursorOn = 1;
  k = ReadKey();
  CursorOn = 0;
  return k;
}

/* AskKey until one of the keys in `valid` (ESC always allowed) */
static int AskOneOf(const char *q, const char *valid)
{
  int a;
  do {
    a = AskKey(q);
  } while (a != 27 && (a == 0 || !strchr(valid, a)));
  return a;
}

static u8 AskYesNo(const char *q)
{
  int a = AskOneOf(q, "kKeEyYnN");
  return a == 'k' || a == 'K' || a == 'y' || a == 'Y';
}

static u8 AskKE(const char *q)
{
  int a = AskOneOf(q, S_YN);
  return a == 'k' || a == 'K';
}

/* ===========================================================================
   Rendering: a 40-column window onto world columns ViewX..ViewX+39
   =========================================================================== */
/* screen code and colour of tiles $00..$36 (the CP437 glyphs of the
   original; the symbols PETSCII lacks are custom characters $80..) */
static const u8 TileScr[T_KARKURI + 1] = {
  0x20, 0x25, 0x26, C_SIGMA, 0x21, C_BSLASH, C_SZ, C_THETA,      /* $00 */
  0x20, 0x20, 0x20, C_PHI, 0x24, C_IQUEST, C_HOUSE, C_INF,       /* $08 */
  0x0F, C_YEN, 0x28, 0x20, 0x2E, 0x3E, C_APPROX, C_AE_LIG,       /* $10 */
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x43, 0x43,                /* $18 */
  0x43, 0x43, 0x43, 0x4D, 0x4B, 0x56, 0x54, C_OMEGA,             /* $20 */
  0x4E, 0x48, 0x51, 0x52, 0x49, 0x42, 0x44, 0x47,                /* $28 */
  0x4F, 0x53, 0x41, 0x50, C_NTILDE, 0x4A, 0x45                   /* $30 */
};
static const char AttrStr[] =
  "04477777000" "4368437408359" "000000" "55555" "66" "2" "6" "2" "6666"
  "22222222222";

#define SC_GRAVE  0x5B     /* ┼ */
#define SC_FENCE  0x23     /* # */
#define SC_VLINE  0x5D     /* │ */
#define SC_PLAYER 0x00     /* @ */
#define VIEW_MARGIN 12

static u8 ViewX = WORLD_X0;

/* keeps the player VIEW_MARGIN columns from the window edges */
static void UpdateView(void)
{
  int v = ViewX;
  if (PX < v + VIEW_MARGIN) v = PX - VIEW_MARGIN;
  else if (PX > v + SCRW - 1 - VIEW_MARGIN) v = PX - (SCRW - 1 - VIEW_MARGIN);
  if (v < WORLD_X0) v = WORLD_X0;
  if (v > VIEW_MAX) v = VIEW_MAX;
  ViewX = (u8)v;
}

/* world-coordinate text, clipped to the window */
static void WPut(int x, u8 y, const char *s, u8 at)
{
  u8 *sp = SCREEN + Row40[y], *cp = COLRAM + Row40[y];
  u8 c = Colour[at];
  int sc = x - ViewX;
  for (; *s && sc < SCRW; s++, sc++)
    if (sc >= 0) {
      sp[sc] = ToScr[(u8)*s];
      cp[sc] = c;
    }
}

static void WVLine(int x, u8 r0, u8 r1, const char *g)
{
  for (; r0 <= r1; r0++) WPut(x, r0, g, A_CYAN);
}

static void DrawFrameRow(u8 row, u8 left, u8 right)
{
  u8 sc;
  int x = ViewX;
  u8 *sp = SCREEN + (u16)row * SCRW, *cp = COLRAM + (u16)row * SCRW;
  for (sc = 0; sc < SCRW; sc++, x++) {
    sp[sc] = x == WORLD_X0 ? left : x == WORLD_X1 ? right : 0x40;
    cp[sc] = Colour[A_CYAN];
  }
  /* arrows: more street beyond the window edge */
  if (ViewX > WORLD_X0) SPutScr(0, row, 0x94, A_YELLOW);
  if (ViewX < VIEW_MAX) SPutScr(SCRW - 1, row, 0x95, A_YELLOW);
}

static void DrawFrame(void)
{
  DrawFrameRow(FRAME_TOP, 0x70, 0x6E);     /* ┌ ┐ */
  DrawFrameRow(FRAME_BOT, 0x6D, 0x7D);     /* └ ┘ */
}

/* building glyphs and names (map renderer file 0x3A80..0x404C) */
static void DrawCityscape(void)
{
  const u8 N = A_WHITE;
  /* top-left: ISKU / ALCO */
  WVLine(20, 3, 5, G_HALF);
  WPut(14, 6, G_UPPR G_UPPR G_SHAD G_SHAD G_UPPR G_UPPR G_UPPR, A_CYAN);
  WPut(15, 4, FlagAlco ? "ALCO" : "ISKU", N);
  /* top-middle: SAIRAALA / HOTELLI */
  WVLine(34, 3, 6, G_FULL);
  WVLine(55, 3, 6, G_FULL);
  WPut(35, 6, G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR
              G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR G_LOWR
              G_LOWR G_LOWR, A_CYAN);
  WPut(44, 6, G_TEE G_TEE, A_CYAN);
  WPut(41, 4, HotelName ? "HOTELLI" : "SAIRAALA", N);
  /* bottom-left (DIVARI) */
  WPut(14, 19, G_UPPR G_UPPR G_UPPR G_SHAD G_UPPR G_UPPR G_FULL, A_CYAN);
  WVLine(20, 20, 22, G_FULL);
  if (FlagDivari) WPut(14, 21, "DIVARI", N);
  /* bottom-middle and bottom-right */
  WPut(43, 19, G_FULL G_UPPR G_UPPR G_UPPR G_SHAD G_UPPR G_UPPR G_UPPR G_FULL, A_CYAN);
  WPut(54, 19, G_FULL G_UPPR G_UPPR G_UPPR G_SHAD G_UPPR G_UPPR G_UPPR G_FULL, A_CYAN);
  WVLine(43, 20, 22, G_FULL);
  WVLine(51, 20, 22, G_FULL);
  WVLine(54, 20, 22, G_FULL);
  WVLine(62, 20, 22, G_FULL);
  if (FlagPoliisi) {
    WPut(44, 21, "POLIISI", N);
    WPut(55, 21, "VANKILA", N);
  }
  /* top-right alcove wall (flag [0x2E9]) */
  if (FlagAlcove) WVLine(76, 3, 5, G_HALF);
}

/* screen code and C64 colour of every tile code, built once; DrawRow
   (drawrow.s) copies one row of the window with them */
u8 TS[T_COUNT], TC[T_COUNT];
const u8 *DrSrc;
u8 *DrScr, *DrCol;
u8 DrN;
void DrawRow(void);

static void BuildTileTables(void)
{
  u8 t;
  for (t = 0; t < T_COUNT; t++) {
    TS[t] = 0x20;
    TC[t] = Colour[A_NORM];
  }
  for (t = 0; t <= T_KARKURI; t++) {
    TS[t] = TileScr[t];
    TC[t] = Colour[AttrStr[t] - '0'];
  }
  TS[T_GRAVE] = SC_GRAVE; TC[T_GRAVE] = Colour[A_DIM];
  TS[T_FENCE] = SC_FENCE; TC[T_FENCE] = Colour[A_YELLOW];
  TC[T_GHOST] = Colour[A_DIM];
}

static void DrawMap(void)
{
  u8 *sp, *cp;
  const u8 *mp;
  u8 y, i, i0, i1, t;
  UpdateView();
  DrawFrame();
  if (!TC[T_FENCE]) BuildTileTables();
  /* "Aaveita et selvänä näe" -- drunk eyes see the ghosts */
  TS[T_GHOST] = Prom >= PI10(20) ? C_DEGREE : 0x20;
  /* the window: screen columns i0..i1 show map cells, the others the
     frame sides */
  i0 = ViewX == WORLD_X0 ? 1 : 0;
  i1 = ViewX == VIEW_MAX ? SCRW - 2 : SCRW - 1;
  sp = SCREEN + MAP_Y0 * SCRW;
  cp = COLRAM + MAP_Y0 * SCRW;
  mp = Map + (ViewX + i0 - MAP_X0);
  for (y = MAP_Y0; y <= MAP_Y1; y++) {
    if (i0) { sp[0] = SC_VLINE; cp[0] = Colour[A_CYAN]; }
    if (i1 < SCRW - 1) { sp[SCRW - 1] = SC_VLINE; cp[SCRW - 1] = Colour[A_CYAN]; }
    if (Blind) memset(sp + i0, 0x20, i1 - i0 + 1);
    else {
      DrSrc = mp;
      DrScr = sp + i0;
      DrCol = cp + i0;
      DrN = (u8)(i1 - i0 + 1);
      DrawRow();
    }
    sp += SCRW;
    cp += SCRW;
    mp += MAPW;
  }
  if (Friendly && !Blind) {
    /* hallucinated letters for every creature in view */
    sp = SCREEN + MAP_Y0 * SCRW;
    mp = Map + (ViewX + i0 - MAP_X0);
    for (y = MAP_Y0; y <= MAP_Y1; y++, sp += SCRW, mp += MAPW)
      for (i = i0; i <= i1; i++) {
        t = mp[i - i0];
        if (t >= T_POLICE1 && t <= T_KARKURI) sp[i] = (u8)(0x41 + Random(10));
      }
  }
  if (!Blind) DrawCityscape();
  SPutScr((u8)(PX - ViewX), PY, SC_PLAYER, A_WHITE);
}

/* status row: Taso Mk Voima Kokemus Promille; the turn counter and the
   state text are written into the frame */
static char StatBuf[48];

static void Field(const char *label, const char *val)
{
  StrCat(StatBuf, label, sizeof(StatBuf));
  StrCat(StatBuf, val, sizeof(StatBuf));
}

static void DrawStatus(void)
{
  s32 p = RoundDiv(Prom, PS / 10);               /* tenths */
  StatBuf[0] = 0;
  Field("Taso:", NumStr(Level, 0));
  Field(" Mk:", NumStr(Mk, 0));
  Field(" Voima:", NumStr(RoundDiv(Voima, VS), 0));
  Field(" Kok:", NumStr(Kokemus, 0));
  Field(" Prom:", p < 0 ? "-" : "");
  if (p < 0) p = -p;
  Field(NumStr(p / 10, 0), ".");
  Field(NumStr(p % 10, 0), "");
  SFill(0, STAT_ROW, SCRW - 1, STAT_ROW);
  SPutN(0, STAT_ROW, StatBuf, SCRW, A_NORM);
  SPut(SCRW - 7, FRAME_TOP, NumStr((s32)(Turn > 99999UL ? 99999UL : Turn), 5), A_DIM);
  if (StateText[0] != ' ') SPut(2, FRAME_BOT, StateText, A_BOLD);
}

static void FullRedraw(void)
{
  DrawMap();
  DrawStatus();
  DrawMsg();
  if (!MsgLine[0]) {
    CurCol = (u8)(PX - ViewX);
    CurRow = PY;
  }
}

/* ===========================================================================
   Map helpers
   =========================================================================== */
static u8 InMap(int x, int y)
{
  return x >= MAP_X0 && x <= MAP_X1 && y >= MAP_Y0 && y <= MAP_Y1;
}

static u8 Tile(int x, int y)
{
  return InMap(x, y) ? M(x, y) : (u8)T_WALL;
}

static void SetTile(int x, int y, u8 t)
{
  if (InMap(x, y)) M(x, y) = t;
}

/* empty floor and not the player's own cell */
static u8 IsFree(int x, int y)
{
  return Tile(x, y) == T_FLOOR && !(x == PX && y == PY);
}

/* memchr is assembler in cc65: about ten times faster than a C loop */
static u8 FindTile(u8 t, int *fx, int *fy)
{
  const u8 *p = (const u8 *)memchr(Map, t, sizeof(Map));
  u16 i;
  if (!p) return 0;
  i = (u16)(p - Map);
  *fx = MAP_X0 + i % MAPW;
  *fy = MAP_Y0 + i / MAPW;
  return 1;
}

static u8 Exists(u8 t)
{
  int x, y;
  return FindTile(t, &x, &y);
}

/* The punk gang (0xA1AC..0xA476) is a "<"-shaped block of five tiles
   around the king at (KingX, KingY):  P P
                                         Ñ     (the mouth at KingX-1 is open)
                                       P P                                  */
static const s8 PUNK_DX[5] = { 0, -1, 0, -1, 0 };
static const s8 PUNK_DY[5] = { 0, -1, -1, 1, 1 };

static void PunkStamp(void)
{
  u8 i, *c;
  int x, y;
  for (i = 0; i < 5; i++) {
    x = KingX + PUNK_DX[i];
    y = KingY + PUNK_DY[i];
    if (!InMap(x, y) || (x == PX && y == PY)) continue;
    c = &M(x, y);
    PunkUnder[i] = (*c == T_PUNK || *c == T_KINGI) ? (u8)T_FLOOR : *c;
    *c = i ? T_PUNK : T_KINGI;
  }
}

/* the original wrote blanks over the gang's old cells; we put back what
   was under it so a passing gang cannot make the level unfinishable */
static void PunkErase(void)
{
  u8 i, t;
  int x, y;
  for (i = 0; i < 5; i++) {
    x = KingX + PUNK_DX[i];
    y = KingY + PUNK_DY[i];
    t = Tile(x, y);
    if (t == T_PUNK || t == T_KINGI) M(x, y) = PunkUnder[i];
  }
}

/* random empty cell in [x0, x0+xn) x [y0, y0+yn) */
static u8 RandomCell(int x0, int xn, int y0, int yn, u8 allowDot, int *rx, int *ry)
{
  u16 tries = 2000;
  u8 ok;
  do {
    *rx = x0 + Random(xn);
    *ry = y0 + Random(yn);
    tries--;
    ok = IsFree(*rx, *ry) ||
         (allowDot && Tile(*rx, *ry) == T_DOT && !(*rx == PX && *ry == PY));
  } while (!ok && tries > 0);
  return ok;
}

/* placement helper 0x2DF3: x=23+Random(56), y=7+Random(12) */
static void Place(u8 t)
{
  int x, y;
  if (RandomCell(PLACE_X0, 56, PLACE_Y0, 12, 0, &x, &y)) M(x, y) = t;
}

static void PlaceHP(u8 t, int hp)
{
  Place(t);
  HP(t) = (s8)hp;
}

/* ===========================================================================
   Equipment / weapon level
   =========================================================================== */
/* weapon level [0x2F6]: Ketjut 5, Stiletti 3, Pamppu 2, +1 with Camel */
void RecomputeWeapon(void)
{
  Weapon = Has(E_KETJUT) ? 5 : Has(E_STILETTI) ? 3 : Has(E_PAMPPU) ? 2 : 0;
  if (Has(E_CAMEL)) Weapon++;
}

/* Random(2) * Random(2) = 1, i.e. a one-in-four chance */
static u8 Lucky(void)
{
  int a = Random(2);
  return (u8)(a & Random(2));
}

/* a won fight may raise Kokemus (only below the creature's cap) and gives
   extra Voima */
static void KillBonus(u16 cap)
{
  if (Lucky() && Kokemus < cap) {
    Kokemus++;
    Voima += VI(Random(Min(Level, 8)) + 1);
  }
}

/* ===========================================================================
   Level generation
   =========================================================================== */
static void WallRect(int x0, int x1, int y0, int y1)
{
  int x;
  for (; y0 <= y1; y0++)
    for (x = x0; x <= x1; x++) M(x, y0) = T_WALL;
}

/* fixed wall template, identical on every level */
static void BuildWalls(void)
{
  memset(Map, T_FLOOR, sizeof(Map));
  WallRect(20, 20, 3, 5);
  WallRect(14, 20, 6, 6);
  WallRect(34, 55, 3, 6);
  WallRect(14, 20, 19, 19);
  WallRect(20, 20, 20, 22);
  WallRect(43, 51, 19, 22);
  WallRect(54, 62, 19, 22);
}

/* the dog and the dot-eater are only created by the level generator: once
   a map scan has not found them, the next turns do not scan again */
#define GONE_DOG    1
#define GONE_PIRKKO 2
static u8 Gone;

static void ResetLevelState(void)
{
  Gone = 0;
  memset(HPt, 0, sizeof(HPt));
  PoliceActive = PortsariOn = PunksOn = Blind = 0;
  DotsGot = 0;
  PX = START_X;
  PY = START_Y;
  BuildWalls();
}

/* level 1 is fixed (byte-identical in every run of the original) */
static const u8 Level1[] = {
  52, 10, T_POLICE1,     59, 10, T_KALJA,      77, 11, T_MUMMO,
  65, 13, T_POLICE1 + 1, 61, 14, T_HINT,       64, 14, T_DOT,
  26, 15, T_MAKKARA,     46, 18, T_POLICE1 + 2, 51, 18, T_STILETTI,
  EXIT_X, EXIT_Y, T_EXIT
};

static void BuildLevel1(void)
{
  u8 i;
  for (i = 0; i < sizeof(Level1); i += 3) M(Level1[i], Level1[i + 1]) = Level1[i + 2];
  HP(T_POLICE1) = HP(T_POLICE1 + 1) = HP(T_POLICE1 + 2) = 3;
  DotsNeeded = 1;
  FlagAlco = FlagDivari = FlagPoliisi = 0;
  FlagGrave = FlagZoo = FlagAlcove = HotelName = 0;
}

/* levels 13 and 31 (0x2F32): graves, a ghost and a priest */
static void BuildGraveyard(void)
{
  int x, y;
  for (x = 28; x <= 72; x++)
    for (y = 9; y <= 17; y++)
      if (M(x, y) == T_FLOOR && x % 3 == 1 && y % 2 == 1 && Random(3) > 0)
        M(x, y) = T_GRAVE;
  PlaceHP(T_GHOST, 10 + Random(8));
  PlaceHP(T_PAPPI, 3 + Random(Level));
}

/* levels 17 and 25 (0x308A): electric fences, a gorilla, a hippie */
static void BuildZoo(void)
{
  int x, y;
  for (x = 27; x <= 75; x++)
    for (y = 8; y <= 17; y++)
      if (M(x, y) == T_FLOOR && Random(9) == 0) M(x, y) = T_FENCE;
  PlaceHP(T_GORILLA, 10 + Random(7));
  PlaceHP(T_HIPPI, 7 + Random(7));
}

/* make sure every dot can be reached from the start: graves and fences are
   permanent obstacles, so a dot boxed in by them is moved.  The flood fill
   marks reached cells with bit 7 of the map byte (tile codes are < $80),
   so it needs no extra memory. */
#define SEEN 0x80

/* the row routines are in flood.s: in C they took several seconds */
u8 *FrCur, *FrNb;
u8 AlongRow(void);
u8 FromRow(void);

static void FixReachability(void)
{
  int rx, ry;
  u8 grew, y, *row;
  u16 i, tries;
  M(START_X, START_Y) |= SEEN;
  /* 8-connected flood fill as row sweeps, down and up, until nothing new
     is reached (the C89 port swept cell by cell: too slow on a 6502) */
  do {
    grew = 0;
    for (y = 0; y < MAPH; y++) {
      FrCur = row = Map + MapRow[y];
      FrNb = row - MAPW;
      if (y > 0) grew |= FromRow();
      grew |= AlongRow();
    }
    for (y = MAPH - 1; y-- > 0; ) {
      FrCur = row = Map + MapRow[y];
      FrNb = row + MAPW;
      grew |= FromRow();
      grew |= AlongRow();
    }
  } while (grew);
  /* an unreached dot is moved to a random reachable empty cell (the same
     random draws as RandomCell + "until seen" in the Pascal version) */
  for (i = 0; i < MAPW * MAPH; i++)
    if (Map[i] == T_DOT) {                       /* a dot without SEEN */
      Map[i] = T_FLOOR;
      tries = 30000;
      do {
        rx = PLACE_X0 + Random(56);
        ry = PLACE_Y0 + Random(12);
      } while ((M(rx, ry) != (SEEN | T_FLOOR) || (rx == PX && ry == PY)) && --tries);
      if (tries) M(rx, ry) = SEEN | T_DOT;
    }
  for (i = 0; i < MAPW * MAPH; i++) Map[i] &= (u8)~SEEN;
}

/* the creature roll table of the level generator, in the original order */
static void BuildCreatures(void)
{
  int n;
  if (Random(10) == 1 && Level > 1) {
    /* punk gang around (19,12), the king in the middle */
    KingX = 19; KingY = 12;
    PunkStamp();
    n = Level - Random(Level);
    HP(T_KINGI) = (s8)(n + Random(10));
    PunksOn = 1;
  }
  if (Random(10) == 9 && Level > 7) PlaceHP(T_SKIN, 4 + Random(10 + Level / 2));
  if (!PunksOn && Level > 1 && Random(10) > 7) M(16, 12) = T_KAKE;
  if (Level > 1 && Random(100) > 77) PlaceHP(T_PIRKKO, 0);  /* banked dots */
  if (Random(10) == 4 && Level > 3) PlaceHP(T_SNAKE, 4 + Random(Level));
  if (Level > 3 && Random(7) == 4) PlaceHP(T_AIDS, Min(Level, 8));
  if (Level > 1 && Random(100) < 23 && !Exists(T_PIRKKO)) {
    M(PORTSARI_X, PORTSARI_Y) = T_PORTSARI;
    HP(T_PORTSARI) = (s8)Min(Level, 9);
    PortsariOn = 1;
  }
  if (Mk > 0 && Level > 1 && Random(10) > 7) PlaceHP(T_VARAS, Min(Level, 13));
  if (Mk > 30 && Level > 2 && Random(5) == 1) Place(T_TEEKKARI);
  if (Level > 2 && Random(5) == 3)
    PlaceHP(T_DOG, Max(Min((int)TruncDiv(Voima, 2 * VS), 8), 2));
  if (Level > 3 && Random(16) > 10) Place(T_NEEKERI);
  if (Level > 4 && Random(18) > 12) Place(T_HINTTI);
  if (Level > 10 && Random(18) > 13 && !Exists(T_PAPPI))
    PlaceHP(T_PAPPI, Min(Level - Random(Level), 6));
  if (Level > 2 && Random(7) == 6 && !Exists(T_HIPPI))
    PlaceHP(T_HIPPI, Max(Min(Level - Random(Level), 8), 1));
  if (Level > 3 && Mk > 50 && Random(21) == 13) Place(T_TROKARI);
  if (Random(Level) > 4) PlaceHP(T_JUPPI, 6 + Random(Level / 2 + 1));
  if (FlagPoliisi && Level > 5 && Random(4) == 1)
    PlaceHP(T_KARKURI, 5 + Min(Random(Level), 12));
}

#define ITEM_GT 0x80
static const u8 Items[] = {
  10, 9, T_MONEY,         21, 6, T_CAMEL,         10, ITEM_GT | 7, T_LONKKA,
  10, ITEM_GT | 7, T_HINT, 10, ITEM_GT | 6, T_KALJA, 20, 6, T_MILK,
  30, 21, T_CHOCO,        20, 11, T_APPLE,        10, 6, T_SILAKKA,
  10, 9, T_MAKKARA,       10, 5, T_POOP,          20, 19, T_STILETTI,
  21, 20, T_HAALARI,      30, 19, T_KETJUT
};

/* level generator (file 0x4216..0x4D20).  Called after Level was raised. */
static void BuildLevel(void)
{
  int i, x, y, n;
  ResetLevelState();
  if (Level == 1) { BuildLevel1(); return; }
  M(EXIT_X, EXIT_Y) = T_EXIT;

  FlagGrave   = Level == 13 || Level == 31;
  FlagZoo     = Level == 17 || Level == 25;
  FlagAlco    = Random(100) + Level > 90;
  FlagDivari  = Random(100) < (int)Level;
  FlagPoliisi = Random(Level) > 2 && !FlagGrave && !FlagZoo;
  FlagAlcove  = Random(Level) > 3;
  HotelName   = Random(2) == 1;
  if (FlagAlcove) WallRect(76, 76, 3, 5);

  if (FlagGrave) BuildGraveyard();
  if (FlagZoo) BuildZoo();
  if (Level >= MAX_LEVEL && Has(E_APY)) {
    /* the cognac (0x322F) + a guarding ghost.  The original tested slot 8
       for Äpy, which is stored in slot 10, so v1.17 never had a cognac.
       Here the intended rule: level 50 with Äpy bought from a Teekkari. */
    M(KONJAKKI_X, KONJAKKI_Y) = T_KONJAKKI;
    PlaceHP(T_GHOST, 11 + Random(10));
  }

  /* police: slots $1E .. $1E+Random(5) */
  n = T_POLICE1 + Random(5);
  for (i = T_POLICE1; i <= n; i++) PlaceHP((u8)i, 2 + Min(Random(Level) + 1, 18));
  PlaceHP(T_MUMMO, Min(Random(Level) - 1, 10));

  /* dots: one per level number */
  for (i = 1; i <= Level; i++) Place(T_DOT);

  /* items: Random(n) == v, or Random(n) > v when ITEM_GT is set; overalls,
     boots and chains only when not carried */
  for (i = 0; i < (int)sizeof(Items); i += 3) {
    n = Random(Items[i]);
    if ((Items[i + 1] & ITEM_GT) ? n > (Items[i + 1] & 0x7F) : n == Items[i + 1])
      if (!((Items[i + 2] == T_HAALARI || Items[i + 2] == T_CAMEL ||
             Items[i + 2] == T_KETJUT) && Has(Items[i + 2])))
        Place(Items[i + 2]);
  }
  /* a puddle now and then ("Puli juo lammikon kuiviin!") */
  if (Random(6) == 0 && RandomCell(PLACE_X0, 50, PLACE_Y0, 10, 0, &x, &y)) {
    n = Random(3) + 1;
    for (i = 0; i <= n; i++)
      if (IsFree(x + i, y)) M(x + i, y) = T_WATER;
  }

  BuildCreatures();
  FixReachability();
  /* the exit opens when every dot placed here has been eaten */
  DotsNeeded = 0;
  for (i = 0; i < MAPW * MAPH; i++)
    if (Map[i] == T_DOT) DotsNeeded++;
}

/* level init (file 0x4216..0x4412): Level+1, Score+1, Voima+1 */
static void NextLevel(void)
{
  Level++;
  AddScore(1);
  Voima += VI(1);
  if (Friendly && FriendlyTurns <= 0) Friendly = 0;
  BuildLevel();
}

/* ===========================================================================
   Death
   =========================================================================== */
static void Die(const char *cause)
{
  if (Dead) return;
  Dead = 1;
  DeathCause = cause;
}

/* ===========================================================================
   Hint pager and help files (streamed from disk a line at a time)
   =========================================================================== */
static char Line[LINEMAX];

static u8 IsBlank(const char *s)
{
  while (*s == ' ') s++;
  return *s == 0;
}

/* a random non-blank line of SPU.TIP (left in Line) */
static const char *ReadTip(void)
{
  u16 n = 0, k;
  if (TextOpen("SPU.TIP")) {
    while (TextLine(Line))
      if (!IsBlank(Line)) n++;
    if (n) {
      k = (u16)Random(n);
      TextRewind();
      while (TextLine(Line))
        if (!IsBlank(Line) && k-- == 0) break;
    }
    TextClose();
  }
  return n ? Line : "Kippis!";
}

/* two-page pager (0x5186) */
static void HintPager(void)
{
  SetMsg("Ruumiisi alkaa vavahdella ja kuulet taivaallisen " AE AE "nen!");
  DrawMsg();
  PressAny();
  SetMsg("Se sanoo: ");
  MsgCat(ReadTip());
  DrawMsg();
  PressAny();
  MsgLine[0] = 0;
}

static const char *const HelpLines[] = {
  "",
  "  KOMENNOT:",
  "",
  " hjklyubn liiku (my" OE "s nuolet/joystick)",
  " HJKLYUBN juokse, kunnes jokin est" AE AE,
  " q juo        e sy" OE "        t heit" AE,
  " i inventaario           . odota",
  " > seuraavalle tasolle   ? ohjeet",
  " / sepustus (mik" AE " merkki on mit" AE ")",
  " v versio     Q luovuta",
  "",
  " Versiosta 2.24 lis" AE "tyt:",
  " S talletus (peli jatkuu seuraavalla",
  "   k" AE "ynnistyskerralla)",
  " p viimeisin viesti   c kello",
  " CLR/HOME piirr" AE " ruutu uudelleen",
  "",
  " F1 = ohjeet, F3 = inventaario,",
  " RUN/STOP = peru",
  "",
  " Ovet: ALCO (17,7) myy juomia,",
  " DIVARI (17,18) ostaa tavaroita."
};
#define HELP_LINES (sizeof(HelpLines) / sizeof(HelpLines[0]))

/* help text source: SPU.HLP (or a "missing" line), a page break, the
   command summary */
static u8 HelpStage, HelpIdx;

static u8 HelpNext(char *l)
{
  switch (HelpStage) {
    case 0:
      if (TextLine(l)) return 1;
      HelpStage = 2;
      StrCopy(l, "<lis", LINEMAX);      /* forces a page break */
      return 1;
    case 1:
      HelpStage = 2;
      StrCopy(l, "spu.hlp puuttuu.", LINEMAX);
      return 1;
    case 2:
      HelpStage = 3;
      StrCopy(l, "<lis", LINEMAX);
      return 1;
    default:
      if (HelpIdx >= HELP_LINES) return 0;
      StrCopy(l, HelpLines[HelpIdx++], LINEMAX);
      return 1;
  }
}

/* pages the help lines; a "<lis" line forces a page break, RUN/STOP or q
   quits */
static void ShowHelp(void)
{
  u8 have, row;
  int k;
  HelpStage = TextOpen("spu.hlp") ? 0 : 1;
  HelpIdx = 0;
  have = HelpNext(Line);
  while (have) {
    SClear();
    for (row = 0; have && row < SCRH - 1; ) {
      if (strstr(Line, "<lis")) {
        have = HelpNext(Line);
        break;
      }
      SPutN(0, row++, Line, SCRW, A_NORM);
      have = HelpNext(Line);
    }
    SPut(31, SCRH - 1, "<lis" AE AE ">", A_BOLD);
    CurRow = SCRH - 1;
    CurCol = SCRW - 1;
    k = ReadKey();
    if (k == KEY_ESC || k == 'q') break;
  }
  TextClose();
  SClear();
}

/* '/' : "Sepusta mistä >" -- explain a map glyph from SPU.SPE */
static void Sepustus(void)
{
  int c = AskKey("Sepusta mist" AE " >");
  u8 found = 0;
  if (TextOpen("spu.spe")) {
    while (!found && TextLine(Line))
      found = Line[0] && (u8)Line[0] == (u8)c;
    TextClose();
  }
  SetMsg(found ? Line : "Eih" AE "n tommosia olekaan!");
}

/* ===========================================================================
   Inventory, drinking, eating
   =========================================================================== */
/* inventory panel (0x501C), drawn over the left edge of the map: drink
   counts, then entries 3..11 as 1..9 */
#define INV_ROW 3

static void DrawInventory(void)
{
  u8 i;
  char s[4];
  SFill(0, INV_ROW, 12, INV_ROW + 13);
  SPut(1, INV_ROW, "Kaljaa", A_NORM);
  SPut(10, INV_ROW, NumStr(InvKalja, 0), A_NORM);
  SPut(1, INV_ROW + 1, "Lonkkaa", A_NORM);
  SPut(10, INV_ROW + 1, NumStr(InvLonkka, 0), A_NORM);
  SPut(1, INV_ROW + 2, "Kiekuraa", A_NORM);
  SPut(10, INV_ROW + 2, NumStr(InvKiekura, 0), A_NORM);
  s[1] = '.'; s[2] = ' '; s[3] = 0;
  for (i = 3; i <= 11; i++) {
    s[0] = (char)('0' + i - 2);
    SPut(0, (u8)(INV_ROW + i), s, A_NORM);
    if (Has(i)) SPut(3, (u8)(INV_ROW + i), EquipName[i], A_NORM);
  }
}

static void DoInventory(void)
{
  DrawInventory();
  SPut(0, INV_ROW + 13, "PAINA RETURN", A_BOLD);
  CurRow = INV_ROW + 13;
  CurCol = 12;
  ReadKey();
}

/* consume routine 0x7E50: kalja +0.2, lonkka +0.3 promille */
static void DoDrink(void)
{
  int ch;
  if (InvKalja + InvLonkka == 0) {
    SetMsg("Valitan, sulla on juomiset lopussa.");
    return;
  }
  ch = InvKalja == 0 ? '2' : InvLonkka == 0 ? '1' :
       AskOneOf("Kumpaako kittaat, kaljaa vai lonkkua (1/2) >", "012");
  MsgLine[0] = 0;
  if (ch != '1' && ch != '2') return;
  if (ch == '1') { Dec8(&InvKalja); Prom += PI10(2); }
  else { Dec8(&InvLonkka); Prom += PI10(3); }
  SetMsg("Gluglugluglugluglu...");
}

static void DoEat(void)
{
  if (InvKiekura == 0)
    SetMsg("Et omista mit" AE AE "n ruoaksi kelpaavaa.");
  else {
    Dec8(&InvKiekura);
    Voima += VI(1);
    SetMsg("R" OE "yh...");
  }
}

/* ===========================================================================
   Shops and building doors
   =========================================================================== */
/* door (17,7) under the top-left building (0x7774..0x794D) */
static void AlcoShop(void)
{
  int ch, price;
  if (Prom > PI10(10)) {
    SetMsg("Teille ei myyd" AE ", te olette humalassa!");
    return;
  }
  if (Mk < 15 || !FlagAlco) {
    SetMsg("Putiikki vaikuttaa suljetulta.");
    return;
  }
  ch = AskOneOf("Ja mit" AE "h" AE "n herralle saisi olla? (1=kalja/2=lonkka) >", "012");
  MsgLine[0] = 0;
  if (ch != '1' && ch != '2') return;
  price = 5 * (ch - '0' + 1);
  Inc8(ch == '1' ? &InvKalja : &InvLonkka);
  AddMk(-price);
  SetMsg(NumStr(price, 0));
  MsgCat(" mk, kiitos.");
}

/* pawn shop door (17,18) (0x7993..0x7CC6) */
static void Divari(void)
{
  int ch, e, price = 0;
  DrawInventory();
  StrCopy(MsgLine, "Tervetuloa, ", MSGMAX);
  StrCat(MsgLine, PlayerName, MSGMAX);
  StrCat(MsgLine, ", mit" AE "s myisit? >", MSGMAX);
  ch = AskOneOf(MsgLine, "0123456789");
  DrawMap();
  MsgLine[0] = 0;
  if (ch < '1' || ch > '9') return;
  e = ch - '0' + 2;
  if (!Has((u8)e)) return;
  switch (e) {
    case E_HAALARI:
      Hurt(K_TUNTEMATON, VI(2));
      price = 100;
      AddScore(-(int)TruncDiv(Score, 10));
      break;
    case E_PAMPPU:   price = Random(50); break;
    case E_STILETTI: price = Random(80); break;
    case E_KETJUT:   price = Random(100); break;
    case E_CAMEL:
      price = Random(101);
      Hurt(K_TUNTEMATON, VI(1));
      break;
    case E_APY:      price = 10; break;
    case E_HENNESSY: price = 250; break;
  }
  SetMsg(EquipName[e]);
  MsgCat(" meni ");
  MsgCat(NumStr(price, 0));
  MsgCat(" markalla.");
  DrawMsg();
  PressAny();
  SetMsg("N" AE "kemiin kiitos!");
  Equip[e] = 0;
  AddMk(price);
  RecomputeWeapon();
}

/* building door handler (0x7774..0x7E0E); bumping costs 0.1 Voima */
static void BumpWall(void)
{
  if (PY == 7 && PX == 17) AlcoShop();
  else if (FlagAlco && PY == 7 && PX == 16) SetMsg("Toinen ovi, tollo!");
  else if (FlagDivari && PY == 18 && PX == 17) Divari();
  else if (PY == 7 && (PX == 44 || PX == 45)) {
    /* v2.24 sold an AIDS vaccine (69 mk); here the hospital treats sober
       paying customers */
    if (Aids && !HotelName && Prom <= PI10(10) && Mk >= 69) {
      AddMk(-69);
      Aids = 0;
      StateText = ST_BLANK;
      SetMsg("Saat l" AE AE "kekuurin, 69 mk. AIDS ei en" AE AE " paina.");
    } else SetMsg("Emme ota juoppoja!");
  }
  else if (PY == 18 && PX == 47) SetMsg("Ei ket" AE AE "n kotona.");
  else if (PY == 18 && PX == 58) SetMsg("Valitan, ei vapaita huoneita.");
  else {
    SetMsg("Isket p" AE AE "t" AE "si sein" AE AE "n, mielenkiintoista.");
    Hurt(K_KALLO, V_01);
  }
}

/* ===========================================================================
   Fighting (player walks into a creature, turn processor 0x607B..0x7774)
   =========================================================================== */
static void TeleportPirkko(int bx, int by)
{
  int rx, ry;
  if (RandomCell(22, 58, 3, 20, 0, &rx, &ry)) {
    SetTile(bx, by, T_FLOOR);
    SetTile(rx, ry, T_PIRKKO);
  }
}

/* the punk king falls (0x5195..0x5472): the gang leaves, loot drops */
static void PunkBlackout(void)
{
  u16 i;
  SetMsg("H" AE "n sammuu!");
  Msg(Voima > VI(5) ? "Pojat l" AE "htev" AE "t nukkumaan." : "Punkkarit luikkivat pakoon.");
  PunkErase();
  for (i = 0; i < MAPW * MAPH; i++)
    if (Map[i] == T_PUNK || Map[i] == T_KINGI) Map[i] = T_FLOOR;
  PunksOn = 0;
  AddScore(2);
  if (IsFree(PX + 1, PY)) {
    if (!Has(E_KETJUT)) M(PX + 1, PY) = T_KETJUT;
    else if (Random(2) == 1) M(PX + 1, PY) = T_MONEY;
  }
}

static void PoliceDrop(int x, int y)
{
  SetTile(x, y, !Has(E_PAMPPU) ? T_PAMPPU : Random(10) == 9 ? T_MONEY : T_FLOOR);
}

/* damage a creature; 1 when it drops (HP below one) */
static u8 Damage(u8 t, int d)
{
  int v = HP(t) - d;
  HP(t) = (s8)(v < -100 ? -100 : v);
  return HP(t) < 1;
}

/* the creature at (x,y) goes down: message, score, Voima, Kokemus, loot */
static void Fell(const char *m, int score, int voima, u8 cap, int x, int y, u8 drop)
{
  Msg(m);
  Voima += VI(voima);
  AddScore(score);
  if (cap) KillBonus(cap);
  SetTile(x, y, drop);
}

static void Attack(int x, int y)
{
  u8 t = M(x, y);
  int d = 1 + Weapon, n;

  SndPlay(SND_HIT);

  if (t >= T_POLICE1 && t <= T_POLICE5) {
    PoliceActive = 1;
    SetMsg("Huitaiset skoudea!");
    if (Prom > PI10(5) && Prom < PI10(15)) d++;   /* just tipsy enough */
    if (Damage(t, d)) {
      Msg("H" AE "n v" AE "s" AE "ht" AE AE "!");
      KillBonus(6);
      AddScore(1);
      PoliceDrop(x, y);
    }
    return;
  }

  /* hallucinating: these just get a friendly pat */
  if (Friendly) {
    const char *m = NULL;
    switch (t) {
      case T_HINTTI:  m = "Tapaat mukavan pojan!"; break;
      case T_GORILLA: m = "Sekoat elukkaan."; break;
      case T_SKIN:    m = "Taputat klania."; break;
      case T_DOG:     m = "Taputat hauvaa."; break;
    }
    if (m) { SetMsg(m); return; }
  }

  switch (t) {
    case T_MUMMO:
      if (HP(t) < d) {
        SetMsg("Isket mummon kumoon!");
        PoliceActive = 1;
        SetTile(x, y, T_MONEY);
      } else {
        SetMsg("El" AE "kel" AE "inen ly" OE " sinua k" AE "silaukullaan!");
        Damage(t, d);
        Hurt(K_MUMMO, VI(1));
      }
      break;
    case T_KAKE:
      SetMsg("Huitaiset Kakea.");
      if (InvKalja > 0) {
        Msg("H" AE "n p" OE "llii sinulta yhden kaljan.");
        Dec8(&InvKalja);
      }
      SetTile(x, y, T_FLOOR);
      break;
    case T_VARAS:
      SetMsg("Ly" OE "t varasta.");
      if (Damage(t, d)) Fell("Varas tuupertuu maahan.", 1, 1, 9, x, y, T_MONEY);
      else if (Mk > 0 && Random(3) == 0) {
        Msg("Lompakkosi kevenee!");
        AddMk(-Min(Mk, Random(4 * Level) + 1));
        SetTile(x, y, T_FLOOR);
      }
      break;
    case T_TEEKKARI:
      SetMsg("Ly" OE "t opiskelijaa.");
      break;
    case T_PORTSARI:
      SetMsg("Otat er" AE AE " poken kanssa!");
      Hurt(K_VAKIVALTA, VI(1 + Random(2)));
      break;
    case T_NEEKERI:
      PoliceActive = 1;
      SetMsg(Friendly ? "Tapaat vanhan is" AE "si!" : "Hakkaat neekeri" AE ".");
      if (Random(4) == 1) Fell("H" AE "n taintuu.", 0, 1, 0, x, y, T_MONEY);
      else Hurt(K_NEEKERI, VI(1));
      break;
    case T_HINTTI:
      PoliceActive = 1;
      if (Random(4) == 1) {
        SetMsg("Hakkaat homoa, joka luhistuu kadulle.");
        KillBonus(6);
        Voima += VI(1);
        AddScore(2);
        SetTile(x, y, T_POOP);
      } else SetMsg("Hakkaat homoa, joka nauttii siit" AE "!");
      break;
    case T_GORILLA:
      SetMsg("Hakkaat gorillaa.");
      if (Damage(t, d + Has(E_KETJUT))) Fell(S_UINAHTAA, 1, 0, 10, x, y, T_FLOOR);
      break;
    case T_SKIN:
      SetMsg("Hakkaat kaljua.");
      if (Damage(t, d)) {
        Fell(S_HAN_UIN, 2, 0, 0, x, y, T_HINT);
        if (Lucky() && Kokemus < 18) {
          Kokemus++;
          Voima += VI(3);
        }
      }
      break;
    case T_SNAKE:
      SetMsg("Isket luikertelijaa.");
      if (Damage(t, d)) Fell(S_UINAHTAA, 1, 0, 0, x, y, T_MAKKARA);
      break;
    case T_AIDS:
      SetMsg("Isket Sven-Olofia.");
      if (Damage(t, d)) Fell(S_HAN_UIN, 1, 0, 0, x, y, Random(4) == 2 ? T_CHOCO : T_FLOOR);
      break;
    case T_PAPPI:
      PoliceActive = 1;
      SetMsg("Ly" OE "t pappia mutta Jeesus suojelee h" AE "nt" AE "!");
      break;
    case T_HIPPI:
      SetMsg("Tempaiset narkkista.");
      if (Damage(t, d)) Fell("H" AE "n ottaa tripin!", 1, 0, 0, x, y, T_FLOOR);
      break;
    case T_TROKARI:
      SetMsg("Trokari v" AE "ist" AE AE ".");
      break;
    case T_PIRKKO:
      if (HP(t) > 0) {
        /* milk one banked dot back out of the dot-eater */
        int rx, ry;
        HP(t)--;
        if (RandomCell(25, 50, 3, 20, 0, &rx, &ry)) SetTile(rx, ry, T_DOT);
        MsgLine[0] = 0;
      } else {
        SetMsg("Tuhoat pistepirkon.");
        AddScore(1);
        SetTile(x, y, T_FLOOR);
      }
      break;
    case T_DOG:
      SetMsg("Hakkaat koiraa.");
      if (Damage(t, d + 2 * Has(E_KETJUT))) Fell("Hauveli uinahtaa.", 1, 0, 9, x, y, T_FLOOR);
      break;
    case T_PUNK:
      SetMsg("Satutat rystysesi niitteihin.");
      Hurt(K_PUNKKARIT, V_05);
      break;
    case T_KINGI:
      SetMsg("Sivallat punkkarien kingi" AE ".");
      if (Damage(t, d)) PunkBlackout();
      break;
    case T_JUPPI:
      SetMsg("Ly" OE "t juppia.");
      if (Damage(t, d)) Fell("Jupin kunto pett" AE AE ".", 2, 1, 11, x, y, T_MONEY);
      break;
    case T_KARKURI:
      SetMsg("Ly" OE "t vankikarkuria.");
      if (Damage(t, d)) Fell("Mies tuupertuu maahan.", 2, 1, 12, x, y, T_STILETTI);
      break;
    case T_GHOST:
      SetMsg("Huidot aavetta!");
      if (Damage(t, d))
        Fell("Aave luhistuu kasaan!", 2, 5, 0, x, y, Random(10) == 9 ? T_MAKKARA : T_FLOOR);
      break;
    case T_GRAVE:
      SetMsg("Eip" AE "s tallata hautoja!");
      Hurt(K_ILKEYS, V_05);
      /* "Älä tallaa hautoja, aaveet suuttuvat" */
      if (!Exists(T_GHOST) && Random(3) == 0) {
        n = 8 + Random(8);
        PlaceHP(T_GHOST, n);
      }
      break;
    case T_FENCE:
      MsgLine[0] = 0;
      break;
  }
}

/* ===========================================================================
   Player movement and pick-ups
   =========================================================================== */
/* weapon pick-up: equipment slot, "got it" and "already have" messages */
static void PickWeapon(u8 e, const char *got, const char *dud)
{
  if (!Has(e)) {
    SndPlay(SND_ITEM);
    Equip[e] = 1;
    RecomputeWeapon();
    SetMsg(got);
  } else SetMsg(dud);
}

static void PickUp(u8 t)
{
  int n;
  switch (t) {
    case T_DOT:
      SndPlay(SND_DOT);
      Inc8(&DotsGot);
      /* 0x57BA: only said while you are blind */
      if (Blind) SetMsg("Sokea kana l" OE "ysi jyv" AE "n!");
      break;
    case T_HAALARI:
      SndPlay(SND_ITEM);
      Equip[E_HAALARI] = 1;
      SetMsg("L" OE "yd" AE "t haalarit.");
      break;
    case T_PAMPPU:
      PickWeapon(E_PAMPPU, "Takavarikoit kyt" AE "n pampun.", "Maassa lojuu risa pamppu.");
      break;
    case T_STILETTI:
      PickWeapon(E_STILETTI, "Otat maassa lojuvan stiletin.",
                 "Maassa lojuu tyls" AE " voiveitsi.");
      break;
    case T_KETJUT:
      PickWeapon(E_KETJUT, "Korjaat ketjut talteen.",
                 "N" AE "et maassa katkenneet ketjut.");
      break;
    case T_CAMEL:
      if (!Has(E_CAMEL)) {
        AddScore(1);
        Voima += VI(1);
      }
      SndPlay(SND_ITEM);
      Equip[E_CAMEL] = 1;
      RecomputeWeapon();
      SetMsg("L" OE "yd" AE "t aidot Camel bootsit!");
      break;
    case T_KONJAKKI:
      SndPlay(SND_BOOZE);
      SetMsg("L" OE "yd" AE "t konjakkipullon ja korkkaat sen!");
      Voima += VI(3);
      Prom += PI10(5);
      Equip[E_HENNESSY] = 1;
      break;
    case T_MONEY:
      SndPlay(SND_CASH);
      n = Random(21);
      AddMk(n);
      SetMsg("Lompakkosi lihavoituu ");
      MsgCat(NumStr(n, 0));
      MsgCat(" markalla.");
      break;
    case T_HINT:
      SndPlay(SND_HINT);
      HintPager();
      break;
    case T_MILK:
      SetMsg("Kaadat maitokannun!");
      AddScore(-1);
      break;
    case T_CHOCO:
      SndPlay(SND_ITEM);
      SetMsg("Ahmit maahan pudonneen suklaalevyn!");
      Voima += VI(1);
      AddScore(1);
      break;
    case T_APPLE:
      n = AskKE("T" AE "ss" AE " on omena, vaan uskallatko sy" OE "d" AE "? (k/e)");
      MsgLine[0] = 0;
      if (!n) break;
      if (Random(3) == 0) {
        SetMsg("Omena oli pilaantunut!");
        AddScore(-1);
        Hurt(K_MYRKYTYS, VI(3));
      } else {
        SndPlay(SND_ITEM);
        SetMsg("Omena oli herkullinen!");
        Voima += VI(1);
      }
      break;
    case T_SILAKKA:
      SetMsg("Hyi h..i, mik" AE " t" AE AE "ll" AE " lemuaa!");
      Hurt(K_MYRKYTYS, V_01);
      break;
    case T_MAKKARA:
      SndPlay(SND_ITEM);
      Inc8(&InvKiekura);
      SetMsg("Kappas, kiekuralenkki.");
      break;
    case T_POOP:
      SndPlay(SND_POOP);
      SetMsg("Liukastut paskaan!");
      AddScore(-1);
      Hurt(K_KALLO, VI(1));
      break;
    case T_WATER:
      SndPlay(SND_WATER);
      if (!Has(E_HAALARI)) {
        SetMsg(S_HUKUT);
        Killer = K_HUKKUMINEN;
        Die(S_HUKUT);
      } else {
        SetMsg("Varpaasi kastuvat!");
        Hurt(K_VILUSTUMINEN, V_03);
      }
      break;
    case T_KALJA:
      SndPlay(SND_BOOZE);
      Inc8(&InvKalja);
      SetMsg("Miehen mittainen!");
      break;
    case T_LONKKA:
      SndPlay(SND_BOOZE);
      Inc8(&InvLonkka);
      SetMsg("1 Lonkka for the road!");
      break;
    default:
      MsgLine[0] = 0;
      break;
  }
}

/* Returns 1 if a turn was used.  A step off the map edge is silently
   ignored (the dispatcher's edge pre-check), everything else costs a turn.
   *drunk tells whether a drunk stagger replaced the step. */
static u8 TryMove(int dx, int dy, u8 *drunk)
{
  int nx, ny;
  u8 t;
  *drunk = Prom > PI10(30);
  if (*drunk) {
    /* drunk stagger (0x55A0): a random step instead of the intended one */
    dx = Random(3) - 1;
    dy = Random(3) - 1;
  }
  nx = PX + dx;
  ny = PY + dy;
  if (!InMap(nx, ny)) {
    if (*drunk) MsgLine[0] = 0;
    return *drunk;            /* a drunk stumble still wastes the turn */
  }
  if (dx == 0 && dy == 0) { MsgLine[0] = 0; return 1; }
  t = M(nx, ny);
  if (t == T_WALL) BumpWall();
  else if (t > 0x1D) Attack(nx, ny);
  else {
    /* walkable: move, then handle what was there */
    PX = (u8)nx;
    PY = (u8)ny;
    if (t != T_EXIT && t != T_WATER) M(nx, ny) = T_FLOOR;
    PickUp(t);
  }
  return 1;
}

/* ===========================================================================
   Throw (t) -- v1.17 spec (file 0x81A7..0x9290)
   =========================================================================== */
static const char DirKeys[] = "hjklyubn";
static const s8 DirDX[8] = { -1, 0, 0, 1, -1, 1, -1, 1 };
static const s8 DirDY[8] = { 0, 1, -1, 0, -1, -1, 1, 1 };

/* damage by key '1'..'9' (items 1..9) and 'a'..'c' (kalja, lonkka, kiekura) */
/* a byte table: cc65 would translate some escapes in a string (\12) */
static const u8 ThrowDmg[12] = { 1, 2, 3, 4, 2, 0, 0, 10, 3, 1, 1, 0 };

static void ThrowHit(int key, int dmg, int cx, int cy, u8 t);

static u8 DoThrow(void)
{
  int key, dkey, dx, dy, cx, cy;
  u8 t, item, *inv = NULL;
  const char *dp;
  DrawInventory();
  key = AskOneOf("Mit" AE " viskaat (1-9 tavara, a=kalja b=lonkka c=kiekura, 0=peru) >",
                 "0123456789abc");
  DrawMap();
  MsgLine[0] = 0;
  if (key == 27 || key == '0') return 0;
  if (key <= '9') {
    item = (u8)(key - '0' + 2);          /* the item's tile code = entry */
    if (!Has(item)) { SetMsg("Sinulla ei ole sellaista."); return 0; }
  } else {
    static const char *const NoneOf[3] = { "kaljaa.", "lonkkaa.", "kiekuraa." };
    static const u8 ItemOf[3] = { T_KALJA, T_LONKKA, T_MAKKARA };
    u8 *invs[3];
    invs[0] = &InvKalja; invs[1] = &InvLonkka; invs[2] = &InvKiekura;
    inv = invs[key - 'a'];
    if (*inv == 0) {
      SetMsg("Sinulla ei ole ");
      MsgCat(NoneOf[key - 'a']);
      return 0;
    }
    item = ItemOf[key - 'a'];
  }

  dkey = AskOneOf("Suunta >", ".hjklyubn");
  MsgLine[0] = 0;
  dp = (dkey != 27 && dkey != '.') ? strchr(DirKeys, dkey) : NULL;
  dx = dp ? DirDX[dp - DirKeys] : 0;
  dy = dp ? DirDY[dp - DirKeys] : 0;
  if (!dp || !InMap(PX + dx, PY + dy)) {
    SetMsg("Hassu suunta!");
    return 0;
  }

  /* the item leaves the inventory */
  if (inv) Dec8(inv);
  else {
    Equip[item] = 0;
    RecomputeWeapon();
    if (item > E_CAMEL) item = T_FLOOR;
  }

  /* flight: '*' trail over empty cells, stop at anything bigger than $1D */
  cx = PX; cy = PY;
  while (InMap(cx + dx, cy + dy)) {
    cx += dx; cy += dy;
    t = M(cx, cy);
    if (t > 0x1D) break;
    if (t == T_FLOOR) WPut(cx, (u8)cy, "*", A_YELLOW);
  }
  SleepMs(120);

  t = Tile(cx, cy);
  if (t <= 0x1D && t != T_FLOOR)
    SetMsg("Heittosi suhahtaa ilmaan!");
  else if (t == T_FLOOR) {
    if (item != T_FLOOR) SetTile(cx, cy, item);
  } else
    ThrowHit(key, ThrowDmg[key <= '9' ? key - '1' : key - 'a' + 9], cx, cy, t);
  return 1;
}

/* the thrown item hit creature t at (cx,cy): damage it, `m` the message,
   `drop` what is left when it falls */
static void ThrowDown(u8 t, int dmg, const char *m, int cx, int cy, u8 drop)
{
  const char *m2 = S_UINAHTAA;
  if (t == T_DOG) m2 = "Hauveli uinahtaa.";
  else if (t == T_SKIN) m2 = S_HAN_UIN;
  SetMsg(m);
  if (Damage(t, dmg)) Fell(m2, 0, 0, 0, cx, cy, drop);
}

static void ThrowHit(int key, int dmg, int cx, int cy, u8 t)
{
  SndPlay(SND_HIT);
  if (t >= T_POLICE1 && t <= T_POLICE5) {
    SetMsg("Heit" AE "t skoudea!");
    PoliceActive = 1;
    if (Damage(t, 1 + dmg)) {
      SetMsg("Poliisi p" OE "kertyy!");
      PoliceDrop(cx, cy);
    }
    return;
  }
  switch (t) {
    case T_MUMMO:
      SetTile(cx, cy, T_MONEY);
      SetMsg("Kolkkaat mummelin brutaalisti.");
      PoliceActive = 1;
      break;
    case T_KAKE:
      SetMsg("Kiusaat Kakea.");
      break;
    case T_VARAS:
      if (Damage(t, dmg)) {
        SetMsg("H" AE "n tuupertuu ketoon.");
        SetTile(cx, cy, T_MONEY);
      } else SetMsg("Osut varkaaseen.");
      break;
    case T_TEEKKARI:
      if (key == 'a' || key == 'b') {
        SetTile(cx, cy, T_FLOOR);
        SetMsg("Teekkari nappaa pullon lennosta ja katoaa.");
      } else SetMsg("Ohi meni!");
      break;
    case T_PORTSARI:
      if (Damage(t, dmg)) {
        SetMsg("H" AE "n sammahtaa.");
        SetTile(cx, cy, T_FLOOR);
        PortsariOn = 0;
      } else SetMsg("Osut portsariin.");
      break;
    case T_PAPPI:
      Damage(t, dmg);
      SetMsg("Osut pappismieheen.");
      PoliceActive = 1;
      break;
    case T_HIPPI:
      SetMsg("H" AE "n sammahtaa ja kolehtihaavi aukeaa.");
      SetTile(cx, cy, key == '3' ? T_STILETTI : T_MONEY);
      break;
    case T_TROKARI:
      if (key == '3') {
        /* "Lentävä veitsi pysäyttää trokarin" */
        SetMsg("Lent" AE "v" AE " veitsi pys" AE "ytt" AE AE " trokarin.");
        SetTile(cx, cy, T_KALJA);
      } else SetMsg("Trokari v" AE "ist" AE AE ".");
      break;
    case T_PUNK:
      SetMsg("Heittosi osuu punkkariin.");
      break;
    case T_KINGI:
      SetMsg("Heittosi osuu kingiin.");
      if (Damage(t, dmg)) PunkBlackout();
      break;
    case T_NEEKERI:
    case T_PIRKKO:
    case T_JUPPI:
      SetMsg("Ohi meni!");
      break;
    case T_KARKURI:
      SetMsg("Et osunut");
      break;
    case T_GORILLA:
      ThrowDown(t, dmg, "Viskaat gorillaa.", cx, cy, T_FLOOR);
      break;
    case T_SKIN:
      ThrowDown(t, dmg, "Viskaat klania.", cx, cy, T_HINT);
      break;
    case T_SNAKE:
      ThrowDown(t, dmg, "Viskaat k" AE AE "rmett" AE ".", cx, cy, T_MAKKARA);
      break;
    case T_AIDS:
      SetMsg("Osut potilaaseen.");
      break;
    case T_HINTTI:
      SetMsg("Hintti v" AE "ist" AE AE ".");
      break;
    case T_DOG:
      ThrowDown(t, dmg, "Viskaat rakkia.", cx, cy, T_FLOOR);
      break;
    default:
      SetMsg("Heittosi suhahtaa ilmaan!");
      break;
  }
}

/* ===========================================================================
   Creatures' turn (NPC update chain 0x9EC4..0xE0E0)
   =========================================================================== */
#define MAXCOPS 8

/* what the police do with the creature they walk into */
static const char *const Arrests[] = {
  "Poliisit viev" AE "t eukon turvaan.",          /* T_MUMMO */
  "Poliisit nappaavat kaverisi Kaken.",            /* T_KAKE */
  "Poliisit pid" AE "tt" AE "v" AE "t taskuvarkaan.", /* T_VARAS */
  NULL,                                             /* T_TEEKKARI */
  "Koppalakit nappaavat huijari-poken.",           /* T_PORTSARI */
  NULL,                                             /* T_NEEKERI */
  "Poliisi poistaa hintin.",                       /* T_HINTTI */
  "Koppalakit nappaavat trokarin.",                /* T_TROKARI */
  NULL,                                             /* T_PAPPI */
  "Poliisi vie hipin.",                            /* T_HIPPI */
  NULL,                                             /* T_PIRKKO */
  "Virkavalta poistaa vesikauhuisen hurtan.",      /* T_DOG */
  "Virkavalta poistaa karkulaisen.",               /* T_GORILLA */
  NULL, NULL, NULL, NULL, NULL, NULL,              /* T_SKIN..T_JUPPI */
  "Poliisit pid" AE "tt" AE "v" AE "t vankikarkurin." /* T_KARKURI */
};

/* police block (0xA7EC..0xB09F): asleep until a crime, then every officer
   walks one cell toward the player and beats him when adjacent */
static void UpdatePolice(void)
{
  u8 cx[MAXCOPS], cy[MAXCOPS], cv[MAXCOPS];
  u8 n = 0, p, t;
  int x, y, dx, dy, tx, ty, fx, fy, ox, oy;
  const u8 *mp = Map;
  if (!PoliceActive) return;
  for (y = MAP_Y0; y <= MAP_Y1; y++)
    for (x = MAP_X0; x <= MAP_X1; x++, mp++)
      if (*mp >= T_POLICE1 && *mp <= T_POLICE5 && n < MAXCOPS) {
        cx[n] = (u8)x; cy[n] = (u8)y; cv[n] = *mp;
        n++;
      }
  for (p = 0; p < n; p++) {
    ox = cx[p]; oy = cy[p];
    if (M(ox, oy) != cv[p]) continue;
    dx = PX - ox; dy = PY - oy;
    if (Abs(dx) < 2 && Abs(dy) < 2) {
      if (Friendly) Msg("Yst" AE "v" AE "llinen herra ojentaa sinulle kukkasen!");
      else {
        Msg("Poliisi pamputtaa!");
        Hurt(K_POLIISI, VI(1 + Random(3)));
      }
      continue;
    }
    tx = ox + Sgn(dx); ty = oy + Sgn(dy);
    if (!InMap(tx, ty)) { tx = ox; ty = oy; }
    fx = tx; fy = ty;
    t = M(tx, ty);
    if (t < T_DOT) {
      ;                                          /* walks over it */
    } else if (t == T_DOT || t == T_EXIT) {
      /* a dot is moved away; at the exit the officer jumps elsewhere */
      if (!RandomCell(20, 60, 3, 20, 0, &x, &y)) { fx = ox; fy = oy; }
      else if (t == T_DOT) M(x, y) = T_DOT;
      else { fx = x; fy = y; }
    } else if (t == T_PIRKKO) {
      TeleportPirkko(tx, ty);
      if (M(tx, ty) != T_FLOOR) { fx = ox; fy = oy; }
    } else if (t == T_TEEKKARI) {
      /* the officer swaps places with the student */
      M(ox, oy) = T_TEEKKARI;
      M(tx, ty) = cv[p];
      continue;
    } else if (t >= T_MUMMO && t <= T_KARKURI && Arrests[t - T_MUMMO]) {
      Msg(Arrests[t - T_MUMMO]);
      if (t == T_PORTSARI) PortsariOn = 0;
    } else {
      /* wall, water, another officer, ...: step aside or stay */
      if (tx == PX) {
        if (IsFree(tx + 1, ty)) fx = tx + 1;
        else if (IsFree(tx - 1, ty)) fx = tx - 1;
        else { fx = ox; fy = oy; }
      } else {
        if (IsFree(tx, ty + 1)) fy = ty + 1;
        else if (IsFree(tx, ty - 1)) fy = ty - 1;
        else { fx = ox; fy = oy; }
      }
      if ((fx != ox || fy != oy) && !IsFree(fx, fy)) { fx = ox; fy = oy; }
    }
    if (fx == PX && fy == PY) { fx = ox; fy = oy; }
    if (fx != ox || fy != oy) {
      M(ox, oy) = T_FLOOR;
      M(fx, fy) = cv[p];
    }
  }
}

/* dog block (0xCA4B..0xCEA1): bites when adjacent, otherwise walks toward
   the player and infects whatever it walks into */
static void UpdateDog(void)
{
  int x, y, tx, ty;
  u8 t;
  const char *who = NULL;
  if ((Gone & GONE_DOG) || !FindTile(T_DOG, &x, &y)) { Gone |= GONE_DOG; return; }
  if (Abs(x - PX) < 2 && Abs(y - PY) < 2) {
    if (Friendly) return;
    Msg("Piski puraisee sinua!");
    Hurt(K_RAKKIKOIRA, VI(1 + Random(4)));
    return;
  }
  tx = x + Sgn(PX - x);
  ty = y + Sgn(PY - y);
  t = Tile(tx, ty);
  if (t >= T_POLICE1 && t <= T_POLICE5) who = "Poliisi";
  else switch (t) {
    case T_MUMMO:
      Msg("Mummeli j" AE AE " hurtan hampaisiin.");
      M(tx, ty) = T_FLOOR;
      break;
    case T_KAKE:    who = "Kake"; break;
    case T_NEEKERI: who = "Nekru"; break;
    case T_HINTTI:  who = "Hintti"; break;
    case T_TROKARI: who = "Trokari"; break;
    case T_PAPPI:   who = "Pappi"; break;
    case T_HIPPI:   who = "Hippi"; break;
    case T_PIRKKO:  TeleportPirkko(tx, ty); break;
    case T_GHOST:
      Msg("Haamut tappavat rakin.");
      M(x, y) = T_FLOOR;
      return;
    case T_EXIT:
      M(x, y) = T_FLOOR;                         /* the dog leaves the level */
      return;
  }
  if (who) {
    char m[32];
    StrCopy(m, who, sizeof(m));
    StrCat(m, " saa verenmyrkytyksen.", sizeof(m));
    Msg(m);
    M(tx, ty) = T_FLOOR;
  }
  if (!IsFree(tx, ty)) {
    tx = x + Sgn(PX - x); ty = y;
    if (tx == x || !IsFree(tx, ty)) {
      tx = x; ty = y + Sgn(PY - y);
      if (ty == y || !IsFree(tx, ty)) return;
    }
  }
  M(x, y) = T_FLOOR;
  M(tx, ty) = T_DOG;
}

/* dot-eater block (0xC943..0xCA4B): teleports to a random empty-or-dot
   cell every turn, eating (banking) the dot it lands on */
static void UpdatePirkko(void)
{
  int x, y, rx, ry;
  if ((Gone & GONE_PIRKKO) || !FindTile(T_PIRKKO, &x, &y)) {
    Gone |= GONE_PIRKKO;
    return;
  }
  if (!RandomCell(22, 58, 3, 20, 1, &rx, &ry)) return;
  if (M(rx, ry) == T_DOT && HP(T_PIRKKO) < 127) HP(T_PIRKKO)++;
  M(x, y) = T_FLOOR;
  M(rx, ry) = T_PIRKKO;
}

/* "Hippi huitoo sinua piikillään, ja osuu!" -- a long hallucination */
static void HippieTrip(void)
{
  u8 n = (u8)strlen(MsgLine);
  if (n > 0 && MsgLine[n - 1] == '!') {
    MsgLine[n - 1] = 0;
    MsgCat(", ja osuu!");
  } else Msg("Hippi osuu!");
  Voima += VI(2);
  Prom += PI10(12);
  AddScore(1);
  Friendly = 1;
  FriendlyTurns = (s16)(40 + Random(40));
}

/* show pending messages before a k/e question */
static void Flush(void)
{
  if (MsgLine[0]) { DrawMsg(); PressAny(); }
}

/* what one static creature does to a player standing next to it */
static void Neighbour(u8 t, int x, int y)
{
  int n;
  switch (t) {
    case T_GHOST:
      if (Friendly) Msg("Sinua kutittaa!");
      else {
        Msg("Aave hakkaa sinua!");
        Hurt(K_HAAMU, VI(1 + Random(2)));
      }
      break;
    case T_VARAS:
      if (Mk > 0) {
        Hurt(K_TASKUVARAS, V_02);
        if (Random(4) == 0) {
          Msg("K" AE "teiskassasi kutistuu.");
          AddMk(-Min(Mk, Random(4 * Level) + 1));
          M(x, y) = T_FLOOR;
        }
      }
      break;
    case T_KARKURI:
      Msg("Vankikarkuri pahoinpitelee sinua.");
      Hurt(K_MURHA, VI(Random(5)));
      break;
    case T_TEEKKARI:
      if (Mk > 30 && Random(2) == 0) {
        if (Has(E_APY))
          Msg("Teekkari ylipuhuu sinut ostamaan " AE "pyn, vaikka sinulla oli jo!");
        else {
          Msg("Teekkari ylipuhuu sinut ostamaan " AE "pyn.");
          Kokemus++;
        }
        AddMk(-30);
        Equip[E_APY] = 1;
        Voima += VI(1);
        AddScore(1);
        M(x, y) = T_FLOOR;
      }
      break;
    case T_KAKE:
      if (InvKalja > 0) {
        Flush();
        if (AskYesNo("Kamusi Kake pummaa kaljaa, annatko (k/e)?")) {
          SetMsg("Kake kiitt" AE AE " ja katoaa.");
          Voima += VI(1);
          Dec8(&InvKalja);
          M(x, y) = T_FLOOR;
        } else {
          SetMsg("Kake taputtaa palleaasi.");
          Hurt(K_KAKE, VI(1));
        }
      }
      break;
    case T_PAPPI:
      Msg("Pappi saarnaa sinulle viinan vaaroista.");
      /* Random * 3 < Prom, in promille units of 1/100000 */
      if (((s32)Random(30000) * 10L < Prom || Friendly) && InvKalja + InvLonkka > 0) {
        Msg("Hetken hairahduksesta kaadat juomasi maahan!");
        Voima += VI(1);
        Kokemus++;
        AddScore(1);
        InvKalja = InvLonkka = 0;
      }
      break;
    case T_HINTTI:
      Msg(Friendly ? "Joku hyv" AE "ilee sinua." : "Hinttari hiplaa sinua!");
      Hurt(K_HINTTAILU, V_05);
      break;
    case T_TROKARI:
      if (Mk > 50) {
        Flush();
        if (AskKE("H" AE "m" AE "r" AE " veikko tarjoaa taikajuomaa pullostaan, 50 mk (k/e)?")) {
          AddMk(-50);
          if (Random(2) == 1) {
            SetMsg("Kurkkuasi l" AE "mmitt" AE AE " mukavasti.");
            Voima += VI(2);
            Prom += PI10(12);
            AddScore(1);
            if (Kokemus < 10) Kokemus++;
          } else {
            SetMsg("Tulet sokeaksi.");
            Hurt(K_MASINOL, VI(2));
            Blind = 1;
          }
        } else {
          SetMsg("Tunnet vihlaisun maksassasi!");
          Hurt(K_MASINOL, VI(1));
        }
        M(x, y) = T_FLOOR;
      }
      break;
    case T_JUPPI:
      Msg("Juppi uhoaa.");
      Hurt(K_JUPPI, V_02);
      break;
    case T_HIPPI:
      Msg("Hippi huitoo sinua piikill" AE AE "n!");
      Hurt(K_PIIKKI, V_05);
      if (!Friendly && Voima + VI(Random(13)) > VI(21)) HippieTrip();
      break;
    case T_SKIN:
      if (!Friendly) {
        Msg("Skinhead murjoo sinua apinan raivolla!");
        n = Min((int)TruncDiv(Voima, 3 * VS), 4);
        Hurt(K_VAKIVALTA, VI(1 + Random((n < 0 ? 0 : n) + 1)));
      }
      break;
    case T_SNAKE:
      Msg("K" AE AE "rme puree sinua.");
      Hurt(K_KAARME, VI(1));
      if (Random(Max(1, (int)TruncDiv(Voima, VS))) == 0 && !Has(E_STILETTI)) {
        /* 0xD4B8: Voima := -1, so the end-of-turn check collapses you */
        DeathPre = "Valitettavasti purema oli myrkyllinen ...";
        Killer = K_MYRKYTYS;
        SetMsg(DeathPre);
        Voima = -VI(1);
      }
      break;
    case T_GORILLA:
      if (!Friendly) {
        Msg("Gorilla hakkaa sinua.");
        Hurt(K_GORILLA, VI(1 + Random(2)));
      }
      break;
    case T_AIDS:
      Msg("Sven-Olof halaa sinua.");
      if (!Aids && Random(4) == 0) {
        Msg("Samalla HIV-virus siirtyy suoneesi!");
        Aids = 1;
      }
      break;
  }
}

/* every creature kind next to the player acts once */
static void UpdateNeighbours(void)
{
  int x, y;
  u8 t, done[9], nd = 0, i;
  for (y = PY - 1; y <= PY + 1; y++)
    for (x = PX - 1; x <= PX + 1; x++) {
      if (Dead || !InMap(x, y)) continue;
      t = M(x, y);
      for (i = 0; i < nd && done[i] != t; i++) ;
      if (i < nd) continue;
      done[nd++] = t;
      Neighbour(t, x, y);
    }
}

/* 0xA1AC: while you stand in the gang's mouth column it beats you up;
   otherwise the whole gang steps one column left and lines up with your row */
static void UpdatePunks(void)
{
  if (!PunksOn || Dead) return;
  if (PX == KingX - 1) {
    Msg(Friendly ? "Tanssit jonkun mukavan pojan kanssa." : "Punkkarit muiluttavat sinua!");
    Hurt(K_PUNKKARIT, VI(Friendly ? 1 : 2));
  } else if (KingX - 2 >= MAP_X0) {   /* the original walked off the map */
    PunkErase();
    KingY = PY;
    KingX--;
    PunkStamp();
  }
}

/* ===========================================================================
   End of turn (per-turn handler 0xE0E8..0xE36A)
   =========================================================================== */
static void EndTurn(void)
{
  Turn++;
  if (Friendly && --FriendlyTurns <= 0) Friendly = 0;

  /* drunk state text + effect */
  if (Aids) {
    StateText = ST_AIDS;
    Hurt(K_AIDS, V_02);
  } else if (Prom < PS / 20) {                               /* < 0.05 */
    if (StateText != ST_SICK) SndPlay(SND_SICK);
    StateText = ST_SICK;
  }
  else if (Prom > PI10(40)) {
    StateText = ST_VOMIT;
    Hurt(K_ALKOMYRKYTYS, VI(1));
    if (IsFree(PX + 1, PY)) M(PX + 1, PY) = T_POOP;
  } else StateText = Prom > PI10(30) ? ST_STAGGER : ST_BLANK;

  /* Promille decay and Kankkunen */
  if (Prom >= 0) Prom -= P_DECAY;
  else {
    Killer = K_KRAPULA;
    Die(KANKKUNEN);
  }

  /* Voima regeneration (0x9B6D): Random/87 + Level/300 per turn, which is
     exactly Random(300) + 87*Level in units of 1/26100 */
  Voima += (s32)Random(300) + 87L * Level;

  if (Voima < 0) Die("Voimasi ehtyv" AE "t, lyhistyt maahan ...");
}

/* ===========================================================================
   Descent ('>')
   =========================================================================== */
/* Returns 1 if a turn passed. */
static u8 TryDescend(void)
{
  int toll;
  if (PX != EXIT_X || PY != EXIT_Y) {
    SetMsg("Ei ei, vain portaita pitkin");
    return 0;
  }
  if (DotsGot < DotsNeeded) {
    SetMsg("Ensin pisteet ...");
    return 0;
  }
  if (PortsariOn && Tile(PORTSARI_X, PORTSARI_Y) == T_PORTSARI) {
    toll = Min(5 * Level, Mk);
    StrCopy(MsgLine, "Poke vaatii ", MSGMAX);
    StrCat(MsgLine, NumStr(toll, 0), MSGMAX);
    StrCat(MsgLine, " mk juomarahaa, maksatko (k/e) >", MSGMAX);
    if (!AskYesNo(MsgLine)) {
      SetMsg("Portsari ei p" AE AE "st" AE " sinua portista.");
      return 1;
    }
    AddMk(-toll);
  }
  if (Has(E_HENNESSY)) {
    /* Score := Score + 20 + Voima + Trunc(10*Prom), then truncated */
    Score = Clamp16(TruncDiv(((s32)Score + 20 + TruncDiv(Prom, PS / 10)) * VS + Voima, VS));
    Won = 1;
    return 1;
  }
  SetMsg("Siirryt seuraavalle tasolle...");
  DrawMsg();
  NextLevel();
  MsgLine[0] = 0;
  return 1;
}

/* ===========================================================================
   New game, name entry, main loop
   =========================================================================== */
static void NewGame(void)
{
  Level = StartLevel;
  Mk = StartMk;
  Score = StartLevel;
  Voima = VI(10);
  Prom = PI10(2);
  Turn = 0;
  InvKalja = 1; InvLonkka = 0; InvKiekura = 1;   /* start kit (0x4FAB) */
  memset(Equip, 0, sizeof(Equip));
  Equip[E_HAALARI] = 1;
  Kokemus = 1;
  Friendly = 0; FriendlyTurns = 0; Aids = 0;
  RecomputeWeapon();
  BuildLevel();
}

/* C64 keys for the Finnish letters: SHIFT+: ([), SHIFT+; (]) and the pound
   key give ä, ö, å; with SHIFT held down for the capitals there are no
   free keys, so names use small ä/ö/å */
static void ReadName(void)
{
  int ch;
  u8 len = 0;
  char *p = PlayerName;
  PlayerName[0] = 0;
  SClear();
  SPut(0, 3, "[ = " AE "   ] = " OE "     = " AA, A_DIM);
  SPutScr(16, 3, 0x1C, A_DIM);                 /* the pound sign */
  for (;;) {
    SFill(0, 0, SCRW - 1, 0);
    SPut(0, 0, "Kerropa nimesi >", A_NORM);
    SPut(16, 0, PlayerName, A_BOLD);
    CurRow = 0;
    CurCol = (u8)(16 + len);
    CursorOn = 1;
    ch = ReadKey();
    CursorOn = 0;
    if (ch == KEY_RETURN) break;
    if (ch == KEY_DEL) {
      if (len) PlayerName[--len] = 0;
      continue;
    }
    if (ch == '[') ch = C_AE;
    else if (ch == ']') ch = C_OE;
    else if (ch == 0x5C) ch = C_AA;         /* the pound key */
    else if (!((ch >= 0x20 && ch <= 0x5A) || (ch >= 0xC1 && ch <= 0xDA))) continue;
    if (len < NAMEMAX - 1) {
      PlayerName[len++] = (char)ch;
      PlayerName[len] = 0;
    }
  }
  /* trim */
  while (*p == ' ') p++;
  while (len > 0 && PlayerName[len - 1] == ' ') PlayerName[--len] = 0;
  if (!*p) StrCopy(PlayerName, "Spurgu", NAMEMAX);
  else memmove(PlayerName, p, strlen(p) + 1);
}

static void Move(int c, int dx, int dy, u8 upper)
{
  u8 drunk;
  Acted = TryMove(dx, dy, &drunk);
  RepeatCh = ((upper || WasRepeat) && Acted && !drunk && !MsgLine[0] &&
              Tile(PX + dx, PY + dy) == T_FLOOR) ? c : 0;
}

/* no real-time clock: the KERNAL jiffy clock counts from power-on */
static void ShowClock(void)
{
  u32 m = Jiffies() / 3600UL;
  char hm[6];
  u8 h = (u8)((m / 60) % 24), mi = (u8)(m % 60);
  hm[0] = (char)('0' + h / 10); hm[1] = (char)('0' + h % 10);
  hm[2] = ':';
  hm[3] = (char)('0' + mi / 10); hm[4] = (char)('0' + mi % 10);
  hm[5] = 0;
  SetMsg("Kello on ");
  MsgCat(hm);
  MsgCat(" (koneen k" AE "ynnistyksest" AE ").");
}

/* movement keys: hjklyubn walk, SHIFT + hjklyubn run */
static u8 DoMoveKey(int ch)
{
  const char *p;
  int lower = (ch >= 'A' && ch <= 'Z') ? ch - ('A' - 'a') : ch;
  if (!lower || !(p = strchr(DirKeys, lower))) return 0;
  Move(lower, DirDX[p - DirKeys], DirDY[p - DirKeys], (u8)(ch != lower));
  return 1;
}

/* one game: returns when the player died, won or saved */
void MainLoop(void)
{
  int ch;

  Dead = Won = Saved = 0;
  DeathPre = DeathCause = NULL;
  Killer = K_TUNTEMATON;
  ViewX = WORLD_X0;
  MsgLine[0] = LastMsg[0] = 0;
  SClear();
  SPut(10, 12, "Levyke py" OE "rii...", A_DIM);
  if (SaveFileExists() &&
      AskYesNo("L" OE "ytyi talletettu peli. Jatketaanko sit" AE " (k/e)?") && LoadGame()) {
    DeleteSave();
    Gone = 0;
    Randomize(Jiffies() ^ ((u32)*(volatile u8 *)0xD012 << 24));
    SetMsg("Tervetuloa takaisin, ");
    MsgCat(PlayerName);
    MsgCat("!");
  } else {
    ReadName();
    /* the time taken to type the name seeds the generator */
    Randomize(Jiffies() ^ ((u32)*(volatile u8 *)0xD012 << 24));
    SClear();
    SPut(12, 12, "Rakennetaan...", A_DIM);
    NewGame();
    SetMsg("Heippa ");
    MsgCat(PlayerName);
    MsgCat(", tervetuloa SpurguXiin!");
  }
  StateText = ST_BLANK;
  SClear();
  FullRedraw();

  RepeatCh = 0;
  while (!Dead && !Won && !Saved) {
    WasRepeat = RepeatCh != 0;
    if (WasRepeat) {
      ch = RepeatCh;
      /* a key press stops the run (the key is read next) */
      if (*(volatile u8 *)0xC6) {
        RepeatCh = 0;
        continue;
      }
    } else ch = ReadKey();
    Acted = 0;
    MsgLine[0] = 0;

    if (!DoMoveKey(ch)) {
      RepeatCh = 0;
      switch (ch) {
        case '.': Acted = 1; break;
        case 'q': DoDrink(); Acted = 1; break;
        case 'e': DoEat(); Acted = 1; break;
        case 'i': DoInventory(); break;
        case 't': Acted = DoThrow(); break;
        case '>': Acted = TryDescend(); break;
        case 'v':
          SetMsg("SpurguX Vs. 1.17, Last edited 09.09.1987, (C) Petri Niska."
                 " C64-versio 2026.");
          break;
        case '?': ShowHelp(); break;
        case '/': Sepustus(); break;
        case 'p': StrCopy(MsgLine, LastMsg, MSGMAX); break;         /* v2.24 */
        case 'c': ShowClock(); break;
        case 12:  SClear(); break;                                  /* redraw */
        case 'S':                                                    /* v2.24 */
          if (AskYesNo("Talletetaanko peli ja lopetetaan (k/e)?")) {
            SetMsg("Talletetaan...");
            DrawMsg();
            if (SaveGame()) Saved = 1;
            else SetMsg("Talletus ep" AE "onnistui!");
          } else MsgLine[0] = 0;
          break;
        case 'Q':
          if (AskYesNo("Todella luovutat (k/e) ? ")) {
            Dead = 1;
            DeathCause = NULL;
            Killer = K_ITSEMURHA;
          } else MsgLine[0] = 0;
          break;
        case KEY_ESC: case 0:
          break;
        default:
          if (ch >= ' ' && ch < 0x80) {
            char s[2];
            s[0] = (char)ch;
            s[1] = 0;
            SetMsg("Tuntematon komento ");
            MsgCat(s);
          }
          break;
      }
    }

    if (Acted && !Dead && !Won) {
      s32 v = Voima;
      UpdatePunks();
      UpdatePirkko();
      UpdateDog();
      UpdatePolice();
      UpdateNeighbours();
      if (Voima < v) SndPlay(SND_OUCH);
      if (!Dead) EndTurn();
      if (MsgLine[0] || Dead) RepeatCh = 0;
    }
    SyncLast();
    FullRedraw();
  }

  if (Saved) {
    SClear();
    SPut(0, 10, "Peli talletettu. N" AE "kemiin,", A_NORM);
    SPut(0, 11, PlayerName, A_BOLD);
    SPut((u8)strlen(PlayerName), 11, "!", A_NORM);
    CurRow = 12;
    CurCol = 0;
    ReadKeyAfterPause(1000);
    return;
  }

  if (Won) {
    SClear();
    SPut(0, 0, "ONNITTELUT, ", A_YELLOW);
    SPut(12, 0, PlayerName, A_YELLOW);
    SPut(0, 1, " TEIT SEN ! Sinussa on ainesta !", A_YELLOW);
    CurRow = 2;
    CurCol = 0;
    ReadKeyAfterPause(1500);
  } else if (DeathCause) {
    SndPlay(SND_DEATH);
    /* each death has its own message; Kankkunen is only the sobering-up
       death, not an epilogue to every death */
    if (DeathPre) {
      SetMsg(DeathPre);
      FullRedraw();
      Prompt(1);
    }
    SetMsg(DeathCause);
    FullRedraw();
    Prompt(1);
  }
  ShowHiscore();
}
