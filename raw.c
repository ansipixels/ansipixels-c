/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * No warranty implied or expressly granted. Licensed under MIT (see LICENSE).
 */
#include "raw.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h> // for atexit
#include <termios.h>
#include <unistd.h>

static struct termios original_termios;

int term_raw(void) {
  // Save current terminal settings so we can restore them on exit
  if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
    return 1;
  }
#if DEBUG
  LOG_DEBUG("Entering raw mode");
#endif
  struct termios raw = original_termios;

  /*
    golang has in
    https://github.com/golang/term/blob/v0.39.0/term_unix.go#L30-L38 :
    This attempts to replicate the behaviour documented for cfmakeraw in
    the termios(3) manpage.

   termios.Lflag &^= unix.ECHO | unix.ECHONL | unix.ICANON | \
     unix.ISIG | unix.IEXTEN
  */
  // Local flags: disable echo and canonical mode for raw input
  // ECHO: don't echo typed characters back to terminal
  // ICANON: disable canonical mode (cooked), enable raw input
  // ISIG: disable INTR, QUIT, SUSP signal generation (so Ctrl-C, Ctrl-Z, etc
  // are sent as raw input instead of generating signals)
  // ECHONL isn't needed as we disable ECHO.
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  // termios.Cflag &^= unix.CSIZE | unix.PARENB
  // termios.Cflag |= unix.CS8
  raw.c_cflag &= ~(CSIZE | PARENB);
  raw.c_cflag |= CS8; // use 8-bit chars

  // Input flags: disable flow control and CR/LF conversion
  /* Go:
   termios.Iflag &^= unix.IGNBRK | unix.BRKINT | unix.PARMRK | \
     unix.ISTRIP | unix.INLCR | unix.IGNCR | unix.ICRNL | unix.IXON
  */
  // ISTRIP Don't strip the 8th bit off input bytes (we want full 8-bit data)
  // IXON Disable XON/XOFF software flow control
  // ICRNL Don't convert carriage return to newline (in input)
  raw.c_iflag &=
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);

  // Pure raw would be:
  // termios.Oflag &^= unix.OPOST
  // raw.c_oflag &= ~(OPOST);

  // For now we do instead:
  // Output flags: enable LF -> CR+LF conversion only (which requires OPOST
  // enabled)
  raw.c_oflag = (OPOST | ONLCR);

  // Control characters: configure blocking read
  // termios.Cc[unix.VMIN] = 1
  // termios.Cc[unix.VTIME] = 0
  raw.c_cc[VMIN] = 1;  // Blocking read - wait for at least 1 byte
  raw.c_cc[VTIME] = 0; // No timeout - block indefinitely until data arrives
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    return 2;
  }
  return 0;
}

void term_restore(void) {
#if DEBUG
  LOG_DEBUG("Restoring normal mode");
#endif
  // Restore the original terminal attributes on exit
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}
