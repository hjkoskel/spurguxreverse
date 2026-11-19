/* game.c -- SpurguX v1.17 rules (ported from spurgux117.pas).

   Game rules follow the v1.17 disassembly:
     * level generator   (file 0x4216..0x4D20)
     * turn processor    (file 0x5473..0x7E50)
     * NPC update chain  (file 0x9EC4..0xE0E0)
     * per-turn handler  (file 0xE0E8..0xE36A)
   Additions merged from v2.24: S = save and quit, p = repeat last message,
   c = clock, Ctrl-L = redraw.                                              */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "spurgux.h"

/* ===========================================================================
   Global state
   =========================================================================== */
u8  Map[SCRW + 2][SCRH + 1];
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
s16 HP[T_COUNT];
u8  PoliceActive, Friendly, Aids, Blind, PortsariOn, PunksOn;
s16 FriendlyTurns;
u8  KingX, KingY;
u8  PunkUnder[5];
u8  FlagAlco, FlagDivari, FlagPoliisi, FlagGrave, FlagZoo, FlagAlcove;
u8  HotelName;
char MsgLine[MSGMAX], LastMsg[MSGMAX];
const char *StateText;
u8  Dead, Won, Saved;
const char *DeathCause;
const char *DeathPre;
u8  Killer;
char PlayerName[NAMEMAX];
u8  UseColor = 1;
u8  StartLevel = 1;
s16 StartMk = 0;

const char *const EquipName[E_COUNT] = {
  "", "", "", "Haalarit", "Pamppu", "Stiletti", "Ketjut", "Camel",
  "", "", "Äpy", "Hennessy"
};

const char *const Causes[K_COUNT] = {
  "", "tuntematon", "itsemurha", "krapula", "myrkytys", "hukkuminen",
  "kallonmurtuma", "mummo", "väkivalta", "neekeri", "punkkarit", "ilkeys",
  "vilustuminen", "poliisi", "rakkikoira", "haamu", "taskuvaras", "murha",
  "Kake", "hinttailu", "masinol", "juppi", "piikki", "käärme", "gorilla",
  "AIDS", "alkoholimyrkytys", "", "", "", "", ""
};

static const char ST_AIDS[]    = "AIDS painaa!";
static const char ST_BLANK[]   = "            ";
static const char ST_SICK[]    = "Voit pahoin!";
static const char ST_VOMIT[]   = "Oksennat!   ";
static const char ST_STAGGER[] = "Horjahtelet ";
static const char KANKKUNEN[]  = "Kankkunen saapuu ja katkaisee taipaleesi ...";

/* main loop state (the Pascal nested Move procedure used these) */
static int RepeatCh;
static u8  Acted, WasRepeat, Drunk;

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

/* C89 leaves signed division rounding implementation-defined: be explicit */
s32 TruncDiv(s32 a, s32 b)
{
  s32 q;
  u8 neg = 0;
  if (a < 0) { a = -a; neg ^= 1; }
  if (b < 0) { b = -b; neg ^= 1; }
  q = a / b;
  return neg ? -q : q;
}

s32 RoundDiv(s32 a, s32 b)
{
  if (a < 0) return -TruncDiv(-a + b / 2, b);
  return TruncDiv(a + b / 2, b);
}

