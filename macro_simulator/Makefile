# Makefile for dxsim (GNU make with gcc or clang).
# DOS / Windows / z/OS builds: see src/makefile.wat and README.md.

CC      ?= cc
CFLAGS  ?= -std=c89 -pedantic -Wall -Wextra -O2
SRC      = src/dxmain.c src/dxinterp.c src/dxexpr.c src/dxmotion.c src/dxprog.c \
           src/dxout.c src/dxcorpus.c src/dxscreen.c src/dxparam.c src/dxcomp.c src/dxmap.c

FBSRC    = src/fbmain.c src/fbread.c src/fbdial.c src/fbeval.c src/fbout.c src/fbcheck.c

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

test-screen: dxsim-scstub tests/dxtest
	cd tests && ./dxtest -sc ../dxsim-scstub

# The Windows programs and the batch files, as on a Windows machine: MinGW builds of DXSIM32, DXSIMW,
# FB2DX and DXTEST, then RUNTESTS.BAT and PREVIEW.BAT under Wine, the preview on a virtual X display.
# Needs mingw-w64, wine64 and xvfb (apt: gcc-mingw-w64-x86-64 wine64 xvfb). Not a Watcom build.
MINGW   ?= x86_64-w64-mingw32-gcc
WINE    ?= /usr/lib/wine/wine64
WSRC     = $(subst src/,,$(SRC))
WFBSRC   = $(subst src/,,$(FBSRC))

test-wine:
	cd src && $(MINGW) -O2 -o DXSIM32.EXE $(WSRC) && \
	  $(MINGW) -O2 -DDX_SCREEN -DDX_WIN -o DXSIMW.EXE $(WSRC) dxwin.c -lgdi32 -luser32 && \
	  $(MINGW) -O2 -o FB2DX.EXE $(WFBSRC) && $(MINGW) -O2 -o ../tests/DXTEST.EXE ../tests/dxtest.c
	cd tests && WINEDEBUG=-all $(WINE) cmd /c RUNTESTS.BAT ..\\src\\DXSIM32.EXE > /dev/null 2>&1; \
	  tail -1 RESULTS.TXT; grep -q ', 0 failed' RESULTS.TXT
	cd tests && WINEDEBUG=-all xvfb-run -a -s "-screen 0 1024x768x24" $(WINE) cmd /c PREVIEW.BAT > /dev/null 2>&1; \
	  tail -1 PREVIEW.TXT; grep -q ', 0 failed' PREVIEW.TXT

.PHONY: all test test-screen test-wine clean
