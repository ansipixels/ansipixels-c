/**
 * ansipixels-c:
 * A C library for rendering fast Terminal User Interfaces (TUIs)
 * using ANSI codes. Inspired by the Go library
 * https://pkg.go.dev/fortio.org/terminal/ansipixels
 *
 * (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
 * Licensed under Apache-2.0 (see LICENSE).
 */
#include "buf.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>

buffer new_buf(size_t size) {
    return (buffer){
        calloc(1, size),
        0,
        size
#if DEBUG
        ,
        1
#endif
    };
}

void free_buf(buffer *b) {
    if (b == NULL) {
        return; // nothing to free
    }
    if (b->cap == 0) {
        return; // nothing to free
    }
    free(b->data);
    b->cap = 0;
    b->size = 0;
    b->data = NULL;
}

ssize_t read_buf(int fd, buffer *b) {
    ssize_t n = read(fd, b->data + b->size, b->cap - b->size);
    if (n > 0) {
        b->size += n;
    }
    return n;
}

ssize_t read_at_least(int fd, buffer *b, size_t min) {
    ensure_cap(b, b->size + min);
    return read_buf(fd, b);
}

ssize_t read_n(int fd, buffer *b, size_t n) {
    ensure_cap(b, b->size + n);
    ssize_t r = read(fd, b->data + b->size, n);
    if (r > 0) {
        b->size += r;
    }
    return r;
}

void consume(buffer *b, size_t n) {
#if DEBUG
    if (n > b->size) {
        LOG_ERROR("Attempt to consume more bytes than available in buffer: %zu > %zu", n, b->size);
        abort();
    }
#endif
    if (n == b->size) {
        b->size = 0; // consume all
    } else {
        memmove(b->data, b->data + n, b->size - n);
        b->size -= n;
    }
}

void transfer(buffer *dest, buffer *src, size_t n) {
#if DEBUG
    if (n > src->size) {
        LOG_ERROR("transfer n greater than src size: %zu > %zu", n, src->size);
        abort();
    }
    if (dest == src) {
        LOG_ERROR("transfer src==dst %p", (void *)src);
        abort();
    }
#endif
    append_data(dest, src->data, n);
    consume(src, n);
}

void append_buf(buffer *dest, buffer src) { append_data(dest, src.data, src.size); }

size_t max(size_t a, size_t b) { return a > b ? a : b; }

void ensure_cap(buffer *dest, size_t new_cap) {
    if (new_cap <= dest->cap) {
        return; // already have enough capacity
    }
    new_cap = max(new_cap,
                  dest->cap * 2); // double capacity to reduce future reallocs
    dest->data = realloc(dest->data, new_cap);
    dest->cap = new_cap;
#if DEBUG
    dest->allocs++;
#endif
}

void append_data(buffer *dest, const char *data, size_t size) {
    ensure_cap(dest, dest->size + size);
    memcpy(dest->data + dest->size, data, size);
    dest->size += size;
}

void append_str(buffer *dest, string src) { append_data(dest, src.data, src.size); }

void append_byte(buffer *dest, char byte) { append_data(dest, &byte, 1); }

buffer slice_buf(buffer b, size_t start, size_t end) {
    return (buffer){
        b.data + start,
        end - start,
        0
#if DEBUG
        ,
        0
#endif
    }; // 0 cap for subslice
}

char to_hex_digit(int c) {
    if (c < 10) {
        return '0' + c;
    } else {
        return 'A' + (c - 10);
    }
}

buffer debug_quote(const char *s, size_t size) {
    // +1 for null terminator
    // +2 for open and close quotes
    // +1 for guessing a likely \n = +4 even if no quoting end up actually needed
    buffer b = new_buf(size + 4);
    quote_buf(&b, s, size);
    return b;
}

const char *debug_buf(buffer *shared_buf, buffer b) { return debug_data(shared_buf, b.data, b.size); }

const char *debug_data(buffer *shared_buf, const char *data, size_t size) {
    shared_buf->size = 0; // reset shared buffer for reuse
    quote_buf(shared_buf, data, size);
    return shared_buf->data;
}

void quote_buf(buffer *b, const char *s, size_t size) {
    append_byte(b, '"');
    for (size_t i = 0; i < size; i++) {
        char c = s[i];
        switch (c) {
        case '\n':
            append_str(b, STR("\\n"));
            break;
        case '\r':
            append_str(b, STR("\\r"));
            break;
        case '\t':
            append_str(b, STR("\\t"));
            break;
        case '\\':
            append_str(b, STR("\\\\"));
            break;
        case '"':
            append_str(b, STR("\\\""));
            break;
        default:
            if (c < 32 || c >= 127) {
                append_str(b, STR("\\x"));
                append_byte(b, to_hex_digit((c >> 4) & 0xF));
                append_byte(b, to_hex_digit(c & 0xF));
            } else {
                append_byte(b, c);
            }
        }
    }
    append_byte(b, '"');
    append_byte(b, '\0'); // null-terminate for printing
}

void debug_print_buf(buffer b) {
    buffer quoted = debug_quote(b.data, b.size);
    fprintf(
        stderr,
        GREEN "INF buffer { data: %p = %s, size: %zu, cap: %zu, allocs: %d/%d "
              "}" END_LOG,
        (void *)b.data,
        quoted.data,
        b.size,
        b.cap,
#if DEBUG
        b.allocs,
        quoted.allocs
#else
        -1,
        -1
#endif
    );
    free_buf(&quoted);
}

const char *mempbrk(const char *s, size_t n, const char *accept, size_t accept_len) {
    unsigned char table[256] = {0};
    for (size_t i = 0; i < accept_len; i++) {
        table[((unsigned char *)accept)[i]] = 1;
    }

    for (size_t i = 0; i < n; i++) {
        if (table[(unsigned char)s[i]]) {
            return (s + i);
        }
    }
    return NULL;
}

ssize_t write_all(int fd, const char *buf, ssize_t len) {
    if (len <= 0) {
        return len; // nothing to write
    }
    ssize_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return total ? total : -1; // keep the error for later if we did partial write.
        }
        total += n;
    }
    return total;
}

ssize_t write_buf(int fd, buffer b) { return write_all(fd, b.data, b.size); }
