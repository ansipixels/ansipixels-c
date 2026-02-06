/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * No warranty implied or expressly granted. Licensed under MIT (see LICENSE).
 */
#pragma once

#include "buf.h"
#include "log.h"
#include "raw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>

typedef struct ap {
  int out;
  int h, w;
} *ap_t;

ap_t ap_open(void);

void ap_paste_on(ap_t ap);
void ap_paste_off(ap_t ap);
