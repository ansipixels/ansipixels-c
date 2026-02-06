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

#if NO_COLOR
// No color in logs/stderr:
#define RED ""
#define BLUE ""
#define CYAN ""
#define GREEN ""
#define RESET ""
#else
// ANSI color codes for logs:
#define RED "\033[31m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"
#define GREEN "\033[32m"
#define RESET "\033[0m"
#endif

#define END_LOG RESET "\n"

#if DEBUG
void log_debug(const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
#define LOG_DEBUG(...) log_debug(__FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)(0 && printf(__VA_ARGS__)))
#endif

#define LOG_ERROR(fmt, ...)                                                    \
  do {                                                                         \
    fprintf(stderr, RED "ERR %s:%d: ", __FILE__, __LINE__);                    \
    fprintf(stderr, fmt END_LOG, __VA_ARGS__);                                 \
  } while (0)
