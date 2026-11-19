/* term.c -- raw termios, 80x25 screen buffer with diff output, keyboard. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include "spurgux.h"

#define OUTMAX 8192

typedef struct {
  char ch[5];
  u8   at;
} TCell;

static struct termios SavedTerm;
static u8 TermInited = 0;
static u8 ScreenInited = 0;
static char OutBuf[OUTMAX];
static u16 OutLen = 0;
static TCell Scr[SCRH][SCRW];
static TCell Shown[SCRH][SCRW];

u8 ForceRedraw = 1;
u8 CursorOn = 0;
u8 CurCol = 1;
u8 CurRow = 1;

/* --- output ------------------------------------------------------------- */
void OutFlush(void)
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

void OutStr(const char *s)
{
  while (*s) {
    if (OutLen >= OUTMAX) OutFlush();
    OutBuf[OutLen++] = *s++;
  }
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

/* SIGWINCH: the terminal was resized.  Only a flag is set here; the next
   key read (or screen flush) repaints the whole screen. */
static volatile sig_atomic_t Resized = 0;

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
  sa.sa_flags = 0;
  sigaction(SIGWINCH, &sa, NULL);
}

/* --- UTF-8 helpers -------------------------------------------------------- */
u8 U8Len(u8 c)
{
  if (c < 0x80) return 1;
  if (c >= 0xC0 && c <= 0xDF) return 2;
  if (c >= 0xE0 && c <= 0xEF) return 3;
  if (c >= 0xF0 && c <= 0xF7) return 4;
  return 1;
}

u16 ULen(const char *s)
{
  u16 n = 0;
  while (*s) {
    u8 l = U8Len((u8)*s);
    while (l > 0 && *s) { s++; l--; }
    n++;
  }
  return n;
}

/* --- screen buffer -------------------------------------------------------- */
void SPut(int col, int row, const char *s, u8 at)
{
  u8 n, i;
  while (*s) {
    n = U8Len((u8)*s);
    if (row >= 1 && row <= SCRH && col >= 1 && col <= SCRW) {
      TCell *c = &Scr[row - 1][col - 1];
      for (i = 0; i < n && s[i]; i++) c->ch[i] = s[i];
      c->ch[i] = 0;
      c->at = at;
    }
    for (i = 0; i < n && *s; i++) s++;
    col++;
  }
}

void SFill(int c0, int r0, int c1, int r1)
{
  int r, c;
  for (r = r0; r <= r1; r++)
    for (c = c0; c <= c1; c++)
      if (r >= 1 && r <= SCRH && c >= 1 && c <= SCRW) {
        Scr[r - 1][c - 1].ch[0] = ' ';
        Scr[r - 1][c - 1].ch[1] = 0;
        Scr[r - 1][c - 1].at = A_NORM;
      }
}

void SClear(void)
{
  SFill(1, 1, SCRW, SCRH);
}

static const char *AttrSeq(u8 at)
{
  if (!UseColor) {
    if (at == A_BOLD || at == A_WHITE) return "\033[0;1m";
    return "\033[0m";
  }
  switch (at) {
    case A_BOLD:    return "\033[0;1m";
    case A_RED:     return "\033[0;1;31m";
    case A_GREEN:   return "\033[0;1;32m";
    case A_YELLOW:  return "\033[0;1;33m";
    case A_BLUE:    return "\033[0;1;34m";
    case A_MAGENTA: return "\033[0;1;35m";
    case A_CYAN:    return "\033[0;36m";
    case A_WHITE:   return "\033[0;1;37m";
    case A_DIM:     return "\033[0;2m";
    default:        return "\033[0m";
  }
}

