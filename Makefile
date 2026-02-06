
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


.PHONY: clean all format
