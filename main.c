#include "ansipixels.h"
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

  // Read from stdin in paste mode until 'Ctrl-C' or 'Ctrl-D' is pressed;
  // input is logged via the buffer debug print but not echoed to stdout
  PASTE_MODE_ON();
  write_str(STDOUT_FILENO,
            STR("Type something (press 'Ctrl-C' or 'Ctrl-D' to quit):\n"));
  b = new_buf(4096);
  while (1) {
    b.size = 0; // clear buffer for reuse
    ssize_t n = read_buf(STDIN_FILENO, &b);
    if (n < 0) {
      LOG_ERROR("Error reading from stdin: %s", strerror(errno));
      break;
    } else if (n == 0) {
      // Because we block for at least 1 byte, this should not happen.
      LOG_DEBUG("No input read, trying again...");
      continue; // no data, try again
    }
    debug_print_buf(b);
    // write_buf(STDOUT_FILENO, b);
    void *found;
    if ((found = memchr(b.data, '\x03', b.size)) ||
        (found = memchr(b.data, '\x04', b.size))) {
      char *fc = (char *)found;
      LOG_DEBUG("Exit character %d found at offset %zd, exiting.", *fc,
                fc - b.data);
      break; // exit on 'Ctrl-C' or 'Ctrl-D' press
    }
  }
  free_buf(b);
  return 0;
}
