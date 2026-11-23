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

# el simulador y el firmware (fm1-base)
.PHONY: sim fw
sim:
	python3 base/tools/build.py app --host
	app/build/host/barullin_host app/tests/basic.txt app/build/host

fw:
	python3 base/tools/build.py app
