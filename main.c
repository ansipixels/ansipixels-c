/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */
#include "ansipixels.h"

int main(void) {
  ap_t ap = ap_open();
  if (!ap) {
    return 1; // error already logged in ap_open
  }
  buffer b = {0};
  debug_print_buf(b); // check 0 init is fine
  // add some binary:
  append_str(&b, STR("A\01B\00C\02D\n"));
  // debug print & output stdout
  debug_print_buf(b);
  ssize_t written = write_buf(STDOUT_FILENO, b);
  LOG_DEBUG("Wrote %zd bytes", written);

  b.size = 0; // clear/reset/reuse
  append_str(&b, UTF8("Hello, 🌎!\n"));
  // same: debug print & output stdout
  debug_print_buf(b);
  write_buf(STDOUT_FILENO, b);
  free_buf(b);

  LOG_INFO("Initial size: %dx%d", ap->w, ap->h);
  // Read from stdin in paste mode until 'Ctrl-C' or 'Ctrl-D' is pressed;
  // input is logged via the buffer debug print but not echoed to stdout
  ap_paste_on(ap);
  write_str(STDOUT_FILENO,
            STR("Resize the window or type something (press 'Ctrl-C' or 'Ctrl-D' to quit):\n"));
  b = new_buf(4096);
  int last_w = ap->w;
  int last_h = ap->h;

  while (1) {
    if (ap->w != last_w || ap->h != last_h) {
      LOG_INFO("Size changed: %dx%d", ap->w, ap->h);
      last_w = ap->w;
      last_h = ap->h;
    }
    b.size = 0; // clear buffer for reuse
    ssize_t n = read_buf(STDIN_FILENO, &b);
    if (n < 0) {
      if (errno == EINTR) {
        LOG_DEBUG(
            "Read interrupted by signal, (likely SIGWINCH), continuing loop");
        continue; // retry on signal
      }
      LOG_ERROR("Error reading from stdin: %s", strerror(errno));
      break;
    } else if (n == 0) {
      // Because we block for at least 1 byte, this should not happen.
      LOG_ERROR("Unexpected eof for raw stdin (%zd)", n);
      break; // no data, try again
    }
    debug_print_buf(b);
    // write_buf(STDOUT_FILENO, b);
    void *found;
    if ((found = memchr(b.data, '\x03', b.size)) ||
        (found = memchr(b.data, '\x04', b.size))) {
      char *fc = (char *)found;
      LOG_DEBUG("Exit character %d found at offset %zd, exiting.", *fc,
                fc - b.data);
      break; // exit on 'Ctrl-C' or 'Ctrl-D' press
    }
  }
  free_buf(b);
  return 0;
}
