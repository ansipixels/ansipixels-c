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
void free_buf(buffer *b);
void ensure_cap(buffer *dest, size_t new_cap);

ssize_t read_buf(int fd, buffer *b);

// Ensure there is enough capacity to read at least min bytes at the end of current buffer
// and tries to read as much as the remaining capacity allows.
ssize_t read_at_least(int fd, buffer *b, size_t min);
// Ensures there is enough capacity to read n bytes at the end of current buffer
// and tries to read that many. Returns number of bytes read, or -1 on error.
// Can be less than n at end of file or if not enough data is currently available,
// but will never read more than n.
ssize_t read_n(int fd, buffer *b, size_t n);

ssize_t write_buf(int fd, buffer b);
ssize_t write_all(int fd, const char *buf, ssize_t len);

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
const char *mempbrk(const char *s, size_t n, const char *accept, size_t accept_len);

void consume(buffer *b, size_t n);
void transfer(buffer *dest, buffer *src, size_t n);

// Version that keeps reusing the same quote buffer to avoid unnecessary allocations
// when debugging buffers in a loop and usable directly in a LOG_DEBUG %s.
const char *debug_buf(buffer *shared_buf, buffer b);
// Same with ptr + len.
const char *debug_data(buffer *shared_buf, const char *data, size_t size);
