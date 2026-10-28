# Open questions: register and how to answer them

Every open question about the control that dxsim depends on, in one place, with how to settle it. The
feature documents (`CUTCOMP.md`, `FILLET.md`) and `../LANGUAGE.md` section 15 point here. When an item is
answered, its row moves to "Answered", and the answer goes into the document it belongs to, and into the
code if it changes behaviour.

**How an item is answered:**
- **F (firmware trace):** done by Claude in `../../BMDC.BIN` with `../tools/m68k_dis.py` and
  `../../decompiled/BMDC.asm`. Each item names the code to start from.
- **B (bench test):** run by the owner on the machine. The programs are in `../bench/`. They are short, need
  no part on the table, and say what to look for. Run them on **6.89**, or note the firmware version with the
  result: the TC22 is on 6.73 until the upgrade (`../../VERSIONS.md`).
- **X (DOS-side trace):** in `BXX.EXE` (`../../decompiled/BXX.c`).

## Answered in this round (2026-10-27)

| ID | Question | Answer | Source | dxsim |
|---|---|---|---|---|
| A1 | Which of 0x32ec bits 3/4 is the X mirror? | G31 sets bit 3 (X, and stores 2 × X at 0x3d58); G32 sets bit 4 (Y, 0x3d64); G30 clears both | F: 0x20c48-0x20c84 | no change (only the XOR matters for G41/G42) |
| A2 | When are Z (and the 4th axis and S) travel-checked, and when does the "may stay above the + limit" rule apply? | Only when the **top nibble of param 98** (0xaf58, the machine family) ≠ 0 or **param 62** (Z velocity gain, 0x2988) ≠ 0. The stay-above rule applies only when the nibble is 0. Both `BMDCPRMS.SYS` files have param 98 = 0x21221220 (nibble 2), so Z is always checked and the rule never applies | F: 0x23260, `../../BMDC_PARAM_TABLE.md` 62, 98 | **changed**: read from `-P`, shown in the trace header (`zcheck`); tests `pzrule`, `pzon` |
| A3 | What sets 0x4089 bit 3, which skips the travel check? | **G54-G59** (0x20dc0, which also stores the shift number at 0x41d6). The bit is cleared at each block start, so a block containing G54-G59 is not travel-checked | F: 0x20dc0, G table entries 54-59 | **changed**: test `pg54` |
| A4 | `Parser: Cannot Comp "C" Axis` | a C word during a fillet sequence (state 0x32f5) | F: 0x1f1f6 | recorded in `CUTCOMP.md`, `FILLET.md` |
| A5 | What is 0xaf34 (the spindle minimum)? | A table of **8 per-range minimum speeds** at 0xaf34, next to the 8 range maximums at 0xaf10 (params 74-81). An S is placed in the first range whose [min, max] holds it; if none does, a machine message. The minimums are computed (A9) | F: 0xe8b8-0xe8e8, 0x34620 | no change |
| A6 | The `=N` loop handler: depth, end blocks (L4) | Five levels, not the manual's four: the push (0x20468) traps at the sixth with `Parser: #14 Internal Addressing Error` (0x204a6). Each block with an N counts a pass of the innermost loop ending there, and drops finished loops so the next one out is checked against the same block (0x1cc5a): end blocks can be shared | F: 0x20468, 0x204a6, 0x1cc5a; the message table at 0x32f63 | **changed**: 5 levels and #14 (was 4 and #8); tests `eloop5`, `eloop6`; `bench/BLOOP.TXT` confirms it on the machine |
| A7 | What C9 does in the G81 driver, `G0U1V1C9` (L3) | 0x20882-0x208ae: C9 (0x4e08) = (4th-axis position 0x40d6 − 0xacae) / 250000, or 0 when 0xaef8 bit 6 is set (meaning of that bit not traced). The C word handler (0x1f0f6) multiplies by 250000 and makes it the 4th-axis target (0xac72). So C9 returns the C axis to where it was relative to 0xacae: it never moves X, Y or Z and is not a Z correction | F: 0x20878-0x208ae, 0x1f0f6 | no change (dxsim ignores C words, which is right for a 3-axis machine) |
| A8 | The `MRG` mask (0x764c) and the RDI/WRI channel map (L5) | RDI GET WRI PUT(n) read and write entry n of a table of 151 firmware variables at 0x263a2 (types: 1 writable, 0 read-only); there are no I/O channels as such. Entries 0-3 are the GPAUF1/GPAUF2 (user AUF) input words, 10-13 their output words, 64 is the `MRG` mask (0 at power-up, so `MRG(a,b)` = b until `WRI(64,m)`). Full map in `../LANGUAGE.md` section 4 | F: 0x253f2 (function table), 0x255a8, 0x2566a, 0x25724, 0x259b8, 0x25b5c, 0x30b8e | **changed**: MRG uses the mask (was a or b); writes to read-only entries are ignored; RDI/GET read back what WRI/PUT wrote; n rounds and clamps to 150; test `fuio` |
| A9 | What writes the per-range spindle minimums at 0xaf34 (P1)? | 0x364ea, called by mailbox 0x24 `installParams` (0x2b3c6) and 0x02 `doDrives` (0x282a6): minimum[i] = maximum[i] × param 72 / param 73, rounded, for all 8 ranges (0xaf10-0xaf2c → 0xaf34-0xaf50). Only ranges 1-3 have parameters (74-76); 4-8 stay 0. The S check at 0x34620 raises S below range 1's minimum with `Spindle Speed Too Low.` and lowers S above range 2's maximum with `Spindle Speed Too High.` Both shipped `BMDCPRMS.SYS` files have param 72 = 0, so every minimum is 0 there | F: 0x364ea-0x3659a, 0x34620-0x3467c | **changed**: with `-P`, an S below the range 1 minimum is warned about; test `pslow` |

