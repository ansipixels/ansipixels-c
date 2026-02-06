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
