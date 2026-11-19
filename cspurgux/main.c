/* main.c -- SpurguX v1.17 C89 port: command line and start-up.

   Run:      ./spurgux        (data files SPU.HLP/.SPE/.TIP/.TOP are looked
                               up in ., the program dir, ./spurg117 and
                               <program dir>/../spurg117)
   Options:  --mono       no colours (also when NO_COLOR is set)
             --seed N     repeatable game
             --level N    debug: start deeper
             --mk N       debug: start with money
             --dumpmap F  debug: dump level 1 tile bytes to F and exit  */
#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "spurgux.h"

static s32 ParseNum(const char *s, s32 def)
{
  s32 v = 0;
  u8 neg = 0, any = 0;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9') {
    if (v < 100000000L) v = v * 10 + (*s - '0');
    s++;
    any = 1;
  }
  if (!any || *s) return def;
  return neg ? -v : v;
}

int main(int argc, char **argv)
{
  int i;
  s32 seed = -1, v;

  InitDataDirs(argv[0]);
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--dumpmap") == 0 && i + 1 < argc) {
      DumpMapToFile(argv[i + 1]);
      return 0;
    } else if (strcmp(argv[i], "--mono") == 0) {
      UseColor = 0;
    } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = ParseNum(argv[++i], 0);
      if (seed < 0) seed = 0;
    } else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
      v = ParseNum(argv[++i], 1);
      StartLevel = (u8)((v < 1 || v > MAX_LEVEL) ? 1 : v);
    } else if (strcmp(argv[i], "--mk") == 0 && i + 1 < argc) {
      v = ParseNum(argv[++i], 0);
      if (v < 0) v = 0;
      if (v > 30000) v = 30000;
      StartMk = (s16)v;
    }
  }
  if (getenv("NO_COLOR") && getenv("NO_COLOR")[0]) UseColor = 0;
  if (seed >= 0) Randomize((u32)seed);
  else Randomize((u32)time(NULL) ^ ((u32)getpid() << 16));

  SetRaw();
  InstallSignals();
  ScreenInit();
  SClear();
  MainLoop();
  RestoreTerm();
  return 0;
}
