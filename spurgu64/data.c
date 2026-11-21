/* data.c -- C64 disk files: streamed text files (SEQ, PETSCII, CR line
   ends), the SPU.TOP high-score table and save/load.  Uses the cbm_*
   KERNAL wrappers instead of stdio, on the drive the game was loaded from. */
#include <cbm.h>
#include <string.h>
#include "spurgux.h"

#define LFN_FILE 2
#define LFN_CMD  15

static u8 Dev;
static char Name[24];

static u8 Device(void)
{
  if (!Dev) {
    Dev = *(volatile u8 *)0xBA;          /* the drive we were loaded from */
    if (Dev < 8) Dev = 8;
  }
  return Dev;
}

/* The command channel stays open: closing it would also close every
   other file open on the drive. */
static u8 CmdOpen;

static u8 CmdChannel(void)
{
  if (!CmdOpen) CmdOpen = cbm_open(LFN_CMD, Device(), 15, "") == 0;
  return CmdOpen;
}

void DataClose(void)
{
  if (CmdOpen) cbm_close(LFN_CMD);
  CmdOpen = 0;
}

/* reads the whole drive status ("00, OK,00,00"): 1 when it is 00 */
static u8 DriveOk(void)
{
  char st[40];
  if (!CmdChannel()) return 0;
  return cbm_read(LFN_CMD, st, sizeof(st)) >= 2 && st[0] == '0' && st[1] == '0';
}

static void DriveCmd(const char *cmd)
{
  if (!CmdChannel()) return;
  cbm_write(LFN_CMD, cmd, strlen(cmd));
  DriveOk();                             /* reads "01, FILES SCRATCHED" etc. */
}

static void Scratch(const char *name)
{
  StrCopy(Name, "s0:", sizeof(Name));
  StrCat(Name, name, sizeof(Name));
  DriveCmd(Name);
}

/* "name,s,r" / "name,s,w" on LFN_FILE; 1 if the drive accepted it */
static u8 OpenSeq(const char *name, u8 write)
{
  StrCopy(Name, name, sizeof(Name));
  StrCat(Name, write ? ",s,w" : ",s,r", sizeof(Name));
  if (cbm_open(LFN_FILE, Device(), write ? CBM_WRITE : CBM_READ, Name) != 0)
    return 0;
  if (!DriveOk()) {
    cbm_close(LFN_FILE);
    return 0;
  }
  return 1;
}

/* ===========================================================================
   Text files (already converted to PETSCII by tools/mkdata.py)
   =========================================================================== */
static u8 TOpen, TEof;
static const char *TName;
static char TBuf[32];
static u8 TPos, TLen;

u8 TextOpen(const char *name)
{
  TName = name;
  TPos = TLen = 0;
  TEof = 0;
  TOpen = OpenSeq(name, 0);
  return TOpen;
}

void TextClose(void)
{
  if (TOpen) cbm_close(LFN_FILE);
  TOpen = 0;
}

void TextRewind(void)
{
  TextClose();
  TextOpen(TName);
}

static int TGet(void)
{
  int n;
  if (TPos >= TLen) {
    if (TEof) return -1;
    n = cbm_read(LFN_FILE, TBuf, sizeof(TBuf));
    if (n <= 0) { TEof = 1; return -1; }
    if (n < (int)sizeof(TBuf)) TEof = 1;
    TLen = (u8)n;
    TPos = 0;
  }
  return (u8)TBuf[TPos++];
}

u8 TextLine(char *line)
{
  int c;
  u8 n = 0, got = 0;
  line[0] = 0;
  if (!TOpen) return 0;
  for (;;) {
    c = TGet();
    if (c < 0) break;
    got = 1;
    if (c == 13 || c == 10) break;
    if (n < MSGW) line[n++] = (char)c;
  }
  line[n] = 0;
  return got;
}

/* ===========================================================================
   High scores (SPU.TOP, 20 records x 28 bytes, the DOS file format: names
   are CP437 and the score is a 6-byte Turbo Pascal real)
   =========================================================================== */
