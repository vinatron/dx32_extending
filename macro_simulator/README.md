# Macro Language Simulator (`dxsim`)

A PC-side simulator for the Bridgeport DX32 / BMDC part-program language: BOSS G-code plus the
PPPL/3PL macro extension (variables, expressions, conditionals, labels, loops, macros). Use it to try
programs, including new macro and probing cycles (`../EXT_PROBING.md`), on a PC before they go near the
machine.

* Target firmware: **6.89** (repo-root `BMDC.BIN`). The TC22 is moving to 6.89 (`../VERSIONS.md`).
* Written in **ANSI C89** for DOS and Windows 98. A z/OS port with GDDM plots is planned for a later,
  separate PR; the plot-device layer is ready for it.
* The canned cycles are not reimplemented: `dxsim` runs **the firmware's own cycle programs**, extracted
  from `BMDC.BIN` into `src/dxcorpus.c`, with the same interpreter as user programs.
* The output is a **trace** (every move, dwell, tool change, optionally every variable assignment) and an
  optional **plot** (SVG, HPGL or PostScript). There is no machine I/O and no timing; motion is geometric.

`LANGUAGE.md` is the language reference: the manual cross-checked against the firmware, with
CONFIRMED/TENTATIVE marks and firmware addresses. `ROADMAP.md` lists the features planned next.

## Layout

| Path | Contents |
|---|---|
| `LANGUAGE.md` | Language reference (manual + firmware 6.89), canned-cycle variable rules, open questions |
| `bench/` | Short programs for the machine that answer open questions (`features/OPENQ.md`), CR/LF, 8.3 names |
| `features/` | One design document per feature being built (`features/README.md` is the index), e.g. `CUTCOMP.md` for cutter compensation |
| `ROADMAP.md` | Planned features: machine parameters, cutter compensation, fillets, program flow view, z/OS |
| `manual/` | Programmer's manual (1994), operator's manual (1997), work-shift appendix (PDF) |
| `src/` | The simulator (C89): `dxmain.c` command line, `dxinterp.c` interpreter and flow control, `dxexpr.c` numbers, variables and PPPL expressions, `dxmotion.c` geometry, `dxout.c` trace and plots, `dxprog.c` program text, `dxcorpus.c` built-in cycles (generated), `dxscreen.c` live preview (DOS VGA; Windows window via `dxwin.c`/`dxgraph.h`) |
| `src/fbmain.c`, `fbread.c`, `fbdial.c`, `fbeval.c`, `fb2dx.h` | `fb2dx`, the FANUC custom macro translator (in progress, `features/FB2DX.md`): command line, FANUC reader, series table and detection, evaluator |
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
make                                  # Linux/macOS: gcc or clang, produces ./dxsim and ./fb2dx

cd src                                # Open Watcom 1.9 or 2.0
wmake -f makefile.wat dos             # DXSIM.EXE    16-bit DOS, large model (8086 and up), with -S preview
wmake -f makefile.wat dos32           # DXSIM4G.EXE  DOS/4GW extender, with -S preview
wmake -f makefile.wat win32           # DXSIM32.EXE  console, Windows 95/98/NT and later
wmake -f makefile.wat win32w          # DXSIMW.EXE   same, with the -S preview in a window
wmake -f makefile.wat fb              # FB2DX.EXE    the FANUC macro translator, DOS (fb32 for Windows)
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

## Running

