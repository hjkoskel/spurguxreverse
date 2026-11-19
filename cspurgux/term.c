/* term.c -- raw termios, 80x25 CP437 screen buffer with diff output,
   keyboard.  This is the only module that talks to the terminal. */
#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include "spurgux.h"

#define OUTMAX 256
#define CELLS  (SCRW * SCRH)

static struct termios SavedTerm;
static u8 TermInited = 0;
static u8 ScreenInited = 0;
static char OutBuf[OUTMAX];
static u16 OutLen = 0;

/* screen: one CP437 byte + a 4-bit attribute per cell, and a dirty bit */
static u8 ScrCh[CELLS];
static u8 ScrAt[CELLS / 2];
static u8 Dirty[CELLS / 8];

#define AT_GET(i) ((u8)((ScrAt[(i) >> 1] >> (((i) & 1) << 2)) & 15))

u8 ForceRedraw = 1;
u8 CursorOn = 0;
u8 CurCol = 1;
u8 CurRow = 1;

/* SIGWINCH: the terminal was resized.  Only a flag is set here; the next
   key read (or screen flush) repaints the whole screen. */
static volatile sig_atomic_t Resized = 0;

/* --- output ------------------------------------------------------------- */
static void OutFlush(void)
{
  u16 p = 0;
  long w;
  while (p < OutLen) {
    w = (long)write(1, OutBuf + p, (size_t)(OutLen - p));
    if (w <= 0) break;
    p = (u16)(p + w);
  }
  OutLen = 0;
}

static void OutCh(char c)
{
  if (OutLen >= OUTMAX) OutFlush();
  OutBuf[OutLen++] = c;
}

void OutStr(const char *s)
{
  while (*s) OutCh(*s++);
}

static void OutNum(u8 v)
{
  if (v >= 10) OutCh((char)('0' + v / 10));
  OutCh((char)('0' + v % 10));
}

#ifndef OUT_CP437
/* Unicode code points of CP437 0x7F..0xFF */
static const u16 Uni[129] = {
  0x2302,
  0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,
  0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
  0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,
  0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
  0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,
  0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
  0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
  0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
  0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,
  0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
  0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,
  0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
  0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,
  0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
  0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,
  0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x0020
};

/* one CP437 byte as UTF-8 */
static void OutGlyph(u8 c)
{
  u16 u;
  if (c < 0x7F) { OutCh((char)c); return; }
  u = Uni[c - 0x7F];
  if (u < 0x80) OutCh((char)u);
  else if (u < 0x800) {
    OutCh((char)(0xC0 | (u >> 6)));
    OutCh((char)(0x80 | (u & 0x3F)));
  } else {
    OutCh((char)(0xE0 | (u >> 12)));
    OutCh((char)(0x80 | ((u >> 6) & 0x3F)));
    OutCh((char)(0x80 | (u & 0x3F)));
  }
}
#else
#define OutGlyph(c) OutCh((char)(c))
#endif

/* CP437 text written straight to the terminal (after RestoreTerm) */
void OutText(const char *s)
{
  while (*s) OutGlyph((u8)*s++);
  OutFlush();
}

/* --- terminal mode ------------------------------------------------------- */
void SetRaw(void)
{
  struct termios t;
  if (TermInited) return;
  if (tcgetattr(0, &t) != 0) return;
  SavedTerm = t;
  t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
  t.c_iflag &= ~IXON;
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(0, TCSANOW, &t);
  TermInited = 1;
}

void RestoreTerm(void)
{
  if (ScreenInited) {
    OutStr("\033[0m\033[?25h\033[?1049l");
    OutFlush();
    ScreenInited = 0;
  }
  if (TermInited) {
    tcsetattr(0, TCSANOW, &SavedTerm);
    TermInited = 0;
  }
}

void ScreenInit(void)
{
  OutStr("\033[?1049h\033[2J\033[H");
  OutFlush();
  ScreenInited = 1;
  ForceRedraw = 1;
}

static void SignalHandler(int sig)
{
  (void)sig;
  RestoreTerm();
  _exit(1);
}

static void ResizeHandler(int sig)
{
  (void)sig;
  Resized = 1;
}

void InstallSignals(void)
{
  struct sigaction sa;
  signal(SIGTERM, SignalHandler);
  signal(SIGHUP, SignalHandler);
  /* no SA_RESTART: a resize interrupts the blocking read() in ReadKey */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = ResizeHandler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGWINCH, &sa, NULL);
}

/* --- screen buffer -------------------------------------------------------- */
void SPut(int col, int row, const char *s, u8 at)
{
  u16 i;
  if (row < 1 || row > SCRH) return;
  for (; *s; s++, col++) {
    if (col < 1 || col > SCRW) continue;
    i = (u16)((row - 1) * SCRW + col - 1);
    if (ScrCh[i] != (u8)*s || AT_GET(i) != at) {
      ScrCh[i] = (u8)*s;
      if (i & 1) ScrAt[i >> 1] = (u8)((ScrAt[i >> 1] & 0x0F) | (at << 4));
      else ScrAt[i >> 1] = (u8)((ScrAt[i >> 1] & 0xF0) | at);
      Dirty[i >> 3] |= (u8)(1 << (i & 7));
    }
  }
}