## Open

### Language (`../LANGUAGE.md`)

| ID | Question | Why it matters | How to answer |
|---|---|---|---|
| L1 | Is `?Q7<NE>0W8=1.27!` (G87 body, 0x2757b) an assignment? The firmware's fixed-position test says no; the context says yes | G87's behaviour when Q7 ≠ 0 | **B:** `bench/BQ7.TXT`. At M0, W8 = 1.27 means an assignment, 0 means literal. dxsim predicts 0 |
| L2 | 6.89 limits against the 1994 manual: variable indices to 50, 36 functions, macro nesting 8 | dxsim follows 6.89 | **B:** `bench/BLIMITS.TXT` (X50 = 1.5, Q19 = 2, A1 = 1, A2 = 2.3013, A3 = 4 at M0, then an error at `Q20=1.`); `bench/BNEST.TXT` (an error when the 9th level is called; dxsim stops at `=#9` with `Parser: #1 Internal Addressing Error`) |
| L6 | What BXX does to a program on load: blanks, CR/LF, `%` leaders | Whether `X1 = 5.` with blanks is an assignment | **B:** `bench/BBLANKS.TXT`. Look at the block in the editor, and X1 at M0: 5 = the blanks were removed (dxsim's default), 0 = kept (dxsim `-keep`). **X:** BXX's program loader |
| L7 | In `?Q1<LT>5`, is the 5 a number or variable Q5? (The variable rule with the left-hand letter) | Conditions written without a decimal point | **B:** `bench/BVARRULE.TXT`: Q9 = 0 at M0 means variable (dxsim), 1 means number |
| L8 | Do the 32-bit functions (`LSR NOT AND`...) round their arguments or truncate them? The firmware converts with `fmove.l` and never sets FPCR outside library routines, so round to nearest, ties to even | `FIX` for the FANUC translator (`FB2DX.md`); bit tests on non-integer values | **B:** `bench/BFIX.TXT`. At M0: A1=2 A2=4 A3=3 A4=2 A5=1 means rounding (dxsim), A2=3 A3=2 A5=0 means truncation |
| L9 | Does a bit written with `WRI(11,v)` (0x31f4, GPAUF1 outputs 32-63, param 280) reach the board, or does the next servo tick's I/O mapping overwrite it? | Machine I/O from macros: this belongs to the G/M-code track (`../../IO_CTL.md`), and dxsim only traces the write | **B:** with the spindle and coolant off, `A1=RDI(11)` then `WRI(11,SET(n,A1))` for a spare output bit n, M0, and watch MAINTPG. **F:** whether the per-tick mapping (`../../PROTOCOL_NOTES.md` "AUF Output Bits") rebuilds 0x31f4 or only sets and clears its own bits |

### Machine parameters (`../LANGUAGE.md` 14a)

| ID | Question | How to answer |
|---|---|---|
| P2 | Is the feed clamped at the manual's 295 ipm, or by params 7/8? | **F:** the velocity computation at 0x16df6 (`../../MOTION_PLANNER.md` "traverse-rate clamps"). **B:** F400 on a long G1 at 100% override: time it |
| P3 | Which line does the control show for a travel error inside a canned cycle? | **B:** `tests/PTRAVCYC.TXT` with the same `-zero` (part zero at machine X10 Y8 Z-15). dxsim reports the calling line |

### Cutter compensation (`CUTCOMP.md` section 7)

| ID | Question | How to answer |
|---|---|---|
| C1 | Non-uniform G73 scale with arcs | **F:** whether 0x1d6e4 takes the arc radius from the scaled centre/end (0x4720 computed from 0x4010/0x4058). **B:** G73 X1.5Y1. with an arc, then compare with the plot |
| C2 | M codes and Z-only pairs while compensating | **B:** `tests/CCWARN.TXT`: watch where the tool goes after the M8 and the two Z moves |
| C3 | An input that raises `Dummy Radius Won't Fit` | **F:** search for a path where C's offset start changes between 0x246e6 and 0x24cec. Low priority |
| C4 | Do the gouging paths match the control's graphics? | **B:** the bench sheet in `CUTCOMP.md` section 6 (`CCSTEP`, `CCNOTCH`) |
| C5 | Blocks without a move (F, S, T alone, an assignment) while compensating | **F:** each block the look-ahead takes goes 0x224b4 (finalizer) → `bra 0x1d03e` (0x1c50c-0x1c526). What is left is whether the parse step (0x1e9b2, 0x1ec7e) hands over blocks without a move, and what 0x1d6e4 builds from an unchanged 0x4010 |
| C6 | G41/G42 again while compensating, and canned cycles while compensating | **F:** a second G41 sets state 2 in mid-compensation (0x20d0e): follow 0x1d0b0 with records held. Cycle blocks run through the same block loop, so they are probably compensated: trace one G81 block through 0x1c4d2 |
| C7 | Which line the control shows for a compensation error | **B:** `tests/CCNEG.TXT` (`Cutter-Comp: Negative Radius`): dxsim reports the arc's line |

### Fillets (`FILLET.md` section 7)

| ID | Question | How to answer |
|---|---|---|
| F1 | When does a Z word in a fillet block move? | **F:** what Z target (0x3f54) holds when 0x1dc50/0x1dd3e make the moves. **B:** `bench/BZMOVE.TXT` (dry run, Z clear): watch when Z goes to .5. dxsim moves it with the line into the corner |
| F2 | The 0x32fa state machine next to the fillet one ("AutoBlend", G9) | **Mostly answered:** it is G9 precision cornering's corner blending (`../../MOTION_PLANNER.md` "What P controls"). With a valid `G9 P`, corners up to 96° get inserted sub-segments, deviating about 0.004-0.01 in; a program starts with no valid tolerance, so nothing is blended until `G9 P`. The fillet helpers (0x1dc80, 0x1dd3e) make the moves. dxsim does not draw these blends: **roadmap candidate, low priority**. Still to trace (**F**): how 0x1e706's classes become the 0x32fa states (0x1ddca sets state 1) |
| F3 | Does the control really leave the fillet values in X3/Y3/I1...? | **B:** the bench sheet in `FILLET.md` section 6, item 2 |

## Bench session (for the owner)

The programs are in `macro_simulator/bench/` (CR/LF, 8.3 names). For each one:
- load it;
- dry-run it with Z clear, or run it with no part on the table;
- write down what the comment at the top asks for, and any message with its line.

The cutter-compensation and fillet sheets (`CUTCOMP.md` and `FILLET.md`, section 6) use programs from
`tests/`.

| Program | Answers | dxsim predicts | Result |
|---|---|---|---|
| `BQ7.TXT` | L1 | W8 = 0 | |
| `BVARRULE.TXT` | L7 | Q9 = 0 | |
| `BLIMITS.TXT` | L2 | the values at M0 as listed in the file, then an error at `Q20=1.` | |
| `BNEST.TXT` | L2 | error when `=#9` calls the 9th level | |
| `BBLANKS.TXT` | L6 | X1 = 5 (blanks removed on load) | |
| `BZMOVE.TXT` | F1 | Z goes to .5 with the line into the corner | |
| `BLOOP.TXT` | A6 (confirms) | an error at the 6th `=N` line, #14 | |
| `BFIX.TXT` | L8 | A1=2 A2=4 A3=3 A4=2 A5=1 | |

**Preconditions:**
- The assignment programs need the **PPPL option** (param 285 bit 4). If the control stops with
  `PPPL option not enabled`, that is a result too.
- `BNEST.TXT` and `BBLANKS.TXT` don't need it to show their main result.

## Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-27 | (this commit) | L8 added: the 32-bit functions round (fmove.l, default FPCR); dxsim changed from truncation; `bench/BFIX.TXT` |
| 2026-10-27 | 4dfddc0 | F2 identified as G9 corner blending; C5 narrowed to the parse step |
| 2026-10-27 | 226fc92 | Register created. A1-A5 answered from the firmware; A2 and A3 change dxsim (Z check per params 98/62, no travel check in a G54-G59 block). Bench programs `bench/*.TXT` for L1, L2, L6, L7, F1 |