#define TOP_FILE   "spu.top"
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
static char Row[SCRW + 1];

/* the letters that differ between CP437 and the program's PETSCII */
static const u8 CpFin[6]  = { 0x84, 0x94, 0x86, 0x8E, 0x99, 0x8F };

static u8 FromCp437(u8 c)
{
  u8 i;
  if (c >= 0x41 && c <= 0x5A) return (u8)(c + 0x80);
  if (c >= 0x61 && c <= 0x7A) return (u8)(c - 0x20);
  for (i = 0; i < 6; i++) if (CpFin[i] == c) return (u8)(0x80 + i);
  return (c >= 0x20 && c < 0x40) ? c : '?';
}

static u8 ToCp437(u8 c)
{
  if (c >= 0xC1 && c <= 0xDA) return (u8)(c - 0x80);
  if (c >= 0x41 && c <= 0x5A) return (u8)(c + 0x20);
  if (c >= 0x80 && c <= 0x85) return CpFin[c - 0x80];
  return (c >= 0x20 && c < 0x40) ? c : '?';
}

/* Trunc(real48 + 0.0001) for values in the integer range */
static s16 RecScore(const u8 *r)
{
  u8 e = r[R_REAL], sh;
  u32 m;
  if (e < 129) return 0;
  if (r[R_REAL + 5] & 0x80) return 0;
  sh = (u8)(e - 129);
  if (sh > 14) return 32767;
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

static u8 SameRec(const u8 *a, const u8 *b)
{
  return memcmp(a, b, 5) == 0 && memcmp(a + 10, b + 10, 6) == 0;
}

static void MakeRec(void)
{
  u8 len = (u8)strlen(PlayerName), i;
  memset(NewRec, 0, REC_SIZE);
  NewRec[0] = len;
  for (i = 0; i < len; i++) NewRec[1 + i] = ToCp437((u8)PlayerName[i]);
  if (Score < 32767 && Score + 1 > 0) RealEncode(NewRec + R_REAL, (s16)(Score + 1));
  NewRec[R_LEVEL] = Level;
  NewRec[R_CMARK] = CAUSE_MARK;
  NewRec[R_CAUSE] = Won ? (u8)K_WON : (Killer ? Killer : (u8)K_TUNTEMATON);
}

/* one 40-column row: "nn. score name      tt cause" */
static void PutRow(u8 row, u8 rank, const u8 *r, u8 at)
{
  u8 ln = r[0] > 9 ? 9 : r[0], i, cause = 0xFF;
  const char *c;
  memset(Row, ' ', SCRW);
  Row[SCRW] = 0;
  if (rank) {
    c = NumStr(rank, 2);
    Row[0] = c[0]; Row[1] = c[1]; Row[2] = '.';
  }
  c = NumStr(RecScore(r), 5);
  memcpy(Row + 3, c, 5);
  for (i = 0; i < ln; i++) Row[9 + i] = (char)FromCp437(r[1 + i]);
  c = NumStr(r[R_LEVEL] | (r[R_LEVEL + 1] << 8), 2);
  memcpy(Row + 19, c, strlen(c) > 2 ? 2 : strlen(c));
  if (r[R_CMARK] == CAUSE_MARK && r[R_CAUSE] < K_COUNT) cause = r[R_CAUSE];
  if (cause == K_WON) c = "KONJAKKI!";
  else if (cause == 0xFF) c = "-";
  else c = Causes[cause];
  memcpy(Row + 22, c, strlen(c) > 18 ? 18 : strlen(c));
  SPut(0, row, Row, at);
}

void ShowHiscore(void)
{
  u8 n = 0, i, pos = 1, count, upper;
  s16 mine;
  int got;
  MakeRec();
  mine = RecScore(NewRec);
  memset(Slots, 0, sizeof(Slots));
  SClear();
  SPut(12, 12, "Levyke py" OE "rii...", A_DIM);
  if (OpenSeq(TOP_FILE, 0)) {
    got = cbm_read(LFN_FILE, Slots, sizeof(Slots));
    cbm_close(LFN_FILE);
    if (got > 0) n = (u8)(got / REC_SIZE);
  }
  while (pos <= TOP_COUNT && pos <= n && mine <= RecScore(SLOT(pos))) pos++;
  if (pos <= TOP_COUNT) {
    upper = (u8)(n + 1 < TOP_COUNT ? n + 1 : TOP_COUNT);
    for (i = upper; i > pos; i--) memcpy(SLOT(i), SLOT(i - 1), REC_SIZE);
    memcpy(SLOT(pos), NewRec, REC_SIZE);
    count = upper;
  } else
    count = TOP_COUNT;
  Scratch(TOP_FILE);
  if (OpenSeq(TOP_FILE, 1)) {        /* write-protected disk: still shown */
    cbm_write(LFN_FILE, Slots, (u16)count * REC_SIZE);
    cbm_close(LFN_FILE);
  }

  SClear();
  SPut(10, 0, "20 PARASTA SPURGUA :", A_BOLD);
  SPut(0, 1, "    pist nimi      ta kuolinsyy", A_DIM);
  for (i = 1; i <= count; i++)
    PutRow((u8)(i + 1), i, SLOT(i), SameRec(SLOT(i), NewRec) ? A_YELLOW : A_NORM);
  if (pos > TOP_COUNT) PutRow(22, 0, NewRec, A_YELLOW);
  SPut(4, 24, "SpurguX Vs. 1.17, (C) 1987 P.N.", A_NORM);
  CurRow = 24;
  CurCol = 39;
  ReadKeyAfterPause(1500);
}

/* ===========================================================================
   Save / load ('S').  Same field order as the C89 port, little-endian.
   =========================================================================== */
#define SAVE_FILE  "spu.sav"
#define SAVE_MAGIC "SPURGU64SAVE1"

static u8 Saving, SaveBad;

u8 SaveFileExists(void)
{
  u8 ok = OpenSeq(SAVE_FILE, 0);
  if (ok) cbm_close(LFN_FILE);
  return ok;
}

void DeleteSave(void)
{
  Scratch(SAVE_FILE);
}

static void FN(void *p, u16 n)
{
  if (SaveBad) return;
  if (Saving) {
    if (cbm_write(LFN_FILE, p, n) != (int)n) SaveBad = 1;
  } else {
    if (cbm_read(LFN_FILE, p, n) != (int)n) SaveBad = 1;
  }
}

/* cc65 is little-endian like the file, so the values are written as is */
#define F8(v)  FN(v, 1)
#define F16(v) FN(v, 2)
#define F32(v) FN(v, 4)

static u8 Serialise(void)
{
  static const char magic[] = SAVE_MAGIC;
  char m[sizeof(magic)];
  u8 i;
  memcpy(m, magic, sizeof(m));
  FN(m, sizeof(m));
  if (memcmp(m, magic, sizeof(m)) != 0) return 0;
  FN(PlayerName, NAMEMAX);
  PlayerName[NAMEMAX - 1] = 0;
  FN(Map, MAPW * MAPH);
  F8(&PX); F8(&PY); F8(&Level); F16(&Mk);
  F32(&Voima); F32(&Prom); F32(&Turn); F16(&Score);
  F8(&DotsNeeded); F8(&DotsGot);
  F8(&InvKalja); F8(&InvLonkka); F8(&InvKiekura);
  FN(Equip, E_COUNT);
  F16(&Kokemus);
  FN(HPt, HP_COUNT);
  F16(&FriendlyTurns);
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
  if (save) Scratch(SAVE_FILE);
  if (!OpenSeq(SAVE_FILE, save)) return 0;
  Saving = save;
  SaveBad = 0;
  ok = Serialise();
  cbm_close(LFN_FILE);
  if (save && !DriveOk()) ok = 0;
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
