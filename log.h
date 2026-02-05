#pragma once

#define RED "\033[31m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"
#define GREEN "\033[32m"
#define RESET "\033[0m"
#define END_LOG RESET "\n"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#if DEBUG
#define LOG_DEBUG(fmt, ...)                                      \
  do {                                                          \
    fprintf(stderr, GREEN fmt END_LOG, ##__VA_ARGS__);           \
  } while (0)
#else
#define LOG_DEBUG(fmt, ...)                                                    \
  while (0) {                                                                  \
    fprintf(stderr, fmt, __VA_ARGS__);                                         \
  }
#endif

#define LOG_ERROR(fmt, ...)                                                    \
  do {                                                                         \
    fprintf(stderr, RED fmt END_LOG, __VA_ARGS__);                             \
  } while (0)
