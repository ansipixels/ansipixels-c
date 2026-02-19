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
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

void usage(const char *prog) {
  fprintf(stderr, "Usage: %s [flags] program args...\n", prog);
  fprintf(stderr, "Flags:\n");
  fprintf(stderr, "  -h, --help    show this help message\n");
  fprintf(stderr, "  --hud         enable HUD feature\n");
}

// Check if buffer ends with a complete UTF8 and ANSI sequence
// Returns true if there's no incomplete sequence at the end.
//
bool ends_with_complete_sequence(const char *buf, size_t len) {
  if (len == 0)
    return true;

  // If we're inside a UTF-8 multi-byte sequence that's also bad to cut:
  // Check if the last byte is a UTF-8 continuation byte (0b10xxxxxx)
  if ((unsigned char)buf[len - 1] >= 0x80) {
    // ends with UTF-8 high byte: incomplete (even if actually it could be
    // the last byte of a valid sequence, we treat it as incomplete to be safe)
    return false;
  }

  // Find the last ESC character (0x1b)
  int last_esc = -1;
  for (int i = (int)len - 1; i >= 0; i--) {
    if ((unsigned char)buf[i] == 0x1b) {
      last_esc = i;
      break;
    }
  }

  if (last_esc == -1) {
    // No ESC found, so no incomplete sequence
    return true;
  }
  // Check if there's a complete sequence end (letter) after the last ESC
  for (size_t i = last_esc + 1; i < len; i++) {
    unsigned char c = (unsigned char)buf[i];
    // ANSI sequences typically end with a letter (A-Z, a-z)
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
      return true; // found complete sequence ending
    }
  }
  // Incomplete sequence (ESC followed by non-terminator chars, or nothing)
  return false;
}

