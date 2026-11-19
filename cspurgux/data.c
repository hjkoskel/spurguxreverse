/* data.c -- data files (CP437 text), SPU.TOP high scores, save/load. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "spurgux.h"

#define MAXDIRS 5
#define RAWMAX  8192

static char DataDirs[MAXDIRS][PATHMAX];
static u8   NDataDirs = 0;
static u8   RawBuf[RAWMAX];
static char TextBuf[TEXTMAX];
static u16  TextLen = 0;

const char *Lines[MAXLINES];
u16 LineCount = 0;

/* ===========================================================================
   CP437 -> UTF-8
   =========================================================================== */
static const char *const Hi437[128] = {
  "Ç","ü","é","â","ä","à","å","ç","ê","ë","è","ï","î","ì","Ä","Å",
  "É","æ","Æ","ô","ö","ò","û","ù","ÿ","Ö","Ü","¢","£","¥","₧","ƒ",
  "á","í","ó","ú","ñ","Ñ","ª","º","¿","⌐","¬","½","¼","¡","«","»",
  "░","▒","▓","│","┤","╡","╢","╖","╕","╣","║","╗","╝","╜","╛","┐",
  "└","┴","┬","├","─","┼","╞","╟","╚","╔","╩","╦","╠","═","╬","╧",
  "╨","╤","╥","╙","╘","╒","╓","╫","╪","┘","┌","█","▄","▌","▐","▀",
  "α","ß","Γ","π","Σ","σ","µ","τ","Φ","Θ","Ω","δ","∞","φ","ε","∩",
  "≡","±","≥","≤","⌠","⌡","÷","≈","°","∙","·","√","ⁿ","²","■"," "
};

static void PutStr(char *dst, u16 *len, u16 size, const char *s)
{
  u16 n = (u16)strlen(s);
  if (*len + n + 1 > size) return;
  memcpy(dst + *len, s, n);
  *len = (u16)(*len + n);
  dst[*len] = 0;
}

void CP437ToUTF8(const u8 *src, u16 n, char *dst, u16 dstsize)
{
  u16 i, len = 0;
  char one[2];
  dst[0] = 0;
  one[1] = 0;
  for (i = 0; i < n; i++) {
    u8 c = src[i];
    if (c >= 0x80) PutStr(dst, &len, dstsize, Hi437[c - 0x80]);
    else if (c == 0x7F) PutStr(dst, &len, dstsize, "⌂");
    else if (c == 9) PutStr(dst, &len, dstsize, "        ");
    else if (c >= ' ') {
      one[0] = (char)c;
      PutStr(dst, &len, dstsize, one);
    }
  }
}

/* ===========================================================================
   Data directories
   =========================================================================== */
static void AddDir(const char *a, const char *b)
{
  u8 i;
  char tmp[PATHMAX];
  if (NDataDirs >= MAXDIRS) return;
  StrCopy(tmp, a, PATHMAX);
  StrCat(tmp, b, PATHMAX);
  for (i = 0; i < NDataDirs; i++)
    if (strcmp(DataDirs[i], tmp) == 0) return;
  StrCopy(DataDirs[NDataDirs++], tmp, PATHMAX);
}

static void DirOf(const char *path, char *out)
{
  char *p;
  StrCopy(out, path, PATHMAX);
  p = strrchr(out, '/');
  if (p) p[1] = 0;
  else StrCopy(out, "./", PATHMAX);
}

void InitDataDirs(const char *argv0)
{
  char cwd[PATHMAX], exe[PATHMAX], exeDir[PATHMAX];
  long n;
  if (!getcwd(cwd, PATHMAX - 2)) StrCopy(cwd, ".", PATHMAX);
  StrCat(cwd, "/", PATHMAX);
  n = (long)readlink("/proc/self/exe", exe, PATHMAX - 1);
  if (n > 0) exe[n] = 0;
  else StrCopy(exe, argv0, PATHMAX);
  DirOf(exe, exeDir);
  NDataDirs = 0;
  AddDir(cwd, "");
  AddDir(exeDir, "");
  AddDir(cwd, "spurg117/");
  AddDir(exeDir, "spurg117/");
  AddDir(exeDir, "../spurg117/");
}

