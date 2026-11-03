# Haas programs in fb2dx (`-series haas`)

Haas mill controls take FANUC-style G-code and a macro language close to FANUC Macro B, so fb2dx reads them
as one more "series" of its FANUC table (`FB2DX.md` 3.7) instead of a translator of their own. Everything
fb2dx does for FANUC (variables, calls, loops, arcs, drilling, `-check`) applies; this document lists what is
different, what is confirmed, and what is not known yet. The rule is the same as for FANUC: where a meaning is
not known, fb2dx stops and names the option that settles it. It never guesses.

## 1. Status

| Step | Status |
|---|---|
| Sources in `../manual/haas/` | the answers book of the *Haas Mill Programming Workbook* (2015): 9 programs, no macro chapter |
| Test corpus: the workbook's 9 programs (`tests/haas/hw*.nc`, `tests/haas.lst`) | **done**: 2 translate and pass `-check`, 2 more with an assumed setting (section 5) |
| `-series haas`, detected by `M97` (section 3) | **done** |
| `M97` local subprograms (section 4) | **done**, test `fbm97` |
| `G53` to machine zero (section 4) | **done**, test `fbg53x` |
| The Haas macro facts in section 6 | **open**: they need the *Mill Operator's Manual*, chapter "Macros" (`www.haascnc.com` is blocked in this environment; a PDF in `../manual/haas/` would do) |
| `G70`/`G71`/`G72` bolt patterns, `G12`/`G13` circular pockets | **not started**: their words (I J K L Q D) are explained in the workbook itself, which is not in `../manual/haas/` |

## 2. The corpus

