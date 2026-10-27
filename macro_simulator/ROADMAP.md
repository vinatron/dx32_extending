# dxsim roadmap

Features planned for dxsim, in the agreed order. All the work stays in `macro_simulator/`. The owner can
change the order.

| # | Feature | Status |
|---|---|---|
| 1 | Machine parameters (`-P BMDCPRMS.SYS`) | **done** (PR #5): `README.md` "Machine parameters", `LANGUAGE.md` 14a |
| 2 | Cutter diameter compensation (G41/G42) | **simulated** (phases 0-5); bench check on the machine pending, see `features/CUTCOMP.md` |
| 3 | Fillets and corner radii (G22/G23) | **simulated**; bench check pending, see `features/FILLET.md` |
| 4 | Program flow view: main program → macros → cycles | **done**: `-flow`, `-pc`, preview call stack and step over, `-map`; see `features/FLOW.md` |
| 5 | FANUC Macro A/B to DX32 translator (`fb2dx`), all FANUC series | **in progress**: phases 0 (firmware checks) and 1 (reader, series detection, evaluator) done; see `features/FB2DX.md` |
| 6 | z/OS port with GDDM plots | deferred ("way down the road") |
| - | G9 P corner blending (the control inserts short sub-segments at corners) | candidate, low priority (`features/OPENQ.md` F2) |

A feature being built has its own document in `features/` (index and conventions in `features/README.md`),
kept up to date with each commit. When it is finished, its description moves into `README.md` and
`LANGUAGE.md`.

### Incoming: probing and machine-function extensions

Another track in this repo is extending probing and some machine functions; see `../GM_EXTENSION_PLAN.md`,
`../EXT_PROBING.md`, `../EXT_CODES.md` and `../IO_CTL.md`. When those changes are merged, that track's
agent will record the details in `HANDOFF.md`. Those entries then take priority over the list above:

1. **Validate.** Check each new or changed code against the 6.89 firmware (`BMDC.BIN`, `decompiled/`,
   `tools/m68k_dis.py`) and against the bench results in `../PHASE1_TEST_PLAN.md`. Label each finding
   CONFIRMED or TENTATIVE.
2. **Report disagreements** to that track: firmware against document, or document against bench result.
   Don't quietly simulate one version.
3. **Add them to dxsim:**
   - new G and M codes and M-macros;
   - the probe cycles: trace the probe moves, and take the touch points from a new option (e.g. the
     surfaces of a simple box) because dxsim has no model of the part;
   - machine I/O through the existing `-i` inputs and the `IO` trace lines.
4. **Write it down and test it:** new sections in `LANGUAGE.md`, test programs with expected traces, and
   the error and warning messages the control gives for misuse.

---

## 1. Machine parameters (done)

Done in PR #5; see `README.md` "Machine parameters" and `LANGUAGE.md` section 14a. Differences from the
plan:
- **Rates:** the units were traced (169.333 units/tick = 1 ipm), so params 7 and 8 are used. The feed
  clamp is still the manual's 295 ipm.
- **Parameter 156** holds only run-time status, so it isn't used.
- **Parameters 272 and 285** are not in `BMDCPRMS.SYS`.
- **Z check:** the firmware applies it only for some machine configurations (0xaf58, 0x2988). That isn't
  traced yet; dxsim always checks Z.

## 2. Cutter diameter compensation (G41/G42) (simulated)

Simulated in PR #5, following the 6.89 firmware's own compensation code: `README.md` "Cutter compensation",
with the full record in `features/CUTCOMP.md`. What is left is phase 6, the bench check on the machine.
The bench sheet is in `features/CUTCOMP.md` section 6.

## 3. Fillets and corner radii (G22/G23) (simulated)

Simulated in PR #5, following the 6.89 firmware's own fillet code: `features/FILLET.md`. The bench sheet is
in its section 6. The firmware keeps the fillet's working values in user variables (X1-X4, Y1-Y4, U1, U2, V1,
V2, I1, J1, R2, R3, B1-B3, P2-P4) and leaves them set. dxsim does the same.