```
dxsim [options] program.txt
  -o FILE    trace to FILE (default: screen)       -q    no trace
  -p FILE    plot: FILE.svg, FILE.plt (HPGL), FILE.ps (PostScript)
  -v VIEW    view g17 (default), g18, g19, iso, all -r    do not draw rapids
  -S / -Sf   live preview, paced per move / at the feed rate (DOS builds, DXSIMW.EXE)
  -s         trace variable assignments             -b/-B echo program blocks / also built-in blocks
  -d         dump the variables set, at the end     -cov  list the built-in blocks executed
  -M FILE    load M-macro definitions (M<n>: ... |)
  -t T=D[,L] tool T diameter (and length offset)    -x V=val   preset a variable (-x X1=2.5)
  -i N=val   user input N for RDI()/GET()           -n N  block limit (endless loops)
  -m / -mm   metric power-on mode / show mm         -peck V    param 272 (default .05 in)
  -nopppl    machine without the PPPL option        -boss      BOSS 4-7 compatibility mode
  -keep      keep blanks in blocks                  -del       block delete switch on
  -p8 0|1    P8 for G170-G179 (machine family flag, default 1)
  -lint      check every line without running it    -silent    no error/warning lines on the console
  -flow      program flow only (calls, returns, cycles, loops, jumps, skips)   -flow+  flow lines in the full trace
  -pc        colour feed moves by the macro or canned cycle that made them, with a key
  -map FILE  call map: FILE.svg as a diagram, otherwise a text tree, with map warnings
  -P FILE    machine parameters (BMDCPRMS.SYS, BMDCPRMS.INI)   -model M   apply [M] of BMDCPRMS.INI
  -zero X,Y,Z  machine position of part zero: check the travel limits (needs -P)
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
Errors and warnings are described in the next section.

**Views** (`-v`, for plot files and the live preview):

| View | Plane | Seen from | Axes on the sheet |
|---|---|---|---|
| `g17` (default; also `xy`) | XY | above | X right, Y up |
| `g18` (also `xz`) | XZ | the front | X right, Z up |
| `g19` (also `yz`) | YZ | the right | Y right, Z up |
| `iso` | | front right, above | isometric |
| `all` | all three planes and iso on one sheet | | third-angle layout below |

```
G17 (top)      ISO
G18 (front)    G19 (right)
```

In `all` the four views share one scale: X lines up between G17 and G18, and Z between G18 and G19. Each
view is labelled and has a grey cross at part zero. A flat part (no Z movement) still gets G18 and G19
views of a minimum height. In the live preview the G17 view is the one paced at the feed rate, and the other
three are drawn at the same time.

Programs: one block per line, as on the control. The manual writes the end of block as `;`, which is also
the comment character, so `N10G1X1.F10.; feed` is fine.

## Program flow (`-flow`)

`DXSIM PROG.TXT -flow` shows how the program runs instead of every move:
- macro calls with their names and the values passed, and the returns;
- canned cycles;
- `=N` loops: start, end and number of passes;
- `=Ln` jumps;
- the lines a false condition skipped.

Lines are indented by macro and loop depth, and later loop passes are folded. `-flow+` puts the same
lines into the full trace, every pass included.

`-pc` colours the feed moves in plots and in the live preview by the macro, M-macro or canned cycle that
made them, with a key under the drawing. In the preview the status line shows the call stack
(`MAIN 21 > #1 5`), and in block-by-block mode O steps over a call.

`-map FILE` draws the program's call structure:
- **What it shows:** boxes for the main program, each macro, M-macro and canned cycle; arrows labelled with
  the calling lines and how often the run made each call. Calls never reached are dashed.
- **Format:** an SVG diagram if FILE ends in `.svg`, otherwise a text tree.
- **Warnings:** macros never called, calls to undefined macros, recursion, nesting deeper than 8, and
  L8/L9 used together with canned cycles.

Details are in `features/FLOW.md`.

```
L20     CALL  #1 "Hole pair" at line 5
L6      CALL    #2 "One hole" at line 9, values 0
L13     RET       back to the call at line 6
L23     JUMP  =L1: back to line 19
L22     SKIP  condition false: lines 22-24 skipped
L28     LOOP  =N30/2: blocks up to N30, 2 passes
```

## Machine parameters

`-P` reads a machine's own parameter file, so a program can be checked against that machine:

```
DXSIM PART.TXT -P BMDCPRMS.SYS                          size of the part against the travel
DXSIM PART.TXT -P BMDCPRMS.SYS -zero 10.,8.,-15.        travel limits, with part zero at that machine position
DXSIM PART.TXT -P BMDCPRMS.SYS -model "VMC-1000/22" -model "8K SPINDLE"
```

Only the parameters that change what a part program does on paper are read. Acceleration, servo, homing,
gear-shift and I/O parameters are ignored on purpose, because they are machine dynamics, not the language.

| Parameters | Used for |
|---|---|
| 17-22 | X, Y, Z travel limits in machine coordinates (2,540,000 units per inch). The trace shows them at the top |
| 7, 8 | rapid rate X/Y and Z (169.333 units/tick = 1 ipm). The live preview paces rapids with them |
| 73-76 | spindle maximum: the highest of the range maximums 74-76. `S` above it is a warning, as the control clamps it |

* **`-model`** applies a section of `BMDCPRMS.INI` (see `../BMDCPRMS_INI.md`) on top of the `.SYS` file, in
  the order given. The `.INI` is looked for next to the first `.SYS` file, or given with another `-P`.
* **`-zero X,Y,Z`** is where part zero is in machine coordinates (in inches, or mm with `-m`). Home is machine
  0,0,0, with the quill at the top, so the simulation then starts at home, as the real machine does. Give
  Z for the tool tip of the tool being used. Tool length offsets are not applied.
* **With `-zero`**, every move is checked as the control does it:
  * The end point of every block, including the blocks inside a canned cycle, is checked against params
    17-22. Over the limit, the run stops with the control's message, e.g.
    `Parser: Commanded X Axis Move Too Far Positive`, and a `^` under the axis word.
  * Z is checked only when param 98's machine-family nibble or param 62 is not 0; true on both shipped
    files. With the nibble 0, a move above the Z positive limit is refused only if it goes higher than Z
    already is. The trace header's `zcheck` line says which applies.
  * A block with G54-G59 is not checked, as on the control.
  * Z limit -2147483648 means no Z check.
  * An arc that bulges past a limit between its end points stops with the `dpCom:` form of the message,
    which is what the interpolator reports on the machine.
* **Without `-zero`**, the summary warns when the program's size on an axis is more than that axis's
  travel.

Parameter 156, the option word, was in the original plan. It turned out to hold only run-time status
(low air, low lube, index in/out), so it plays no part here. The PPPL option (param 285) and the peck
clearance (param 272) are not in `BMDCPRMS.SYS`; `-nopppl` and `-peck` still set them.

## Cutter compensation (G40/G41/G42)

G41 and G42 offset the tool-centre path by the radius of the active tool (`T__D__`, `T__/tlo/dia`, or
`-t T=D`). `dxsim` follows the 6.89 firmware step by step. The trace was taken from the code and is in
`features/CUTCOMP.md`, with the firmware addresses.

- **Turn-on:**
  - The G41/G42 block moves to its point **without** compensation.
  - The next block, the approach, ends one radius out on the normal to the first compensated element.
  - The approach and every compensated move are feed moves at the modal F. A G0 inside compensation is cut
    at the feed rate, as on the control.
- **Corners:** the control reads two blocks ahead.
  - **Inside corners** are cut back to where the two offset elements meet: line-line, line-arc or arc-arc.
  - **Outside corners** get a blend arc of the tool radius round the programmed corner with **G49**, the
    reset state.
  - With **G48**, outside corners are extended to a sharp corner, except arc → arc, which is always blended.
  - The control stops with its own messages: `Cutter-Comp: Negative Radius` (an inside arc smaller than
    the tool), `Cannot Pick Intersection`, `Dummy Radius Won't Fit`.
- **G45**, the reset state, scales the feed on arcs by tool-path radius / part radius. Blend arcs run at
  1.5 × F.
- **Z:**
  - A Z-only move is carried to the next element and made first, after the corner's blend.
  - There is room for one; a second Z-only move before an X/Y move is lost, with a warning.
