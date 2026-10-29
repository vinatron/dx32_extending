# Fillets (G22 / G23)

Roadmap item 3. Sources:
- **M:** programmer's manual p.18, "G22, G23 — Fillet programming", with fig. 3-7;
- **F:** the 6.89 firmware (`../../BMDC.BIN`), with `../tools/m68k_dis.py`.

## 1. Status

| Phase | What | Status |
|---|---|---|
| 0 | Manual, firmware map, plan | **done** |
| 1 | Firmware trace: G-code handlers, state machine 0x32f5, the fillet arithmetic | **done**: section 2 |
| 2 | Simulation in `src/dxmotion.c` (`fillet()`), after the firmware formula by formula | **done** |
| 3 | Tests and hand checks, documentation | **done** |
| 4 | Bench check on the machine | **waiting for the owner**: bench sheet in section 6 |

Fillets are simulated. Everything below comes from the code and the manual; nothing has been run on the
machine yet.

## 2. What the control does

### 2.1 Manual (p.18)

- **Format:** `G22 (G23) X__Y__R__F__`.
  - X/Y is the **corner**: the point where the two lines would meet.
  - R is the fillet radius.
  - G22 makes a clockwise fillet, G23 a counter-clockwise one.
- **The arc** is blended tangent to the line into the corner and the line out of it.
- **Successive fillets** may follow one another. The block after the last G22/G23 must be a **G1 move with X
  and Y**.
- **Group 0** (M table 3-1): G22/G23 apply to their own block only.
- **A misprint in fig. 3-7's own program:** it writes `F20` without a decimal point. The control reads that
  as **variable F20** (0), so the example as printed would stop with `Parser: No Feedrate Specified`. dxsim
  says so (the variable-rule warning). The test uses `F20.`.

### 2.2 Firmware (CONFIRMED from the code unless marked)

