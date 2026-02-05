#include "log.h"
#include <stdarg.h>
#include <stdio.h>

void log_debug(const char *file, int line, const char *fmt, ...) {
  fprintf(stderr, GREEN "DBG %s:%d: ", file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputs(END_LOG, stderr);
}