void SFlush(void)
{
  int r, c, lastR = -1, lastC = -1, lastAt = -1;
  char tmp[32];
  if (Resized) {
    Resized = 0;
    ForceRedraw = 1;
  }
  if (ForceRedraw) {
    OutStr("\033[0m\033[2J");
    lastAt = A_NORM;
  }
  for (r = 0; r < SCRH; r++)
    for (c = 0; c < SCRW; c++) {
      TCell *s = &Scr[r][c];
      TCell *o = &Shown[r][c];
      if (ForceRedraw || strcmp(s->ch, o->ch) != 0 || s->at != o->at) {
        if (r != lastR || c != lastC) {
          sprintf(tmp, "\033[%d;%dH", r + 1, c + 1);
          OutStr(tmp);
        }
        if (s->at != lastAt) {
          OutStr(AttrSeq(s->at));
          lastAt = s->at;
        }
        OutStr(s->ch[0] ? s->ch : " ");
        *o = *s;
        lastR = r;
        lastC = c + 1;
      }
    }
  ForceRedraw = 0;
  sprintf(tmp, "\033[0m\033[%d;%dH", CurRow, CurCol);
  OutStr(tmp);
  OutStr(CursorOn ? "\033[?25h" : "\033[?25l");
  OutFlush();
}

/* --- keyboard ------------------------------------------------------------- */
/* blocking read of one byte; a resize repaints the screen and keeps
   waiting.  Returns 0 only when stdin is closed or fails. */
static u8 ReadByte(u8 *b)
{
  long n;
  for (;;) {
    n = (long)read(0, b, 1);
    if (n == 1) return 1;
    if (n < 0 && errno == EINTR) {
      if (Resized) SFlush();
      continue;
    }
    return 0;
  }
}

static u8 ReadByteTimeout(u8 tenths, u8 *b)
{
  struct termios t;
  u8 ok;
  if (tcgetattr(0, &t) != 0) return 0;
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = tenths;
  tcsetattr(0, TCSANOW, &t);
  ok = read(0, b, 1) == 1;
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(0, TCSANOW, &t);
  return ok;
}

/* v1.17 ESC + key remap (dispatcher file 0x92D0..0x9393) */
static int RemapEscKey(int c)
{
  switch (c) {
    case 'G': return 'y';
    case 'H': return 'k';
    case 'I': return 'u';
    case 'K': return 'h';
    case 'M': return 'l';
    case 'O': return 'b';
    case 'P': return 'j';
    case 'Q': return 'n';
    case 'R': return 'i';
    case 'S': return '.';
    default:  return 'W';
  }
}

/* flushes the screen, then reads one key.  Arrow keys -> h/j/k/l,
   Home/End/PgUp/PgDn -> y/b/u/n, Insert -> i, Delete -> . */
int ReadKey(void)
{
  u8 ch, e1, e2, e3;
  SFlush();
  if (!ReadByte(&ch)) {
    /* stdin closed: nothing more can be played */
    RestoreTerm();
    exit(0);
  }
  if (ch != 0x1B) return ch;
  if (!ReadByteTimeout(1, &e1)) return 27;
  if (e1 != '[' && e1 != 'O') return RemapEscKey(e1);
  if (!ReadByteTimeout(1, &e2)) {
    if (e1 == 'O') return RemapEscKey('O');
    return 0;
  }
  switch (e2) {
    case 'A': return 'k';
    case 'B': return 'j';
    case 'C': return 'l';
    case 'D': return 'h';
    case 'H': return 'y';
    case 'F': return 'b';
    case 'E': return '.';
    default: break;
  }
  if (e2 >= '1' && e2 <= '9') {
    e3 = 0;
    do {
      if (!ReadByteTimeout(1, &e3)) break;
    } while (e3 != '~' && e3 < 0x40);
    switch (e2) {
      case '1': case '7': return 'y';
      case '4': case '8': return 'b';
      case '5': return 'u';
      case '6': return 'n';
      case '2': return 'i';
      case '3': return '.';
      default: break;
    }
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

void FlushInput(void)
{
  tcflush(0, TCIFLUSH);
}

/* shows the screen, waits, throws away keys typed meanwhile, then waits for
   a fresh key press (end-of-game screens cannot be skipped by accident) */
int ReadKeyAfterPause(u16 ms)
{
  SFlush();
  SleepMs(ms);
  FlushInput();
  return ReadKey();
}