void SFill(int c0, int r0, int c1, int r1)
{
  int r, c;
  for (r = r0; r <= r1; r++)
    for (c = c0; c <= c1; c++) SPut(c, r, " ", A_NORM);
}

void SClear(void)
{
  SFill(1, 1, SCRW, SCRH);
}

/* colour escape tails, indexed by attribute (after "\033[0") */
static const char *const AttrTail[10] = {
  "m", ";1m", ";1;31m", ";1;32m", ";1;33m", ";1;34m", ";1;35m", ";36m",
  ";1;37m", ";2m"
};

void SFlush(void)
{
  u16 i, last = 0xFFFF;
  u8 at, lastAt = 0xFF;
  if (Resized) {
    Resized = 0;
    ForceRedraw = 1;
  }
  if (ForceRedraw) {
    OutStr("\033[0m\033[2J");
    lastAt = A_NORM;
    memset(Dirty, 0xFF, sizeof(Dirty));
    ForceRedraw = 0;
  }
  for (i = 0; i < CELLS; i++) {
    if (!(Dirty[i >> 3] & (1 << (i & 7)))) continue;
    Dirty[i >> 3] &= (u8)~(1 << (i & 7));
    if (i != last || i % SCRW == 0) {    /* no reliance on auto-wrap */
      OutStr("\033[");
      OutNum((u8)(i / SCRW + 1));
      OutCh(';');
      OutNum((u8)(i % SCRW + 1));
      OutCh('H');
    }
    at = AT_GET(i);
    if (!UseColor) at = (at == A_BOLD || at == A_WHITE) ? A_BOLD : A_NORM;
    if (at != lastAt) {
      OutStr("\033[0");
      OutStr(AttrTail[at]);
      lastAt = at;
    }
    OutGlyph(ScrCh[i] ? ScrCh[i] : ' ');
    last = (u16)(i + 1);
  }
  OutStr("\033[0m\033[");
  OutNum(CurRow);
  OutCh(';');
  OutNum(CurCol);
  OutCh('H');
  OutStr(CursorOn ? "\033[?25h" : "\033[?25l");
  OutFlush();
}

/* --- keyboard ------------------------------------------------------------- */
/* blocking read of one byte; a resize repaints the screen and keeps
   waiting.  Returns -1 only when stdin is closed or fails. */
int ReadRawByte(void)
{
  u8 b;
  long n;
  for (;;) {
    n = (long)read(0, &b, 1);
    if (n == 1) return b;
    if (n < 0 && errno == EINTR) {
      if (Resized) SFlush();
      continue;
    }
    return -1;
  }
}

static int ReadByteTimeout(void)
{
  struct termios t;
  u8 b;
  int r = -1;
  if (tcgetattr(0, &t) != 0) return -1;
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  tcsetattr(0, TCSANOW, &t);
  if (read(0, &b, 1) == 1) r = b;
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(0, TCSANOW, &t);
  return r;
}

/* v1.17 ESC + key remap (dispatcher file 0x92D0..0x9393): ESC G..S */
static const char EscRemap[] = "ykuWhWlWbjni.";
/* "ESC [ n ~" keys 1..8: Home Ins Del End PgUp PgDn Home End */
static const char TildeKeys[] = "yi.bunyb";
/* "ESC [ A".."ESC [ H": up down right left E(numpad 5) End - Home */
static const char CsiKeys[] = "kjlh.b0y";

/* flushes the screen, then reads one key.  Arrow keys -> h/j/k/l,
   Home/End/PgUp/PgDn -> y/b/u/n, Insert -> i, Delete -> . */
int ReadKey(void)
{
  int ch, e1, e2, e3;
  SFlush();
  ch = ReadRawByte();
  if (ch < 0) {
    /* stdin closed: nothing more can be played */
    RestoreTerm();
    exit(0);
  }
  if (ch != 0x1B) return ch;
  e1 = ReadByteTimeout();
  if (e1 < 0) return 27;
  if (e1 != '[' && e1 != 'O')
    return (e1 >= 'G' && e1 <= 'S') ? EscRemap[e1 - 'G'] : 'W';
  e2 = ReadByteTimeout();
  if (e2 < 0) return e1 == 'O' ? 'b' : 0;
  if (e2 >= 'A' && e2 <= 'H' && CsiKeys[e2 - 'A'] != '0')
    return CsiKeys[e2 - 'A'];
  if (e2 >= '1' && e2 <= '9') {
    do {
      e3 = ReadByteTimeout();
    } while (e3 >= 0 && e3 != '~' && e3 < 0x40);
    if (e2 <= '8') return TildeKeys[e2 - '1'];
  }
  return 0;
}

void SleepMs(u16 ms)
{
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

/* shows the screen, waits, throws away keys typed meanwhile, then waits for
   a fresh key press (end-of-game screens cannot be skipped by accident) */
int ReadKeyAfterPause(u16 ms)
{
  SFlush();
  SleepMs(ms);
  tcflush(0, TCIFLUSH);
  return ReadKey();
}
