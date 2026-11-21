/* c64.c -- Commodore 64 screen, keyboard and timing.

   Screen RAM is moved to $CC00 (VIC bank 3) and the lower/upper case
   character set is copied from the character ROM to $F000, which is RAM
   under the KERNAL ROM: the VIC reads that RAM, CPU writes go to it, and it
   costs no program memory.  Screen codes $80-$95 (reverse-video lowercase
   letters, never used) are replaced with the custom glyphs of glyphs.h.

   Everything is drawn straight into screen and colour RAM, so no screen
   buffer is kept. */
#include <cbm.h>
#include <string.h>
#include "spurgux.h"
#include "glyphs.h"

#define POKE(a, v) (*(volatile u8 *)(a) = (v))
#define PEEK(a)    (*(volatile u8 *)(a))

#define CHARSET ((u8 *)0xF000)
#define CELLS   (SCRW * SCRH)

u8 CurCol = 0, CurRow = 0, CursorOn = 0;

/* attribute -> C64 colour */
const u8 Colour[10] = {
  15,   /* A_NORM    light grey */
  1,    /* A_BOLD    white */
  10,   /* A_RED     light red */
  13,   /* A_GREEN   light green */
  7,    /* A_YELLOW  yellow */
  14,   /* A_BLUE    light blue */
  4,    /* A_MAGENTA purple */
  3,    /* A_CYAN    cyan */
  1,    /* A_WHITE   white */
  11    /* A_DIM     dark grey */
};

/* PETSCII $01..$10 used in strings for graphics (see G_* in spurgux.h);
   $08 and $0A-$0D are unused (cc65 translates them) */
static const u8 GfxCode[16] = {
  0xA0, 0xE1, 0xE2, 0x62, 0x66, 0x40, 0x5D, 0x20,
  0x72, 0x20, 0x20, 0x20, 0x20, 0x5B, 0x94, 0x95
};

u8 ToScr[256];

const u16 Row40[SCRH] = {
  0, 40, 80, 120, 160, 200, 240, 280, 320, 360, 400, 440, 480, 520, 560,
  600, 640, 680, 720, 760, 800, 840, 880, 920, 960
};

static void BuildTable(void)
{
  u16 p;
  u8 s;
  for (p = 0; p < 256; p++) {
    if (p == 0 || (p > 0x10 && p < 0x20)) s = 0x20;
    else if (p <= 0x10) s = GfxCode[p - 1];
    else if (p < 0x40) s = (u8)p;
    else if (p < 0x60) s = (u8)(p - 0x40);
    else if (p < 0x80) s = (u8)(p - 0x20);
    else if (p < 0xA0) s = (u8)p;           /* custom glyphs */
    else if (p < 0xC0) s = (u8)(p - 0x40);
    else if (p < 0xFF) s = (u8)(p - 0x80);
    else s = 0x5E;
    ToScr[p] = s;
  }
}

u8 ScrCode(u8 petscii)
{
  return ToScr[petscii];
}

static void CopyCharset(void)
{
  __asm__("sei");
  POKE(1, 0x33);                        /* character ROM at $D000 */
  memcpy(CHARSET, (u8 *)0xD800, 2048);  /* lower/upper case set */
  POKE(1, 0x36);                        /* I/O back */
  __asm__("cli");
  memcpy(CHARSET + GLYPH_FIRST * 8, Glyphs, sizeof(Glyphs));
}

void ScrInit(void)
{
  BuildTable();
  CopyCharset();
  POKE(0xD020, 0);
  POKE(0xD021, 0);
  POKE(0xDD02, PEEK(0xDD02) | 3);
  POKE(0xDD00, PEEK(0xDD00) & 0xFC);   /* VIC bank 3: $C000-$FFFF */
  POKE(0xD018, 0x3C);                   /* screen $CC00, chars $F000 */
  POKE(0x0288, 0xCC);                   /* KERNAL screen editor */
  POKE(0x0291, 0x80);                   /* no SHIFT+C= case switch */
  POKE(0x028A, 0x80);                   /* all keys repeat */
  SClear();
}

void ScrExit(void)
{
  POKE(0xDD00, PEEK(0xDD00) | 3);
  POKE(0xD018, 0x15);                   /* upper case / graphics */
  POKE(0x0288, 0x04);
  POKE(0x0291, 0x00);
  POKE(0x028A, 0x00);
  POKE(0xD020, 14);
  POKE(0xD021, 6);
  cbm_k_bsout(147);                     /* clear the normal screen */
}

/* --- drawing -------------------------------------------------------------- */
void SPutN(u8 col, u8 row, const char *s, u8 n, u8 at)
{
  u16 i = Row40[row] + col;
  u8 c = Colour[at];
  u8 *sp = SCREEN + i, *cp = COLRAM + i;
  while (*s && n && i < CELLS) {
    *sp++ = ToScr[(u8)*s++];
    *cp++ = c;
    i++;
    n--;
  }
}

