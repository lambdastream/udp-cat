GCC = gcc
CFLAGS = -Wall -ggdb3

all: udp-cat

udp-cat: udp-cat.c
	$(GCC) $(CFLAGS) udp-cat.c -o udp-cat

clean:
	rm udp-cat.o udp-cat

.PHONY: clean