static int Min(int a, int b) { return a < b ? a : b; }
static int Max(int a, int b) { return a > b ? a : b; }
static int Sgn(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
static int Abs(int v) { return v < 0 ? -v : v; }

static void Inc8(u8 *v) { if (*v < 255) (*v)++; }
static void Dec8(u8 *v) { if (*v > 0) (*v)--; }

static void AddMk(int n)
{
  s32 v = (s32)Mk + n;
  if (v > 32000) v = 32000;
  if (v < -32000) v = -32000;
  Mk = (s16)v;
}

static void AddScore(int n)
{
  s32 v = (s32)Score + n;
  if (v > 32000) v = 32000;
  if (v < -32000) v = -32000;
  Score = (s16)v;
}

static void Hurt(u8 cause, s32 amount)
{
  Killer = cause;
  Voima -= amount;
}

/* ===========================================================================
   Random numbers: 32-bit LCG, Random(n) in [0, n) from the high 16 bits
   =========================================================================== */
static u32 RandSeed = 1;

static u16 Rnd16(void)
{
  RandSeed = (RandSeed * 134775813UL + 1UL) & 0xFFFFFFFFUL;
  return (u16)(RandSeed >> 16);
}

void Randomize(u32 seed)
{
  RandSeed = seed & 0xFFFFFFFFUL;
}

int Random(s32 n)
{
  if (n <= 0) return 0;
  if (n > 32767) n = 32767;
  return (int)(((u32)Rnd16() * (u32)n) >> 16);
}

/* ===========================================================================
   Messages and prompts
   =========================================================================== */
void DrawMsg(void)
{
  u16 n;
  SFill(1, MSG_ROW, SCRW, MSG_ROW);
  SPut(1, MSG_ROW, MsgLine, A_NORM);
  CurRow = MSG_ROW;
  n = ULen(MsgLine) + 1;
  CurCol = (u8)(n > SCRW ? SCRW : n);
}

/* adds a message to this turn's message line (joined when it fits) */
void Msg(const char *s)
{
  if (!s[0]) return;
  if (!MsgLine[0]) StrCopy(MsgLine, s, MSGMAX);
  else if (ULen(MsgLine) + 1 + ULen(s) <= SCRW) {
    StrCat(MsgLine, " ", MSGMAX);
    StrCat(MsgLine, s, MSGMAX);
  } else StrCopy(MsgLine, s, MSGMAX);
  StrCopy(LastMsg, MsgLine, MSGMAX);
}

void SetMsg(const char *s)
{
  StrCopy(MsgLine, s, MSGMAX);
  if (s[0]) StrCopy(LastMsg, s, MSGMAX);
}

/* "<lisää>" prompt at row 1 col 73 (original 0x2E82) */
static void PressAny(void)
{
  SFill(73, 1, SCRW, 1);
  SPut(73, 1, "<lisää>", A_BOLD);
  CurRow = 1;
  CurCol = 80;
  ReadKey();
  SFill(73, 1, SCRW, 1);
}

void PressAnySlow(void)
{
  SFill(73, 1, SCRW, 1);
  SPut(73, 1, "<lisää>", A_BOLD);
  CurRow = 1;
  CurCol = 80;
  ReadKeyAfterPause(1000);
  SFill(73, 1, SCRW, 1);
}

int AskKey(const char *q)
{
  int k;
  StrCopy(MsgLine, q, MSGMAX);
  DrawMsg();
  CursorOn = 1;
  k = ReadKey();
  CursorOn = 0;
  return k;
}

u8 AskYesNo(const char *q)
{
  int a;
  do {
    a = AskKey(q);
  } while (!strchr("kKeEyYnN\033", a) || a == 0);
  return a == 'k' || a == 'K' || a == 'y' || a == 'Y';
}

/* AskKey until one of the keys in `valid` (ESC always allowed) */
static int AskOneOf(const char *q, const char *valid)
{
  int a;
  do {
    a = AskKey(q);
  } while (a == 0 || (a != 27 && !strchr(valid, a)));
  return a;
}

/* ===========================================================================
   Rendering
   =========================================================================== */
static const char *TileGlyph(u8 t, u8 *at)
{
  *at = A_NORM;
  if (t >= T_POLICE1 && t <= T_POLICE5) { *at = A_BLUE; return "C"; }
  switch (t) {
    case T_FLOOR:    return " ";
    case T_KALJA:    *at = A_YELLOW;  return "%";
    case T_LONKKA:   *at = A_YELLOW;  return "&";
    case T_HAALARI:  *at = A_CYAN;    return "Σ";
    case T_PAMPPU:   *at = A_CYAN;    return "!";
    case T_STILETTI: *at = A_CYAN;    return "\\";
    case T_KETJUT:   *at = A_CYAN;    return "ß";
    case T_CAMEL:    *at = A_CYAN;    return "Θ";
    case T_KONJAKKI: *at = A_YELLOW;  return "Φ";
    case T_MONEY:    *at = A_GREEN;   return "$";
    case T_HINT:     *at = A_MAGENTA; return "¿";
    case T_MILK:     *at = A_WHITE;   return "⌂";
    case T_CHOCO:    *at = A_YELLOW;  return "∞";
    case T_APPLE:    *at = A_GREEN;   return "o";
    case T_SILAKKA:  *at = A_CYAN;    return "¥";
    case T_MAKKARA:  *at = A_YELLOW;  return "(";
    case T_DOT:      *at = A_WHITE;   return ".";
    case T_EXIT:     *at = A_GREEN;   return ">";
    case T_WATER:    *at = A_BLUE;    return "≈";
    case T_POOP:     *at = A_DIM;     return "æ";
    case T_MUMMO:    *at = A_MAGENTA; return "M";
    case T_KAKE:     *at = A_MAGENTA; return "K";
    case T_VARAS:    *at = A_RED;     return "V";
    case T_TEEKKARI: *at = A_MAGENTA; return "T";
    case T_PORTSARI: *at = A_RED;     return "Ω";
    case T_NEEKERI:  *at = A_MAGENTA; return "N";
    case T_HINTTI:   *at = A_MAGENTA; return "H";
    case T_TROKARI:  *at = A_MAGENTA; return "Q";
    case T_PAPPI:    *at = A_MAGENTA; return "R";
    case T_HIPPI:    *at = A_RED;     return "I";
    case T_PIRKKO:   *at = A_RED;     return "B";
    case T_DOG:      *at = A_RED;     return "D";
    case T_GORILLA:  *at = A_RED;     return "G";
    case T_SKIN:     *at = A_RED;     return "O";
    case T_SNAKE:    *at = A_RED;     return "S";
    case T_AIDS:     *at = A_RED;     return "A";
    case T_PUNK:     *at = A_RED;     return "P";
    case T_KINGI:    *at = A_RED;     return "Ñ";
    case T_JUPPI:    *at = A_RED;     return "J";
    case T_KARKURI:  *at = A_RED;     return "E";
    case T_GHOST:    return " ";
    case T_GRAVE:    *at = A_DIM;     return "±";
    case T_FENCE:    *at = A_YELLOW;  return "#";
    default:         return " ";
  }
}

static u8 IsCreature(u8 t)
{
  return (t >= T_POLICE1 && t <= T_KARKURI) || t == T_GHOST;
}

static void DrawFrame(void)
{
  int c;
  SPut(FRAME_L, FRAME_TOP, "╔", A_CYAN);
  for (c = FRAME_L + 1; c <= FRAME_R - 1; c++) SPut(c, FRAME_TOP, "═", A_CYAN);
  SPut(FRAME_R, FRAME_TOP, "╗", A_CYAN);
  for (c = FRAME_TOP + 1; c <= FRAME_BOT - 1; c++) {
    SPut(FRAME_L, c, "║", A_CYAN);
    SPut(FRAME_R, c, "║", A_CYAN);
  }
  SPut(FRAME_L, FRAME_BOT, "╚", A_CYAN);
  for (c = FRAME_L + 1; c <= FRAME_R - 1; c++) SPut(c, FRAME_BOT, "═", A_CYAN);
  SPut(FRAME_R, FRAME_BOT, "╝", A_CYAN);
}

/* building glyphs and names (map renderer file 0x3A80..0x404C) */
static void DrawCityscape(void)
{
  const u8 B = A_CYAN, N = A_WHITE;
  int r;
  /* top-left: ISKU / ALCO */
  for (r = 3; r <= 5; r++) SPut(20, r, "▐", B);
  SPut(14, 6, "▀▀▒▒▀▀▀", B);
  SPut(15, 4, FlagAlco ? "ALCO" : "ISKU", N);
  /* top-middle: SAIRAALA / HOTELLI */
  for (r = 3; r <= 6; r++) { SPut(34, r, "█", B); SPut(55, r, "█", B); }
  SPut(35, 6, "▄▄▄▄▄▄▄▄▄┬┬▄▄▄▄▄▄▄▄▄", B);
  if (HotelName) SPut(41, 4, "HOTELLI", N); else SPut(41, 4, "SAIRAALA", N);
  /* bottom-left (DIVARI) */
  SPut(14, 19, "▀▀▀▒▀▀█", B);
  for (r = 20; r <= 22; r++) SPut(20, r, "█", B);
  if (FlagDivari) SPut(14, 21, "DIVARI", N);
  /* bottom-middle and bottom-right */
  SPut(43, 19, "█▀▀▀▒▀▀▀█", B);
  SPut(54, 19, "█▀▀▀▒▀▀▀█", B);
  for (r = 20; r <= 22; r++) {
    SPut(43, r, "█", B); SPut(51, r, "█", B);
    SPut(54, r, "█", B); SPut(62, r, "█", B);
  }
  if (FlagPoliisi) {
    SPut(44, 21, "POLIISI", N);
    SPut(55, 21, "VANKILA", N);
  }
  /* top-right alcove wall (flag [0x2E9]) */
  if (FlagAlcove)
    for (r = 3; r <= 5; r++) SPut(76, r, "▐", B);
}

static void DrawMap(void)
{
  int x, y;
  u8 t, at;
  const char *g;
  char hal[2];
  DrawFrame();
  SFill(MAP_X0, MAP_Y0, MAP_X1, MAP_Y1);
  if (!Blind) {
    for (y = MAP_Y0; y <= MAP_Y1; y++)
      for (x = MAP_X0; x <= MAP_X1; x++) {
        t = Map[x][y];
        if (t == T_WALL) continue;
        g = TileGlyph(t, &at);
        if (t == T_GHOST && Prom >= PI10(20)) {
          /* "Aaveita et selvänä näe" -- drunk eyes see the ghosts */
          g = "¤";
          at = A_DIM;
        }
        if (Friendly && IsCreature(t) && t != T_GHOST) {
          hal[0] = (char)('@' + Random(10) + 1);   /* hallucinated letters */
          hal[1] = 0;
          g = hal;
        }
        SPut(x, y, g, at);
      }
    DrawCityscape();
  }
  SPut(PX, PY, "@", A_WHITE);
  SFill(1, 19, 12, 19);
  SPut(1, 19, StateText, A_BOLD);
}

static void DrawStatus(void)
{
  char s[40];
  s32 p;
  SFill(1, STAT_ROW, SCRW, STAT_ROW);
  sprintf(s, "Taso: %u", Level);
  SPut(1, STAT_ROW, s, A_NORM);
  sprintf(s, "Mk: %d", Mk);
  SPut(14, STAT_ROW, s, A_NORM);
  sprintf(s, "Voima: %ld", RoundDiv(Voima, VS));
  SPut(27, STAT_ROW, s, A_NORM);
  sprintf(s, "Kokemus: %u", Kokemus);
  SPut(41, STAT_ROW, s, A_NORM);
  p = RoundDiv(Prom, PS / 10);               /* tenths */
  if (p < 0) sprintf(s, "Prom: -%ld.%ld", (-p) / 10, (-p) % 10);
  else sprintf(s, "Prom: %ld.%ld", p / 10, p % 10);
  SPut(57, STAT_ROW, s, A_NORM);
  sprintf(s, "%5lu", Turn);
  SPut(75, STAT_ROW, s, A_NORM);
}

void FullRedraw(void)
{
  DrawMap();
  DrawStatus();
  DrawMsg();
  if (!MsgLine[0]) {
    CurCol = PX;
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

/* The punk gang (0xA1AC..0xA476) is a "<"-shaped block of five tiles
   around the king at (KingX, KingY):  P P
                                         Ñ     (the mouth at KingX-1 is open)
                                       P P                                  */
static const s8 PUNK_DX[5] = { 0, -1, 0, -1, 0 };
static const s8 PUNK_DY[5] = { 0, -1, -1, 1, 1 };

static u8 PunkCellOk(u8 i)
{
  int x = KingX + PUNK_DX[i], y = KingY + PUNK_DY[i];
  return InMap(x, y) && !(x == PX && y == PY);
}

static void PunkStamp(void)
{
  u8 i;
  int x, y;
  for (i = 0; i < 5; i++)
    if (PunkCellOk(i)) {
      x = KingX + PUNK_DX[i];
      y = KingY + PUNK_DY[i];
      PunkUnder[i] = Map[x][y];
      if (PunkUnder[i] == T_PUNK || PunkUnder[i] == T_KINGI) PunkUnder[i] = T_FLOOR;
      Map[x][y] = (i == 0) ? T_KINGI : T_PUNK;
    }
}

/* the original wrote blanks over the gang's old cells; we put back what
   was under it so a passing gang cannot make the level unfinishable */
static void PunkErase(void)
{
  u8 i;
  int x, y;
  for (i = 0; i < 5; i++) {
    x = KingX + PUNK_DX[i];
    y = KingY + PUNK_DY[i];
    if (InMap(x, y) && (Map[x][y] == T_PUNK || Map[x][y] == T_KINGI))
      Map[x][y] = PunkUnder[i];
  }
}

static u8 Tile(int x, int y)
{
  return InMap(x, y) ? Map[x][y] : (u8)T_WALL;
}

static void SetTile(int x, int y, u8 t)
{
  if (InMap(x, y)) Map[x][y] = t;
}

/* empty floor and not the player's own cell */
static u8 IsFree(int x, int y)
{
  return InMap(x, y) && Map[x][y] == T_FLOOR && !(x == PX && y == PY);
}

static u8 FindTile(u8 t, int *fx, int *fy)
{
  int x, y;
  for (y = MAP_Y0; y <= MAP_Y1; y++)
    for (x = MAP_X0; x <= MAP_X1; x++)
      if (Map[x][y] == t) {
        *fx = x;
        *fy = y;
        return 1;
      }
  return 0;
}

static u8 Adjacent(int x, int y)
{
  return Abs(x - PX) < 2 && Abs(y - PY) < 2;
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
         (allowDot && InMap(*rx, *ry) && Map[*rx][*ry] == T_DOT &&
          !(*rx == PX && *ry == PY));
  } while (!ok && tries > 0);
  return ok;
}

/* placement helper 0x2DF3: x=23+Random(56), y=7+Random(12) */
static void Place(u8 t)
{
  int x, y;
  if (RandomCell(PLACE_X0, 56, PLACE_Y0, 12, 0, &x, &y)) Map[x][y] = t;
}

/* ===========================================================================
   Equipment / weapon level
   =========================================================================== */
/* weapon level [0x2F6]: Ketjut 5, Stiletti 3, Pamppu 2, +1 with Camel */
void RecomputeWeapon(void)
{
  if (Equip[E_KETJUT]) Weapon = 5;
  else if (Equip[E_STILETTI]) Weapon = 3;
  else if (Equip[E_PAMPPU]) Weapon = 2;
  else Weapon = 0;
  if (Equip[E_CAMEL]) Weapon++;
}

/* a won fight may raise Kokemus (only below the creature's cap) and gives
   extra Voima */
static void KillBonus(u16 cap)
{
  int a = Random(2);
  int b = Random(2);
  if (a * b == 1 && Kokemus < cap) {
    Kokemus++;
    Voima += VI(Random(Min(Level, 8)) + 1);
  }
}

/* ===========================================================================
   Level generation
   =========================================================================== */
/* fixed wall template, identical on every level */
static void BuildWalls(void)
{
  int x, y;
  memset(Map, T_FLOOR, sizeof(Map));
  for (y = 3; y <= 5; y++) Map[20][y] = T_WALL;
  for (x = 14; x <= 20; x++) Map[x][6] = T_WALL;
  for (y = 3; y <= 6; y++)
    for (x = 34; x <= 55; x++) Map[x][y] = T_WALL;
  for (x = 14; x <= 20; x++) Map[x][19] = T_WALL;
  for (x = 43; x <= 51; x++) Map[x][19] = T_WALL;
  for (x = 54; x <= 62; x++) Map[x][19] = T_WALL;
  for (y = 20; y <= 22; y++) {
    Map[20][y] = T_WALL;
    for (x = 43; x <= 51; x++) Map[x][y] = T_WALL;
    for (x = 54; x <= 62; x++) Map[x][y] = T_WALL;
  }
}

static void ResetLevelState(void)
{
  memset(HP, 0, sizeof(HP));
  PoliceActive = 0;
  PortsariOn = 0;
  PunksOn = 0;
  Blind = 0;
  DotsGot = 0;
  PX = START_X;
  PY = START_Y;
}

/* level 1 is fixed (byte-identical in every run of the original) */
static void BuildLevel1(void)
{
  ResetLevelState();
  BuildWalls();
  Map[52][10] = T_POLICE1;
  Map[59][10] = T_KALJA;
  Map[77][11] = T_MUMMO;
  Map[65][13] = T_POLICE1 + 1;
  Map[61][14] = T_HINT;
  Map[64][14] = T_DOT;
  Map[26][15] = T_MAKKARA;
  Map[46][18] = T_POLICE1 + 2;
  Map[51][18] = T_STILETTI;
  Map[EXIT_X][EXIT_Y] = T_EXIT;
  HP[T_POLICE1] = 3;
  HP[T_POLICE1 + 1] = 3;
  HP[T_POLICE1 + 2] = 3;
  HP[T_MUMMO] = 0;
  DotsNeeded = 1;
  FlagAlco = FlagDivari = FlagPoliisi = 0;
  FlagGrave = FlagZoo = FlagAlcove = 0;
  HotelName = 0;
}

/* levels 13 and 31 (0x2F32): graves, a ghost and a priest */
static void BuildGraveyard(void)
{
  int x, y;
  for (x = 28; x <= 72; x++)
    for (y = 9; y <= 17; y++)
      if (Map[x][y] == T_FLOOR && x % 3 == 1 && y % 2 == 1 && Random(3) > 0)
        Map[x][y] = T_GRAVE;
  Place(T_GHOST);
  HP[T_GHOST] = (s16)(10 + Random(8));
  Place(T_PAPPI);
  HP[T_PAPPI] = (s16)(3 + Random(Level));
}

/* levels 17 and 25 (0x308A): electric fences, a gorilla, a hippie */
static void BuildZoo(void)
{
  int x, y;
  for (x = 27; x <= 75; x++)
    for (y = 8; y <= 17; y++)
      if (Map[x][y] == T_FLOOR && Random(9) == 0) Map[x][y] = T_FENCE;
  Place(T_GORILLA);
  HP[T_GORILLA] = (s16)(10 + Random(7));
  Place(T_HIPPI);
  HP[T_HIPPI] = (s16)(7 + Random(7));
}

/* make sure every dot can be reached from the start: graves and fences are
   permanent obstacles, so a dot boxed in by them is moved */
static u8 Seen[SCRW + 2][SCRH + 1];
static u8 QX[(MAP_X1 - MAP_X0 + 1) * (MAP_Y1 - MAP_Y0 + 1) + 1];
static u8 QY[(MAP_X1 - MAP_X0 + 1) * (MAP_Y1 - MAP_Y0 + 1) + 1];

static void FixReachability(void)
{
  u16 head = 0, tail = 0;
  int x, y, dx, dy, nx, ny, rx, ry;
  u8 t, ok;
  memset(Seen, 0, sizeof(Seen));
  QX[tail] = START_X; QY[tail] = START_Y; tail++;
  Seen[START_X][START_Y] = 1;
  while (head < tail) {
    x = QX[head]; y = QY[head]; head++;
    for (dx = -1; dx <= 1; dx++)
      for (dy = -1; dy <= 1; dy++) {
        nx = x + dx; ny = y + dy;
        if (!InMap(nx, ny) || Seen[nx][ny]) continue;
        t = Map[nx][ny];
        if (t == T_WALL || t == T_GRAVE || t == T_FENCE) continue;
        Seen[nx][ny] = 1;
        QX[tail] = (u8)nx; QY[tail] = (u8)ny; tail++;
      }
  }
  for (y = MAP_Y0; y <= MAP_Y1; y++)
    for (x = MAP_X0; x <= MAP_X1; x++)
      if (Map[x][y] == T_DOT && !Seen[x][y]) {
        Map[x][y] = T_FLOOR;
        do {
          ok = RandomCell(PLACE_X0, 56, PLACE_Y0, 12, 0, &rx, &ry);
        } while (!ok || !Seen[rx][ry]);
        Map[rx][ry] = T_DOT;
      }
}

/* level generator (file 0x4216..0x4D20).  Called after Level was raised. */
void BuildLevel(void)
{
  int i, x, y, n;
  if (Level == 1) { BuildLevel1(); return; }
  ResetLevelState();
  BuildWalls();
  Map[EXIT_X][EXIT_Y] = T_EXIT;

  FlagGrave   = Level == 13 || Level == 31;
  FlagZoo     = Level == 17 || Level == 25;
  FlagAlco    = Random(100) + Level > 90;
  FlagDivari  = Random(100) - Level < 0;
  FlagPoliisi = Random(Level) > 2 && !FlagGrave && !FlagZoo;
  FlagAlcove  = Random(Level) > 3;
  HotelName   = Random(2) == 1;
  if (FlagAlcove)
    for (y = 3; y <= 5; y++) Map[76][y] = T_WALL;

  if (FlagGrave) BuildGraveyard();
  if (FlagZoo) BuildZoo();
  if (Level >= MAX_LEVEL && Equip[E_APY]) {
    /* the cognac (0x322F) + a guarding ghost.  The original tested slot 8
       for Äpy, which is stored in slot 10, so v1.17 never had a cognac.
       Here the intended rule: level 50 with Äpy bought from a Teekkari. */
    Map[KONJAKKI_X][KONJAKKI_Y] = T_KONJAKKI;
    Place(T_GHOST);
    HP[T_GHOST] = (s16)(11 + Random(10));
  }

  /* police: slots $1E .. $1E+Random(5) */
  n = T_POLICE1 + Random(5);
  for (i = T_POLICE1; i <= n; i++) {
    Place((u8)i);
    HP[i] = (s16)(2 + Min(Random(Level) + 1, 18));
  }
  Place(T_MUMMO);
  HP[T_MUMMO] = (s16)Min(Random(Level) - 1, 10);

  /* dots: one per level number */
  DotsNeeded = Level;
  for (i = 1; i <= Level; i++) Place(T_DOT);

  /* items */
  if (Random(10) == 9) Place(T_MONEY);
  if (Random(21) == 6 && !Equip[E_CAMEL]) Place(T_CAMEL);
  if (Random(10) > 7) Place(T_LONKKA);
  if (Random(10) > 7) Place(T_HINT);
  if (Random(10) > 6) Place(T_KALJA);
  if (Random(20) == 6) Place(T_MILK);
  if (Random(30) == 21) Place(T_CHOCO);
  if (Random(20) == 11) Place(T_APPLE);
  if (Random(10) == 6) Place(T_SILAKKA);
  if (Random(10) == 9) Place(T_MAKKARA);
  if (Random(10) == 5) Place(T_POOP);
  if (Random(20) == 19) Place(T_STILETTI);
  if (!Equip[E_HAALARI] && Random(21) == 20) Place(T_HAALARI);
  if (Random(30) == 19 && !Equip[E_KETJUT]) Place(T_KETJUT);
  /* a puddle now and then ("Puli juo lammikon kuiviin!") */
  if (Random(6) == 0) {
    if (RandomCell(PLACE_X0, 50, PLACE_Y0, 10, 0, &x, &y)) {
      n = Random(3) + 1;
      for (i = 0; i <= n; i++)
        if (IsFree(x + i, y)) Map[x + i][y] = T_WATER;
    }
  }

  /* creatures */
  if (Random(10) == 1 && Level > 1) {
    /* punk gang around (19,12), the king in the middle */
    KingX = 19; KingY = 12;
    PunkStamp();
    n = Level - Random(Level);
    HP[T_KINGI] = (s16)(n + Random(10));
    PunksOn = 1;
  }
  if (Random(10) == 9 && Level > 7) {
    Place(T_SKIN);
    HP[T_SKIN] = (s16)(4 + Random(10 + Level / 2));
  }
  if (!PunksOn && Level > 1 && Random(10) > 7) Map[16][12] = T_KAKE;
  if (Level > 1 && Random(100) > 77) {
    Place(T_PIRKKO);
    HP[T_PIRKKO] = 0;           /* banked dots ("life") */
  }
  if (Random(10) == 4 && Level > 3) {
    Place(T_SNAKE);
    HP[T_SNAKE] = (s16)(4 + Random(Level));
  }
  if (Level > 3 && Random(7) == 4) {
    Place(T_AIDS);
    HP[T_AIDS] = (s16)Min(Level, 8);
  }
  if (Level > 1 && Random(100) < 23 && !FindTile(T_PIRKKO, &x, &y)) {
    Map[PORTSARI_X][PORTSARI_Y] = T_PORTSARI;
    HP[T_PORTSARI] = (s16)Min(Level, 9);
    PortsariOn = 1;
  }
  if (Mk > 0 && Level > 1 && Random(10) > 7) {
    Place(T_VARAS);
    HP[T_VARAS] = (s16)Min(Level, 13);
  }
  if (Mk > 30 && Level > 2 && Random(5) == 1) Place(T_TEEKKARI);
  if (Level > 2 && Random(5) == 3) {
    Place(T_DOG);
    HP[T_DOG] = (s16)Max(Min((int)TruncDiv(Voima, 2 * VS), 8), 2);
  }
  if (Level > 3 && Random(16) > 10) Place(T_NEEKERI);
  if (Level > 4 && Random(18) > 12) Place(T_HINTTI);
  if (Level > 10 && Random(18) > 13 && !FindTile(T_PAPPI, &x, &y)) {
    Place(T_PAPPI);
    HP[T_PAPPI] = (s16)Min(Level - Random(Level), 6);
  }
  if (Level > 2 && Random(7) == 6 && !FindTile(T_HIPPI, &x, &y)) {
    Place(T_HIPPI);
    HP[T_HIPPI] = (s16)Max(Min(Level - Random(Level), 8), 1);
  }
  if (Level > 3 && Mk > 50 && Random(21) == 13) Place(T_TROKARI);
  if (Random(Level) > 4) {
    Place(T_JUPPI);
    HP[T_JUPPI] = (s16)(6 + Random(Level / 2 + 1));
  }
  if (FlagPoliisi && Level > 5 && Random(4) == 1) {
    Place(T_KARKURI);
    HP[T_KARKURI] = (s16)(5 + Min(Random(Level), 12));
  }

  FixReachability();
  /* the exit opens when every dot placed here has been eaten */
  DotsNeeded = 0;
  for (y = MAP_Y0; y <= MAP_Y1; y++)
    for (x = MAP_X0; x <= MAP_X1; x++)
      if (Map[x][y] == T_DOT) DotsNeeded++;
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

static void CheckDeath(void)
{
  if (!Dead && Voima < 0) Die("Voimasi ehtyvät, lyhistyt maahan ...");
}

/* ===========================================================================
   Hint pager and help files
   =========================================================================== */
static u8 IsBlank(const char *s)
{
  while (*s) {
    if (*s != ' ' && *s != '\t') return 0;
    s++;
  }
  return 1;
}

static const char *ReadTip(void)
{
  u16 i, n = 0, k;
  LoadLines("SPU.TIP");
  for (i = 0; i < LineCount; i++)
    if (!IsBlank(Lines[i])) n++;
  if (n == 0) return "Kippis!";
  k = (u16)Random(n);
  for (i = 0; i < LineCount; i++)
    if (!IsBlank(Lines[i])) {
      if (k == 0) return Lines[i];
      k--;
    }
  return "Kippis!";
}

/* two-page pager (0x5186) */
static void HintPager(void)
{
  char buf[MSGMAX];
  SetMsg("Ruumiisi alkaa vavahdella ja kuulet taivaallisen äänen!");
  DrawMsg();
  PressAny();
  StrCopy(buf, "Se sanoo: ", MSGMAX);
  StrCat(buf, ReadTip(), MSGMAX);
  SetMsg(buf);
  DrawMsg();
  PressAny();
  SetMsg("");
}

static void ShowPagedText(void)
{
  u16 i = 0;
  int row, k;
  while (i < LineCount) {
    SClear();
    row = 1;
    while (i < LineCount && row <= SCRH - 1) {
      if (strstr(Lines[i], "<lis")) {
        i++;
        break;
      }
      SPut(1, row, Lines[i], A_NORM);
      row++;
      i++;
    }
    SPut(68, SCRH, "<lisää>", A_BOLD);
    CurRow = SCRH;
    CurCol = 76;
    k = ReadKey();
    if (k == 27 || k == 'q') break;
  }
  SClear();
}

static void ShowHelp(void)
{
  if (!LoadLines("SPU.HLP")) AddLine("SPU.HLP puuttuu.");
  AddLine("<lisää>");
  AddLine("");
  AddLine("  KOMENNOT:");
  AddLine("");
  AddLine("  h j k l y u b n   liiku (nuolinäppäimetkin käyvät)");
  AddLine("  H J K L Y U B N   juokse, kunnes jokin estää");
  AddLine("  q  juo            e  syö            t  heitä");
  AddLine("  i  inventaario    .  odota          >  seuraavalle tasolle");
  AddLine("  ?  ohjeet         /  sepustus (mikä merkki on mitä)");
  AddLine("  v  versio         Q  luovuta");
  AddLine("");
  AddLine("  Versiosta 2.24 lisätyt:");
  AddLine("  S  talletus (peli jatkuu seuraavalla käynnistyskerralla)");
  AddLine("  p  viimeisin viesti   c  kello   ^L  piirrä ruutu uudelleen");
  AddLine("");
  AddLine("  Ovet: ALCO (17,7) myy juomia, DIVARI (17,18) ostaa tavaroita.");
  ShowPagedText();
  ForceRedraw = 1;
}

/* '/' : "Sepusta mistä >" -- explain a map glyph from SPU.SPE */
static void Sepustus(void)
{
  int c;
  u16 i;
  const char *found = NULL;
  c = AskKey("Sepusta mistä >");
  LoadLines("SPU.SPE");
  for (i = 0; i < LineCount; i++)
    if (Lines[i][0] && (u8)Lines[i][0] == (u8)c) {
      found = Lines[i];
      break;
    }
  if (!found) SetMsg("Eihän tommosia olekaan!");
  else SetMsg(found);
}

/* ===========================================================================
   Inventory, drinking, eating
   =========================================================================== */
/* inventory panel (0x501C): drink counts, then entries 3..11 as 1..9 */
static void DrawInventory(void)
{
  char s[40];
  int i;
  SFill(1, 2, 12, 15);
  sprintf(s, "Kaljaa   %u", InvKalja);
  SPut(2, 2, s, A_NORM);
  sprintf(s, " Lonkkaa  %u", InvLonkka);
  SPut(1, 3, s, A_NORM);
  sprintf(s, " Kiekuraa %u", InvKiekura);
  SPut(1, 4, s, A_NORM);
  for (i = 3; i <= 11; i++) {
    sprintf(s, "%d. %s", i - 2, Equip[i] ? EquipName[i] : "");
    SPut(1, i + 2, s, A_NORM);
  }
}

static void DoInventory(void)
{
  DrawInventory();
  SPut(1, 15, "PAINA Enter", A_BOLD);
  CurRow = 15;
  CurCol = 12;
  ReadKey();
  SFill(1, 2, 12, 15);
}

/* consume routine 0x7E50: kalja +0.2, lonkka +0.3 promille */
static void DoDrink(void)
{
  int ch;
  if (InvKalja + InvLonkka == 0) {
    SetMsg("Valitan, sulla on juomiset lopussa.");
    return;
  }
  if (InvKalja == 0) ch = '2';
  else if (InvLonkka == 0) ch = '1';
  else ch = AskOneOf("Kumpaako kittaat, kaljaa vai lonkkua (1/2) >", "012");
  SetMsg("");
  if (ch == '1') {
    Dec8(&InvKalja);
    Prom += PI10(2);
    SetMsg("Gluglugluglugluglu...");
  } else if (ch == '2') {
    Dec8(&InvLonkka);
    Prom += PI10(3);
    SetMsg("Gluglugluglugluglu...");
  }
}

static void DoEat(void)
{
  if (InvKiekura == 0)
    SetMsg("Et omista mitään ruoaksi kelpaavaa.");
  else {
    Dec8(&InvKiekura);
    Voima += VI(1);
    SetMsg("Röyh...");
  }
}

/* ===========================================================================
   Shops and building doors
   =========================================================================== */
/* door (17,7) under the top-left building (0x7774..0x794D) */
static void AlcoShop(void)
{
  int ch, price;
  char buf[40];
  if (Prom > PI10(10)) {
    SetMsg("Teille ei myydä, te olette humalassa!");
    return;
  }
  if (Mk < 15 || !FlagAlco) {
    SetMsg("Putiikki vaikuttaa suljetulta.");
    return;
  }
  ch = AskOneOf("Ja mitähan herralle saisi olla? (1=kalja/2=lonkka) >", "012");
  if (ch != '1' && ch != '2') { SetMsg(""); return; }
  price = 5 * (ch - '0' + 1);
  if (ch == '1') Inc8(&InvKalja); else Inc8(&InvLonkka);
  AddMk(-price);
  sprintf(buf, "%d mk, kiitos.", price);
  SetMsg(buf);
}

/* pawn shop door (17,18) (0x7993..0x7CC6) */
static void Divari(void)
{
  int ch, e, price = 0;
  char buf[MSGMAX];
  DrawInventory();
  sprintf(buf, "Tervetuloa, %s, mitäs myisit? >", PlayerName);
  ch = AskOneOf(buf, "0123456789");
  SFill(1, 2, 12, 15);
  if (ch < '1' || ch > '9') { SetMsg(""); return; }
  e = ch - '0' + 2;
  if (!Equip[e]) { SetMsg(""); return; }
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
    default: break;
  }
  sprintf(buf, "%s meni %d markalla.", EquipName[e], price);
  SetMsg(buf);
  DrawMsg();
  PressAny();
  SetMsg("Näkemiin kiitos!");
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
      SetMsg("Saat lääkekuurin, 69 mk. AIDS ei enää paina.");
    } else SetMsg("Emme ota juoppoja!");
  }
  else if (PY == 18 && PX == 47) SetMsg("Ei ketään kotona.");
  else if (PY == 18 && PX == 58) SetMsg("Valitan, ei vapaita huoneita.");
  else {
    SetMsg("Isket päätäsi seinään, mielenkiintoista.");
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
  int x, y;
  SetMsg("Hän sammuu!");
  if (Voima > VI(5)) Msg("Pojat lähtevät nukkumaan.");
  else Msg("Punkkarit luikkivat pakoon.");
  PunkErase();
  for (y = MAP_Y0; y <= MAP_Y1; y++)
    for (x = MAP_X0; x <= MAP_X1; x++)
      if (Map[x][y] == T_PUNK || Map[x][y] == T_KINGI) Map[x][y] = T_FLOOR;
  PunksOn = 0;
  AddScore(2);
  if (IsFree(PX + 1, PY)) {
    if (!Equip[E_KETJUT]) Map[PX + 1][PY] = T_KETJUT;
    else if (Random(2) == 1) Map[PX + 1][PY] = T_MONEY;
  }
}

static void PoliceDrop(int x, int y)
{
  if (!Equip[E_PAMPPU]) SetTile(x, y, T_PAMPPU);
  else if (Random(10) == 9) SetTile(x, y, T_MONEY);
  else SetTile(x, y, T_FLOOR);
}

static void Attack(int x, int y)
{
  u8 t = Map[x][y];
  int d = 1 + Weapon, n;

  if (t >= T_POLICE1 && t <= T_POLICE5) {
    PoliceActive = 1;
    SetMsg("Huitaiset skoudea!");
    HP[t] -= d;
    if (Prom > PI10(5) && Prom < PI10(15)) HP[t]--;   /* just tipsy enough */
    if (HP[t] < 1) {
      Msg("Hän väsähtää!");
      KillBonus(6);
      AddScore(1);
      PoliceDrop(x, y);
    }
    return;
  }

  switch (t) {
    case T_MUMMO:
      if (HP[t] < d) {
        SetMsg("Isket mummon kumoon!");
        PoliceActive = 1;
        SetTile(x, y, T_MONEY);
      } else {
        SetMsg("Eläkeläinen lyö sinua käsilaukullaan!");
        HP[t] -= d;
        Hurt(K_MUMMO, VI(1));
      }
      break;
    case T_KAKE:
      SetMsg("Huitaiset Kakea.");
      if (InvKalja > 0) {
        Msg("Hän pöllii sinulta yhden kaljan.");
        Dec8(&InvKalja);
      }
      SetTile(x, y, T_FLOOR);
      break;
    case T_VARAS:
      SetMsg("Lyöt varasta.");
      HP[t] -= d;
      if (HP[t] < 1) {
        Msg("Varas tuupertuu maahan.");
        Voima += VI(1);
        AddScore(1);
        KillBonus(9);
        SetTile(x, y, T_MONEY);
      } else if (Mk > 0 && Random(3) == 0) {
        Msg("Lompakkosi kevenee!");
        AddMk(-Min(Mk, Random(4 * Level) + 1));
        SetTile(x, y, T_FLOOR);
      }
      break;
    case T_TEEKKARI:
      SetMsg("Lyöt opiskelijaa.");
      break;
    case T_PORTSARI:
      SetMsg("Otat erää poken kanssa!");
      Hurt(K_VAKIVALTA, VI(1 + Random(2)));
      break;
    case T_NEEKERI:
      PoliceActive = 1;
      if (Friendly) SetMsg("Tapaat vanhan isäsi!");
      else SetMsg("Hakkaat neekeriä.");
      if (Random(4) == 1) {
        Msg("Hän taintuu.");
        Voima += VI(1);
        SetTile(x, y, T_MONEY);
      } else Hurt(K_NEEKERI, VI(1));
      break;
    case T_HINTTI:
      if (Friendly) SetMsg("Tapaat mukavan pojan!");
      else {
        PoliceActive = 1;
        if (Random(4) == 1) {
          SetMsg("Hakkaat homoa, joka luhistuu kadulle.");
          KillBonus(6);
          Voima += VI(1);
          AddScore(2);
          SetTile(x, y, T_POOP);
        } else SetMsg("Hakkaat homoa, joka nauttii siitä!");
      }
      break;
    case T_GORILLA:
      if (Friendly) SetMsg("Sekoat elukkaan.");
      else {
        SetMsg("Hakkaat gorillaa.");
        HP[t] -= d;
        if (Equip[E_KETJUT]) HP[t]--;
        if (HP[t] < 1) {
          Msg("Otus uinahtaa.");
          AddScore(1);
          KillBonus(10);
          SetTile(x, y, T_FLOOR);
        }
      }
      break;
    case T_SKIN:
      if (Friendly) SetMsg("Taputat klania.");
      else {
        SetMsg("Hakkaat kaljua.");
        HP[t] -= d;
        if (HP[t] < 1) {
          int a, b;
          Msg("Hän uinahtaa.");
          AddScore(2);
          a = Random(2);
          b = Random(2);
          if (a * b == 1 && Kokemus < 18) {
            Kokemus++;
            Voima += VI(3);
          }
          SetTile(x, y, T_HINT);
        }
      }
      break;
    case T_SNAKE:
      SetMsg("Isket luikertelijaa.");
      HP[t] -= d;
      if (HP[t] < 1) {
        Msg("Otus uinahtaa.");
        AddScore(1);
        SetTile(x, y, T_MAKKARA);
      }
      break;
    case T_AIDS:
      SetMsg("Isket Sven-Olofia.");
      HP[t] -= d;
      if (HP[t] < 1) {
        Msg("Hän uinahtaa.");
        AddScore(1);
        if (Random(4) == 2) SetTile(x, y, T_CHOCO); else SetTile(x, y, T_FLOOR);
      }
      break;
    case T_PAPPI:
      PoliceActive = 1;
      SetMsg("Lyöt pappia mutta Jeesus suojelee häntä!");
      break;
    case T_HIPPI:
      SetMsg("Tempaiset narkkista.");
      HP[t] -= d;
      if (HP[t] <= 0) {
        Msg("Hän ottaa tripin!");
        AddScore(1);
        SetTile(x, y, T_FLOOR);
      }
      break;
    case T_TROKARI:
      SetMsg("Trokari väistää.");
      break;
    case T_PIRKKO:
      if (HP[t] > 0) {
        /* milk one banked dot back out of the dot-eater */
        int rx, ry;
        HP[t]--;
        if (RandomCell(25, 50, 3, 20, 0, &rx, &ry)) SetTile(rx, ry, T_DOT);
        SetMsg("");
      } else {
        SetMsg("Tuhoat pistepirkon.");
        AddScore(1);
        SetTile(x, y, T_FLOOR);
      }
      break;
    case T_DOG:
      if (Friendly) SetMsg("Taputat hauvaa.");
      else {
        SetMsg("Hakkaat koiraa.");
        HP[t] -= d;
        if (Equip[E_KETJUT]) HP[t] -= 2;
        if (HP[t] < 1) {
          Msg("Hauveli uinahtaa.");
          AddScore(1);
          KillBonus(9);
          SetTile(x, y, T_FLOOR);
        }
      }
      break;
    case T_PUNK:
      SetMsg("Satutat rystysesi niitteihin.");
      Hurt(K_PUNKKARIT, V_05);
      break;
    case T_KINGI:
      SetMsg("Sivallat punkkarien kingiä.");
      HP[t] -= d;
      if (HP[t] < 1) PunkBlackout();
      break;
    case T_JUPPI:
      SetMsg("Lyöt juppia.");
      HP[t] -= d;
      if (HP[t] < 1) {
        Msg("Jupin kunto pettää.");
        Voima += VI(1);
        AddScore(2);
        KillBonus(11);
        SetTile(x, y, T_MONEY);
      }
      break;
    case T_KARKURI:
      SetMsg("Lyöt vankikarkuria.");
      HP[t] -= d;
      if (HP[t] < 1) {
        Msg("Mies tuupertuu maahan.");
        Voima += VI(1);
        AddScore(2);
        KillBonus(12);
        SetTile(x, y, T_STILETTI);
      }
      break;
    case T_GHOST:
      SetMsg("Huidot aavetta!");
      HP[t] -= d;
      if (HP[t] < 1) {
        Msg("Aave luhistuu kasaan!");
        AddScore(2);
        Voima += VI(5);
        if (Random(10) == 9) SetTile(x, y, T_MAKKARA); else SetTile(x, y, T_FLOOR);
      }
      break;
    case T_GRAVE:
      SetMsg("Eipäs tallata hautoja!");
      Hurt(K_ILKEYS, V_05);
      /* "Älä tallaa hautoja, aaveet suuttuvat" */
      if (!FindTile(T_GHOST, &n, &n) && Random(3) == 0) {
        Place(T_GHOST);
        HP[T_GHOST] = (s16)(8 + Random(8));
      }
      break;
    case T_FENCE:
      SetMsg("");
      break;
    default:
      break;
  }
}

/* ===========================================================================
   Player movement and pick-ups
   =========================================================================== */
static void PickUp(u8 t)
{
  int n, a;
  char buf[80];
  switch (t) {
    case T_DOT:
      Inc8(&DotsGot);
      /* 0x57BA: only said while you are blind */
      if (Blind) SetMsg("Sokea kana löysi jyvän!");
      break;
    case T_HAALARI:
      Equip[E_HAALARI] = 1;
      SetMsg("Löydät haalarit.");
      break;
    case T_PAMPPU:
      if (!Equip[E_PAMPPU]) {
        Equip[E_PAMPPU] = 1;
        RecomputeWeapon();
        SetMsg("Takavarikoit kytän pampun.");
      } else SetMsg("Maassa lojuu risa pamppu.");
      break;
    case T_STILETTI:
      if (!Equip[E_STILETTI]) {
        Equip[E_STILETTI] = 1;
        RecomputeWeapon();
        SetMsg("Otat maassa lojuvan stiletin.");
      } else SetMsg("Maassa lojuu tylsä voiveitsi.");
      break;
    case T_KETJUT:
      if (!Equip[E_KETJUT]) {
        Equip[E_KETJUT] = 1;
        RecomputeWeapon();
        SetMsg("Korjaat ketjut talteen.");
      } else SetMsg("Näet maassa katkenneet ketjut.");
      break;
    case T_CAMEL:
      if (!Equip[E_CAMEL]) {
        AddScore(1);
        Voima += VI(1);
      }
      Equip[E_CAMEL] = 1;
      RecomputeWeapon();
      SetMsg("Löydät aidot Camel bootsit!");
      break;
    case T_KONJAKKI:
      SetMsg("Löydät konjakkipullon ja korkkaat sen!");
      Voima += VI(3);
      Prom += PI10(5);
      Equip[E_HENNESSY] = 1;
      break;
    case T_MONEY:
      n = Random(21);
      AddMk(n);
      sprintf(buf, "Lompakkosi lihavoituu %d markalla.", n);
      SetMsg(buf);
      break;
    case T_HINT:
      HintPager();
      break;
    case T_MILK:
      SetMsg("Kaadat maitokannun!");
      AddScore(-1);
      break;
    case T_CHOCO:
      SetMsg("Ahmit maahan pudonneen suklaalevyn!");
      Voima += VI(1);
      AddScore(1);
      break;
    case T_APPLE:
      a = AskOneOf("Tässä on omena, vaan uskallatko syödä? (k/e)", "kKeE");
      SetMsg("");
      if (a == 'k' || a == 'K') {
        if (Random(3) == 0) {
          SetMsg("Omena oli pilaantunut!");
          AddScore(-1);
          Hurt(K_MYRKYTYS, VI(3));
        } else {
          SetMsg("Omena oli herkullinen!");
          Voima += VI(1);
        }
      }
      break;
    case T_SILAKKA:
      SetMsg("Hyi h..i, mikä täällä lemuaa!");
      Hurt(K_MYRKYTYS, V_01);
      break;
    case T_MAKKARA:
      Inc8(&InvKiekura);
      SetMsg("Kappas, kiekuralenkki.");
      break;
    case T_POOP:
      SetMsg("Liukastut paskaan!");
      AddScore(-1);
      Hurt(K_KALLO, VI(1));
      break;
    case T_WATER:
      if (!Equip[E_HAALARI]) {
        SetMsg("Et osaa uida, hukut!");
        Killer = K_HUKKUMINEN;
        Die("Et osaa uida, hukut!");
      } else {
        SetMsg("Varpaasi kastuvat!");
        Hurt(K_VILUSTUMINEN, V_03);
      }
      break;
    case T_KALJA:
      Inc8(&InvKalja);
      SetMsg("Miehen mittainen!");
      break;
    case T_LONKKA:
      Inc8(&InvLonkka);
      SetMsg("1 Lonkka for the road!");
      break;
    default:
      SetMsg("");
      break;
  }
}

/* Returns 1 if a turn was used.  A step off the map edge is silently
   ignored (the dispatcher's edge pre-check), everything else costs a turn. */
static u8 TryMove(int dx, int dy, u8 *drunk)
{
  int nx, ny;
  u8 t;
  *drunk = 0;
  if (Prom > PI10(30)) {
    /* drunk stagger (0x55A0): a random step instead of the intended one */
    *drunk = 1;
    dx = Random(3) - 1;
    dy = Random(3) - 1;
  }
  nx = PX + dx;
  ny = PY + dy;
  if (!InMap(nx, ny)) {
    if (*drunk) SetMsg("");
    return *drunk;            /* a drunk stumble still wastes the turn */
  }
  if (dx == 0 && dy == 0) { SetMsg(""); return 1; }
  t = Map[nx][ny];
  if (t == T_WALL) { BumpWall(); return 1; }
  if (t > 0x1D) { Attack(nx, ny); return 1; }
  /* walkable: move, then handle what was there */
  PX = (u8)nx;
  PY = (u8)ny;
  if (t != T_EXIT && t != T_WATER) Map[nx][ny] = T_FLOOR;
  PickUp(t);
  return 1;
}

/* ===========================================================================
   Throw (t) -- v1.17 spec (file 0x81A7..0x9290)
   =========================================================================== */
static int ThrowDamage(int key)
{
  switch (key) {
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 2;
    case '8': return 10;
    case '9': return 3;
    case 'a': return 1;
    case 'b': return 1;
    default:  return 0;
  }
}

static void ThrowHit(int key, int dmg, int cx, int cy, u8 t);

static u8 DoThrow(void)
{
  int key, dkey, dx, dy, entry, dmg, cx, cy, nx, ny;
  u8 t, item;
  DrawInventory();
  key = AskOneOf("Mitä viskaat (1-9 tavara, a=kalja b=lonkka c=kiekura, 0=peru) >",
                 "0123456789abc");
  SFill(1, 2, 12, 15);
  if (!((key >= '1' && key <= '9') || (key >= 'a' && key <= 'c'))) {
    SetMsg("");
    return 0;
  }
  item = T_FLOOR;
  if (key >= '1' && key <= '9') {
    entry = key - '0' + 2;
    if (!Equip[entry]) { SetMsg("Sinulla ei ole sellaista."); return 0; }
    item = (u8)entry;            /* the item's tile code equals its entry */
    if (entry > E_CAMEL) item = T_FLOOR;
  } else if (key == 'a') {
    if (InvKalja == 0) { SetMsg("Sinulla ei ole kaljaa."); return 0; }
    item = T_KALJA;
  } else if (key == 'b') {
    if (InvLonkka == 0) { SetMsg("Sinulla ei ole lonkkaa."); return 0; }
    item = T_LONKKA;
  } else {
    if (InvKiekura == 0) { SetMsg("Sinulla ei ole kiekuraa."); return 0; }
    item = T_MAKKARA;
  }

  dkey = AskOneOf("Suunta >", ".hjklyubn");
  dx = 0; dy = 0;
  switch (dkey) {
    case 'h': dx = -1; break;
    case 'l': dx = 1; break;
    case 'k': dy = -1; break;
    case 'j': dy = 1; break;
    case 'y': dx = -1; dy = -1; break;
    case 'u': dx = 1; dy = -1; break;
    case 'b': dx = -1; dy = 1; break;
    case 'n': dx = 1; dy = 1; break;
    default: break;
  }
  if ((dx == 0 && dy == 0) || !InMap(PX + dx, PY + dy)) {
    SetMsg("Hassu suunta!");
    return 0;
  }

  /* the item leaves the inventory */
  dmg = ThrowDamage(key);
  if (key >= '1' && key <= '9') {
    Equip[key - '0' + 2] = 0;
    RecomputeWeapon();
  } else if (key == 'a') Dec8(&InvKalja);
  else if (key == 'b') Dec8(&InvLonkka);
  else Dec8(&InvKiekura);

  /* flight: '*' trail over empty cells, stop at anything bigger than $1D */
  cx = PX; cy = PY;
  for (;;) {
    nx = cx + dx; ny = cy + dy;
    if (!InMap(nx, ny)) break;
    cx = nx; cy = ny;
    t = Map[cx][cy];
    if (t > 0x1D) break;
    if (t == T_FLOOR) SPut(cx, cy, "*", A_YELLOW);
  }
  CurCol = PX;
  CurRow = PY;
  SFlush();
  SleepMs(120);

  t = Tile(cx, cy);
  SetMsg("");
  if (!InMap(cx, cy) || (t <= 0x1D && t != T_FLOOR))
    SetMsg("Heittosi suhahtaa ilmaan!");
  else if (t == T_FLOOR) {
    if (item != T_FLOOR) SetTile(cx, cy, item);
  } else
    ThrowHit(key, dmg, cx, cy, t);
  return 1;
}

static void ThrowHit(int key, int dmg, int cx, int cy, u8 t)
{
  if (t >= T_POLICE1 && t <= T_POLICE5) {
    SetMsg("Heität skoudea!");
    PoliceActive = 1;
    HP[t] -= 1 + dmg;
    if (HP[t] <= 0) {
      SetMsg("Poliisi pökertyy!");
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
      HP[t] -= dmg;
      if (HP[t] <= 0) {
        SetMsg("Hän tuupertuu ketoon.");
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
      HP[t] -= dmg;
      if (HP[t] <= 0) {
        SetMsg("Hän sammahtaa.");
        SetTile(cx, cy, T_FLOOR);
        PortsariOn = 0;
      } else SetMsg("Osut portsariin.");
      break;
    case T_PAPPI:
      HP[t] -= dmg;
      SetMsg("Osut pappismieheen.");
      PoliceActive = 1;
      break;
    case T_HIPPI:
      SetMsg("Hän sammahtaa ja kolehtihaavi aukeaa.");
      SetTile(cx, cy, T_MONEY);
      if (key == '3') SetTile(cx, cy, T_STILETTI);
      break;
    case T_TROKARI:
      if (key == '3') {
        /* "Lentävä veitsi pysäyttää trokarin" */
        SetMsg("Lentävä veitsi pysäyttää trokarin.");
        SetTile(cx, cy, T_KALJA);
      } else SetMsg("Trokari väistää.");
      break;
    case T_PUNK:
      SetMsg("Heittosi osuu punkkariin.");
      break;
    case T_KINGI:
      SetMsg("Heittosi osuu kingiin.");
      HP[t] -= dmg;
      if (HP[t] < 1) PunkBlackout();
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
      SetMsg("Viskaat gorillaa.");
      HP[t] -= dmg;
      if (HP[t] < 1) { Msg("Otus uinahtaa."); SetTile(cx, cy, T_FLOOR); }
      break;
    case T_SKIN:
      SetMsg("Viskaat klania.");
      HP[t] -= dmg;
      if (HP[t] < 1) { Msg("Hän uinahtaa."); SetTile(cx, cy, T_HINT); }
      break;
    case T_SNAKE:
      SetMsg("Viskaat käärmettä.");
      HP[t] -= dmg;
      if (HP[t] < 1) { Msg("Otus uinahtaa."); SetTile(cx, cy, T_MAKKARA); }
      break;
    case T_AIDS:
      SetMsg("Osut potilaaseen.");
      break;
    case T_HINTTI:
      SetMsg("Hintti väistää.");
      break;
    case T_DOG:
      SetMsg("Viskaat rakkia.");
      HP[t] -= dmg;
      if (HP[t] < 1) { Msg("Hauveli uinahtaa."); SetTile(cx, cy, T_FLOOR); }
      break;
    default:
      SetMsg("Heittosi suhahtaa ilmaan!");
      break;
  }
}

/* ===========================================================================
   Creatures' turn (NPC update chain 0x9EC4..0xE0E0)
   =========================================================================== */
static void RandomFloorCell(int *rx, int *ry)
{
  if (!RandomCell(20, 60, 3, 20, 0, rx, ry)) {
    *rx = -1;
    *ry = -1;
  }
}

#define MAXCOPS 32

/* police block (0xA7EC..0xB09F): asleep until a crime, then every officer
   walks one cell toward the player and beats him when adjacent */
static void UpdatePolice(void)
{
  u8 cx[MAXCOPS], cy[MAXCOPS], cv[MAXCOPS];
  u8 n = 0, p, t;
  int x, y, dx, dy, tx, ty, fx, fy, rx, ry, ox, oy;
  if (!PoliceActive) return;
  for (y = MAP_Y0; y <= MAP_Y1; y++)
    for (x = MAP_X0; x <= MAP_X1; x++)
      if (Map[x][y] >= T_POLICE1 && Map[x][y] <= T_POLICE5 && n < MAXCOPS) {
        cx[n] = (u8)x; cy[n] = (u8)y; cv[n] = Map[x][y];
        n++;
      }
  for (p = 0; p < n; p++) {
    ox = cx[p]; oy = cy[p];
    if (Map[ox][oy] != cv[p]) continue;
    dx = PX - ox; dy = PY - oy;
    if (Abs(dx) < 2 && Abs(dy) < 2) {
      if (Friendly) Msg("Ystävällinen herra ojentaa sinulle kukkasen!");
      else {
        Msg("Poliisi pamputtaa!");
        Hurt(K_POLIISI, VI(1 + Random(3)));
      }
      continue;
    }
    tx = ox + Sgn(dx); ty = oy + Sgn(dy);
    if (!InMap(tx, ty)) { tx = ox; ty = oy; }
    fx = tx; fy = ty;
    t = Map[tx][ty];
    if (t < T_DOT) {
      ;                                          /* walks over it */
    } else if (t == T_DOT) {
      RandomFloorCell(&rx, &ry);                 /* the dot is moved away */
      if (rx > 0) Map[rx][ry] = T_DOT;
      else { fx = ox; fy = oy; }
    } else if (t == T_EXIT) {
      RandomFloorCell(&rx, &ry);
      if (rx > 0) { fx = rx; fy = ry; }
      else { fx = ox; fy = oy; }
    } else if (t == T_MUMMO)    Msg("Poliisit vievät eukon turvaan.");
    else if (t == T_KAKE)       Msg("Poliisit nappaavat kaverisi Kaken.");
    else if (t == T_VARAS)      Msg("Poliisit pidättävät taskuvarkaan.");
    else if (t == T_PORTSARI) { Msg("Koppalakit nappaavat huijari-poken."); PortsariOn = 0; }
    else if (t == T_HINTTI)     Msg("Poliisi poistaa hintin.");
    else if (t == T_GORILLA)    Msg("Virkavalta poistaa karkulaisen.");
    else if (t == T_TROKARI)    Msg("Koppalakit nappaavat trokarin.");
    else if (t == T_HIPPI)      Msg("Poliisi vie hipin.");
    else if (t == T_DOG)        Msg("Virkavalta poistaa vesikauhuisen hurtan.");
    else if (t == T_KARKURI)    Msg("Poliisit pidättävät vankikarkurin.");
    else if (t == T_PIRKKO) {
      TeleportPirkko(tx, ty);
      if (Map[tx][ty] != T_FLOOR) { fx = ox; fy = oy; }
    } else if (t == T_TEEKKARI) {
      /* the officer swaps places with the student */
      Map[ox][oy] = T_TEEKKARI;
      Map[tx][ty] = cv[p];
      continue;
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
      Map[ox][oy] = T_FLOOR;
      Map[fx][fy] = cv[p];
    }
  }
}

/* dog block (0xCA4B..0xCEA1): bites when adjacent, otherwise walks toward
   the player and infects whatever it walks into */
static void UpdateDog(void)
{
  int x, y, tx, ty;
  u8 t, moved = 0;
  const char *inf = NULL;
  if (!FindTile(T_DOG, &x, &y)) return;
  if (Adjacent(x, y)) {
    if (Friendly) return;
    Msg("Piski puraisee sinua!");
    Hurt(K_RAKKIKOIRA, VI(1 + Random(4)));
    return;
  }
  tx = x + Sgn(PX - x);
  ty = y + Sgn(PY - y);
  t = Tile(tx, ty);
  if (t >= T_POLICE1 && t <= T_POLICE5) inf = "Poliisi saa verenmyrkytyksen.";
  else switch (t) {
    case T_MUMMO:   inf = "Mummeli jää hurtan hampaisiin."; break;
    case T_KAKE:    inf = "Kake saa verenmyrkytyksen."; break;
    case T_NEEKERI: inf = "Nekru saa verenmyrkytyksen."; break;
    case T_HINTTI:  inf = "Hintti saa verenmyrkytyksen."; break;
    case T_TROKARI: inf = "Trokari saa verenmyrkytyksen."; break;
    case T_PAPPI:   inf = "Pappi saa verenmyrkytyksen."; break;
    case T_HIPPI:   inf = "Hippi saa verenmyrkytyksen."; break;
    case T_PIRKKO:  TeleportPirkko(tx, ty); break;
    case T_GHOST:
      Msg("Haamut tappavat rakin.");
      Map[x][y] = T_FLOOR;
      return;
    case T_EXIT:
      Map[x][y] = T_FLOOR;                       /* the dog leaves the level */
      return;
    default: break;
  }
  if (inf) {
    Msg(inf);
    Map[tx][ty] = T_FLOOR;
  }
  if (IsFree(tx, ty)) moved = 1;
  else {
    tx = x + Sgn(PX - x); ty = y;
    if (tx != x && IsFree(tx, ty)) moved = 1;
    else {
      tx = x; ty = y + Sgn(PY - y);
      if (ty != y && IsFree(tx, ty)) moved = 1;
    }
  }
  if (moved) {
    Map[x][y] = T_FLOOR;
    Map[tx][ty] = T_DOG;
  }
}

/* dot-eater block (0xC943..0xCA4B): teleports to a random empty-or-dot
   cell every turn, eating (banking) the dot it lands on */
static void UpdatePirkko(void)
{
  int x, y, rx, ry;
  if (!FindTile(T_PIRKKO, &x, &y)) return;
  if (!RandomCell(22, 58, 3, 20, 1, &rx, &ry)) return;
  if (Map[rx][ry] == T_DOT) HP[T_PIRKKO]++;
  Map[x][y] = T_FLOOR;
  Map[rx][ry] = T_PIRKKO;
}

/* "Hippi huitoo sinua piikillään, ja osuu!" -- a long hallucination */
static void HippieTrip(void)
{
  u16 n = (u16)strlen(MsgLine);
  if (n > 0 && MsgLine[n - 1] == '!') {
    MsgLine[n - 1] = 0;
    StrCat(MsgLine, ", ja osuu!", MSGMAX);
  } else Msg("Hippi osuu!");
  Voima += VI(2);
  Prom += PI10(12);
  AddScore(1);
  Friendly = 1;
  FriendlyTurns = (s16)(40 + Random(40));
}

/* what every static creature does to a player standing next to it */
static void UpdateNeighbours(void)
{
  int x, y, a, n;
  u8 t;
  u8 done[T_COUNT];
  memset(done, 0, sizeof(done));
  for (y = PY - 1; y <= PY + 1; y++)
    for (x = PX - 1; x <= PX + 1; x++) {
      if (Dead || !InMap(x, y)) continue;
      t = Map[x][y];
      if (t > T_MAXCODE || done[t]) continue;
      done[t] = 1;
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
              Msg("Käteiskassasi kutistuu.");
              AddMk(-Min(Mk, Random(4 * Level) + 1));
              Map[x][y] = T_FLOOR;
            }
          }
          break;
        case T_KARKURI:
          Msg("Vankikarkuri pahoinpitelee sinua.");
          Hurt(K_MURHA, VI(Random(5)));
          break;
        case T_TEEKKARI:
          if (Mk > 30 && Random(2) == 0) {
            if (Equip[E_APY])
              Msg("Teekkari ylipuhuu sinut ostamaan äpyn, vaikka sinulla oli jo!");
            else {
              Msg("Teekkari ylipuhuu sinut ostamaan äpyn.");
              Kokemus++;
            }
            AddMk(-30);
            Equip[E_APY] = 1;
            Voima += VI(1);
            AddScore(1);
            Map[x][y] = T_FLOOR;
          }
          break;
        case T_KAKE:
          if (InvKalja > 0) {
            if (MsgLine[0]) { DrawMsg(); PressAny(); }
            if (AskYesNo("Kamusi Kake pummaa kaljaa, annatko (k/e)?")) {
              SetMsg("Kake kiittää ja katoaa.");
              Voima += VI(1);
              Dec8(&InvKalja);
              Map[x][y] = T_FLOOR;
            } else {
              SetMsg("Kake taputtaa palleaasi.");
              Hurt(K_KAKE, VI(1));
            }
          }
          break;
        case T_PAPPI:
          Msg("Pappi saarnaa sinulle viinan vaaroista.");
          /* Random * 3 < Prom, in promille units of 1/100000 */
          if (((s32)Random(30000) * 10L < Prom || Friendly) &&
              InvKalja + InvLonkka > 0) {
            Msg("Hetken hairahduksesta kaadat juomasi maahan!");
            Voima += VI(1);
            Kokemus++;
            AddScore(1);
            InvKalja = 0;
            InvLonkka = 0;
          }
          break;
        case T_HINTTI:
          if (Friendly) Msg("Joku hyväilee sinua.");
          else Msg("Hinttari hiplaa sinua!");
          Hurt(K_HINTTAILU, V_05);
          break;
        case T_TROKARI:
          if (Mk > 50) {
            if (MsgLine[0]) { DrawMsg(); PressAny(); }
            a = AskOneOf("Hämärä veikko tarjoaa taikajuomaa pullostaan, 50 mk (k/e)?",
                         "kKeE");
            if (a == 'k' || a == 'K') {
              AddMk(-50);
              if (Random(2) == 1) {
                SetMsg("Kurkkuasi lämmittää mukavasti.");
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
            Map[x][y] = T_FLOOR;
          }
          break;
        case T_JUPPI:
          Msg("Juppi uhoaa.");
          Hurt(K_JUPPI, V_02);
          break;
        case T_HIPPI:
          Msg("Hippi huitoo sinua piikillään!");
          Hurt(K_PIIKKI, V_05);
          if (!Friendly && Voima + VI(Random(13)) > VI(21)) HippieTrip();
          break;
        case T_SKIN:
          if (!Friendly) {
            Msg("Skinhead murjoo sinua apinan raivolla!");
            n = Min((int)TruncDiv(Voima, 3 * VS), 4);
            if (n < 0) n = 0;
            Hurt(K_VAKIVALTA, VI(1 + Random(n + 1)));
          }
          break;
        case T_SNAKE:
          Msg("Käärme puree sinua.");
          Hurt(K_KAARME, VI(1));
          if (Random(Max(1, (int)TruncDiv(Voima, VS))) == 0 && !Equip[E_STILETTI]) {
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
        default:
          break;
      }
    }
}

/* 0xA1AC: while you stand in the gang's mouth column it beats you up;
   otherwise the whole gang steps one column left and lines up with your row */
static void UpdatePunks(void)
{
  if (!PunksOn || Dead) return;
  if (PX == KingX - 1) {
    if (Friendly) {
      Msg("Tanssit jonkun mukavan pojan kanssa.");
      Hurt(K_PUNKKARIT, VI(1));
    } else {
      Msg("Punkkarit muiluttavat sinua!");
      Hurt(K_PUNKKARIT, VI(2));
    }
  } else if (KingX - 2 >= MAP_X0) {   /* the original walked off the map */
    PunkErase();
    KingY = PY;
    if (KingY < MAP_Y0) KingY = MAP_Y0;
    if (KingY > MAP_Y1) KingY = MAP_Y1;
    KingX--;
    PunkStamp();
  }
}

static void UpdateCreatures(void)
{
  UpdatePunks();
  UpdatePirkko();
  UpdateDog();
  UpdatePolice();
  UpdateNeighbours();
}

/* ===========================================================================
   End of turn (per-turn handler 0xE0E8..0xE36A)
   =========================================================================== */
static void EndTurn(void)
{
  Turn++;
  if (Friendly) {
    FriendlyTurns--;
    if (FriendlyTurns <= 0) Friendly = 0;
  }

  /* drunk state text + effect */
  if (Aids) {
    StateText = ST_AIDS;
    Hurt(K_AIDS, V_02);
  } else if (Prom < PS / 20) StateText = ST_SICK;            /* < 0.05 */
  else if (Prom > PI10(40)) {
    StateText = ST_VOMIT;
    Hurt(K_ALKOMYRKYTYS, VI(1));
    if (IsFree(PX + 1, PY)) Map[PX + 1][PY] = T_POOP;
  } else if (Prom > PI10(30)) StateText = ST_STAGGER;
  else StateText = ST_BLANK;

  /* Promille decay and Kankkunen */
  if (Prom >= 0) Prom -= P_DECAY;
  else {
    Killer = K_KRAPULA;
    Die(KANKKUNEN);
  }

  /* Voima regeneration (0x9B6D): Random/87 + Level/300 per turn, which is
     exactly Random(300) + 87*Level in units of 1/26100 */
  Voima += (s32)Random(300) + 87L * Level;

  CheckDeath();
}

/* ===========================================================================
   Descent ('>')
   =========================================================================== */
static void WinGame(void)
{
  /* Score := Score + 20 + Voima + Trunc(10*Prom), then truncated */
  s32 total = ((s32)Score + 20 + TruncDiv(Prom, PS / 10)) * VS + Voima;
  total = TruncDiv(total, VS);
  if (total > 32000) total = 32000;
  if (total < -32000) total = -32000;
  Score = (s16)total;
  Won = 1;
}

/* Returns 1 if a turn passed. */
static u8 TryDescend(void)
{
  int toll;
  char buf[96];
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
    sprintf(buf, "Poke vaatii %d mk juomarahaa, maksatko (k/e) >", toll);
    if (!AskYesNo(buf)) {
      SetMsg("Portsari ei päästä sinua portista.");
      return 1;
    }
    AddMk(-toll);
  }
  if (Equip[E_HENNESSY]) {
    WinGame();
    return 1;
  }
  NextLevel();
  SetMsg("");
  return 1;
}

/* ===========================================================================
   Debug: dump level 1 (screen x=14..79, y=3..22, one byte per cell)
   =========================================================================== */
void DumpMapToFile(const char *fn)
{
  FILE *f;
  int x, y;
  Level = 1;
  BuildLevel();
  f = fopen(fn, "wb");
  if (!f) return;
  for (y = MAP_Y0; y <= MAP_Y1; y++)
    for (x = MAP_X0; x <= MAP_X1; x++) fputc(Map[x][y], f);
  fclose(f);
}

/* ===========================================================================
   New game, name entry, main loop
   =========================================================================== */
void NewGame(void)
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

static void ReadName(void)
{
  int ch;
  u16 len = 0;
  char s[NAMEMAX], line[NAMEMAX + 24];
  char *p;
  s[0] = 0;
  SClear();
  for (;;) {
    SFill(1, 1, SCRW, 1);
    StrCopy(line, "Kerropa nimesi >", sizeof(line));
    StrCat(line, s, sizeof(line));
    SPut(1, 1, line, A_NORM);
    CurRow = 1;
    CurCol = (u8)(17 + ULen(s));
    CursorOn = 1;
    ch = ReadKey();
    CursorOn = 0;
    if (ch == 13 || ch == 10 || ch == 4) break;
    if (ch == 8 || ch == 127) {
      while (len > 0 && ((u8)s[len - 1] & 0xC0) == 0x80) s[--len] = 0;
      if (len > 0) s[--len] = 0;
    } else if (ch >= ' ' && ch < 0x80 && ULen(s) < 9 && len + 1 < NAMEMAX) {
      s[len++] = (char)ch;
      s[len] = 0;
    } else if (ch >= 0x80 && len < 20 && len + 1 < NAMEMAX) {
      s[len++] = (char)ch;     /* UTF-8 bytes of ä/ö */
      s[len] = 0;
    }
  }
  /* trim */
  p = s;
  while (*p == ' ') p++;
  len = (u16)strlen(p);
  while (len > 0 && p[len - 1] == ' ') p[--len] = 0;
  if (!p[0]) StrCopy(PlayerName, "Spurgu", NAMEMAX);
  else StrCopy(PlayerName, p, NAMEMAX);
}

static void Move(int c, int dx, int dy, u8 upper)
{
  int nx, ny;
  Acted = TryMove(dx, dy, &Drunk);
  nx = PX + dx;
  ny = PY + dy;
  if ((upper || WasRepeat) && Acted && !Drunk && !MsgLine[0] &&
      InMap(nx, ny) && Map[nx][ny] == T_FLOOR)
    RepeatCh = c;
  else
    RepeatCh = 0;
}

static void ShowClock(void)
{
  char buf[40], hm[8];
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  if (tm) strftime(hm, sizeof(hm), "%H:%M", tm);
  else StrCopy(hm, "??:??", sizeof(hm));
  sprintf(buf, "Kello on %s.", hm);
  SetMsg(buf);
}

void MainLoop(void)
{
  int ch;
  char buf[MSGMAX];

  Dead = 0; Won = 0; Saved = 0;
  DeathPre = NULL;
  DeathCause = NULL;
  Killer = K_TUNTEMATON;
  if (SaveFileExists() &&
      AskYesNo("Löytyi talletettu peli. Jatketaanko sitä (k/e)?") && LoadGame()) {
    DeleteSave();
    sprintf(buf, "Tervetuloa takaisin, %s!", PlayerName);
    SetMsg(buf);
  } else {
    ReadName();
    NewGame();
    sprintf(buf, "Heippa %s, tervetuloa SpurguXiin!", PlayerName);
    SetMsg(buf);
  }
  StateText = ST_BLANK;
  SClear();
  FullRedraw();

  RepeatCh = 0;
  while (!Dead && !Won && !Saved) {
    WasRepeat = RepeatCh != 0;
    if (WasRepeat) {
      ch = RepeatCh;
      SFlush();
      SleepMs(25);
    } else ch = ReadKey();
    Acted = 0;
    MsgLine[0] = 0;

    switch (ch) {
      case 'h': Move('h', -1, 0, 0); break;
      case 'H': Move('h', -1, 0, 1); break;
      case 'j': Move('j', 0, 1, 0); break;
      case 'J': Move('j', 0, 1, 1); break;
      case 'k': Move('k', 0, -1, 0); break;
      case 'K': Move('k', 0, -1, 1); break;
      case 'l': Move('l', 1, 0, 0); break;
      case 'L': Move('l', 1, 0, 1); break;
      case 'y': Move('y', -1, -1, 0); break;
      case 'Y': Move('y', -1, -1, 1); break;
      case 'u': Move('u', 1, -1, 0); break;
      case 'U': Move('u', 1, -1, 1); break;
      case 'b': Move('b', -1, 1, 0); break;
      case 'B': Move('b', -1, 1, 1); break;
      case 'n': Move('n', 1, 1, 0); break;
      case 'N': Move('n', 1, 1, 1); break;
      case '.': Acted = 1; RepeatCh = 0; break;
      case 'q': DoDrink(); Acted = 1; RepeatCh = 0; break;
      case 'e': DoEat(); Acted = 1; RepeatCh = 0; break;
      case 'i': RepeatCh = 0; DoInventory(); break;
      case 't': RepeatCh = 0; Acted = DoThrow(); break;
      case '>': RepeatCh = 0; Acted = TryDescend(); break;
      case 'v':
        RepeatCh = 0;
        SetMsg("SpurguX Vs. 1.17, Last edited 09.09.1987, (C) Petri Niska");
        break;
      case '?': RepeatCh = 0; ShowHelp(); break;
      case '/': RepeatCh = 0; Sepustus(); break;
      case 'p': RepeatCh = 0; StrCopy(MsgLine, LastMsg, MSGMAX); break; /* v2.24 */
      case 'c': RepeatCh = 0; ShowClock(); break;
      case 12:  RepeatCh = 0; ForceRedraw = 1; break;                   /* ^L */
      case 'S':                                                          /* v2.24 */
        RepeatCh = 0;
        if (AskYesNo("Talletetaanko peli ja lopetetaan (k/e)?")) {
          if (SaveGame()) Saved = 1;
          else SetMsg("Talletus epäonnistui!");
        } else SetMsg("");
        break;
      case 'Q': case 3: case 4:
        RepeatCh = 0;
        if (AskYesNo("Todella luovutat (k/e) ? ")) {
          Dead = 1;
          DeathCause = NULL;
          Killer = K_ITSEMURHA;
        } else SetMsg("");
        break;
      case 27: case 0:
        RepeatCh = 0;
        break;
      default:
        RepeatCh = 0;
        if (ch >= ' ') {
          sprintf(buf, "Tuntematon komento %c", ch);
          SetMsg(buf);
        }
        break;
    }

    if (Acted && !Dead && !Won) {
      UpdateCreatures();
      if (!Dead) EndTurn();
      if (RepeatCh != 0 && (MsgLine[0] || Dead)) RepeatCh = 0;
    }
    if (MsgLine[0]) StrCopy(LastMsg, MsgLine, MSGMAX);
    FullRedraw();
  }

  if (Saved) {
    char fn[PATHMAX];
    RestoreTerm();
    SavePath(fn);
    printf("Peli talletettu (%s). Näkemiin, %s!\n", fn, PlayerName);
    return;
  }

  if (Won) {
    SClear();
    sprintf(buf, "ONNITTELUT, %s", PlayerName);
    SPut(1, 1, buf, A_YELLOW);
    SPut(1, 2, " TEIT SEN ! Sinussa on ainesta !", A_YELLOW);
    CurRow = 3;
    CurCol = 1;
    ReadKeyAfterPause(1500);
  } else if (DeathCause) {
    /* each death has its own message; Kankkunen is only the sobering-up
       death, not an epilogue to every death */
    if (DeathPre) {
      SetMsg(DeathPre);
      FullRedraw();
      PressAnySlow();
    }
    SetMsg(DeathCause);
    FullRedraw();
    PressAnySlow();
  }
  ShowHiscore();
  RestoreTerm();
  if (Won)
    printf("ONNITTELUT, %s ! TEIT SEN ! Sinussa on ainesta !\n", PlayerName);
}
