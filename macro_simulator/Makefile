# Makefile for dxsim (GNU make with gcc or clang).
# DOS / Windows / z/OS builds: see src/makefile.wat and README.md.

CC      ?= cc
CFLAGS  ?= -std=c89 -pedantic -Wall -Wextra -O2
SRC      = src/dxmain.c src/dxinterp.c src/dxexpr.c src/dxmotion.c src/dxprog.c \
           src/dxout.c src/dxcorpus.c src/dxscreen.c src/dxparam.c src/dxcomp.c src/dxmap.c

FBSRC    = src/fbmain.c src/fbread.c src/fbdial.c src/fbeval.c src/fbout.c

all: dxsim fb2dx

dxsim: $(SRC) src/dxsim.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

# the FANUC custom macro translator (features/FB2DX.md)
fb2dx: $(FBSRC) src/fb2dx.h
	$(CC) $(CFLAGS) -o $@ $(FBSRC) -lm

# regenerate the built-in cycle text from the firmware image
src/dxcorpus.c: ../BMDC.BIN tools/gen_corpus_c.py tools/extract_corpus.py
	python3 tools/gen_corpus_c.py

tests/dxtest: tests/dxtest.c
	$(CC) $(CFLAGS) -o $@ tests/dxtest.c -lm

test: dxsim fb2dx tests/dxtest
	cd tests && ./dxtest ../dxsim
	python3 tools/check_grammar.py

clean:
	rm -f dxsim fb2dx dxsim-scstub tests/dxtest

# the VGA preview (dxscreen.c) with graph.lib, keyboard and delay() replaced by a
# logger: every test program must stay inside the 640x448 drawing area
dxsim-scstub: $(SRC) src/dxsim.h
	$(CC) $(CFLAGS) -DDX_SCREEN -DDX_SCSTUB -o $@ $(SRC) -lm

test-screen: dxsim-scstub
	cd tests && grep -v '^;' tests.lst | while read t o; do \
	  case "$$o" in *-lint*) continue;; esac; \
	  for v in g17 all; do \
	    DXSC_KEYS= ../dxsim-scstub $$t.txt $$o -Sf -v $$v >/dev/null 2>&1; \
	    printf '%-8s %-4s %s\n' $$t $$v "`tail -1 dxscreen.log`"; \
	    tail -1 dxscreen.log | grep -q ' offscreen 0 ' || exit 1; \
	  done; \
	done; rm -f dxscreen.log

.PHONY: all test test-screen clean
