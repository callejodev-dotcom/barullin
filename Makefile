CC ?= cc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Werror -ffp-contract=off

.PHONY: test clean
test: build/test_chords
	./build/test_chords

build/test_chords: tests/test_chords.c app/core/chords.c app/core/chords.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/test_chords.c app/core/chords.c

clean:
	rm -rf build