- **Exit:**
  - G40 ends the last element one radius out on the normal at its end point.
  - The G40 block then moves to its point **without** compensation.
- **Mirror:** with exactly one axis mirrored (G31 or G32), G41 and G42 swap, so a mirrored part is still
  cut from the same side.
- **Output:**
  - The trace shows the tool-centre path, tagged `CDC`; blend arcs are tagged `CDC blend`.
  - Plots and the live preview also draw the programmed path as a thin green line.
  - `-zero` checks the travel limits on the tool-centre path.
- **Warnings:**
  - for the manual's rules (chapter 7);
  - when the path runs backwards, a **gouge** the control does not check for (figs. 7-13, 7-14).

## Fillets (G22/G23)

`G22 X__Y__R__` (CW) or `G23` (CCW) rounds the corner X Y with radius R. The arc is tangent to the line in
and the line out. The block after the last G22/G23 must be `G1 X__Y__`. `dxsim` uses the 6.89 firmware's
own arithmetic (`features/FILLET.md`), including what it leaves behind:
- **Variables:** the control computes the fillet in user variables and leaves them set: X1-X4, Y1-Y4, U1,
  U2, V1, V2, I1, J1, R2, R3, B1-B3, P2-P4. A program that keeps its own values there loses them at a fillet.
- **No checks:** a G22/G23 that doesn't match the turn, or a radius too big for the lines, makes the
  control loop round the outside or back up. dxsim draws it the same way and warns.
- **Cutter compensation:** fillet moves are compensated like any others.
- **A misprint in the manual:** fig. 3-7's program writes `F20`, which the control reads as variable F20.

## Errors and warnings

An **error** stops the run, as on the control. The message is the control's own (operator's manual
appendix D), with the line, the block and a `^` under the word in error. It goes to the trace and, when
the trace is in a file, to the console. On the preview it appears on the status line. The exit code is 2.

```
ERROR   line 3: Parser: Illegal G-Code
        N2G33X1.
          ^
```

An error inside a canned cycle (for example a G170 block without F) is reported against your line, with
the firmware's cycle block for reference:

```
ERROR   line 5, in its canned cycle (built-in block @26965: G91G1U1W1F3!): Parser: No Feedrate Specified
        N3G170X1.Y1.Z0X1.Y1.R.1Z.2P.1P.1
```

A **warning** does not stop the run. It marks something the control rejects or ignores, or a likely
mistake. Warnings are only given for your program's lines, never for the built-in cycle text:

| Warning | Why |
|---|---|
| `X1 reads variable X1, which was never set` | a word without a decimal point is a variable (manual ch.10); usually a missing point |
| `?P7 tests variable P7, which was never set`, `P3 is used in an expression but was never set` | the same, in a condition or an expression |
| `'=5.' is ignored: an assignment must start its block` | `N3X1=5.` is not an assignment on the control: the third character must be `=` |
| `Y1.23456 has more decimals than the input increment allows` | manual ch.2: finer than .0001 in (.001 mm) is an input error |
| `F400 is above the maximum feed` | the control clamps to 295 ipm / 7500 mm/min |
| `more than one M code in a block` | manual ch.9 |
| `S5000 is outside the range 1-4200` | manual table 2-2; with `-P`, the machine's spindle maximum (params 73-76) |
| `feed move with no M3/M4 programmed` | the control stops with SPINDLE NOT ON CANNOT CUT unless the operator started the spindle |
| E words | not simulated (see the end of this file) |
| fillets: wrong G22/G23 for the turn, radius too big, parallel lines, missing X/Y | the fillet arithmetic the firmware does without checks (see Fillets) |
| cutter compensation: co-linear moves, an M code, two Z-only moves, G92/G73/helix while it is on, a gouge, no G40 | the manual's rules, chapter 7 (see Cutter compensation) |

The summary at the end of the trace counts the warnings. When the trace goes to a file, the console also
says how many there were (`-silent` turns these console lines off).

