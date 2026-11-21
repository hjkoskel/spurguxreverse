/* main.c -- SpurguX v1.17, Commodore 64 version: start-up and the
   "play again" loop. */
#include "spurgux.h"

int main(void)
{
  int k;
  ScrInit();
  SndInit();
  for (;;) {
    MainLoop();
    SClear();
    SPut(8, 12, "Uusi peli (k/e)?", A_BOLD);
    CurRow = 12;
    CurCol = 25;
    CursorOn = 1;
    do {
      k = ReadKey();
    } while (k != 'k' && k != 'K' && k != 'e' && k != 'E' &&
             k != 'y' && k != 'n' && k != KEY_ESC);
    CursorOn = 0;
    if (k != 'k' && k != 'K' && k != 'y') break;
  }
  DataClose();
  SndExit();
  ScrExit();
  return 0;
}
