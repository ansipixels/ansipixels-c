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

    clear_buf(&b); // clear/reset/reuse
    append_str(&b, UTF8("Hello, 🌎!\n"));
    // same: debug print & output stdout
    debug_print_buf(b);
    write_buf(STDOUT_FILENO, b);
    free_buf(&b);

    LOG_INFO("Initial size: %dx%d", ap->w, ap->h);
    // Read from stdin in paste mode until 'Ctrl-C' or 'Ctrl-D' is pressed;
    // input is logged via the buffer debug print but not echoed to stdout
    ap_paste_on(ap);
    write_str(
        STDOUT_FILENO,
        STR("Resize the window or type something (press "
            "'Ctrl-C' or 'Ctrl-D' to quit):\n")
    );
    b = new_buf(4096);
    int last_w = ap->w;
    int last_h = ap->h;

    while (1) {
        if (ap->w != last_w || ap->h != last_h) {
            ap_start(ap);
            ap_clear_screen(ap, false); // buffered clear to avoid flicker on resize
            ap_move_to(ap, ap->w / 2 - 10, ap->h / 2 - 1);
            ap_str(ap, STR("Size changed: "));
            ap_itoa(ap, ap->w);
            append_byte(&ap->buf, 'x');
            ap_itoa(ap, ap->h);
            ap_move_to(ap, 0, 0);
            ap_end(ap);
            LOG_INFO("Size changed: %dx%d", ap->w, ap->h);
            last_w = ap->w;
            last_h = ap->h;
        }
        clear_buf(&b); // clear buffer for reuse
        ssize_t n = read_buf(STDIN_FILENO, &b);
        if (n < 0) {
            if (errno == EINTR) {
                LOG_DEBUG("Read interrupted by signal, (likely SIGWINCH), continuing loop");
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
        const char *fc;
        static const char endlist[] = {'\x03', '\x04'}; // Ctrl-C and Ctrl-D
        if ((fc = mempbrk(b.data + b.start, b.size, endlist, sizeof(endlist))) != NULL) {
            LOG_DEBUG("Exit character %d found at offset %zd, exiting.", *fc, fc - b.data);
            break; // exit on 'Ctrl-C' or 'Ctrl-D' press
        }
    }
    free_buf(&b);
    return 0;
}
