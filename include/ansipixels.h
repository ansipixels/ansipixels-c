/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */
#pragma once

#include "buf.h"
#include "log.h"
#include "raw.h"
#include "timer.h"
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>

typedef struct ap {
    int out;
    int h, w;
    int xpixel, ypixel;
    buffer buf;
    bool first_clear; // for ap_clear_screen
    bool resized;
} *ap_t;

ap_t ap_open(void);

void ap_start(ap_t ap);
void ap_end(ap_t ap);

void ap_clear_screen(ap_t ap, bool immediate);

void ap_paste_on(ap_t ap);
void ap_paste_off(ap_t ap);

void ap_itoa(ap_t ap, int n);

void ap_move_to(ap_t ap, int x, int y);

void ap_flush(ap_t ap);

void ap_save_cursor(ap_t ap);
void ap_restore_cursor(ap_t ap);

void ap_hide_cursor(ap_t ap);
void ap_show_cursor(ap_t ap);

bool ap_stdin_ready(ap_t ap);

void ap_str(ap_t ap, string s);
