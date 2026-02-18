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
#include <signal.h>
#include <stdio.h>
#include <sys/select.h>
#include <unistd.h>
#include <util.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s program args...\n", argv[0]);
    return 1; // error already logged
  }
  ap_t ap = ap_open();
  if (ap == NULL) {
    return 1; // error already logged
  }
  char *program = argv[1];
  char path[4096];
  int fd;
  pid_t pid = forkpty(&fd, path, NULL,
                      NULL); // fork a new process with a new pseudo-terminal
  if (pid < 0) {
    LOG_ERROR("Error forking process: %s", strerror(errno));
    return 1;
  }
  if (pid == 0) {
    // In child process: execute the requested program
    LOG_INFO("In child process, executing program '%s'", program);
    int ret = execvp(program, argv + 1);
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
  ssize_t n;
  int pty_closed = 0;
  int stdin_closed = 0;
  sigset_t origmask, newmask;

  // Block SIGCHLD, will be unmasked during pselect()
  sigemptyset(&newmask);
  sigaddset(&newmask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &newmask, &origmask);
  int ourStatus = 0;
  buffer quoted = {0};
  while (!pty_closed) {
    FD_ZERO(&readfds);
    if (!stdin_closed) {
      FD_SET(STDIN_FILENO, &readfds); // monitor stdin (parent's input)
    }
    FD_SET(fd, &readfds); // monitor PTY (child's output)

    // pselect unmasks SIGCHLD atomically during select, so we wake on child
    // exit
    int ret = pselect(fd + 1, &readfds, NULL, NULL, NULL, &origmask);
    LOG_DEBUG("pselect ret=%d, errno=%d, stdin_closed=%d, pty_closed=%d", ret,
              errno, stdin_closed, pty_closed);
    if (ret < 0) {
      if (errno != EINTR) {
        LOG_ERROR("pselect error: %s", strerror(errno));
        break;
      }
      // If EINTR, fall through to check waitpid
    } else if (ret == 0) {
      // Timeout (shouldn't happen with NULL timeout), continue
      continue;
    }
    bool iodone = false;
    // Data available from stdin - send to child
    if (!stdin_closed && ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
      n = read(STDIN_FILENO, buf, sizeof(buf));
      if (n > 0) {
        quoted.size = 0; // reset quoted buffer for reuse
        quote_buf(&quoted, buf, n);
        LOG_DEBUG("Read %zd bytes from stdin, sending to child %s", n, quoted.data);
        write(fd, buf, n); // send to PTY
        iodone = true;
      } else if (n == 0) {
        // EOF on stdin, stop monitoring it
        stdin_closed = 1;
      }
    }
    // Data available from PTY - output from child
    if (ret > 0 && FD_ISSET(fd, &readfds)) {
      n = read(fd, buf, sizeof(buf));
      if (n > 0) {
        quoted.size = 0; // reset quoted buffer for reuse
        quote_buf(&quoted, buf, n);
        LOG_DEBUG("Read %zd bytes from PTY, outputting to stdout %s", n, quoted.data);
        write(1, buf, n);
        iodone = true;
      } else if (n == 0 || (n < 0 && errno == EIO)) {
        // PTY closed or EIO - child has ended
        pty_closed = 1;
      }
    }
    if (iodone) {
      continue; // if we did I/O, skip waitpid check to the expense.
    }

    // Check if child has exited (even if PTY still open)
    int status;
    pid_t wpid = waitpid(pid, &status, WNOHANG);
    LOG_DEBUG("waitpid returned %d, errno=%d", wpid, errno);
    if (wpid > 0) {
      // Child exited, log it and close loop
      if (WIFEXITED(status)) {
        LOG_INFO("Program '%s' exited with status %d", program,
                 WEXITSTATUS(status));
        ourStatus = 1;
      } else if (WIFSIGNALED(status)) {
        LOG_INFO("Program '%s' was killed by signal %d", program,
                 WTERMSIG(status));
        ourStatus = 2;
      }
      pty_closed = 1; // force exit even if PTY still has data
    }
  }
  close(fd);
  sigprocmask(SIG_SETMASK, &origmask, NULL);
  LOG_INFO("Exiting parent, cleaning up and exiting with %d", ourStatus);
  return ourStatus;
}
