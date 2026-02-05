#include "raw.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h> // for atexit
#include <termios.h>
#include <unistd.h>

static struct termios original_termios;

int term_raw(void) {
  if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
    return 1;
  }
#if DEBUG
  LOG_DEBUG("Entering raw mode");
#endif
  atexit(term_restore);
  return 0;
}

void term_restore(void) {
#if DEBUG
  LOG_DEBUG("Restoring normal mode");
#endif
  // Restore the original terminal attributes on exit
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}
