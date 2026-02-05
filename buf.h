#pragma once
#include "str.h"
#include <unistd.h>

typedef struct buf {
  char *data;
  size_t size;
  size_t cap;
#if DEBUG
  int allocs; // for debugging reallocs
#endif
} buffer;

buffer new_buf(size_t size);
void free_buf(buffer b);

ssize_t read_buf(int fd, buffer *b);
ssize_t write_buf(int fd, buffer b);

void append_data(buffer *dest, const char *data, size_t size);
void append_buf(buffer *dest, buffer src);
void append_str(buffer *dest, string src);
void append_byte(buffer *dest, char byte);

buffer slice_buf(buffer b, size_t start, size_t end);

buffer debug_quote(const char *s, size_t size);
void debug_print_buf(buffer b);
