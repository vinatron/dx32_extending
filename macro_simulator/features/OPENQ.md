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
| A5 | What is 0xaf34 (the spindle minimum)? | A table of **8 per-range minimum speeds** at 0xaf34, next to the 8 range maximums at 0xaf10 (params 74-81). An S is placed in the first range whose [min, max] holds it; if none does, a machine message. What *writes* 0xaf34 is still open (P1) | F: 0xe8b8-0xe8e8, 0x34620 | no change |

## Open

### Language (`../LANGUAGE.md`)

| ID | Question | Why it matters | How to answer |
|---|---|---|---|
| L1 | Is `?Q7<NE>0W8=1.27!` (G87 body, 0x2757b) an assignment? The firmware's fixed-position test says no; the context says yes | G87's behaviour when Q7 ≠ 0 | **B:** `bench/BQ7.TXT`. At M0, W8 = 1.27 means an assignment, 0 means literal. dxsim predicts 0 |
| L2 | 6.89 limits against the 1994 manual: variable indices to 50, 36 functions, macro nesting 8 | dxsim follows 6.89 | **B:** `bench/BLIMITS.TXT` (X50 = 1.5, Q19 = 2, A1 = 1, A2 = 2.3013, A3 = 4 at M0, then an error at `Q20=1.`); `bench/BNEST.TXT` (an error when the 9th level is called; dxsim stops at `=#9` with `Parser: #1 Internal Addressing Error`) |
| L3 | What C9 does in the G81 driver (`G0U1V1C9`, 0x208aa stores a Z difference / 250000 in C9) | dxsim ignores C words; this matters only on machines with a 4th axis or if it shifts Z | **F:** trace 0x20880-0x208b0 (where fp0 comes from), then the C word handler's branch for 0x32e3 bit 7 (0x1f13a: it adjusts 0x3e3c/0x3f78 and 0xac72). That decides whether C9 moves the C axis or acts as a Z correction |
| L4 | The `=N` loop handler (0x1f732) and the load pass that registers `#n` | Loop nesting, shared end blocks; dxsim follows the manual | **F:** trace 0x1f732 (count stack, depth, end-block match). **B:** a 5-deep `=N` nest should fail at the 5th level if the limit is 4 |
| L5 | The `MRG` mask (0x764c) and the RDI/WRI channel map | `MRG` results; I/O in macros | **F:** 0x764c appears only in a data table at 0x265a2, next to the dual-port RAM address 0x01200554, so it is probably written by the PC through the mailbox: find the code that indexes that table, and the mailbox command (`../../PROTOCOL_NOTES.md`). **X:** BXX's writes to it. The channel map belongs to the G/M-code track (`../../IO_CTL.md`); ask through `../HANDOFF.md` |
| L6 | What BXX does to a program on load: blanks, CR/LF, `%` leaders | Whether `X1 = 5.` with blanks is an assignment | **B:** `bench/BBLANKS.TXT`. Look at the block in the editor, and X1 at M0: 5 = the blanks were removed (dxsim's default), 0 = kept (dxsim `-keep`). **X:** BXX's program loader |
| L7 | In `?Q1<LT>5`, is the 5 a number or variable Q5? (The variable rule with the left-hand letter) | Conditions written without a decimal point | **B:** `bench/BVARRULE.TXT`: Q9 = 0 at M0 means variable (dxsim), 1 means number |

### Machine parameters (`../LANGUAGE.md` 14a)

| ID | Question | How to answer |
|---|---|---|
| P1 | What writes the per-range spindle minimums at 0xaf34 (A5)? | **F:** trace `installParams` (mailbox 0x24 → 0x2b3b8, `../../MOTION_PLANNER.md`) and the boot copy at 0x1c174 for a loop over 0xaf34..0xaf50. A candidate: range maximum × param 72 / param 73 |
| P2 | Is the feed clamped at the manual's 295 ipm, or by params 7/8? | **F:** the velocity computation at 0x16df6 (`../../MOTION_PLANNER.md` "traverse-rate clamps"). **B:** F400 on a long G1 at 100% override: time it |
| P3 | Which line does the control show for a travel error inside a canned cycle? | **B:** `tests/PTRAVCYC.TXT` with the same `-zero` (part zero at machine X10 Y8 Z-15). dxsim reports the calling line |

### Cutter compensation (`CUTCOMP.md` section 7)

| ID | Question | How to answer |
|---|---|---|
| C1 | Non-uniform G73 scale with arcs | **F:** whether 0x1d6e4 takes the arc radius from the scaled centre/end (0x4720 computed from 0x4010/0x4058). **B:** G73 X1.5Y1. with an arc, then compare with the plot |
| C2 | M codes and Z-only pairs while compensating | **B:** `tests/CCWARN.TXT`: watch where the tool goes after the M8 and the two Z moves |
| C3 | An input that raises `Dummy Radius Won't Fit` | **F:** search for a path where C's offset start changes between 0x246e6 and 0x24cec. Low priority |
| C4 | Do the gouging paths match the control's graphics? | **B:** the bench sheet in `CUTCOMP.md` section 6 (`CCSTEP`, `CCNOTCH`) |
| C5 | Blocks without a move (F, S, T alone, an assignment) while compensating | **F:** whether the block loop calls the dispatcher 0x1d03e for such blocks (0x1c4d2 and the even states), and what 0x1d6e4 builds from an unchanged 0x4010 |
| C6 | G41/G42 again while compensating, and canned cycles while compensating | **F:** a second G41 sets state 2 in mid-compensation (0x20d0e): follow 0x1d0b0 with records held. Cycle blocks run through the same block loop, so they are probably compensated: trace one G81 block through 0x1c4d2 |
| C7 | Which line the control shows for a compensation error | **B:** `tests/CCNEG.TXT` (`Cutter-Comp: Negative Radius`): dxsim reports the arc's line |

### Fillets (`FILLET.md` section 7)

| ID | Question | How to answer |
|---|---|---|
| F1 | When does a Z word in a fillet block move? | **F:** what Z target (0x3f54) holds when 0x1dc50/0x1dd3e make the moves. **B:** `bench/BZMOVE.TXT` (dry run, Z clear): watch when Z goes to .5. dxsim moves it with the line into the corner |
| F2 | The 0x32fa state machine next to the fillet one ("AutoBlend", G9) | **F:** trace 0x1c580-0x1c602 and where 0x32fa is set to 1 (0x1ddee). If it is G9's automatic corner blending, it becomes a roadmap item |
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

**Preconditions:**
- The assignment programs need the **PPPL option** (param 285 bit 4). If the control stops with
  `PPPL option not enabled`, that is a result too.
- `BNEST.TXT` and `BBLANKS.TXT` don't need it to show their main result.

## Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-27 | (this commit) | Register created. A1-A5 answered from the firmware; A2 and A3 change dxsim (Z check per params 98/62, no travel check in a G54-G59 block). Bench programs `bench/*.TXT` for L1, L2, L6, L7, F1 |
