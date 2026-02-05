#include "buf.h"
#include "log.h"
#include "raw.h"
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>

int main(void) {
  if (term_raw() != 0) {
    LOG_ERROR("Failed to enter raw mode: %s", strerror(errno));
    return 1;
  }
  buffer b = {0};
  debug_print_buf(b); // check 0 init is fine
  // add some binary:
  append_str(&b, STR("A\01B\00C\02D\n"));
  // debug print & output stdout
  debug_print_buf(b);
  ssize_t written = write_buf(STDOUT_FILENO, b);
  LOG_DEBUG("Written bytes: %zd", written);

  b.size = 0; // clear/reset/reuse
  append_str(&b, UTF8("Hello, 🌎!\n"));
  // same: debug print & output stdout
  debug_print_buf(b);
  write_buf(STDOUT_FILENO, b);
  free_buf(b);
}