## 4. Program flow view

This shows how a program moves between its parts: the main program, `#n` macros, `M<n>:` macros, canned
cycles (and the built-in `%` routines inside them), `=N` loops, `=Ln` jumps and `?…!` conditionals. All
of it comes from the interpreter's existing call/return points (`gosub`/`subret`, `syscall`/`sysret`,
`nextpos`, the label jump and the skip counter), so it records what the control really does, not a second
parse of the program. Three views, all in C89 with no extra libraries:

### 4a. Flow trace (`-flow`, text)

An indented record of every entry and exit, with line numbers, parameters and depth, in the trace file or on
the console:

```
line  12  =#3Z*-.5F*10.        -> #3 "Peck Cycle"  (line 40)      depth 1  params -.5 10.
line  41    G1Z*F*
line  42    G0Z*
line  43    $                  <- back to line 13
line  20  =N60/5               loop to N60, 5 passes
          ... pass 2-5 the same as pass 1 (shown once)
line  25  G83X1.Y1.Z-.5...     -> canned cycle G83 (built-in A3)    depth 1
line  30  ?V1<0.5!             condition false: lines 31-33 skipped
```

Identical repeats (loop passes, the same call with the same values) are folded into one line with a count,
so a 42-hole pattern doesn't fill pages. `-flow all` shows every pass, and `-flow cyc` also opens up the
built-in cycle text.

### 4b. Call map (`-map file`, SVG or text)

A diagram of the program's structure:
- **Boxes:** one for the main program, each macro, each M-macro and each canned cycle used.
- **Arrows:** from caller to callee, labelled with the calling lines and how many times the run took them.
- **Dashed arrows:** calls that exist in the program but the run never took, for example inside a
  condition that was false. These are found by the pre-scan that already registers macro definitions.
- **Layout:** columns by call depth, so the main program is on the left, macros in the middle and cycles
  on the right. No Graphviz is needed. With a `.TXT` name the same map prints as an indented tree for
  the DOS console.

The map also gives new warnings, in the same format as the existing ones:
- a macro defined but never called;
- a call to a macro that is never defined (already an error at run time; the map finds it without running);
- nesting deeper than 8 levels (the 6.89 limit) or 4 levels (what the manuals promise);
- a macro that calls itself;
- a macro that uses L8 or L9 while its caller loops over a canned cycle. The cycles use those labels
  too, see `LANGUAGE.md` section 6.

### 4c. In the plot and the live preview

- **Plots:** each move is coloured by the part of the program that made it, for example main program blue,
  `#3` green and canned cycles grey, with a key. This makes it easy to see which macro cut which feature.
- **Live preview:**
  - The status line shows the call stack, e.g. `MAIN 12 > #3 41 > G83`.
  - In block-by-block mode, `I` steps into a call and `O` steps over it, like a debugger.

Suggested order: 4a first (smallest, and the other two use its records), then 4c, then 4b.

## 5. FANUC Macro B to DX32 translator (`fb2dx`)

A separate C89 program in this directory. It reads a FANUC custom macro program (main program, `O`
subprograms and `G65` custom macros), in Macro A or Macro B from any FANUC series: the series is detected from
the program or given with `-series` (`features/FB2DX.md` 3.7) and writes a DX32 program that uses PPPL, `#n` macros and `M<n>:` macros. It
translates what it can prove to mean the same thing, and **refuses the rest** with the line number and the
reason. A silent mistranslation on a real machine is the main risk, so fb2dx never guesses.

### What maps across