static u8 FileExists(const char *fn)
{
  FILE *f = fopen(fn, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

u8 FindData(const char *name, char *out)
{
  u8 i;
  for (i = 0; i < NDataDirs; i++) {
    StrCopy(out, DataDirs[i], PATHMAX);
    StrCat(out, name, PATHMAX);
    if (FileExists(out)) return 1;
  }
  out[0] = 0;
  return 0;
}

/* ===========================================================================
   Text files: loads a CP437 file, splitting on CR LF, lone CR or lone LF.
   The lines live in TextBuf until the next LoadLines.
   =========================================================================== */
static void AddRawLine(const u8 *s, u16 n)
{
  char *dst;
  u16 room;
  if (LineCount >= MAXLINES || TextLen + 1 >= TEXTMAX) return;
  dst = TextBuf + TextLen;
  room = (u16)(TEXTMAX - TextLen);
  CP437ToUTF8(s, n, dst, room);
  Lines[LineCount++] = dst;
  TextLen = (u16)(TextLen + strlen(dst) + 1);
}

void AddLine(const char *s)
{
  if (LineCount < MAXLINES) Lines[LineCount++] = s;
}

u8 LoadLines(const char *name)
{
  char fn[PATHMAX];
  FILE *f;
  u16 size, i, start;
  LineCount = 0;
  TextLen = 0;
  if (!FindData(name, fn)) return 0;
  f = fopen(fn, "rb");
  if (!f) return 0;
  size = (u16)fread(RawBuf, 1, RAWMAX, f);
  fclose(f);
  start = 0;
  i = 0;
  while (i < size) {
    u8 c = RawBuf[i];
    if (c == 13) {
      AddRawLine(RawBuf + start, (u16)(i - start));
      if (i + 1 < size && RawBuf[i + 1] == 10) i++;
      start = (u16)(i + 1);
    } else if (c == 10) {
      AddRawLine(RawBuf + start, (u16)(i - start));
      start = (u16)(i + 1);
    } else if (c == 26) {
      break;
    }
    i++;
  }
  if (i > start) AddRawLine(RawBuf + start, (u16)(i - start));
  return 1;
}

/* ===========================================================================
   High scores (SPU.TOP, 20 records x 28 bytes)
   The score is the 6-byte Turbo Pascal real at r[10..15].  It is encoded
   and decoded here with integer arithmetic only.
   =========================================================================== */
#define TOP_FILE   "SPU.TOP"
#define TOP_COUNT  20
#define REC_SIZE   28
#define R_REAL     10
#define R_LEVEL    16
#define R_CMARK    18
#define R_CAUSE    19
#define CAUSE_MARK 0xC5

static u8 Slots[TOP_COUNT + 1][REC_SIZE];
static u8 NewRec[REC_SIZE];

/* Trunc(real48 + 0.0001) for values in the integer range */
static s16 RecScore(const u8 *r)
{
  u8 e = r[R_REAL];
  u8 sh;
  u32 m;
  if (e < 129) return 0;                 /* zero or below one */
  if (r[R_REAL + 5] & 0x80) return 0;    /* negative */
  sh = (u8)(e - 129);
  if (sh > 14) return 32767;
  /* 1.m with the top 23 mantissa bits: value * 2^(23-sh) */
  m = 0x800000UL | ((u32)(r[R_REAL + 5] & 0x7F) << 16) |
      ((u32)r[R_REAL + 4] << 8) | (u32)r[R_REAL + 3];
  m += (1UL << (23 - sh)) / 10000UL;
  return (s16)(m >> (23 - sh));
}

static void RealEncode(u8 *r, s16 v)
{
  u8 k, i;
  u32 m;
  for (i = 0; i < 6; i++) r[i] = 0;
  if (v <= 0) return;
  k = 0;
  while (k < 14 && (v >> (k + 1)) != 0) k++;
  m = ((u32)v - (1UL << k)) << (23 - k);
  r[0] = (u8)(k + 129);
  r[5] = (u8)((m >> 16) & 0x7F);
  r[4] = (u8)((m >> 8) & 0xFF);
  r[3] = (u8)(m & 0xFF);
}

static u16 RecLevel(const u8 *r)
{
  return (u16)(r[R_LEVEL] | (r[R_LEVEL + 1] << 8));
}

static void RecName(const u8 *r, char *out, u16 size)
{
  u8 ln = r[0];
  if (ln > 9) ln = 9;
  CP437ToUTF8(r + 1, ln, out, size);
}

/* NULL = no cause stored (old/DOS record), "*" = won the game */
static const char *RecCause(const u8 *r)
{
  if (r[R_CMARK] == CAUSE_MARK && r[R_CAUSE] < K_COUNT) {
    if (r[R_CAUSE] == K_WON) return "*";
    if (Causes[r[R_CAUSE]][0]) return Causes[r[R_CAUSE]];
  }
  return NULL;
}

static void RecLine(const u8 *r, char *out)
{
  char name[64];
  const char *c = RecCause(r);
  RecName(r, name, sizeof(name));
  if (c && c[0] == '*')
    sprintf(out, "%s selvisi tasolta %u konjakin kera!", name, RecLevel(r));
  else if (!c)
    sprintf(out, "%s menehtyi tasolle %u", name, RecLevel(r));
  else
    sprintf(out, "%s kuoli tasolle %u, kuolinsyy: %s.", name, RecLevel(r), c);
}

static u8 SameRec(const u8 *a, const u8 *b)
{
  u8 i;
  for (i = 0; i <= 4; i++) if (a[i] != b[i]) return 0;
  for (i = 10; i <= 15; i++) if (a[i] != b[i]) return 0;
  return 1;
}

static void TopPath(char *out)
{
  if (FindData(TOP_FILE, out)) return;
  if (!getcwd(out, PATHMAX - 16)) StrCopy(out, ".", PATHMAX);
  StrCat(out, "/", PATHMAX);
  StrCat(out, TOP_FILE, PATHMAX);
}

static u8 LoadTop(void)
{
  char fn[PATHMAX];
  FILE *f;
  u8 n = 0;
  memset(Slots, 0, sizeof(Slots));
  TopPath(fn);
  f = fopen(fn, "rb");
  if (!f) return 0;
  while (n < TOP_COUNT && fread(Slots[n + 1], 1, REC_SIZE, f) == REC_SIZE) n++;
  fclose(f);
  return n;
}

static void SaveTop(u8 count)
{
  char fn[PATHMAX];
  FILE *f;
  u8 i;
  TopPath(fn);
  f = fopen(fn, "wb");
  if (!f) return;               /* read-only: the table is still shown */
  for (i = 1; i <= count; i++) fwrite(Slots[i], 1, REC_SIZE, f);
  fclose(f);
}

/* names are stored as CP437 bytes; Finnish letters are mapped back */
static void UTF8ToCP437Name(const char *s, u8 *out, u8 *outlen, u8 max)
{
  u8 n = 0, l;
  const u8 *p = (const u8 *)s;
  while (*p && n < max) {
    l = U8Len(*p);
    if (l == 1) out[n++] = *p;
    else if (l == 2 && p[0] == 0xC3) {
      switch (p[1]) {
        case 0xA4: out[n++] = 0x84; break;   /* ä */
        case 0xB6: out[n++] = 0x94; break;   /* ö */
        case 0x84: out[n++] = 0x8E; break;   /* Ä */
        case 0x96: out[n++] = 0x99; break;   /* Ö */
        case 0xA5: out[n++] = 0x86; break;   /* å */
        case 0x85: out[n++] = 0x8F; break;   /* Å */
        default:   out[n++] = '?'; break;
      }
    } else out[n++] = '?';
    while (l > 0 && *p) { p++; l--; }
  }
  *outlen = n;
}

static void MakeRec(void)
{
  u8 len;
  s16 sc;
  memset(NewRec, 0, REC_SIZE);
  UTF8ToCP437Name(PlayerName, NewRec + 1, &len, 9);
  NewRec[0] = len;
  sc = (Score >= 32767) ? 32767 : (s16)(Score + 1);
  if (sc > 0) RealEncode(NewRec + R_REAL, sc);
  NewRec[R_LEVEL] = (u8)(Level & 0xFF);
  NewRec[R_LEVEL + 1] = 0;
  NewRec[R_CMARK] = CAUSE_MARK;
  NewRec[R_CAUSE] = Won ? (u8)K_WON : (Killer ? Killer : (u8)K_TUNTEMATON);
}

void ShowHiscore(void)
{
  u8 n, i, pos, count, upper;
  char line[160], num[16];
  MakeRec();
  n = LoadTop();
  pos = 1;
  while (pos <= TOP_COUNT && pos <= n &&
         RecScore(NewRec) <= RecScore(Slots[pos]))
    pos++;
  if (pos <= TOP_COUNT) {
    upper = (u8)(n + 1 < TOP_COUNT ? n + 1 : TOP_COUNT);
    for (i = upper; i >= pos + 1; i--) memcpy(Slots[i], Slots[i - 1], REC_SIZE);
    memcpy(Slots[pos], NewRec, REC_SIZE);
    count = upper;
  } else
    count = TOP_COUNT;
  SaveTop(count);

  SClear();
  SPut(30, 1, "20 PARASTA SPURGUA :", A_BOLD);
  for (i = 1; i <= count; i++) {
    sprintf(num, "%u.", i);
    SPut(2, i + 2, num, A_NORM);
    sprintf(num, "%6d", RecScore(Slots[i]));
    SPut(7, i + 2, num, A_NORM);
    RecLine(Slots[i], line);
    if (SameRec(Slots[i], NewRec)) {
      SPut(1, i + 2, ">", A_YELLOW);
      SPut(20, i + 2, line, A_YELLOW);
    } else
      SPut(20, i + 2, line, A_NORM);
  }
  if (pos > TOP_COUNT) {
    sprintf(num, "%6d", RecScore(NewRec));
    SPut(7, 23, num, A_NORM);
    RecLine(NewRec, line);
    SPut(20, 23, line, A_YELLOW);
  }
  SPut(30, 25, "SpurguX Vs. 1.17, (C) 1987 P.N.", A_NORM);
  CurRow = 25;
  CurCol = 1;
  ReadKeyAfterPause(1500);
}

/* ===========================================================================
   Save / load (v2.24 "S = talletus").  Byte-serialised, little-endian.
   =========================================================================== */
#define SAVE_FILE  "SPU.SAV"
#define SAVE_MAGIC "SPURGUXC89SAVE1"
#define SAVEMAX    4096

static u8  SaveBuf[SAVEMAX];
static u16 SavePos;
static u16 SaveLen;
static u8  SaveBad;

void SavePath(char *out)
{
  char *p;
  TopPath(out);
  p = strrchr(out, '/');
  if (p) p[1] = 0;
  else out[0] = 0;
  StrCat(out, SAVE_FILE, PATHMAX);
}

u8 SaveFileExists(void)
{
  char fn[PATHMAX];
  SavePath(fn);
  return FileExists(fn);
}

void DeleteSave(void)
{
  char fn[PATHMAX];
  SavePath(fn);
  remove(fn);
}

static void Put8(u8 v)
{
  if (SavePos < SAVEMAX) SaveBuf[SavePos++] = v;
  else SaveBad = 1;
}

static void Put16(u16 v)
{
  Put8((u8)(v & 0xFF));
  Put8((u8)(v >> 8));
}

static void Put32(u32 v)
{
  Put16((u16)(v & 0xFFFFUL));
  Put16((u16)((v >> 16) & 0xFFFFUL));
}

static void PutStr8(const char *s)
{
  u8 n = (u8)strlen(s), i;
  Put8(n);
  for (i = 0; i < n; i++) Put8((u8)s[i]);
}

static u8 Get8(void)
{
  if (SavePos < SaveLen) return SaveBuf[SavePos++];
  SaveBad = 1;
  return 0;
}

static u16 Get16(void)
{
  u16 lo = Get8();
  return (u16)(lo | ((u16)Get8() << 8));
}

static u32 Get32(void)
{
  u32 lo = Get16();
  return lo | ((u32)Get16() << 16);
}

static void GetStr8(char *out, u16 size)
{
  u8 n = Get8(), i;
  for (i = 0; i < n; i++) {
    u8 c = Get8();
    if (i + 1 < size) out[i] = (char)c;
  }
  out[n < size ? n : size - 1] = 0;
}

static s32 ToS32(u32 v)
{
  if (v & 0x80000000UL) return -(s32)((~v + 1UL) & 0x7FFFFFFFUL);
  return (s32)v;
}

/* one routine for both directions keeps the field order in sync */
static void Serialise(u8 save)
{
  u16 x, y, i;
  u8 *flags[14];
  flags[0] = &PoliceActive; flags[1] = &Friendly; flags[2] = &Aids;
  flags[3] = &Blind; flags[4] = &PortsariOn; flags[5] = &PunksOn;
  flags[6] = &FlagAlco; flags[7] = &FlagDivari; flags[8] = &FlagPoliisi;
  flags[9] = &FlagGrave; flags[10] = &FlagZoo; flags[11] = &FlagAlcove;
  flags[12] = &HotelName; flags[13] = &Killer;
  if (save) {
    for (x = 0; x < SCRW + 2; x++)
      for (y = 0; y < SCRH + 1; y++) Put8(Map[x][y]);
    Put8(PX); Put8(PY); Put8(Level); Put16((u16)Mk);
    Put32((u32)Voima); Put32((u32)Prom); Put32(Turn); Put16((u16)Score);
    Put8(DotsNeeded); Put8(DotsGot);
    Put8(InvKalja); Put8(InvLonkka); Put8(InvKiekura);
    for (i = 0; i < E_COUNT; i++) Put8(Equip[i]);
    Put16(Kokemus);
    for (i = 0; i < T_COUNT; i++) Put16((u16)HP[i]);
    Put16((u16)FriendlyTurns);
    Put8(KingX); Put8(KingY);
    for (i = 0; i < 5; i++) Put8(PunkUnder[i]);
    for (i = 0; i < 14; i++) Put8(*flags[i]);
  } else {
    for (x = 0; x < SCRW + 2; x++)
      for (y = 0; y < SCRH + 1; y++) Map[x][y] = Get8();
    PX = Get8(); PY = Get8(); Level = Get8(); Mk = (s16)Get16();
    Voima = ToS32(Get32()); Prom = ToS32(Get32()); Turn = Get32();
    Score = (s16)Get16();
    DotsNeeded = Get8(); DotsGot = Get8();
    InvKalja = Get8(); InvLonkka = Get8(); InvKiekura = Get8();
    for (i = 0; i < E_COUNT; i++) Equip[i] = Get8();
    Kokemus = Get16();
    for (i = 0; i < T_COUNT; i++) HP[i] = (s16)Get16();
    FriendlyTurns = (s16)Get16();
    KingX = Get8(); KingY = Get8();
    for (i = 0; i < 5; i++) PunkUnder[i] = Get8();
    for (i = 0; i < 14; i++) *flags[i] = Get8();
  }
}

u8 SaveGame(void)
{
  char fn[PATHMAX];
  FILE *f;
  u8 ok;
  SavePos = 0;
  SaveBad = 0;
  PutStr8(SAVE_MAGIC);
  PutStr8(PlayerName);
  Serialise(1);
  if (SaveBad) return 0;
  SavePath(fn);
  f = fopen(fn, "wb");
  if (!f) return 0;
  ok = fwrite(SaveBuf, 1, SavePos, f) == SavePos;
  if (fclose(f) != 0) ok = 0;
  return ok;
}

u8 LoadGame(void)
{
  char fn[PATHMAX], magic[32];
  FILE *f;
  SavePath(fn);
  f = fopen(fn, "rb");
  if (!f) return 0;
  SaveLen = (u16)fread(SaveBuf, 1, SAVEMAX, f);
  fclose(f);
  SavePos = 0;
  SaveBad = 0;
  GetStr8(magic, sizeof(magic));
  if (SaveBad || strcmp(magic, SAVE_MAGIC) != 0) return 0;
  GetStr8(PlayerName, NAMEMAX);
  Serialise(0);
  if (SaveBad || Killer >= K_COUNT || PX < MAP_X0 || PX > MAP_X1 ||
      PY < MAP_Y0 || PY > MAP_Y1)
    return 0;
  RecomputeWeapon();
  return 1;
}