void SPut(u8 col, u8 row, const char *s, u8 at)
{
  SPutN(col, row, s, 255, at);
}

void SPutScr(u8 col, u8 row, u8 scr, u8 at)
{
  u16 i;
  if (col >= SCRW || row >= SCRH) return;
  i = Row40[row] + col;
  SCREEN[i] = scr;
  COLRAM[i] = Colour[at];
}

void SFill(u8 c0, u8 r0, u8 c1, u8 r1)
{
  u8 n = (u8)(c1 - c0 + 1);
  u16 i;
  for (; r0 <= r1; r0++) {
    i = Row40[r0] + c0;
    memset(SCREEN + i, 0x20, n);
  }
}

void SClear(void)
{
  memset(SCREEN, 0x20, CELLS);
  memset(COLRAM, Colour[A_NORM], CELLS);
}

/* --- timing --------------------------------------------------------------- */
u32 Jiffies(void)
{
  u8 a, b, c;
  do {
    c = PEEK(0xA2);
    b = PEEK(0xA1);
    a = PEEK(0xA0);
  } while (c != PEEK(0xA2));
  return ((u32)a << 16) | ((u16)b << 8) | c;
}

static u16 Jiffy16(void)
{
  u8 b, c;
  do {
    c = PEEK(0xA2);
    b = PEEK(0xA1);
  } while (c != PEEK(0xA2));
  return (u16)((b << 8) | c);
}

void SleepMs(u16 ms)
{
  u16 start = Jiffy16();
  u16 n = (u16)(((u32)ms * 3 + 49) / 50);   /* 60 jiffies per second */
  while ((u16)(Jiffy16() - start) < n) ;
}

/* --- keyboard and joystick ------------------------------------------------ */
/* joystick port 2 ($DC00, active low): up down left right fire */
static u8 JoyLast = 0;
static u16 JoyTime;

static u8 JoyKey(void)
{
  static const char Dir[11] = { 'k', 'j', 0, 'h', 'y', 'b', 0, 'l', 'u', 'n', 0 };
  u8 j = (u8)(~PEEK(0xDC00) & 0x1F);
  u8 k;
  u16 now;
  /* the keyboard scan also pulls $DC00 lines low; ignore while a key is
     held (no key -> $DC01 reads $FF) */
  if (j == 0x1F) j = 0;
  if (j & 0x10) k = '>';              /* fire = descend */
  else if (j & 0x0F) {
    k = (u8)(j & 0x0F);
    k = (u8)Dir[k <= 10 ? k - 1 : 2];
  } else k = 0;
  now = Jiffy16();
  if (!k) { JoyLast = 0; return 0; }
  if (k != JoyLast) {
    JoyLast = k;
    JoyTime = (u16)(now + 18);           /* first repeat after 0.3 s */
    return k;
  }
  if ((s16)(now - JoyTime) >= 0) {
    JoyTime = (u16)(now + 8);
    return k;
  }
  return 0;
}

static u8 Translate(u8 c)
{
  switch (c) {
    case KEY_STOP: return KEY_ESC;
    case 0x11: return 'j';               /* CRSR down */
    case 0x91: return 'k';               /* CRSR up */
    case 0x1D: return 'l';               /* CRSR right */
    case 0x9D: return 'h';               /* CRSR left */
    case KEY_HOME:
    case KEY_CLR: return 12;             /* redraw */
    case 0x85: return '?';               /* F1 */
    case 0x86: return 'i';               /* F3 */
    case 0x94: return 'i';               /* INST */
  }
  return c;
}

int ReadKey(void)
{
  u8 c, k = 0, under = 0, blink = 0;
  u16 i = (u16)CurRow * SCRW + CurCol, t = 0;
  if (i >= CELLS) i = CELLS - 1;
  if (CursorOn) under = SCREEN[i];
  for (;;) {
    c = cbm_k_getin();
    if (c) {
      k = Translate(c);
      /* a held key repeats faster than turns are played: keep at most
         one key waiting so the player does not run on after letting go */
      if (PEEK(0xC6) > 1) POKE(0xC6, 1);
      break;
    }
    if (PEEK(0xDC01) == 0xFF && (k = JoyKey()) != 0) break;
    if (CursorOn && (u16)(Jiffy16() - t) >= 20) {
      t = Jiffy16();
      blink ^= 1;
      SCREEN[i] = blink ? 0xA0 : under;
      COLRAM[i] = Colour[A_BOLD];
    }
  }
  if (CursorOn) SCREEN[i] = under;
  return k;
}

int ReadKeyAfterPause(u16 ms)
{
  SleepMs(ms);
  POKE(0xC6, 0);                        /* empty the keyboard buffer */
  return ReadKey();
}