| What | Where |
|---|---|
| **G22** (0x20bf6), **G23** (0x20c18): with the fillet state 0x32f5 = 0, set the direction (0x32e2 bit 3: clear for G22 = CW, set for G23) and the state to 1. If a fillet is already pending, set the **next** fillet's direction (bit 4) and "another fillet follows" (0x32e6 bit 1) | G table 0x205e2 entries 22, 23 |
| **X/Y words in state 1** (X 0x216e0, Y 0x219ca): X1 = X, U1 = X − the position before the block (Y1, V1 the same). The position register 0x3f0c is moved to the corner, but **nothing moves** | 0x21696 |
| **X/Y words in state 2** (the next block): X2 = X, U2 = X2 − X1 (Y2, V2 the same) | 0x216ba |
| **R word:** R2 in state 1, R3 in state 2 (the next fillet's radius) | 0x1f900 |
| **End of the G22/G23 block:** the offset of line 1 (0x1d89e); state 2; G99 for the block | 0x1c53e, 0x1cf48 |
| **End of the next block:** line 2's offset and the fillet (0x1d946). The **line to the first tangent point** is made (G1, the modal feed); state 3 | 0x1cf24 |
| **State 3** (0x1c64c, before the next block): the **arc** to the second tangent point, CW or CCW (0x1dc80). If another fillet follows, the values shift: X1←X2, Y1←Y2, U1←U2, V1←V2, R2←R3, direction ← next direction (0x1dce2). Line 1's offset is recomputed, and the state goes back to 2. Otherwise state 4 | 0x1c64c |
| **State 4:** the **last line**, to X2/Y2 (0x1dd3e); state 0 | 0x1c606 |
| **Feeds into cutter compensation:** fillet moves go on to the compensation state machine (`jsr 0x224b4; bra 0x1d03e`) | 0x1c55a, 0x1c62a |
| **`Parser: Cannot Comp "C" Axis`** is raised by a C word while 0x32f5 ≠ 0, i.e. **a C word inside a fillet sequence** | 0x1f1f6 |

**The arithmetic (0x1d89e, 0x1d946).** Each line is taken through the corner as `y = m·x` (relative to
the corner) and moved by the radius to `y = m·x + c`:
- **c** starts as R2. It becomes `−|R2|` when U < 0, or when U = 0 and V ≥ 0. It is negated for a CW fillet
  (G22). Then, if U ≠ 0: m = V/U, and if V ≠ 0 as well, c = c·√(m²+1).
  - The result puts the centre on the inside of the turn **when G22/G23 matches the turn**.
  - A vertical line (U = 0) is `x = c`.
- **The centre (I1, J1)**, relative to the corner:
  - line 1 vertical: I1 = c1, J1 = m2·I1 + c2;
  - line 2 vertical: I1 = c2, J1 = m1·I1 + c1;
  - otherwise: I1 = (c2 − c1)/(m1 − m2), J1 = m1·I1 + c1.
- **Tangent points:** the feet of the perpendiculars from the centre.
  - On a horizontal line (V = 0): (I1, 0).
  - On a vertical line: (0, J1).
  - Otherwise: x = (I1/m + J1)/(1/m + m), y = m·x.
- **Absolute values:** the corner is then added. The results are X3/Y3 (line 1), X4/Y4 (line 2) and
  I1/J1 (the centre).

**The work variables are user variables.** The firmware stores all of this in the letter-variable records,
in program units, and leaves the values there:

| Variable | Holds |
|---|---|
| X1 Y1 | the corner |
| U1 V1 | line 1: corner − start |
| X2 Y2 | the next point |
| U2 V2 | line 2: next − corner |
| R2 / R3 | this / the next fillet's radius |
| B1 B2 B3 | the offset lines' intercepts c1, c2, and c2 − c1 |
| P2 P3 P4 | slopes m1, m2, and m1 − m2 |
| I1 J1 | the centre |
| X3 Y3, X4 Y4 | the tangent points |

The addresses are at the 0x4be0 + n·0xe4 records, e.g. X2 = 0x6068, U1 = 0x5db0. **A program that uses
any of these variables loses its values at a fillet.** Most of them are also canned-cycle variables
(`LANGUAGE.md` section 3). dxsim sets them the same way, so `-d` and later reads show what the control
would hold.

**No checks in the firmware:**
- **G22/G23 that doesn't match the turn:** the centre ends up outside, and the arc goes the long way round.
- **A radius too big for the lines:** the tangent points fall outside the lines, and the tool backs up.
- **Parallel lines:** m1 = m2, and the firmware divides by zero.
- **Both lines vertical:** m2 is stale.
- **A block without X or Y:** the old X1/X2/U/V values are used.

## 3. Design (as built)

- **Where it hooks in:** `dxgcode()` handles G22/G23 like 0x20bf6/0x20c18. `dxmend()` hands every block to
  `fillet()` while `s->fstate` ≠ 0.
- **`fillet()`** follows the states:
  - state 1 sets X1/Y1/U1/V1/R2 and line 1's offset;
  - state 2 sets X2/Y2/U2/V2/R3, line 2's offset, the centre and the tangent points, then makes the line and
    the arc, then either shifts for the next fillet or makes the last line.
- **Values:** they are written straight into the letter variables (`fv()`), in program units, like the
  firmware. They are not traced as assignments.
- **Moves:** they go through `line()`/`arcseg()`, so transformations, the plots, the travel check and
  **cutter compensation** all apply. The arcs take G45's feed factor under compensation.
- **Z:** a Z word in the G23 block moves Z with the line into the corner (the next block's Z if it has
  one), and Z stays there for the arc and the last line, as the finalizer does (`OPENQ.md` A14). In G73
  the firmware drops the G23 block's Z (L10).

## 4. Errors and warnings

The firmware has no fillet messages. The run continues as on the control, with these warnings:

| Warning | Why |
|---|---|
| G22/G23 needs both X and Y | the firmware keeps the old X1/Y1 |
| the block after the last G22/G23 must be a G1 move with X and Y | manual p.18; the firmware keeps the old X2/Y2 |
| fillet radius too big for its lines | a tangent point is outside its line; the tool goes backwards |
| fillet arc of more than 180° | G22/G23 doesn't match the turn (the centre is outside), or the radius is too big |
| fillet between parallel lines | the firmware divides by zero. dxsim goes straight to the corner instead |

## 5. Tests

| Test | Program | Checked |
|---|---|---|
| `ffig37` | manual fig. 3-7: seven R.25 fillets, alternating G22/G23 | **7 arcs of R.25; fillet 1 from (2.3662,.5) to (2.5742,.6113), centre (2.3662,.75)**, worked out by hand. The plot matches the figure |
| `fsquare` | two fillets on horizontal and vertical lines, `-d` | **tangent points (1.5,0) (2,.5) (2,1.5) (1.5,2)**; the variable dump shows what the fillet leaves in B, I, J, P, R, U, V, X, Y |
| `fwarn` | G22 on a left turn; a radius too big | the warnings; the 270° arc the firmware's arithmetic gives |
| `fcomp` | fillets inside cutter compensation, G41 .5 dia | the fillet arcs are compensated (R.5 → R.25, inside), with G45's F × .5 |

Items in bold are also asserted by `dxtest`.

## 6. Bench sheet (phase 4, for the owner)

| # | Program | Look for | Result |
|---|---|---|---|
| 1 | `tests/FFIG37.TXT` | the shape of fig. 3-7, all seven fillets | |
| 2 | `tests/FSQUARE.TXT`, then display X3, Y3, I1 (or run a block `X9=X3` and look at X9) | X3 = 2, Y3 = 1.5, I1 = 1.5: the control leaves the fillet values in the variables | |
| 3 | `tests/FWARN.TXT` (first part, dry run, Z clear) | with G22 on a left turn, the tool loops round the outside of the corner | |

## 7. Open questions

These are also in `OPENQ.md` (F1-F3), with how to answer them.

1. **Z in fillet blocks:** when the Z move happens. Not traced.
2. **The `0x32fa` state machine** next to the fillet one (0x1c580-0x1c602) uses the same helpers
   (0x1dc80, 0x1dce2, 0x1dd3e) with G9's flag 0x32e0 bit 7. It looks like precision-cornering's automatic
   blend ("AutoBlend", message 0x331ba). Not part of this feature; noted for the roadmap.

## 8. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-27 | (this commit) | Phases 0-3: manual and firmware traced (G22/G23 handlers, state machine 0x32f5, the slope-intercept fillet arithmetic in user variables, hand-off to cutter compensation, the C-axis message); simulated in `dxmotion.c`; tests `ffig37`, `fsquare`, `fwarn`, `fcomp` with two hand checks (89 passed). Found: fig. 3-7's `F20` is variable F20 |
