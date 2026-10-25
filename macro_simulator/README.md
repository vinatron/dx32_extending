# Macro Language Simulator (`dxsim`)

A PC-side simulator for the Bridgeport DX32 / BMDC part-program language: BOSS G-code plus the
PPPL/3PL macro extension (variables, expressions, conditionals, labels, loops, macros). Use it to try
programs, including new macro and probing cycles (`../EXT_PROBING.md`), on a PC before they go near the
machine.

* Target firmware: **6.89** (repo-root `BMDC.BIN`). The TC22 is moving to 6.89 (`../VERSIONS.md`).
* Written in **ANSI C89** for DOS, Windows 98 and later IBM z/OS (plots through GDDM).
* The canned cycles are not reimplemented: `dxsim` runs **the firmware's own cycle programs**, extracted
  from `BMDC.BIN` into `src/dxcorpus.c`, with the same interpreter as user programs.
* The output is a **trace** (every move, dwell, tool change, optionally every variable assignment) and an
  optional **plot** (SVG, HPGL or PostScript). There is no machine I/O and no timing; motion is geometric.

`LANGUAGE.md` is the language reference: the manual cross-checked against the firmware, with
CONFIRMED/TENTATIVE marks and firmware addresses.

## Layout

| Path | Contents |
|---|---|
| `LANGUAGE.md` | Language reference (manual + firmware 6.89), canned-cycle variable rules, open questions |
| `manual/` | Programmer's manual (1994), operator's manual (1997), work-shift appendix (PDF) |
| `src/` | The simulator (C89): `dxmain.c` command line, `dxinterp.c` interpreter and flow control, `dxexpr.c` numbers, variables and PPPL expressions, `dxmotion.c` geometry, `dxout.c` trace and plots, `dxprog.c` program text, `dxcorpus.c` built-in cycles (generated), `dxscreen.c` live VGA preview (DOS), `dxgddm.c` GDDM device (z/OS, not built by default) |
| `src/makefile.wat` | Open Watcom: DOS 16-bit, DOS/4GW, Win32 console |
| `Makefile` | gcc / clang on Linux, macOS, Cygwin |
| `tests/` | Test programs (`*.txt`, 8.3 names, most from the manual's figures, listed in `tests.lst`), expected traces, `dxtest.c` runner, `RUNTESTS.BAT` |
| `corpus_689.txt` | The 6.89 built-in cycle text, one block per line with offsets and table entries |
| `tools/extract_corpus.py`, `tools/gen_corpus_c.py` | Regenerate `corpus_689.txt` / `src/dxcorpus.c` from `../BMDC.BIN` |
| `tools/check_grammar.py` | Independent Python recognizer: every corpus block fits the grammar |
| `tools/m68k_dis.py` | `python3 tools/m68k_dis.py START END`: disassembler for `../BMDC.BIN` (capstone), with its own decoding of 68882 instructions and FBcc, used to trace the interpreter |

## Building

The source is plain C89 with no POSIX calls, so any C compiler should do.

```
make                                  # Linux/macOS: gcc or clang, produces ./dxsim

cd src                                # Open Watcom 1.9 or 2.0
wmake -f makefile.wat dos             # DXSIM.EXE    16-bit DOS, large model (8086 and up), with -S preview
wmake -f makefile.wat dos32           # DXSIM4G.EXE  DOS/4GW extender, with -S preview
wmake -f makefile.wat win32           # DXSIM32.EXE  console, Windows 95/98/NT and later
wmake -f makefile.wat tests           # ..\tests\DXTEST.EXE test runner (tests32 for Windows)

bcc -ml -A dxmain.c dxinterp.c dxexpr.c dxmotion.c dxprog.c dxout.c dxcorpus.c   # Borland/Turbo C++ (untested)
gcc -std=c89 -o dxsim.exe *.c -lm                                              # DJGPP (untested)
```

Checked here: gcc and clang with `-std=c89 -pedantic -Wall -Wextra`, and Open Watcom 1.9 with `-za -wx`
for the three targets above and the test runner (no warnings). Built and run by the owner on
Windows 98 SE.

Portability rules kept in the source: no `//` comments, declarations only at the top of blocks, no
`snprintf`; nothing larger than 64 KB in one object (the block table is chunked, the simulator state is
under 40 KB); letters are looked up in a string, never with `c - 'A'`, so EBCDIC works; external names are
unique in 8 characters ignoring case.

### z/OS and GDDM (later)

`src/dxgddm.c` is a skeleton plot device for GDDM (FSINIT, GSFLD, GSWIN, GSMOVE, GSLINE, ASREAD, FSTERM).
Build with `-DDX_GDDM`, include `dxgddm.c`, and run with `-g` to draw on a 3270 graphics terminal. The
GDDM C binding (header name, argument types) has to be checked against the installed GDDM before the first
build; only the `DXG_` macros at the top of the file should need changes. File names are 8.3 and external
names fit 8 characters, so the members can go into a PDS as they are.

## Running

```
dxsim [options] program.txt
  -o FILE    trace to FILE (default: screen)       -q    no trace
  -p FILE    plot: FILE.svg, FILE.plt (HPGL), FILE.ps (PostScript)
  -v VIEW    plot view xy (default), xz, yz, iso   -r    do not draw rapids
  -S / -Sf   live VGA preview, paced per move / at the feed rate (DOS builds)
  -s         trace variable assignments             -b/-B echo program blocks / also built-in blocks
  -d         dump the variables set, at the end     -cov  list the built-in blocks executed
  -M FILE    load M-macro definitions (M<n>: ... |)
  -t T=D[,L] tool T diameter (and length offset)    -x V=val   preset a variable (-x X1=2.5)
  -i N=val   user input N for RDI()/GET()           -n N  block limit (endless loops)
  -m / -mm   metric power-on mode / show mm         -peck V    param 272 (default .05 in)
  -nopppl    machine without the PPPL option        -boss      BOSS 4-7 compatibility mode
  -keep      keep blanks in blocks                  -del       block delete switch on
  -p8 0|1    P8 for G170-G179 (machine family flag, default 1)
  -e EXPR    evaluate a PPPL expression and exit    -corpus / -check   list / parse the built-in cycles
```

Example (`tests/m1341.txt`, the manual's 3PL example):

```
$ ./dxsim tests/m1341.txt -s -p circles.svg
L2      TOOL  M6 change to T1 (TLO 0.0000 DIA 0.0000)
L3      SET   X1 = 5
L5      RAPID X0.0000 Y0.0000 Z1.0000
L5      RAPID X5.0000 Y0.0000 Z1.0000
L6      FEED  X5.0000 Y0.0000 Z0.0000  F40.0
L7      ARC2  X5.0000 Y0.0000 Z0.0000  CX0.0000 Y0.0000 R5.0000  -360.000 deg  F40.0
L8      SET   X1 = 4
...
```

The first column is the source: `L7` is line 7 of the program, `@26986` a block of the built-in cycle text
(its offset in `BMDC.BIN`, see `corpus_689.txt`). Coordinates are base part coordinates (after G92, G97,
G73, mirror). `ARC2`/`ARC3` give the end point, centre, radius and signed sweep; `SPIRL` a polar spiral.
Errors use the control's own messages (operator's manual appendix D) and name the block.

Programs: one block per line, as on the control. The manual writes the end of block as `;`, which is also
the comment character, so `N10G1X1.F10.; feed` is fine.

## Live preview on the screen (DOS)

The DOS builds (`wmake -f makefile.wat dos` or `dos32`) draw the tool path on a VGA screen while the program
runs, like the control's own **F5 PREVIEW** (operator's manual chapter 3). They use Open Watcom's
`graph.lib`. This works in plain DOS and full-screen in a Windows 98 DOS box.

