/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */
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

void quote_buf(buffer *b, const char *s, size_t size);
buffer debug_quote(const char *s, size_t size);
void debug_print_buf(buffer b);

// mempbrk is like memchr but searches for any of the bytes in accept
// and returns a pointer to the first occurrence in s, or NULL if not found.
const char *mempbrk(const char *s, size_t n, const char *accept,
                    size_t accept_len);
