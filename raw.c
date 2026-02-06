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
  struct termios raw = original_termios;

  // Local flags: disable echo and canonical mode for raw input
  raw.c_lflag &= ~(ECHO     // Don't echo typed characters back to terminal
                   | ICANON // Disable canonical mode (cooked), enable raw input
                   | ISIG); // Disable INTR, QUIT, SUSP signal generation

  // Input flags: disable flow control and CR/LF conversion
  raw.c_iflag &= ~(IXON      // Disable XON/XOFF software flow control
                   | ICRNL); // Don't convert carriage return to newline

  // Pure raw:
  // raw.c_oflag &= ~(OPOST);

  // For now: Output flags: enable LF -> CR+LF conversion (requires OPOST
  // enabled)
  raw.c_oflag |=
      (OPOST |
       ONLCR); // OPOST enables output processing, ONLCR converts LF to CR+LF

  // Control characters: configure blocking read
  raw.c_cc[VMIN] = 1;  // Blocking read - wait for at least 1 byte
  raw.c_cc[VTIME] = 0; // No timeout - block indefinitely until data arrives
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    return 2;
  }
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
