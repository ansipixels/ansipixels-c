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

static ap_t global_ap = NULL;

static void ap_update_size(ap_t ap) {
  struct winsize ws;
  if (ioctl(ap->out, TIOCGWINSZ, &ws) < 0) {
    LOG_ERROR("ioctl(TIOCGWINSZ) failed: %s", strerror(errno));
    return;
  }
  if (ws.ws_col == ap->w && ws.ws_row == ap->h) {
    LOG_DEBUG("Sigwinch: UNchanged size: %dx%d", ap->w, ap->h);
    return; // no change
  }
  LOG_DEBUG("Sigwinch: CHANGED size: %dx%d -> %dx%d", ap->w, ap->h, ws.ws_col,
            ws.ws_row);
  ap->h = ws.ws_row;
  ap->w = ws.ws_col;
}

static void handle_winch(int sig) {
  LOG_DEBUG("Signal: received signal %d", sig);
  if (global_ap) {
    ap_update_size(global_ap);
  }
}

void ap_cleanup(void) {
  if (!global_ap) {
    return; // nothing to clean up
  }
  ap_paste_off(global_ap);
  term_restore();
  free(global_ap);
  global_ap = NULL;
}

ap_t ap_open(void) {
  if (global_ap) {
    LOG_ERROR("ap_open called but ap is already open (%p)", (void *)global_ap);
    return NULL;
  }
  time_init();
  if (term_raw() != 0) {
    LOG_ERROR("Failed to enter raw mode (%s)", strerror(errno));
    return NULL;
  }
  ap_t ap = malloc(sizeof(struct ap));
  ap->h = 0;
  ap->w = 0;
  ap->out = STDOUT_FILENO;
  ap_update_size(ap);
  // Set up SIGWINCH handler without SA_RESTART so read() gets interrupted
  struct sigaction sa = {0};
  sa.sa_handler = handle_winch;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; // Explicitly no SA_RESTART - we want EINTR
  sigaction(SIGWINCH, &sa, NULL);
  global_ap = ap; // store in global for atexit cleanup if needed
  atexit(ap_cleanup);
  return ap;
}

void ap_paste_on(ap_t ap) {
  LOG_DEBUG("Enabling paste mode");
  write_str(ap->out, STR("\033[?2004h"));
}

void ap_paste_off(ap_t ap) {
  LOG_DEBUG("Disabling paste mode");
  write_str(ap->out, STR("\033[?2004l"));
}
