/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 * And this binary the translation of:
 * https://github.com/ldemailly/go-scratch/blob/main/windows_terminal_issue/issue18800.go
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */
#include "ansipixels.h"
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  int num_iter = 1000 * 1000;
  int time_every = 100 * 1000;

  // Manual flag parsing to avoid getopt dependency/complexity for just 2 args
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      num_iter = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      time_every = atoi(argv[++i]);
    }
  }

  ap_t ap = ap_open();
  if (ap == NULL) {
    return 1; // error already logged
  }
  dprintf(STDOUT_FILENO, "Terminal in raw mode - 'q' to exit early\n");
  dprintf(STDOUT_FILENO, "it should end on its own otherwise after %d\n",
          num_iter);

  const char *req = "\033[6n";
  int req_len = strlen(req);

  uint64_t now = now_ns();
  buffer quoted = new_buf(32);
  char buf[80];
  for (int iter = 1; iter <= num_iter; iter++) {
    if (write(STDOUT_FILENO, req, req_len) != req_len) {
      return LOGF("Error writing to terminal");
    }

    int n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) {
      return LOGF("Error reading from terminal");
    }

    int quit = 0;
    for (int i = 0; i < n; i++) {
      if (buf[i] == 'q') {
        quit = 1;
        break;
      }
    }
    if (quit) {
      break;
    }
    quoted.size = 0; // Reset quoted buffer
    quote_buf(&quoted, buf, n);
    n = snprintf(buf, sizeof(buf), "\r[%05d] Read %d bytes: %s      ", iter, n,
                 quoted.data);
    if (write(STDOUT_FILENO, buf, n) != n) {
      return LOGF("Error writing to terminal");
    }

    if (iter % time_every == 0) {
      uint64_t current = now_ns();
      double delta_sec = (double)(current - now) / 1e9;
      double fps = (double)time_every / delta_sec;
      dprintf(STDOUT_FILENO, "\t-- at %d took %.3fs -- %.1f fps\n", iter,
              delta_sec, fps);
#if DEBUG
      debug_print_buf(quoted);
#endif
      now = current;
    }
  }
  dprintf(STDOUT_FILENO, "\nDone\n");

  return 0;
}
