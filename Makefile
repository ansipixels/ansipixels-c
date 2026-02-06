#
# ansipixels-c:
# A C library for rendering fast Terminal User Interfaces (TUIs)
# using ANSI codes. Inspired by the Go library
# https://pkg.go.dev/fortio.org/terminal/ansipixels
#
# (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
# No warranty implied or expressly granted. Licensed under Apache 2.0 (see LICENSE).

all: fps ansipixels run-fps

format:
	clang-format -i *.c *.h

DEBUG ?= 1
SAN ?= -fsanitize=address
NO_COLOR ?= 0
OPTS ?= -O3 -flto
CFLAGS = $(OPTS) -Wall -Wextra -pedantic -Werror $(SAN) -DNO_COLOR=$(NO_COLOR) -DDEBUG=$(DEBUG)

LIB_OBJS:=buf.o str.o raw.o log.o timer.o ansipixels.o

ansipixels: $(LIB_OBJS) main.o
	$(CC) $(CFLAGS) -o $@ $^

fps: $(LIB_OBJS) fps.o
	$(CC) $(CFLAGS) -o $@ $^

run-fps: fps
	./fps -n 10000 -t 1000

clean:
	rm -rf *.o *.dSYM ansipixels fps

update-headers:
	./update_headers.sh

.PHONY: clean all format update-headers run-fps
