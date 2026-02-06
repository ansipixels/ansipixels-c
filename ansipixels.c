/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * No warranty implied or expressly granted. Licensed under MIT (see LICENSE).
 */
#include "ansipixels.h"

static ap_t global_ap = NULL;

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
  if (term_raw() != 0) {
    LOG_ERROR("Failed to enter raw mode (%s)", strerror(errno));
    return NULL;
  }
  ap_t ap = malloc(sizeof(struct ap));
  ap->h = 0;
  ap->w = 0;
  ap->out = STDOUT_FILENO;
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
