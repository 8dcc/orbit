
CC=gcc
CFLAGS=-Wall -Wextra -ggdb3
LDFLAGS=$(shell sdl2-config --cflags --libs) -lm

BINS=orbit.out simple-collision.out

#-------------------------------------------------------------------------------

.PHONY: clean all

all: $(BINS)

clean:
	rm -f $(BINS)

#-------------------------------------------------------------------------------

$(BINS): %.out : src/%.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