```
DXSIM -S  PROGRAM.TXT        draw as the program runs, a fixed pause after each move
DXSIM -Sf PROGRAM.TXT        draw at the programmed feed rate (rapids at 472 ipm): about machine time
```

| Key | Action |
|---|---|
| SPACE | pause / continue |
| B | block by block on/off: stops before each program block and shows it (a canned cycle runs as one block) |
| + / - | faster / slower: pause per move 0-800 ms, or feed-rate factor ×1/64…×64 |
| F | switch between per-move and feed-rate pacing |
| ESC | stop |

The status lines show the block, the position, the feed and the pacing. Rapids are drawn red and dashed,
feeds cyan, drilled holes as a small x. The view is chosen with `-v xy|xz|yz|iso` and the whole part is fitted to
the screen (a quick first pass measures it). The trace goes to a file only with `-o`, so it does not
write over the graphics. The DOS timer ticks 18.2 times a second, so feed-rate pacing is approximate.

`make test-screen` (Linux) runs `dxscreen.c` with `graph.lib`, the keyboard and `delay()` replaced by a logger
(`-DDX_SCSTUB`). It checks that every test program stays inside the 640×448 drawing area and reports the
paced time.

## Testing

The test runner `tests/dxtest.c` is C89 like the simulator, so the tests run wherever `dxsim` runs.

```
make test                                   Linux/macOS (also runs tools/check_grammar.py)

cd src                                      DOS / Windows 98 with Open Watcom
wmake -f makefile.wat dos tests             DXSIM.EXE + ..\tests\DXTEST.EXE (16-bit DOS)
wmake -f makefile.wat win32 tests32         DXSIM32.EXE + DXTEST.EXE (Windows console)
cd ..\tests
RUNTESTS                                    results on screen and in RESULTS.TXT
```

`DXTEST [-u] [path-to-dxsim]` must run in `tests/`. It starts `dxsim` with `system()` and only reads the
files it writes (`-o`), so it needs neither shell redirection nor exit codes, which COMMAND.COM does not
return. Test programs use 8.3 names and are listed in `tests/tests.lst`.

1. `dxsim -check` parses all 652 blocks of the built-in cycle text (628 non-empty).
2. Every program in `tests.lst` must reproduce `expected/<name>.out`. Numbers may differ by one unit in
   the last printed digit, because C libraries round halfway cases differently. After a reviewed change,
   `DXTEST -u` (or `RUNTESTS path\DXSIM.EXE -u`) rewrites the expected files; check the diff.
3. Results worked out by hand from the manual's figures: the five 3PL circles, the G81 example, the G191
   frame (18 holes), the G181 row, both bolt circles (6 holes at 30°…330° on R1.75), the nested-loop example
   (42 holes in 6 rows), G83 peck depths, macro parameters, mirror image, the three arc forms, and the
   expression evaluator.
4. The tests together execute 617 of the 628 non-empty built-in blocks. The rest are G178's empty body
   and the A33 helper, which no block calls.

## Not simulated (yet)

Cutter diameter compensation (G41/G42: the plot shows the programmed path), fillets (G22/G23: cut as a
straight line to the corner), spherical coordinates (E), constant surface feed and feed per revolution
(recorded only), tool length offsets and clear-point moves (traced, not applied), feed timing. `dxsim`
prints a warning the first time a program uses one of them.
