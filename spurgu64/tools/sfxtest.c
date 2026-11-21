/* sfxtest.c -- plays the SpurguX sound effects: keys 1-9 and a-z play
   the effects, RETURN plays them all in turn, Q quits.  Built by `make sfxtest`
   with the game's sound.s and the tables from tools/mkfx.py. */
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include "sfx.h"

typedef unsigned char u8;
void SndInit(void);
void SndExit(void);
void __fastcall__ SndPlay(u8 id);

static const char *const Names[] = { SND_NAMES };

static void Wait(unsigned jiffies)
{
  clock_t t = clock();
  while ((unsigned)(clock() - t) < jiffies) ;
}

/* effect i is played with key 1-9, then a-z */
static char Key(u8 i)
{
  return (char)(i < 9 ? '1' + i : 'a' + i - 9);
}

int main(void)
{
  u8 i;
  char c;
  SndInit();
  clrscr();
  cputs("spurgu64 sound effects\r\n\r\n");
  for (i = 0; i < SND_COUNT; i++)
    cprintf("  %c  %s\r\n", Key(i), Names[i]);
  cputs("\r\n  return = all, q = quit\r\n");
  for (;;) {
    c = cgetc();
    if (c == 'q' || c == 'Q') break;
    if (c == '\r' || c == '\n') {
      for (i = 0; i < SND_COUNT; i++) {
        SndPlay(i);
        Wait(150);
      }
    } else {
      for (i = 0; i < SND_COUNT; i++)
        if (c == Key(i)) SndPlay(i);
    }
  }
  SndExit();
  return 0;
}
