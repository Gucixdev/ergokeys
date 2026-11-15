# ergokeys build. One translation unit (src/main.c includes the rest), C89, no dependencies beyond libc.
#   make          build/ergokeys
#   make test     every test under gcc and clang, with sanitizers, plus the C89 pedantic build
#   make fuzz     the parser fuzz targets with many more iterations (make test runs them with fewer)
#   make bench    bench/bench.c, appends to bench/RESULTS.md
#   make install  PREFIX (default /usr/local): binary and layouts
CC ?= gcc
PREFIX ?= /usr/local
STD = -std=c89 -pedantic -Wall -Wextra -Werror -ffp-contract=off
CFLAGS ?= -O2
DATADIR = $(PREFIX)/share/ergokeys

SRC = $(wildcard src/*.c src/*.h) build/help.inc

all: build/ergokeys

build:
	mkdir -p build

# --help is the text between HELP-BEGIN and HELP-END in the header of src/main.c
build/help.inc: src/main.c | build
	sed -n '/HELP-BEGIN/,/HELP-END/p' src/main.c | sed '1d;$$d' | sed 's/^ \* \{0,1\}//; s/^ \*$$//' \
	  | sed 's/\\/\\\\/g; s/"/\\"/g; s/^/"/; s/$$/",/' | { echo 'static const char *const ek_help_lines[] = {'; cat; echo '0};'; } > $@

build/ergokeys: $(SRC) | build
	$(CC) $(STD) $(CFLAGS) -Ibuild -DEK_DATADIR='"$(DATADIR)"' src/main.c -o $@

test: build/ergokeys
	sh tests/run-tests

fuzz: build/ergokeys
	FUZZ_ITERS=$${FUZZ_ITERS:-300000} sh tests/run-tests fuzz

bench: build/ergokeys
	sh bench/run-bench

# the layout directory is compiled in, so the installed binary is built for this PREFIX, not copied from build/
install: $(SRC) | build
	$(CC) $(STD) $(CFLAGS) -Ibuild -DEK_DATADIR='"$(DATADIR)"' src/main.c -o build/ergokeys.install
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(DATADIR)/layouts
	cp build/ergokeys.install $(DESTDIR)$(PREFIX)/bin/ergokeys
	cp layouts/*.layout $(DESTDIR)$(DATADIR)/layouts/

.PHONY: all test fuzz bench install
