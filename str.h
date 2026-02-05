#pragma once

#include <unistd.h>

typedef struct str {
  const char *data;
  const size_t size;
} string;

#define STR(s) ((string){(s), sizeof(s) - 1})
#define UTF8(s) ((string){(const char *)(u8"" s), sizeof(u8"" s) - 1})

ssize_t write_str(int fd, string s);
