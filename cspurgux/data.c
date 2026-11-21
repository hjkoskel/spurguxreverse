/* data.c -- the compiled-in text files (CP437), SPU.TOP high scores,
   save/load. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "spurgux.h"
#include "spudata.h"

/* one file is open at a time: they all share this stdio buffer, so the
   C library does not allocate one */
static char IoBuf[128];
static char ExeDir[PATHMAX];
static char Path[PATHMAX];

/* ===========================================================================
   Data directories: ., the program dir, ./spurg117, <prog>/spurg117,
   <prog>/../spurg117
   =========================================================================== */
void InitDataDirs(const char *argv0)
{
  char *p;
  long n = (long)readlink("/proc/self/exe", ExeDir, PATHMAX - 1);
  if (n > 0) ExeDir[n] = 0;
  else StrCopy(ExeDir, argv0, PATHMAX);
  p = strrchr(ExeDir, '/');
  if (p) p[1] = 0;
  else ExeDir[0] = 0;
}

static FILE *OpenF(const char *fn, const char *mode)
{
  FILE *f = fopen(fn, mode);
  if (f) setvbuf(f, IoBuf, _IOFBF, sizeof(IoBuf));
  return f;
}

static u8 FileExists(const char *fn)
{
  FILE *f = fopen(fn, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

/* full path of a data file in Path, or NULL */
static const char *FindData(const char *name)
{
  static const char *const Sub[5] = {
    "", "", "spurg117/", "spurg117/", "../spurg117/"
  };
  u8 i;
  for (i = 0; i < 5; i++) {
    Path[0] = 0;
    if (i & 1 || i == 4) StrCopy(Path, ExeDir, PATHMAX);
    StrCat(Path, Sub[i], PATHMAX);
    StrCat(Path, name, PATHMAX);
    if (FileExists(Path)) return Path;
  }
  return NULL;
}

/* ===========================================================================
   Text files: the original SPU.HLP, SPU.SPE and SPU.TIP are compiled in
   (spudata.h, made by tools/mktexts.py), so they are not needed at run
   time.  The lines are CP437 and at most SCRW characters long, as the old
   file reader returned them.
   =========================================================================== */
static const char *const HlpLines[] = { SPU_HLP_LINES };
static const char *const SpeLines[] = { SPU_SPE_LINES };
static const char *const TipLines[] = { SPU_TIP_LINES };

#define NLINES(a) ((u16)(sizeof(a) / sizeof((a)[0])))

static const struct {
  const char *name;
  const char *const *lines;
  u16 count;
} Texts[3] = {
  { "SPU.HLP", HlpLines, NLINES(HlpLines) },
  { "SPU.SPE", SpeLines, NLINES(SpeLines) },
  { "SPU.TIP", TipLines, NLINES(TipLines) }
};

static const char *const *TLines;
static u16 TCount, TNext;

u8 TextOpen(const char *name)
{
  u8 i;
  TLines = NULL;
  TCount = TNext = 0;
  for (i = 0; i < 3; i++)
    if (strcmp(name, Texts[i].name) == 0) {
      TLines = Texts[i].lines;
      TCount = Texts[i].count;
    }
  return TLines != NULL;
}

void TextRewind(void)
{
  TNext = 0;
}

void TextClose(void)
{
  TLines = NULL;
}

u8 TextLine(char *line)
{
  line[0] = 0;
  if (!TLines || TNext >= TCount) return 0;
  StrCopy(line, TLines[TNext++], SCRW + 1);
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
#define SLOT(i)    (Slots + ((i) - 1) * REC_SIZE)

static u8 Slots[TOP_COUNT * REC_SIZE];
static u8 NewRec[REC_SIZE];

/* Trunc(real48 + 0.0001) for values in the integer range */
static s16 RecScore(const u8 *r)
{
  u8 e = r[R_REAL], sh;
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
  u8 k = 0;
  u32 m;
  memset(r, 0, 6);
  if (v <= 0) return;
  while (k < 14 && (v >> (k + 1)) != 0) k++;
  m = ((u32)v - (1UL << k)) << (23 - k);
  r[0] = (u8)(k + 129);
  r[5] = (u8)((m >> 16) & 0x7F);
  r[4] = (u8)(m >> 8);
  r[3] = (u8)m;
}

/* "<name> menehtyi tasolle <n>" etc. into Path (used as a line buffer) */
static const char *RecLine(const u8 *r)
{
  char *o = Path;
  u8 ln = r[0] > 9 ? 9 : r[0], cause = 0xFF;
  const char *lv = NumStr(r[R_LEVEL] | (r[R_LEVEL + 1] << 8), 0);
  memcpy(o, r + 1, ln);
  o[ln] = 0;
  if (r[R_CMARK] == CAUSE_MARK && r[R_CAUSE] < K_COUNT) cause = r[R_CAUSE];
  if (cause == K_WON) {
    StrCat(o, " selvisi tasolta ", PATHMAX);
    StrCat(o, lv, PATHMAX);
    StrCat(o, " konjakin kera!", PATHMAX);
  } else if (cause == 0xFF) {
    StrCat(o, " menehtyi tasolle ", PATHMAX);
    StrCat(o, lv, PATHMAX);
  } else {
    StrCat(o, " kuoli tasolle ", PATHMAX);
    StrCat(o, lv, PATHMAX);
    StrCat(o, ", kuolinsyy: ", PATHMAX);
    StrCat(o, Causes[cause], PATHMAX);
    StrCat(o, ".", PATHMAX);
  }
  return o;
}

/* the original's "it is you" test: 5-byte name and 6-byte score match */
static u8 SameRec(const u8 *a, const u8 *b)
{
  return memcmp(a, b, 5) == 0 && memcmp(a + 10, b + 10, 6) == 0;
}

static const char *TopPath(void)
{
  const char *p = FindData(TOP_FILE);
  if (p) return p;
  StrCopy(Path, TOP_FILE, PATHMAX);
  return Path;
}

static void MakeRec(void)
{
  u8 len = (u8)strlen(PlayerName);
  memset(NewRec, 0, REC_SIZE);
  NewRec[0] = len;
  memcpy(NewRec + 1, PlayerName, len);
  if (Score < 32767 && Score + 1 > 0) RealEncode(NewRec + R_REAL, (s16)(Score + 1));
  NewRec[R_LEVEL] = Level;
  NewRec[R_CMARK] = CAUSE_MARK;
  NewRec[R_CAUSE] = Won ? (u8)K_WON : (Killer ? Killer : (u8)K_TUNTEMATON);
}

static void PutRow(u8 row, const u8 *r, u8 at)
{
  SPut(7, row, NumStr(RecScore(r), 6), A_NORM);
  SPut(20, row, RecLine(r), at);
}

void ShowHiscore(void)
{
  FILE *f;
  u8 n = 0, i, pos = 1, count, upper;
  s16 mine;
  MakeRec();
  mine = RecScore(NewRec);
  memset(Slots, 0, sizeof(Slots));
  if ((f = OpenF(TopPath(), "rb")) != NULL) {
    while (n < TOP_COUNT && fread(SLOT(n + 1), 1, REC_SIZE, f) == REC_SIZE) n++;
    fclose(f);
  }
  while (pos <= TOP_COUNT && pos <= n && mine <= RecScore(SLOT(pos))) pos++;
  if (pos <= TOP_COUNT) {
    upper = (u8)(n + 1 < TOP_COUNT ? n + 1 : TOP_COUNT);
    for (i = upper; i > pos; i--) memcpy(SLOT(i), SLOT(i - 1), REC_SIZE);
    memcpy(SLOT(pos), NewRec, REC_SIZE);
    count = upper;
  } else
    count = TOP_COUNT;
  if ((f = OpenF(TopPath(), "wb")) != NULL) {   /* read-only: still shown */
    fwrite(Slots, 1, (size_t)count * REC_SIZE, f);
    fclose(f);
  }

  SClear();
  SPut(30, 1, "20 PARASTA SPURGUA :", A_BOLD);
  for (i = 1; i <= count; i++) {
    SPut(2, i + 2, NumStr(i, 0), A_NORM);
    SPut(i < 10 ? 3 : 4, i + 2, ".", A_NORM);
    if (SameRec(SLOT(i), NewRec)) {
      SPut(1, i + 2, ">", A_YELLOW);
      PutRow((u8)(i + 2), SLOT(i), A_YELLOW);
    } else
      PutRow((u8)(i + 2), SLOT(i), A_NORM);
  }
  if (pos > TOP_COUNT) PutRow(23, NewRec, A_YELLOW);
  SPut(30, 25, "SpurguX Vs. 1.17, (C) 1987 P.N.", A_NORM);
  CurRow = 25;
  CurCol = 1;
  ReadKeyAfterPause(1500);
}

/* ===========================================================================
   Save / load (v2.24 "S = talletus").  Written field by field,
   little-endian, straight to the file (no buffer).
   =========================================================================== */
#define SAVE_FILE  "SPU.SAV"
#define SAVE_MAGIC "SPURGUXC89SAVE2"

static FILE *SF;
static u8 Saving, SaveBad;

/* the save file lives next to SPU.TOP */
const char *SavePathStr(void)
{
  char *p;
  TopPath();
  p = strrchr(Path, '/');
  if (p) p[1] = 0;
  else Path[0] = 0;
  StrCat(Path, SAVE_FILE, PATHMAX);
  return Path;
}

u8 SaveFileExists(void)
{
  return FileExists(SavePathStr());
}

void DeleteSave(void)
{
  remove(SavePathStr());
}

static void F8(u8 *v)
{
  int c;
  if (Saving) {
    if (fputc(*v, SF) == EOF) SaveBad = 1;
  } else {
    if ((c = fgetc(SF)) == EOF) SaveBad = 1;
    *v = (u8)c;
  }
}

static void F16(u16 *v)
{
  u8 lo = (u8)*v, hi = (u8)(*v >> 8);
  F8(&lo);
  F8(&hi);
  *v = (u16)(lo | (hi << 8));
}

static void F32(u32 *v)
{
  u16 lo = (u16)(*v & 0xFFFFUL), hi = (u16)(*v >> 16);
  F16(&lo);
  F16(&hi);
  *v = (u32)lo | ((u32)hi << 16);
  if (*v & 0x80000000UL) *v |= ~0xFFFFFFFFUL;   /* sign-extend for s32 */
}

static void FN(u8 *p, u16 n)
{
  while (n--) F8(p++);
}

/* one routine for both directions keeps the field order in sync */
static u8 Serialise(void)
{
  static char magic[] = SAVE_MAGIC;
  char m[sizeof(magic)];
  u8 i;
  memcpy(m, magic, sizeof(m));
  FN((u8 *)m, sizeof(m));
  if (memcmp(m, magic, sizeof(m)) != 0) return 0;
  FN((u8 *)PlayerName, NAMEMAX);
  PlayerName[NAMEMAX - 1] = 0;
  FN(Map, MAPW * MAPH);
  F8(&PX); F8(&PY); F8(&Level); F16((u16 *)&Mk);
  F32((u32 *)&Voima); F32((u32 *)&Prom); F32(&Turn); F16((u16 *)&Score);
  F8(&DotsNeeded); F8(&DotsGot);
  F8(&InvKalja); F8(&InvLonkka); F8(&InvKiekura);
  FN(Equip, E_COUNT);
  F16(&Kokemus);
  FN((u8 *)HPt, HP_COUNT);
  F16((u16 *)&FriendlyTurns);
  F8(&KingX); F8(&KingY);
  FN(PunkUnder, 5);
  F8(&PoliceActive); F8(&Friendly); F8(&Aids); F8(&Blind);
  F8(&PortsariOn); F8(&PunksOn);
  F8(&FlagAlco); F8(&FlagDivari); F8(&FlagPoliisi); F8(&FlagGrave);
  F8(&FlagZoo); F8(&FlagAlcove); F8(&HotelName); F8(&Killer);
  for (i = 0; i < E_COUNT; i++) Equip[i] = Equip[i] != 0;
  return !SaveBad;
}

static u8 SaveIO(u8 save)
{
  u8 ok;
  SF = OpenF(SavePathStr(), save ? "wb" : "rb");
  if (!SF) return 0;
  Saving = save;
  SaveBad = 0;
  ok = Serialise();
  if (fclose(SF) != 0) ok = 0;
  return ok;
}

u8 SaveGame(void)
{
  return SaveIO(1);
}

u8 LoadGame(void)
{
  if (!SaveIO(0) || Killer >= K_COUNT || PX < MAP_X0 || PX > MAP_X1 ||
      PY < MAP_Y0 || PY > MAP_Y1)
    return 0;
  RecomputeWeapon();
  return 1;
}
