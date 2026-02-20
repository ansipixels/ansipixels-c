#
# ansipixels-c:
# A C library for rendering fast Terminal User Interfaces (TUIs)
# using ANSI codes. Inspired by the Go library
# https://pkg.go.dev/fortio.org/terminal/ansipixels
#
# (C) 2026 Laurent Demailly <ldemailly at gmail> and contributors.
# No warranty implied or expressly granted. Licensed under Apache 2.0 (see LICENSE).

DEMO_BINS:=$(patsubst demos/%.c,%,$(wildcard demos/*.c))

all: $(DEMO_BINS) run-fps

ci-check: clean $(DEMO_BINS) test

demo-binaries: $(DEMO_BINS)

format:
	clang-format -i demos/*.c src/*.c include/*.h

DEBUG ?= 1
WAIT_FOR_DEBUGGER ?= 0
SAN ?= -fsanitize=address
NO_COLOR ?= 0
OPTS ?= -O3 -flto
CFLAGS = $(OPTS) -I./include -Wall -Wextra -pedantic -Werror $(SAN) -DNO_COLOR=$(NO_COLOR) -DDEBUG=$(DEBUG) -DDEBUGGER_WAIT=$(WAIT_FOR_DEBUGGER)

LIB_OBJS:=src/buf.o src/str.o src/raw.o src/log.o src/timer.o src/ansipixels.o

libansipixels.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

demos/%.o: demos/%.c include/ansipixels.h
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rule: make any demo by name (e.g., 'make foo' builds demos/foo.c)
%: demos/%.o libansipixels.a
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Keep .o files for debugging
.PRECIOUS: demos/%.o src/%.o

run-fps: fps record
	./record --hud -- ./fps -n 10000 -t 1000
	$(RM) -f record.log
	./record --hud --output ./record.log -- sh -c "echo hi && sleep 1 && echo bye"
	@echo "--- record.log ---"
	@cat record.log

clean:
	rm -rf src/*.o demos/*.o $(DEMO_BINS) libansipixels.a dist/*

update-headers:
	./scripts/update_headers.sh


GPERF_LIB_DIR ?= /usr/local/lib

profile-demos:
	make clean demo-binaries OPTS="-g -O2" LDFLAGS="-L $(GPERF_LIB_DIR) -lprofiler" SAN= DEBUG=0

local-check:
	./scripts/run.sh

# later... add unit tests
test:

.PHONY: clean all format update-headers run-fps ci-check test local-check profile-demos