int main(int argc, char **argv) {
  bool hud = false;
  int opt;

  // Define long options
  static struct option long_options[] = {
      {"help", no_argument, 0, 'h'}, {"hud", no_argument, 0, 'H'}, {0, 0, 0, 0}
      // terminator
  };

  // Parse flags using getopt_long
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "h", long_options, &option_index)) !=
         -1) {
    switch (opt) {
    case 'h':
      usage(argv[0]);
      return 0;
    case 'H': // --hud
      hud = true;
      break;
    default: // '?' for unknown option
      fprintf(stderr, "Error: unknown flag\n");
      usage(argv[0]);
      return 1;
    }
  }

  // Check if we have a program to execute (after parsed options)
  if (optind >= argc) {
    fprintf(stderr, "Error: no program specified\n");
    usage(argv[0]);
    return 1;
  }
  ap_t ap = ap_open();
  if (ap == NULL) {
    return 1; // error already logged
  }
  // Get the terminal size from ap for the parent terminal
  struct winsize ws = {ap->h, ap->w, ap->xpixel, ap->ypixel};
  LOG_INFO("Parent terminal size: %dx%d (%dx%d pixels)", ws.ws_col, ws.ws_row,
           ws.ws_xpixel, ws.ws_ypixel);
  char *program = argv[optind];
  char path[4096];
  int fd;
  pid_t pid = forkpty(&fd, path, NULL, &ws);
  if (pid < 0) {
    LOG_ERROR("Error forking process: %s", strerror(errno));
    return 1;
  }
  if (pid == 0) {
    // In child process: execute the requested program
    LOG_INFO("In child process, executing program '%s'", program);
    int ret = execvp(program, argv + optind);
    if (ret < 0) {
      LOG_ERROR("Error executing program '%s': %s", program, strerror(errno));
      return 1;
    }
  }
  // In parent: bidirectional I/O using pselect with SIGCHLD handling
  LOG_INFO("Started program '%s' with PID %d and path '%s'", program, pid,
           path);
  char buf[4096];
  fd_set readfds;
  bool done = false;
  bool stdin_closed = false;
  sigset_t empty;
  sigemptyset(&empty);
  int ourStatus = 0;
  buffer quoted = {0};
  size_t totalRead = 0;
  size_t totalWritten = 0;
  bool last_child_output_complete =
      true; // track if last child output ends with complete sequence
  while (!done) {
    FD_ZERO(&readfds);
    if (!stdin_closed) {
      FD_SET(STDIN_FILENO, &readfds); // monitor stdin (parent's input)
    }
    FD_SET(fd, &readfds); // monitor PTY (child's output)
    // pselect unmasks SIGCHLD (and SIGWINCH) atomically during select,
    // so we wake on child exit or resize (or IOs).
    int ret = pselect(fd + 1, &readfds, NULL, NULL, NULL, &empty);
    LOG_DEBUG("pselect ret=%d, errno=%d, stdin_closed=%d", ret, errno,
              stdin_closed);
    // Check for terminal resize (SIGWINCH is handled by ap, just poll size)
    struct winsize current_ws;
    if (ap->resized) {
      ap->resized = false; // reset flag
      current_ws.ws_col = ap->w;
      current_ws.ws_row = ap->h;
      current_ws.ws_xpixel = ap->xpixel;
      current_ws.ws_ypixel = ap->ypixel;
      if (ioctl(fd, TIOCSWINSZ, &current_ws) >= 0) {
        LOG_DEBUG("Forwarded resize: %dx%d", current_ws.ws_col,
                  current_ws.ws_row);
      } else {
        LOG_ERROR("Could not set PTY window size: %s", strerror(errno));
      }
    }
    bool iodone = false; // track if we did I/O to skip waitpid check if so
    ssize_t readn = 0;
    ssize_t writen = 0;
    if (ret < 0) {
      if (errno != EINTR) {
        LOG_ERROR("pselect error: %s", strerror(errno));
        break;
      }
    }
    // If EINTR, fall through to check for other events
    // Data available from stdin - send to child
    if (!stdin_closed && ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
      readn = read(STDIN_FILENO, buf, sizeof(buf));
      if (readn > 0) {
        quoted.size = 0; // reset quoted buffer for reuse
        quote_buf(&quoted, buf, readn);
        LOG_DEBUG("Read %zd bytes from stdin, sending to child %s", readn,
                  quoted.data);
        ssize_t n = write(fd, buf, readn); // send to PTY
        if (n != readn) {
          LOG_ERROR("Error writing %zd vs %zd to PTY: %s", n, readn,
                    strerror(errno));
          break;
        }
        iodone = true;
      } else if (readn == 0) {
        // EOF on stdin, stop monitoring it
        stdin_closed = true;
      }
    }
    // Data available from PTY - output from child
    if (ret > 0 && FD_ISSET(fd, &readfds)) {
      writen = read(fd, buf, sizeof(buf));
      if (writen > 0) {
        quoted.size = 0; // reset quoted buffer for reuse
        quote_buf(&quoted, buf, writen);
        LOG_DEBUG("Read %zd bytes from PTY, outputting to stdout %s", writen,
                  quoted.data);
        ssize_t n = write(1, buf, writen);
        if (n != writen) {
          LOG_ERROR("Error writing %zd vs %zd to stdout: %s", n, writen,
                    strerror(errno));
          break;
        }
        // Check if child output ends with complete ANSI sequence
        last_child_output_complete = ends_with_complete_sequence(buf, writen);
        iodone = true;
      } else if (writen == 0 || (writen < 0 && errno == EIO)) {
        // PTY closed or EIO - child has ended
        LOG_DEBUG("PTY closed (read %zd, errno=%d), child likely exited",
                  writen, errno);
        done = true;
      }
    }
    if (iodone) {
      totalRead += readn;
      totalWritten += writen;
      // Only update HUD if child output ended with a complete ANSI sequence
      if (hud && last_child_output_complete) {
        // If we did I/O, update the HUD with the latest child output
        ap_save_cursor(ap);
        ap_move_to(ap, 0, 0); // move to top
        // Inverse colors for visibility, and show total read/written
        append_str(&ap->buf, STR("\033[7m")); // inverse colors
        append_str(&ap->buf, STR("R: "));
        ap_itoa(ap, readn);
        append_str(&ap->buf, STR(" ("));
        ap_itoa(ap, totalRead);
        append_str(&ap->buf, STR("), W: "));
        ap_itoa(ap, writen);
        append_str(&ap->buf, STR(" ("));
        ap_itoa(ap, totalWritten);
        append_str(&ap->buf, STR(") \033[m")); // reset colors
        ap_restore_cursor(ap);
        ap_flush(ap);
      }
      continue; // if we did I/O, skip waitpid check to the expense.
    }
    // Check if child has exited (even if PTY still open)
    int status;
    pid_t wpid = waitpid(pid, &status, WNOHANG);
    LOG_DEBUG("waitpid returned %d, errno=%d", wpid, errno);
    if (wpid > 0) {
      // Child exited, log it and close loop
      if (WIFEXITED(status)) {
        int status_code = WEXITSTATUS(status);
        LOG_INFO("Program '%s' exited with status %d", program, status_code);
        ourStatus = status_code ? 1 : 0;
      } else if (WIFSIGNALED(status)) {
        LOG_INFO("Program '%s' was killed by signal %d", program,
                 WTERMSIG(status));
        ourStatus = 2;
      }
      done = true; // force exit even if PTY still has data
    }
  }
  close(fd);
  LOG_INFO("Total read: %zu bytes, total written : %zu bytes", totalRead,
           totalWritten);
  LOG_INFO("Exiting parent, cleaning up and exiting with %d", ourStatus);
  free_buf(&quoted);
  return ourStatus;
}
