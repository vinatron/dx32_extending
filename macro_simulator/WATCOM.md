# Testing the Open Watcom builds

dxsim and fb2dx are tested here with gcc and clang on Linux (`make test`: 252 passed; `make test-screen`: 157). Earlier versions
were also compiled here with Open Watcom 1.9, and built by the owner on Windows 98 SE. Everything since
then has not been through Watcom: fb2dx (including its 16-bit DOS changes) and the firmware fixes of
2026-10-28/29. The Watcom tools could not be run in this session. This is the procedure for building and
testing on a DOS or Windows machine, and what to report. It takes about 15 minutes per target.

## Quick start: one batch file

From `macro_simulator`, with the Watcom environment set (`SETVARS.BAT` or `OWSETENV.BAT`):

```
BUILDTST WIN        Windows 95/98/NT: builds DXSIM32, DXSIMW, FB2DX, DXTEST, then runs all the tests
BUILDTST DOS        16-bit DOS: builds DXSIM (with the VGA preview), FB2DX, DXTEST, then the tests
```

It builds (compiler output in `BUILD.TXT`), stops with a message if the build fails, then runs
`tests\RUNTESTS.BAT` and `tests\PREVIEW.BAT` and ends with their totals:

| | Windows | 16-bit DOS |
|---|---|---|
| `RUNTESTS` (`tests\RESULTS.TXT`): traces, plots, fb2dx, the FANUC corpus | `252 passed, 0 failed` | `211 passed, 0 failed`, and a `skip` line for the corpus (long file names) |
| `PREVIEW` (`tests\PREVIEW.TXT`): the preview's drawing and its keys | `157 passed, 0 failed` | `157 passed, 0 failed` |

It takes about 7 minutes on Windows; leave the mouse and keyboard alone while windows flash past.
Afterwards open `tests\SCREENS.HTM` and scroll through the pictures. If something fails, sections 4 and
6 say what to look at and what to send. The sections below describe each step on its own.

`BUILDTST.BAT` was run here under Wine with MinGW builds standing in for Watcom (success, a failed
build, and no argument), not with Watcom itself.

## 1. What you need

- **Open Watcom 1.9 or 2.0**, installed with its `SETVARS.BAT` (or `OWSETENV.BAT`) run, so that `wmake`,
  `wcl`, `wcl386` and the `WATCOM`, `PATH` and `INCLUDE` variables are set. Type `wcl` alone to check: it
  prints its version.
- **One of these to run on:**
  - plain DOS, or a Windows 95/98 DOS box: for the 16-bit and DOS/4GW builds;
  - a Windows 95/98/XP or later command prompt: for the Win32 builds;
  - DOSBox also works for the 16-bit build. Mount the folder as `C:` and run the Watcom DOS-hosted tools
    inside it.
- **The `macro_simulator` folder** with its subfolders `src`, `tests` and `bench`, copied as they are.
  Every file the build and the tests use has an 8.3 name. The `.md` files are not needed on the target.
  LF or CRLF line ends both work.

## 2. Build

Run everything from `macro_simulator\src`. Pick **one** platform per run: the DOS and the Windows targets
write files with the same names (`FB2DX.EXE`, `..\tests\DXTEST.EXE`).

| Platform | Command | Writes |
|---|---|---|
| 16-bit DOS | `wmake -f makefile.wat dos fb tests` | `DXSIM.EXE`, `FB2DX.EXE`, `..\tests\DXTEST.EXE` |
| Windows console | `wmake -f makefile.wat win32 fb32 tests32` | `DXSIM32.EXE`, `FB2DX.EXE`, `..\tests\DXTEST.EXE` |
| DOS/4GW (optional) | `wmake -f makefile.wat dos32` | `DXSIM4G.EXE` (tested with the 16-bit `DXTEST` and `FB2DX`) |
| Windows preview (optional) | `wmake -f makefile.wat win32w` | `DXSIMW.EXE` (checked by hand, section 5) |

**What to expect:** no warnings and no errors. The flags include `-za` (strict ANSI) and `-wx` (all
warnings), so anything the compiler prints is worth reporting.

**If it stops:** each `wcl` writes `NAME.ERR` next to the source with the messages for that file. Keep
the `.ERR` files and the screen text. In COMMAND.COM, `wmake ... > BUILD.TXT` captures the screen text.

