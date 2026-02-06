#
# ansipixels-c:
# A C library for rendering fast Terminal User Interfaces (TUIs)
# using ANSI codes. Inspired by the Go library
# https://pkg.go.dev/fortio.org/terminal/ansipixels
#
# (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
# No warranty implied or expressly granted. Licensed under MIT (see LICENSE).

all: ansipixels

format:
	clang-format -i *.c *.h

DEBUG ?= 1
SAN ?= -fsanitize=address
NO_COLOR ?= 0
CFLAGS = -g -Wall -Wextra -pedantic -Werror $(SAN) -DNO_COLOR=$(NO_COLOR) -DDEBUG=$(DEBUG)

ansipixels: buf.o str.o raw.o log.o ansipixels.o main.o
	$(CC) $(CFLAGS) -o $@ $^
	./$@

clean:
	rm -rf *.o *.dSYM ansipixels

update-headers:
	./update_headers.sh

.PHONY: clean all format update-headers
