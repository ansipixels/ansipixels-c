#pragma once

#define RED "\033[31m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"
#define GREEN "\033[32m"
#define RESET "\033[0m"
#define END_LOG RESET "\n"

#if DEBUG
void log_debug(const char *file, int line, const char *fmt, ...);
#define LOG_DEBUG(...) log_debug(__FILE__, __LINE__, __VA_ARGS__)
#else
#define LOG_DEBUG(...)                                                         \
  do {                                                                         \
    if (0)                                                                     \
      printf(__VA_ARGS__);                                                     \
  } while (0)
#endif

#define LOG_ERROR(fmt, ...)                                                    \
  do {                                                                         \
    fprintf(stderr, RED "%s:%d: ", __FILE__, __LINE__);                        \
    fprintf(stderr, fmt END_LOG, __VA_ARGS__);                                 \
  } while (0)
