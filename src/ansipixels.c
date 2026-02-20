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
    if (ws.ws_col == ap->w && ws.ws_row == ap->h && ws.ws_xpixel == ap->xpixel && ws.ws_ypixel == ap->ypixel) {
        LOG_DEBUG("Sigwinch: UNchanged size: %dx%d (%dx%d pixels)", ap->w, ap->h, ap->xpixel, ap->ypixel);
        return; // no change
    }
    LOG_DEBUG(
        "Sigwinch: CHANGED size: %dx%d (%dx%d pixels) -> %dx%d (%dx%d pixels)",
        ap->w,
        ap->h,
        ap->xpixel,
        ap->ypixel,
        ws.ws_col,
        ws.ws_row,
        ws.ws_xpixel,
        ws.ws_ypixel
    );
    ap->h = ws.ws_row;
    ap->w = ws.ws_col;
    ap->xpixel = ws.ws_xpixel;
    ap->ypixel = ws.ws_ypixel;
    ap->resized = true;
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
    ap_show_cursor(global_ap);
    ap_end(global_ap);
    ap_paste_off(global_ap);
    term_restore();
    free_buf(&global_ap->buf);
    free(global_ap);
    global_ap = NULL;
}

#if DEBUGGER_WAIT
volatile int wait_for_debugger = 1;
#endif

ap_t ap_open(void) {
    if (global_ap) {
        LOG_ERROR("ap_open called but ap is already open (%p)", (void *)global_ap);
        return NULL;
    }
#if DEBUGGER_WAIT
    fprintf(stderr, "PID: %d - Waiting for debugger (set wait_for_debugger=0 to continue)\n", getpid());
    while (wait_for_debugger) {
        sleep(1);
    }
#endif
    time_init();
    ap_t ap = calloc(1, sizeof(struct ap));
    if (!ap) {
        LOG_ERROR("Failed to allocate ap struct (%s)", strerror(errno));
        return NULL;
    }
    ap->out = STDOUT_FILENO;
    ap->first_clear = true;
    if (term_raw() != 0) {
        LOG_ERROR("Failed to enter raw mode (%s)", strerror(errno));
        return NULL;
    }
    ap_update_size(ap); // get the initial size.
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

void ap_clear_screen(ap_t ap, bool immediate) {
    // First time we clear the screen, we use 2J to push old content to the
    // scrollback buffer, otherwise we use H+0J to not pile up on the scrollback.
    string what = ap->first_clear ? STR("\033[2J\033[H") : STR("\033[H\033[0J");
    ap->first_clear = false;
    if (immediate) {
        write_str(ap->out, what);
        return;
    }
    ap_str(ap, what);
}

void ap_start(ap_t ap) {
    ap->buf.size = 0;
    ap_str(ap, STR("\033[?2026h")); // start sync/batch mode
}

void ap_end(ap_t ap) {
    ap_str(ap, STR("\033[?2026l")); // end sync/batch mode
    write_buf(ap->out, ap->buf);
    ap->buf.size = 0;
}

void ap_itoa(ap_t ap, int n) {
    char buf[16];
    int sign = n < 0 ? -1 : 1;
    char *end = buf + sizeof buf - 1;
    char *p = end;
    do {
        *p-- = '0' + sign * (n % 10);
        n /= 10;
    } while (n);
    if (sign < 0) {
        *p-- = '-';
    }
    append_data(&ap->buf, p + 1, (size_t)(end - p));
}

void ap_move_to(ap_t ap, int x, int y) {
    ap_str(ap, STR("\033["));
    ap_itoa(ap, y + 1); // ANSI rows are 1-based
    append_byte(&ap->buf, ';');
    ap_itoa(ap, x + 1); // ANSI columns are 1-based
    append_byte(&ap->buf, 'H');
}

void ap_flush(ap_t ap) {
    write_buf(ap->out, ap->buf);
    ap->buf.size = 0;
}

void ap_save_cursor(ap_t ap) {
    ap_str(ap, STR("\0337")); // save cursor position
}

void ap_restore_cursor(ap_t ap) {
    ap_str(ap, STR("\0338")); // restore cursor position
}

void ap_hide_cursor(ap_t ap) {
    ap_str(ap, STR("\033[?25l")); // hide cursor
}

void ap_show_cursor(ap_t ap) {
    ap_str(ap, STR("\033[?25h")); // show cursor
}

// Poll stdin without changing file status flags (which may be shared with stdout/stderr on a tty).
bool ap_stdin_ready(ap_t _) {
    (void)_;  // mark as unused for now.
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {0, 0};
    int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    if (r < 0) {
        if (errno != EINTR) {
            LOG_ERROR("Error polling stdin: %s", strerror(errno));
        }
        return false;
    }
    return r > 0 && FD_ISSET(STDIN_FILENO, &rfds);
}

void ap_start_sync(ap_t ap) {
    ap_str(ap, STR("\033[?2026h")); // start sync/batch mode
}

void ap_end_sync(ap_t ap) {
    ap_str(ap, STR("\033[?2026l")); // end sync/batch mode
    ap_flush(ap);
}

void ap_str(ap_t ap, string s) {
    append_data(&ap->buf, s.data, s.size);
}