`tests/haas/hw0010.nc`-`hw0081.nc` are the nine programs of the answers book, transcribed as printed
(comments in capitals, the book's `I 0.25` and `I0 .3` with their blanks), each with a comment naming the
source. The book allows reproduction for educational use. All dimensions are inches (the book's tools are
`1/2 DIA.` and so on), so `tests/haas.rsp` gives `-inch`; the programs have no G20.

`tests/haas.lst` runs each program with `@haas.rsp` (`-series haas -inch -g28 m25 -g28xy stay -tap float`) and
`-w ... -check`. Runs whose name ends in `a` add a setting fb2dx needs but Haas has not been checked for
(section 6), as an assumption, so the rest of the translation is exercised; their results are not a claim
about Haas. The list is part of `DXTEST` (`RUNTESTS`, `make test`); the names are 8.3, so it runs on 16-bit
DOS too.

## 3. The series

- **Key `haas`** in `fb2dx -series list`. Its column says `y` for Macro B (the workbook's controls take
  `G65`, locals `#1-#33`) and `?` for every other construct, so none is refused for Haas, and none is
  claimed either.
- **Detection:** a program is Haas only if it uses `M97` (a local subprogram call, which no FANUC series
  has) or is given `-series haas`. Otherwise the candidates are the FANUC series, as before, so no FANUC
  report changed.
- **`-fparam default` is FANUC's factory setting.** On Haas, `dpi=`, `nat=` and `sup=` must be given one
  by one where a program needs them; `-fparam default` alone stops with the reason.
- **The repeat count is `L`** (CONFIRMED by the workbook: `G13 ... L3`, `X2.75 Y-2.75 L0` "no hole here").
  `K` is refused on Haas until the manual is checked.
- **The report and the output** say Haas instead of FANUC (`reaches all 13 Haas end points`).

## 4. Built

- **`M97 P n`**: calls the blocks from `N n` to the next `M99` in the same program. fb2dx makes those
  blocks a program of their own (numbered 100000000 + n inside fb2dx, shown as `N n (M97)`), and the call an
  `M98` to it; from there it is a called program like any other, written as a DX32 `#n` macro. The `N n`
  block must come after the program's `M30`/`M2`, where Haas programs put local subprograms; one inside the
  main program's flow is refused. Locals: FANUC's `M98` shares the caller's locals; fb2dx does the same for
  `M97` (a forum quote of the Haas manual says neither nests the locals; TENTATIVE).
- **`G53` to machine zero**, `G53 G49 Z0. M05` at the end of every tool section of the workbook:
  - `Z0` is written like `G28 Z`: `-g28 m25` gives `M25`, the DX32 quill home (TENTATIVE: check that it goes
    where the Haas Z home is), `-g28 drop` leaves it out;
  - `X0`/`Y0` are left out with `-g28xy stay` (the DX32 has no X/Y home move), and refused without it;
  - a machine position other than 0 (`G53 Z-2.`), or one given by a variable, is refused: where it is in
    DX32 coordinates is not known;
  - only G0, G49 and G90 may share the block, and G91 must not be in force: G53 is a rapid to absolute
    machine coordinates, and what the control does with G91 or G1 there is not established (found in
    review, test `fbg53x`);
  - G49 is left out with the usual NOTE; M3-M9 in the block are written.
  This also applies to FANUC programs, where G53 means the same.
- **`P` with a decimal point** in G4 and the drilling cycles (`G82 ... P0.2` in the workbook) is refused on
  every series: FANUC takes P as whole milliseconds, and Haas is said to take a point as seconds (not
  confirmed). Before, fb2dx read `P0.2` as 0.2 ms.
- **`G28 G91 Z0.`** (the workbook's word order) and `G28 G91 Y0. Z0.` translate as `G91 G28 Z0` did
  (`FB2DX.md` 3.18). Haas describes G28 as FANUC does (to machine zero through the given point); TENTATIVE
  until the manual is checked.

## 5. Results (2026-11-03, with G84 from `FB2DX.md` 3.19)

| Program | What it is | Result |
|---|---|---|
| `hw0010` | interpolation: lines and R arcs | **translates**, `-check`: 13 end points |
| `hw0020` | circular pockets | refused: `G13` (circular pocket milling, Haas) |
| `hw0030` | cutter compensation | refused: the G41 start-up type on Haas; with `sup=0` (`hw0030a`) it translates, 13 end points |
| `hw0040` | cutter compensation | as `hw0030`; `hw0040a` translates, 16 end points |
| `hw0050` | G81, G83, G82 | refused: the G83 clearance (a Haas setting) and `G82 ... P0.2` (P with a point); `hw0050a` gives the clearance and still stops at P0.2 |
| `hw0060` | rows of holes, G81 G83 G84 | refused: the G83 clearance; with it (`hw0060a`) it translates, 387 end points, the taps as the DX32 G84 |
| `hw0070` | bolt patterns | refused: `G70`/`G71`/`G72`, the G83 clearance |
| `hw0080` + `hw0081` | profile, drilling, a pocket, `M98 P81` under G81, G83 and G84 | refused: the G41 start-up type, `G13`, the G83 clearance; with the first and the last assumed, only `G13` is left |

## 6. Not known yet (needs the Haas manual)

| Item | Why fb2dx needs it | Until then |
|---|---|---|
| H1 Numbers without a decimal point (`X2`): whole units or least increments? | every dimension word | `-fparam dpi=` must be given |
| H2 The ATAN range (and ASIN/ACOS) | macros with angles | `-fparam nat=` must be given |
| H3 The G41/G42 start-up (FANUC types A/B) | `G41 X0. D01` | `-fparam sup=` must be given |
| H4 The G83 clearance and G73 retract (settings) | peck drilling | `-fparam d83=`/`d73=` |
| H5 P in G4/G82: seconds with a point, ms without? | dwell | P with a point refused |
| H6 Haas variable numbers: modal information (`#4001`...), positions (`#5001`...), offsets (`#2001-#2800`), user ranges (`#600-#699`, `#800-#999` from a search) | macros that read the machine | system variables are refused, as on FANUC (`#4001`-`#4010` folded as on FANUC: TENTATIVE that the numbers match) |
| H7 Does `M97`/`M98` keep the caller's locals, and does `G65` nest them? | calls | as FANUC |
| H8 Which constructs of the FANUC table exist (`THEN`, `ASIN`, `LN`, `POW`, `AND`/`OR`, `#_NAME`) | the series column | all `?`: accepted |
| H9 `G70`/`G71`/`G72` and `G12`/`G13`: the meaning of I J K L Q D | the workbook's patterns and pockets | refused |
| H10 `G28` without an axis word (Haas: all axes home) and G28's intermediate point | end of program | refused / as FANUC |

## 7. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-11-03 | (this commit) | Review: two `M97` local subprograms in one program (the second was not found), the same `M97` target called twice (its program was inserted twice); test `fbm97bw` |
| 2026-11-03 | b57f24f | G84 and hole subprograms under several cycles (`FB2DX.md` 3.19): `hw0060a` translates; `-tap float` in `haas.rsp` |
| 2026-11-03 | 0f27338 | G53: G91 and G1 refused (in the block or G91 in force), a variable axis value refused with its own message (Copilot review) |
| 2026-11-03 | 6427caf | Document created. `-series haas` (detected by `M97`), `M97` local subprograms, `G53` to machine zero, P with a point refused; the workbook's 9 programs as `tests/haas.lst`; tests `fbm97w`, `fbm97x`, `fbg53x` |
