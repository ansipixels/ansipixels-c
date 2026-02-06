#
# ansipixels-c:
# A C library for rendering fast Terminal User Interfaces (TUIs)
# using ANSI codes. Inspired by the Go library
# https://pkg.go.dev/fortio.org/terminal/ansipixels
#
# (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
# No warranty implied or expressly granted. Licensed under Apache 2.0 (see LICENSE).

all: fps ansipixels run-fps

ci-check: clean fps ansipixels test

format:
	clang-format -i src/*.c include/*.h

DEBUG ?= 1
SAN ?= -fsanitize=address
NO_COLOR ?= 0
OPTS ?= -O3 -flto
CFLAGS = $(OPTS) -I./include -Wall -Wextra -pedantic -Werror $(SAN) -DNO_COLOR=$(NO_COLOR) -DDEBUG=$(DEBUG)

LIB_OBJS:=src/buf.o src/str.o src/raw.o src/log.o src/timer.o src/ansipixels.o

libansipixels.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

ansipixels: src/main.o libansipixels.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

fps: src/fps.o libansipixels.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

run-fps: fps
	./fps -n 10000 -t 1000

clean:
	rm -rf src/*.o src/*.dSYM ansipixels fps libansipixels.a dist/*

update-headers:
	./scripts/update_headers.sh

# later... add unit tests
test:

.PHONY: clean all format update-headers run-fps ci-check test