| FANUC | DX32 | Notes |
|---|---|---|
| `#1`-`#33` locals, `#100`-`#199`, `#500`-`#999` | letter variables, e.g. `D20`-`D50` | Allocated from a free pool that avoids the variables the built-in cycles use (`LANGUAGE.md` section 3). Locals are global on the DX32, so each call depth gets its own set |
| `G65 P9010 A1. B2.` | assignments in the caller, then `=#n` | `*` can't be used in an expression (`A21=*` is an error, 0x262a6), so the caller assigns the arguments (`features/FB2DX.md` 3.4) |
| `M98 P1000 L3` / `M99` | `#n ... $` called 3 times, or an `=N` loop | |
| `[...]`, `+ - * /` | PPPL expression | The same precedence |
| `SIN COS TAN ATAN SQRT ABS` (degrees) | `SIN COS TAN ATN SQR ABS` (degrees) | FANUC `ATAN[a]/[b]` has a full-circle result, so the quadrant has to be fixed up |
| `AND OR XOR` (bitwise) | `AND LOR XOR` | |
| `IF [cond] THEN #i=...` | `?var<op>value` ... `!` | A DX32 condition compares one variable with a number or a variable, so expressions go into a temporary variable first |
| `IF [cond] GOTO n` going forward | `?` ... `!` around the skipped blocks | The inverse condition, with nested `?` for `AND`/`OR` |
| `GOTO n` going back, `WHILE [..] DO m ... END m` | label `Ln` and `=Ln` | 9 labels (29 inside an M-macro) |

### What doesn't map across (refused, with the reason)

- **System variables:**
  - `#1000`+ I/O and `#2000`+ tool offsets;
  - `#3000` alarms, `#3001` timers;
  - `#4000`+ modal information;
  - `#5000`+ positions, including the probe skip positions `#5061`+.

  Most FANUC probing macros (Renishaw and others) are built on these, so they won't translate. Where an
  equivalent exists, e.g. `WRI`/`RDI` for some I/O or `EXT_PROBING.md` for probing, it is offered as a
  marked suggestion for a person to check, not inserted automatically.
- **Vacant `#0`** and argument-presence tests (`IF [#1 EQ #0]`). The DX32 has no "empty" value. A flag
  variable can stand in only when the caller is translated too.
- **`BCD BIN`** are not in the 6.89 function table. `FIX FUP ROUND` (and `MOD` for positive values) are
  built from `LSR(0,x)`, which rounds to nearest, plus a correction step (`features/FB2DX.md` 3.3).
- **Real recursion**, more than 8 nested calls, or jumps that don't form loops or skips (forward
  `GOTO` into the middle of a loop).
- **The FANUC motion side** where it differs from BOSS:
  - canned cycles (`G73 G76 G81`-`G89` with `R`/`Q`/`G98`/`G99` against the DX32's own forms);
  - `G68` rotation (DX32 `G73`);
  - `G43`/`G49` tool length;
  - `G28`/`G30`, `G10`, `G52`.

  The common ones (`G81` drilling, `G83` pecking, `G2`/`G3` with `R`) get a translation. The rest are
  reported.

### How it is checked

- fb2dx runs `dxsim -lint` on its output.
- It then runs both programs and compares the tool paths. This needs a small FANUC Macro B evaluator in
  fb2dx. It can be smaller than dxsim, because it only evaluates expressions and control flow and
  emits the same move records.
- A difference is reported with both line numbers.
- Test programs: hand-written ones covering every row of the tables above, plus public FANUC macro
  examples (bolt circles, pockets, families of parts).

### Assessment

- **Worth it for parametric part programs:** bolt circles, pocket and slot macros, families of parts,
  engraving. These use variables, arithmetic, `IF`/`WHILE` and `G65`/`M98`, and they translate well.
- **Not worth it for macros built on system variables:** probing, tool management, alarms. Those depend
  on the FANUC control itself, and the report says so, line by line.
- **Depends on items 1 and 4:** the flow view and the machine parameters. They make the translated
  program's checks (nesting, travel) mean something.

## 6. z/OS port with GDDM plots

Deferred by the owner until the DOS/Windows 98 version is settled. The plot-device layer (`src/dxout.c`)
is where a GDDM device would go. Everything else is already portable C89, including the EBCDIC-safe letter
lookups.