`dxsim -lint PROGRAM.TXT` checks every line without running anything and lists **all** errors at once:
syntax of words, expressions and conditions, unknown G codes, label and tool numbers.

## Live preview on the screen (DOS and Windows)

The DOS builds (`wmake -f makefile.wat dos` or `dos32`) draw the tool path on a VGA screen while the program
runs, like the control's own **F5 PREVIEW** (operator's manual chapter 3). They use Open Watcom's
`graph.lib`. This works in plain DOS and full-screen in a Windows 98 DOS box.

On Windows, `wmake -f makefile.wat win32w` builds **DXSIMW.EXE**: the same preview (same keys, pacing and
status lines) in a 640×480 window, through `dxwin.c`, a small Win32 GDI version of the `graph.lib` calls the
preview uses. It runs from a command prompt like the other builds and uses only Windows 95-era calls.
Closing the window is the same as ESC; the picture is redrawn when the window is covered.

```
DXSIM -S  PROGRAM.TXT        draw as the program runs, a fixed pause after each move
DXSIM -Sf PROGRAM.TXT        draw at the programmed feed rate (rapids at 472 ipm): about machine time
```

| Key | Action |
|---|---|
| SPACE | pause / continue |
| B | block by block on/off: stops before each program block, macro bodies included, and shows it (a canned cycle runs as one block) |
| O | at a block-by-block stop: step over, i.e. run the macro this block calls without stopping in it |
| + / - | faster / slower: pause per move 0-800 ms, or feed-rate factor ×1/64…×64 |
| F | switch between per-move and feed-rate pacing |
| ESC | stop |

The status lines show the call stack (`MAIN 21 > #1 5`), the block, the position, the feed and the pacing. Rapids are drawn red and dashed,
feeds cyan, drilled holes as a small x. The view is chosen with `-v g17|g18|g19|iso|all` (see Running) and the whole part is fitted to
the screen (a quick first pass measures it). The trace goes to a file only with `-o`, so it does not
write over the graphics. The DOS timer ticks 18.2 times a second, so feed-rate pacing is approximate.

`make test-screen` (Linux) runs `dxscreen.c` with `graph.lib`, the keyboard and `delay()` replaced by a logger
(`-DDX_SCSTUB`). It checks that every test program stays inside the 640×448 drawing area, in the G17 view and in `-v all`, and reports the
paced time.

## Testing

The test runner `tests/dxtest.c` is C89 like the simulator, so the tests run wherever `dxsim` runs.

```
make test                                   Linux/macOS (also runs tools/check_grammar.py)

cd src                                      DOS / Windows 98 with Open Watcom
wmake -f makefile.wat dos fb tests          DXSIM.EXE + FB2DX.EXE + ..\tests\DXTEST.EXE (16-bit DOS)
wmake -f makefile.wat win32 fb32 tests32    the same for the Windows console
cd ..\tests
RUNTESTS                                    results on screen and in RESULTS.TXT
```

`DXTEST [-u] [path-to-dxsim]` must run in `tests/`. It starts `dxsim` with `system()` and only reads the
files it writes (`-o`), so it needs neither shell redirection nor exit codes, which COMMAND.COM does not
return. Test programs use 8.3 names and are listed in `tests/tests.lst`.

