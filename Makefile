CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDLIBS  = -lm

blackhole: src/main.c src/game.c src/game.h
	$(CC) $(CFLAGS) -o $@ src/main.c src/game.c $(LDLIBS)

run: blackhole
	./blackhole

clean:
	rm -f blackhole

.PHONY: run clean
