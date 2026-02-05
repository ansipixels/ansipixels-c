
all: ansipixels

format:
	clang-format -i *.c *.h

DEBUG ?= 1
SAN ?= -fsanitize=address
CFLAGS = -g -Wall -Wextra -pedantic -Werror $(SAN) -DDEBUG=$(DEBUG)

ansipixels: buf.o str.o raw.o main.o
	$(CC) $(CFLAGS) -o $@ $^
	./$@

clean:
	rm -rf *.o *.dSYM ansipixels


.PHONY: clean all format