1. `dxsim -check` parses all 652 blocks of the built-in cycle text (628 non-empty). `dxsim -selftest` checks
   the cutter-compensation geometry against 31 results worked out by hand (`features/CUTCOMP.md`).
   (The tests run `dxsim` with `-silent`, so the expected errors don't clutter the console.)
2. Every program in `tests.lst` (a name, then optional `dxsim` options) must reproduce
   `expected/<name>.out`, warnings and error reports included; `e*.txt` are the error and warning tests. Numbers may differ by one unit in
   the last printed digit, because C libraries round halfway cases differently. After a reviewed change,
   `DXTEST -u` (or `RUNTESTS path\DXSIM.EXE -u`) rewrites the expected files; check the diff.
3. Results worked out by hand from the manual's figures: the five 3PL circles, the G81 example, the G191
   frame (18 holes), the G181 row, both bolt circles (6 holes at 30°…330° on R1.75), the nested-loop example
   (42 holes in 6 rows), G83 peck depths, macro parameters, mirror image, the three arc forms, and the
   expression evaluator.
4. The tests together execute 620 of the 628 non-empty built-in blocks. The rest are G178's empty body
   and the A33 helper, which no block calls.
5. `p*.txt` check the machine parameters with copies of the 6.89 and the TC22 `BMDCPRMS.SYS` (`P689.SYS`,
   `PTC22.SYS`) and `BMDCPRMS.INI`: each travel limit message, an arc past the limit, a canned cycle past
   the limit, the Z positive rule, the size check and a `-model` section.
6. `cc*.txt` are the cutter-compensation tests: the manual's figures 7-3, 7-13, 7-14 and 7-15, and the
   corner types, negative radius, diameter 0, mirror, Z-only moves and the warnings. The square, the
   triangle, the inside corners and the arc-arc corner are also checked against points worked out by hand.
7. `f*.txt` are the fillet tests: fig. 3-7, horizontal/vertical lines with the variable dump, the
   warnings, and fillets inside compensation; fig. 3-7's first fillet and the square's tangent points are
   checked against hand values.
8. `fb*.nc` are FANUC programs for `fb2dx`, listed in `tests/fb.lst` (expected file, program, options).
   They cover series detection, the `-fparam` stops, Macro A, vacant variables, `M98`, argument
   specification II and read errors. The bolt circle, the Macro A loop, specification II and `M98 P30100`
   are also checked against values worked out by hand. `DXTEST -fb path` names the `fb2dx` to use. Without
   one, these tests are skipped with a message.

## FANUC macro translator (`fb2dx`)

`fb2dx` translates FANUC custom macro programs (Macro A and Macro B, any series) into DX32 programs.
`features/FB2DX.md` has the design.
- **Built:** it reads the program, decides the FANUC series, evaluates the program, and writes it as DX32.
  That covers assignments, `IF..THEN`, `WHILE`/`DO`/`END`, `GOTO` and `IF..GOTO`, `G65`/`M98` calls as
  DX32 `#n` macros, G0/G1 moves, arcs, dwell, the drilling and boring cycles G81/G82/G83/G73/G85/G86/G89 in G90 or G91 with K/L repeats (written out as moves),
  tool changes, and G41/G42 cutter compensation (written so that FANUC's start-up move is the DX32 approach).
- **`-check`** runs dxsim on the written program (`-lint`, then the trace) and compares its moves with the FANUC
  tool path. `-against FILE` does the same for a DX32 program edited by hand.

```
fb2dx -series list                    the series, what exists on which, the -fparam settings
fb2dx -eval PROG.NC [SUB.NC ...]      decide the series and trace the program as the FANUC control runs it
fb2dx -parse PROG.NC                  list the statements as read
fb2dx PROG.NC -w PROG.TXT -check      write the DX32 program (only when nothing is refused) and check it in dxsim
```

The series is detected from what the program uses, or given with `-series a|0|6|10|15|16|0i|30i`. Where
the meaning of a block depends on the series or on a machine parameter, `fb2dx` stops and says which
option settles it (`-series`, `-fparam dpi=`, `nat=`, `is=`, `o8=`, `avar=`, `d83=`, `d73=`, or `-fparam default`
for the factory settings; `-g28 m25|drop` for `G91 G28 Z0`, `-fparam sup=` or `-comp dx32` for G41/G42). It never guesses.

## Not simulated (yet)

Spherical coordinates (E), feed per revolution
(recorded only), constant surface feed outside cutter compensation, tool length offsets and clear-point
moves (traced, not applied), feed timing. `dxsim` prints a warning the first time a program uses one of
them. `ROADMAP.md` has the plan for each.
