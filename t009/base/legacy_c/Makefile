CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O2

.PHONY: all smoke clean
all: smoke

smoke: uds.c uds.h smoke_test.c
	$(CC) $(CFLAGS) -o smoke uds.c smoke_test.c

clean:
	rm -f smoke