Before building a different platform, run `wmake -f makefile.wat clean` (it needs `rm`; otherwise delete
`*.OBJ` and `*.EXE` in `src` by hand).

## 3. Run the tests

```
cd ..\tests
RUNTESTS                       16-bit DOS: uses ..\src\DXSIM.EXE
RUNTESTS ..\src\DXSIM32.EXE    Windows console
RUNTESTS ..\src\DXSIM4G.EXE    DOS/4GW (DOS4GW.EXE from Watcom's BINW folder on the PATH)
```

`RUNTESTS` shows the results and also writes them to `RESULTS.TXT`. Each line is `ok`, `FAIL` or
`skip`. The last line counts them.

- **Expected: `252 passed, 0 failed`.** That includes 16 plot files (SVG, HPGL, PostScript, listed in
  `PLOTS.LST`), compared with `EXPECTED\` number by number, and 41 fb2dx runs on the FANUC macro
  corpus (`CORPUS.LST`, `../fanuc_macros`). On 16-bit DOS those 41 are skipped (they have long file
  names), so expect `211 passed` there, with a `skip` line.
- `skip  the fb2dx tests: ..\src\FB2DX.EXE not found` with `125 passed` (and no corpus runs) means `FB2DX.EXE` was not built.
  Build it (`fb` or `fb32`) and run again.
- A number that differs only in its last printed digit is accepted automatically, because C libraries round
  halfway cases differently. Any other difference is a `FAIL`.
- **Do not run `RUNTESTS ... -u`.** It rewrites the expected files and would hide the differences.

**What the run does:** `DXTEST` runs `DXSIM` and `FB2DX` with `system()` once per test, and reads only
the files they write. So it needs no redirection and no exit codes. On 16-bit DOS, the `fb2dx ... -check`
tests stack three programs: `DXTEST` runs `FB2DX`, which runs `DXSIM`, each through `COMMAND.COM`. That
needs a lot of conventional memory.
- Check with `MEM` first: aim for 600 KB free (load DOS high, and remove drivers you don't need for the
  test).
- If those tests fail with `-check: dxsim wrote no trace`, or `does not read cleanly ... (no output)`,
  memory is the likely cause. Note the `MEM` figure in the report.

## 4. If a test fails

The `FAIL` line names the test and the first line that differs (`line N: got "..."`). To see the whole
output, run the same command by hand in `tests`:

- **A dxsim test** (`trace of NAME.txt ... matches expected/NAME.out`): the options are on NAME's line in
  `TESTS.LST`.
  ```
  ..\src\DXSIM.EXE NAME.TXT <options> -silent -o NAME.GOT
  FC NAME.GOT EXPECTED\NAME.OUT
  ```
- **An fb2dx test** (`fb2dx PROG.nc ... matches expected/NAME.out`): the line in `FB.LST` gives the expected file, the program and
  the options.
  ```
  ..\src\FB2DX.EXE PROG.NC <options> -silent -o PROG.GOT
  FC PROG.GOT EXPECTED\NAME.OUT
  ```
  When the options contain `-w FILE.TXT`, compare `FILE.TXT` with `EXPECTED\FILE.OUT` as well.
- **A plot test** (`plot PROG.txt <options> -p .ext matches expected/NAME.ext`): the line says where
  the first difference is (`line N: got 12.346, expected 12.344`). To keep the file, run it with the
  options from its line in `PLOTS.LST` and a new name with the same extension (the extension picks
  the plot type), and send that file:
  ```
  ..\src\DXSIM.EXE PROG.TXT <options> -p GOT.SVG
  ```
- **A preview test** (`preview NAME VIEW moves ... ; expected: ...`): send the line and the picture,
  `SCR17\NAME.BMP` or `SCRALL\NAME.BMP`.
- **A hand-checked result** (for example `manual G81 example`, `fig 7-15 square`, `fb2dx G41 in dxsim`):
  the line says what was expected. Send it as it is.

## 5. The live preview and its keys: PREVIEW.BAT

With a preview build (`DXSIMW.EXE` from `win32w`, or the 16-bit `DXSIM.EXE` from `dos`) and its
`DXTEST` (`tests32` for Windows, `tests` for DOS):

```
cd ..\tests
PREVIEW                        uses ..\src\DXSIMW.EXE, else ..\src\DXSIM.EXE
PREVIEW ..\src\DXSIM4G.EXE     or name one
```

**Expected: `157 passed, 0 failed`.** Results are on the screen and in `PREVIEW.TXT`. Nothing needs
typing; on Windows about 160 windows flash past, so leave the mouse and keyboard alone until it ends.

**1. The drawing** (146 checks). Every test program runs through the preview twice, in the G17 view and
in `-v all`, with `dxsim -Sa`: it draws as usual, but nothing waits for the clock or a key, and the window
or screen closes by itself. `DXTEST` compares what was drawn with `EXPECTED\SCREEN.OUT`: the moves, the
line end points, any point off the drawing area, and the paced time the run would have taken with `-Sf`.
- Every finished picture is saved as a BMP in `SCR17\` and `SCRALL\` (about 150 KB each, 22 MB in
  all). **Open `SCREENS.HTM`** in a browser (Internet Explorer on Windows 98 shows BMPs): one row per
  program, both views side by side. Scroll through it and note anything that looks wrong: a part not
  centred, a missing path, wrong colours (rapids red dashed, feeds cyan, holes small x's, the
  programmed path green under cutter compensation), text out of place.
- On a slow DOS machine reading back the screen for each BMP takes a few seconds, so the run takes a
  while.

**2. The keys and the window** (11 checks, `UI.LST`). The preview runs with scripted key presses
(`dxsim -Sk KEYS`), and the trace records every key and every change of the two status lines. It must
match `EXPECTED\UI*.OUT`:

| Test | Keys | What it checks |
|---|---|---|
| `uiblock` | B, then a key at each stop | block by block: one stop per program block, the block shown |
| `uiover` | B ... O at the macro call | step over: the macro runs without stopping in it |
| `uiinto` | B ... I at the macro call | step into: stops inside the macro |
| `uipause` | SPACE, SPACE | pause and continue, `PAUSED` on the status line |
| `uispeed` | + + + - F + - - | the per-move speed and the feed-rate factor on the status line, F switching pacing |
| `uiesc` | ESC | stops, `STOPPED` |
| `uiclose` | B ... close the window | closing the window acts like ESC |
| `uirepair` | (cover) | covers the window with another one, takes it away, and compares the repainted window with the picture: `0 pixels differ` |
| `uiend`, `uierr` | a key at the end | the end and an error on the status line |
| `uipc` | B ... | block by block with `-pc` colours |

In `DXSIMW` the scripted keys go through the window's own message queue (`WM_CHAR`, `WM_CLOSE`), so its
key handling is tested, not bypassed. On DOS they come from the script instead of the keyboard, so the
keyboard itself is the one thing to try by hand there, once: `..\src\DXSIM -S CCSQ.TXT -t 1=.5`, then
SPACE, B, + and ESC.

To try a script yourself: `..\src\DXSIMW CBOLT.TXT -Sk /Bnnnn -o OUT.TXT`, with keys after B pressed
at each stop; `/K` is a key typed ahead while it runs, `s` SPACE, `e` ESC, `w` close the window, `r` the
repaint check. One picture: `..\src\DXSIMW -Sa -v all CBOLT.TXT -bmp CBOLT.BMP`.

**Checked here without Watcom:** MinGW builds of the same Windows programs pass `RUNTESTS.BAT` (252)
and `PREVIEW.BAT` (157) under Wine, the preview on a virtual display (`make test-wine`).

## 6. What to send back

Post a comment on PR #5 with:
1. the Watcom version (`wcl` prints it), the platform (DOS version, Windows version, or DOSBox), and the
   targets built;
2. `RESULTS.TXT` and `PREVIEW.TXT`, or at least their `FAIL` lines and last lines;
3. for each `FAIL`, the `.GOT` file from section 4, or its first 20 differing lines from `FC`;
4. any build output: the screen text and the `.ERR` files;
5. on DOS, what `MEM` reported;
6. one line on `SCREENS.HTM`: right or wrong, and what looked wrong.

Everything in sections 3-5 runs in the simulator only and moves nothing on the machine. The bench programs
in `bench\` are a separate job, for the control itself (`features/OPENQ.md`).
