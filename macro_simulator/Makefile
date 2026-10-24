# Makefile for dxsim (GNU make with gcc or clang).
# DOS / Windows / z/OS builds: see src/makefile.wat and README.md.

CC      ?= cc
CFLAGS  ?= -std=c89 -pedantic -Wall -Wextra -O2
SRC      = src/dxmain.c src/dxinterp.c src/dxexpr.c src/dxmotion.c src/dxprog.c \
           src/dxout.c src/dxcorpus.c

dxsim: $(SRC) src/dxsim.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

# regenerate the built-in cycle text from the firmware image
src/dxcorpus.c: ../BMDC.BIN tools/gen_corpus_c.py tools/extract_corpus.py
	python3 tools/gen_corpus_c.py

test: dxsim
	python3 tests/run_tests.py
	python3 tools/check_grammar.py

clean:
	rm -f dxsim

.PHONY: test clean
