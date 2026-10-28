# FANUC Macro B to DX32 translator (`fb2dx`)

Roadmap item 5. A separate C89 program in this directory. It reads a FANUC custom macro program (Macro A or
Macro B, from any FANUC series, section 3.7) and writes a DX32 (BMDC 6.89) program that means the same thing, or refuses, with the line and the reason. A silent
mistranslation on a real machine is the main risk, so **fb2dx never guesses**. The scope and the mapping
tables are in `../ROADMAP.md` section 5. This document holds the detail.

## 1. Status

| Phase | What | Status |
|---|---|---|
| 0 | Firmware checks the design depends on | **done** (section 2) |
| 1 | FANUC front end: dialect table, series detection and `-series`, reader, expression parser, statement list, small evaluator | **done** (section 3.8) |
| 2 | Variables and expressions: allocation, PPPL output, numbers always with a point | **done** for straight-line programs (section 3.9) |
| 3 | Control flow: `IF`/`GOTO`/`WHILE` to `?`/`!`, `Ln`/`=Ln`; refuse what does not fit | **done** (section 3.10), with exit flags for jumps out of nested structures |
| 4 | Calls: `M98`/`M99`, `G65` to `#n` macros with caller-side assignments | **done** (section 3.11) |
| 5 | Motion subset: G0-G3 (with `R`), G81, G83, G90/G91, F, S, T, M | **done** (section 3.12): arcs, dwell, G81/G82/G83/G73 written out as moves, T/M6, G43/G49 |
| 6 | Checking: `dxsim -lint` on the output, then both programs run and the tool paths compared | **done** (section 3.13): `-check`, `-against` |

## 2. What the control does (facts the translation relies on)

| # | Fact | Source | Confidence | Consequence for fb2dx |
|---|---|---|---|---|
| 1 | **`*` cannot be used in an expression.** The PPPL factor dispatch table at 0x262a6 sends `*` (0x2a) to the error entry 0x24fba, so `A21=*` is an error. `*` only works as a word value (`Z*`) | F | CONFIRMED | `G65 P.. A1. B2.` cannot become `=#n A*1. B*2.` with `A21=*` in the body. The arguments are **assigned by the caller** before `=#n` (section 3.4) |
| 2 | **The 32-bit functions round their arguments.** `NOT` (0x25aa6), `LSR` (0x25d66) and the others convert with `fmove.l fp,dn`. FPCR is only written inside library routines (0x374d6+, saved and restored; 0x37c4a sets 0), so the mode is the default, round to nearest with ties to even. dxsim was changed to match (`../LANGUAGE.md` section 4, `OPENQ.md` L8) | F | TENTATIVE until `../bench/BFIX.TXT` is run | `FIX` cannot be `LSR(0,x)` alone; it needs a correction step (section 3.3) |
| 3 | The function table (0x26302, 40 entries) has no `ROUND FIX FUP MOD BCD BIN` | F | CONFIRMED | They are built from other functions (section 3.3) or refused |
| 4 | A bare 1-2 digit integer after a letter is a **variable reference** (`X5` is variable X5) | M ch.10, F 0x23eb2 | CONFIRMED | Every number fb2dx writes has a decimal point (`5.`, `0.`) |
| 5 | A block is an assignment only when its 3rd or 4th character is `=` | F 0x1ed88 | CONFIRMED | An assignment is a block of its own, with no N number in front |
| 6 | A condition compares **one variable** with a number or a variable; no expressions | F 0x1ffc6-0x2021c | CONFIRMED | Expressions in a condition go into temporaries first |
| 7 | Labels are **one global table** (0x3c74): 1-9, 10-29 inside an M-macro; the built-in cycles use L8 and L9 | M ch.10, F 0x1f48e | CONFIRMED | Labels are allocated over the whole output, not per macro (section 3.5) |
| 8 | All variables are global; `#n` macros nest 8 deep in 6.89 (4 in the manuals) | F 0x200c4 | CONFIRMED | FANUC locals `#1-#33` get one set per call depth |
| 9 | The built-in cycles use and change a long list of variables (`../LANGUAGE.md` section 3) | M table 10-1, C | CONFIRMED | Those are never allocated |
| 10 | Assignments in a user program need the PPPL option (param 285 bit 4); inside an M-macro they don't | F 0x1edbc | CONFIRMED | The report says when the output needs PPPL; `-mmacro` puts the translated body in an M-macro |
| 11 | `ATN` gives -90..+90 degrees | M, F | CONFIRMED | FANUC `ATAN[a]/[b]` (0..360) needs a quadrant fix-up |
| 12 | A word can take a variable only **of its own letter**: `X21` is the value of variable X21. A word can't take an expression or another letter's variable | M ch.10, F 0x23eb2 | CONFIRMED | `X[#1+2.]` becomes `X21=D21+2.` and then `X21` in the block (3.6) |
| 13 | **Arc centres:** with G75 and G90, `I J K` are the absolute centre; with G91, the distance from the start point. FANUC `I J K` are always the distance from the start point | M ch.3, F 0x21dba | CONFIRMED | In G90, fb2dx writes the absolute centre (3.6) |
| 14 | **Radius arcs** are always the short arc (≤ 179.998°) | F 0x1f992 | CONFIRMED | A FANUC arc with negative `R` (the long arc) is written with its centre instead |
| 15 | G70/G71 select inch/metric input; the control works in inches inside (2 540 000 counts per inch) and variables hold program units | M table 3-1, F 0x23fe0 | CONFIRMED | FANUC G20/G21 → G70/G71 |
| 16 | In **BOSS 4-7 compatibility mode** (0xaef9 bit 2) a number without a point is scaled (/10000 inch, /1000 metric) and variable references are off | F 0x23cf8, 0x23eb8 | CONFIRMED | fb2dx output needs that mode **off**; the report says so |

## 3. Design

### 3.1 Program shape

```
fb2dx [options] main.nc [sub.nc ...]
  -w file          write the DX32 program (only when nothing is refused)
  -o file          the line-by-line report (default: the console)
  -eval, -parse    the evaluation trace, the statements as read
  -bdel on|off     the block delete switch
  -series KEY      the FANUC series the program was written for (section 3.7); otherwise detected
  -fparam N=V      a FANUC parameter setting that changes what the program means (3.7); repeatable
  -fparam default  take the factory setting for every such parameter, and say so in the report
  -inch | -mm      the unit system when the program has no G20/G21 (see 3.6)
  -series list     print the dialect table and stop
  -mmacro n        put the main program in M-macro n (no PPPL licence needed)
  -check           run dxsim -lint on the output and compare the tool paths (phase 6)
```

Source files `src/fb*.c` and `src/fb2dx.h`. It shares nothing with dxsim at link time, so each can be built
alone with Watcom for DOS. `-check` runs dxsim as a separate program.

### 3.2 Variables

| FANUC | DX32 |
|---|---|
| `#1`-`#33` (locals) | one block of DX32 variables per call depth that the program reaches. Only the locals that are actually used get a variable |
| `#100`-`#199`, `#500`-`#999` (commons) | one DX32 variable each, used ones only |
| temporaries for expressions and flags | a separate pool |

- **The pool:** the letters and indices 1-50 that the built-in cycles don't touch (fact 9), and never G, H, L,
  M, N, S, T (the manual's rule). Indices 20-50 exist only for A C D F I J K R X Y Z (`../LANGUAGE.md`
  section 3), so the pool is mainly `D20`-`D50`, `K20`-`K50`, `C20`-`C50`... A program that needs more than
  the pool is refused.
- **The map is printed** at the top of the output as comments (`; #1 -> D20`), so a person can follow it.
- **Vacant `#0`** has no equivalent. A program that reads `#0` or compares with it is refused at that line.
  FANUC treats an unset local as vacant; fb2dx's evaluator tracks this and refuses a read of a local that may
  be unset, instead of letting it become 0.

### 3.3 Expressions

- `+ - * /`, brackets and the trigonometric functions map one to one (`../ROADMAP.md` table). FANUC `[ ]`
  become `( )`.
- `SQRT` → `SQR`, `ABS` → `ABS`, `SIN COS TAN` → the same names (both work in degrees), `ASIN ACOS` → `ASN ACS`.
- `ATAN[a]/[b]`: the result is in 0..360 on FANUC. fb2dx writes `T=ATN(a/b)` plus fix-ups with `?` on the
  signs of a and b. The `b = 0` case is its own branch (90 or 270), so there is no division by 0.
- `AND OR XOR` between values → `AND LOR XOR`. These work on 32-bit integers on both controls, but the DX32
  rounds non-integers (fact 2). FANUC's behaviour with non-integers depends on the model, so a non-integer
  argument is refused.
- **`FIX`, `FUP`, `ROUND`** are built from `LSR(0,x)` = x rounded (fact 2), with a correction:
  ```
  ; T = FIX(X)  for X >= 0      (FANUC FIX truncates towards 0)
  T=LSR(0,X)
  ?T<GT>X
  T=T-1.!
  ```
  For a value that may be negative, the same on `ABS(X)` and the sign put back with another `?`. `FUP` is
  `FIX` plus 1 when the result is less than the absolute value; `ROUND` is `FIX(ABS(X)+.5)` with the sign
  put back (FANUC rounds halves away from 0). These avoid any constant like `.4999999`, so they are exact
  for every value the 32-bit conversion can hold (|x| < 2³²). LSR's result is read as unsigned (0..2³²−1);
  a larger |x| is refused. TENTATIVE until `BFIX.TXT` confirms fact 2.
- `MOD` (on controls that have it) → `a-b*FIX(a/b)` for a, b > 0; otherwise refused. `BCD BIN` are refused.
- **Precision:** FANUC works in about 8-9 significant digits on older controls; the DX32 in 68882 extended
  precision. Results can differ in the last digits. The comparison in phase 6 uses a tolerance, and
  comparisons with `EQ` on computed values are flagged in the report ("care should be taken when testing
  for <EQ>", M ch.10).

### 3.4 Calls

- **`M98 P1000 L3`** → `=#n` three times, or `=N` around one call when the call block is the only block in the
  loop. `M99` → `$`. `M99 P..` (return to a given block) is refused.
- **`G65 P9010 A1. B2. K3.`**: the arguments are assigned in the caller, then the macro is called:
  ```
  D21=1.            ; #1 (A) at depth 1
  D22=2.            ; #2 (B)
  D26=3.            ; #6 (K)
  =#2
  ```
  `*` can't carry them (fact 1). An argument that isn't given leaves its local vacant on FANUC; fb2dx
  checks with the evaluator that the macro never reads it, and refuses otherwise (3.2).
- **`M98` and locals:** a subprogram called with `M98` uses its caller's locals; only `G65` (and `G66`) gets a
  new set. fb2dx follows that: an `M98` subprogram reads and writes the caller's DX32 block, so the same
  subprogram called from two depths is written twice (once per depth), or refused if that overflows the pool.
- **Argument specification II** (`I J K` repeated) is handled by its own `#n` numbering; specification I is
  the default.
- **`G66`/`G67` modal calls** and user G/M codes that call a macro (parameters 6050-6089) are refused, with a
  note that `M<n>:` macros are the DX32 way to do the second.
- **Depth:** the evaluator follows the calls; deeper than 8 is refused, and deeper than 4 gets a warning (the
  manuals' limit, `OPENQ.md` L2). Recursion is refused.
- Each FANUC `O` program becomes one `#n` macro. `#n` numbers are given in order of first call and printed in
  the map.

### 3.5 Control flow

FANUC jumps are turned into DX32 structures; anything that doesn't fit one of them is refused.

| FANUC | DX32 | Condition |
|---|---|---|
| `IF [c] THEN #i=e` | `?` (c) `#i=e` `!` | |
| `IF [c] GOTO n`, n further down | `?` (not c) around the blocks up to n, then `!` | the region doesn't overlap a loop or another skip in a crossing way |
| `GOTO n`, n further up | label `Lk` at n, `=Lk` | |
| `IF [c] GOTO n`, n further up | `?` (c) `=Lk` `!` | |
| `WHILE [c] DO m ... END m` | `Lk` + `?` (c) ... `=Lk` `!` | |
| `GOTO n` further down, unconditional | `?` on a constant false flag around the skipped blocks | |

- **Conditions** compare one variable with a variable or number (fact 6). A condition `[e1 op e2]` is written
  as `T1=e1`, `T2=e2`, `?T1<op>T2`. `AND`/`OR` between conditions, and any negation, go through a flag
  variable:
  ```
  ; F1 = [#1 GT 0] AND [#2 LT 5]
  F1=0.
  ?D21<GT>0.
  ?D22<LT>5.
  F1=1.!
  !
  ?F1<EQ>1.
  ```
- **Labels:** one global table (fact 7). fb2dx numbers them over the whole output: L1-L7 (L8, L9 only when the
  program uses no canned cycle), and L10-L29 inside an M-macro. A program with more backward targets than that
  is refused. A label is never reused, because a macro that sets L1 changes the caller's L1 too.
- **`?`/`!` nesting:** the skip counter counts `?` up and `!` down across blocks (`../LANGUAGE.md` section 5),
  so each region has to be balanced. A region that would contain an unmatched `?` or `!` is refused.

### 3.6 Motion and words

- **Numbers:** always written with a point (fact 4). A FANUC number without a point means different things by
  series and parameter (whole units, or least increments such as 0.001 mm). It is refused unless `-fparam dpi=`
  (and `is=` for increments) says how to read it (3.7). The unit system comes from G20/G21 in the program, or
  from `-inch`/`-mm`.
- **Word values from variables or expressions** (fact 12): each one goes into a variable of the word's own
  letter, assigned in the block before: `G1 X[#1+2.] Y#2` →
  ```
  X21=D21+2.
  Y21=D22
  G1X21Y21
  ```
  So the pool also keeps carrier variables per word letter (X21-X50, Y21-Y50, I21-..., F21-...). Words with
  constant values are written as numbers.
- **Units:** G20/G21 → G70/G71 (fact 15).
- **G0, G1, G90, G91, F, S, T, M3-M5, M8, M9, M30**: one to one.
- **Arcs** (facts 13, 14): fb2dx keeps the current position in carrier variables (`X20`, `Y20`, `Z20`, set
  after each move whose end depends on a variable). In G91 a FANUC arc is written as it is. In G90 the centre
  is written as an absolute centre, `I20=X20+(i)` and so on, then `G2X..Y..I20J20`; the block before the arc
  must be G75 (single-quadrant G74 is never used). `R` arcs with positive R are written as they are; negative
  R (the long arc) is written with the computed centre.
- **G81, G83** are translated to the DX32 forms (`../LANGUAGE.md` section 13), with the `R` plane and
  `G98`/`G99` turned into explicit moves when they don't match. Other cycles, `G68`, `G43`/`G49`, `G28`/`G30`,
  `G10`, `G52` and system variables are refused (`../ROADMAP.md` section 5).

### 3.7 FANUC series and dialects

FANUC's macro language changed over the series. fb2dx handles all of them with one table of dialects
(`src/fbdial.c`). The table is data: a cell found wrong in a FANUC manual is corrected in one place.

**The series** (`-series KEY`):

| Key | Controls | Macro |
|---|---|---|
| `a` | Custom Macro A, on any series that has it (0, 6, 10/11, 15, early 16/18) | `G65 Hm P#i Q#j R#k` operations |
| `0` | Series 0 (0-M, 0-T, 0-MC, 0-TC, 0-MF), Power Mate | Macro B |
| `6` | Series 6 (6M, 6T) | Macro B |
| `10` | Series 10, 11, 12 | Macro B |
| `15` | Series 15, 150 | Macro B |
| `16` | Series 16, 18, 20, 21, 160/180/210, their i and is models, Power Mate i | Macro B |
| `0i` | 0i-A to 0i-D, 0i Mate | Macro B |
| `30i` | 30i, 31i, 32i, 35i (A and B), 0i-F, 0i-F Plus | Macro B with the newer additions |

The cells below come from FANUC's operator manuals as far as they are known here, without a manual in this
repo to check against. They are all **TENTATIVE**: `?` marks a cell that is not known at all. The Macro A
H-code list matches a FANUC training course's outline ([Mills CNC](https://www.millscnc.co.uk/?p=2578)).
The online copies of the operator's manuals found so far (0i-D B-64304EN, 16i B-63534EN, 30i-B B-64484EN)
are on hosts this session's network policy does not reach (Q6).

**Two kinds of difference.** They are handled differently:

1. **What exists.** A construct is there on some series and not on others. This only matters for detection.
   Where a construct exists, it means the same thing, so it is translated the same way. A wrong or `?` cell
   here can't cause a wrong translation, only a wrong detection message.

   | Construct | a | 0 | 6 | 10 | 15 | 16 | 0i | 30i |
   |---|---|---|---|---|---|---|---|---|
   | `G65 Hm` operations (H01-H99) | yes | - | - | - | - | - | - | - |
   | `#i=expr`, `[ ]`, `SIN COS TAN ATAN SQRT ABS ROUND FIX FUP BIN BCD`, `IF..GOTO`, `WHILE..DO1-3`, `G65 P` with arguments, `#[expr]` | - | yes | yes | yes | yes | yes | yes | yes |
   | `IF [c] THEN statement` | - | no | no | ? | ? | yes | yes | yes |
   | `ASIN ACOS` | - | no | no | ? | ? | ? | yes | yes |
   | `LN EXP` | - | no | no | ? | ? | ? | ? | yes |
   | `POW ADP`, `ATAN[a,b]`, one-argument `ATAN[x]`, `MOD` | - | no | no | no | no | no | ? | yes |
   | `AND OR` between conditions: `IF [[#1 GT 0] AND [#2 LT 5]]` | - | no | no | no | no | no | ? | yes |
   | Named variables: system names `#_ABC`, common names set with `SETVN` | - | no | no | no | no | no | ? | yes |
   | `IF..THEN` / `ELSE` / `ENDIF` over several blocks | - | no | no | no | no | no | no | ? |
   | Common variables beyond `#100-#149`, `#500-#531` | - | option | option | ? | ? | option | option | yes |
   | `G65` nesting | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 5 |

2. **What it means.** The same text gives a different result by series, or by a parameter of the machine.
   These are the ones that can cause a silent mistranslation:

   | Item | Differs by | fb2dx needs |
   |---|---|---|
   | A number without a decimal point (`X10`) | parameter DPI (3401 bit 0 on 16/0i/30i; other series: `?`). 0 = least increments, 1 = whole mm/inch | `-fparam dpi=0\|1`, plus `is=b\|c` (the increment system: IS-B 0.001 mm / 0.0001 in; IS-C ten times finer) when dpi=0 |
   | `ATAN` result range | 0..360 everywhere; on 0i/30i parameter NAT (6004 bit 0) can make it -180..180 | `-fparam nat=0\|1` when the series may be 0i or 30i |
   | `ROUND` inside an address (`X[ROUND[#1]]`) | on 16/0i/30i it rounds to the least increment of that address, not to a whole number (`?` on older series) | the series, and `is=` |
   | `G65 H.. P#..` | Macro A: an operation. Macro B: a call, with H passed as argument `#11` | the series (`a` or not) when the program could be either |
   | Constants in Macro A operations | integers; how coordinates and angles scale (least increment, 0.001°) is `?` | refused in an angle or a coordinate until the Macro A manual is checked |
   | Significant digits | about 8 on the older series, more on 30i | nothing: a `NOTE` with `EQ` on computed values (3.3) |

**Detection.** Without `-series`, fb2dx works the series out from the program:

1. Each construct in the first table that the program uses gives the set of series it exists on. fb2dx
   takes the intersection of those sets. A `yes` or `?` cell counts as "exists"; `no`, `-` and `option`
   count as given.
2. **An empty intersection** means the program mixes series. It is refused, naming both lines: `line 12 uses
   ATAN[a,b] (30i only), line 40 uses G65 H02 (Macro A only)`.
3. **With `-series`**, a construct that doesn't exist on that series is refused at its line.
4. For each item of the second table that the program uses, every candidate series has to give the same
   meaning. Otherwise fb2dx stops and says which option settles it: `line 7: ATAN may give -180..180 on 0i
   and 30i (parameter NAT); give -series or -fparam nat=`.
5. A parameter can't be seen in the program, so a parameter-dependent item always needs its `-fparam`, or
   `-fparam default` to accept the factory setting on purpose.
6. The report's first line says what was decided and why: `DIALECT  16, 0i, 30i (THEN at line 12); no
   construct used depends on which`.

Most programs use only the common Macro B core. For them, detection settles on "any Macro B series" and
no option is needed, except `dpi` if they have numbers without a point.

**Macro A** (`-series a`, or detected from `G65 Hm`). Each operation is one block, with P the destination,
and Q and R the operands:

| H | Operation | H | Operation |
|---|---|---|---|
| 01 | `#i = #j` | 22 | `#i = ABS[#j]` |
| 02, 03 | `#i = #j + #k`, `- #k` | 23 | remainder of `#j / #k` |
| 04, 05 | `#i = #j * #k`, `/ #k` | 26 | `#i = #i * #j / #k` |
| 11, 12, 13 | OR, AND, XOR | 27, 28 | `SQRT[#j² + #k²]`, `SQRT[#j² - #k²]` |
| 21 | `#i = SQRT[#j]` | 31, 32, 33 | `#i = #j * SIN/COS/TAN[#k]` |
| 34 | `#i = ATAN[#j / #k]` | 80 | `GOTO n` (P n) |
| 81-86 | `IF #j = ≠ > < ≥ ≤ #k GOTO n` (P n) | 99 | alarm: refused |
| 24, 25 | BCD, BIN: refused | | |

These become the same internal statements as Macro B, so everything after the reader is shared.

### 3.8 Phase 1 as built

`src/fb2dx.h`, `fbmain.c` (command line, report), `fbread.c` (reader and parser), `fbdial.c` (the series
table, detection, `-fparam`, Macro A), `fbeval.c` (evaluator). `fb2dx -series list` prints the table from
`fbdial.c`, so the program and this document can be compared.

**Reader.** For each line:
- **Cleaning:** comments `( )` are removed, the text is upper-cased, blanks are dropped, a trailing `;`
  (EOB) is dropped, and `%` lines are skipped. `/` (and `/1`-`/9`) marks block delete: `-bdel on|off` must
  say how the switch is set, or fb2dx stops.
- **Blocks:** `O`/`:` starts a program. An `N` number may come first.
- **Statements:** `#i=e`, `IF [c] GOTO n`, `IF [c] THEN #i=e`, `GOTO n`, `WHILE [c] DOm`, `DOm`, `ENDm`.
  `SETVN BPRNT DPRNT POPEN PCLOS` are read and reported, not translated.
- **NC blocks:** everything else. A word value is a number, a variable or `[expression]`, with a sign.
- **Expressions:**
  - precedence: functions; then `* / AND MOD`; then `+ - OR XOR`; then the comparisons `EQ NE GT GE LT LE`
    inside brackets.
  - `AND OR XOR` between two comparisons is the 30i logical form; between values it is bitwise.
  - `ATAN[a]/[b]` is always the two-argument form, on every series.
  - Variables: `#n`, `#[e]`, `#_NAME`.
- **Loops:** `fbpair` matches DO with END in each program, at most 3 deep.

**Detection** (`fbdetect`) is the algorithm in 3.7, with these details:
- The value 0 without a point (`Y0`) is not a DPI item, because 0 is 0 in any unit.
- `G65 H..` is settled first; with Macro A (`-series a`), `fbmacroa` rewrites each operation as the
  Macro B statement it stands for.
- **Macro A remainder (H23):** written as `Q - FIX[Q/R]*R`, TENTATIVE.
- **Macro A constant angles (H31-H34 with a constant R):** refused (Q6).
- **Macro A variables in dimension words** (`X#100`): need `-fparam avar=` (and `is=` for least increments).

**Evaluator** (`fbeval`, `-eval`). It runs the program and prints `SET`, `NC` (word values after DPI
scaling), `CALL`, `AGAIN`, `RET`, `JUMP` and `END` lines.

- **Variables:**
  - **Locals:** `#1-#33` per G65 level (5 levels), shared by `M98`.
  - **Commons:** `#100-#999`.
  - **System variables:** `#1000` and up, and `#_NAME`, are refused.
- **Vacant variables:** Macro B's rules.
  - `#i=#j` keeps a vacant `#j` vacant; in any other expression a vacant variable counts as 0 and gets a
    `VACANT` line in the report.
  - `EQ`/`NE` tell vacant from 0; `GT GE LT LE` count it as 0.
  - A word whose value is a vacant variable is left out of the block.
- **Calls:**
  - `G65 P L` with arguments: specification I, or II when I, J or K repeat; mixing the two is refused.
  - `M98 P L`; `M98 P` with more than 4 digits per the series (count and program, 4 digits each).
  - `M99` returns, or repeats the call while L passes are left. `M99 P` and `G66`/`G67` are refused.
- **Jumps:** `GOTO` searches forward to the end of the program, then from its start.
- **Functions:**
  - Degrees. `ATAN` and `ASIN` follow `nat`: with nat=0, ATAN gives 0..360 and ASIN 270..90.
  - `ROUND` in an address rounds to the least increment.
  - `FIX` cuts towards 0; `FUP` goes away from 0.
  - `AND OR XOR MOD BIN BCD` only on whole numbers ≥ 0.
  - `ADP` is refused.
- **Alarms:** division by 0, SQRT or LN out of range and an unknown variable stop the run with `REFUSED`.
- **G65 arguments without a point** are scaled like the address letter (TENTATIVE: FANUC treats them as
  least increments of that address).
- **Limit:** stops after `-limit` blocks (default 100000).

### 3.9 Phase 2 as built

`src/fbout.c`, option `-w FILE`. The evaluator always runs first (silently without `-eval`), because the
writer needs to know what the program really did. The DX32 program is written only when nothing is
refused; otherwise the file is removed and the report lists every refusal.

- **Program shape:** phase 2 writes straight-line programs:
  - assignments and `IF..THEN`;
  - NC blocks with G0 G1 G17 G90 G91, G20/G21 (written G70/G71), X Y Z, F, S;
  - M0 M1 M2 M3 M4 M5 M7 M8 M9 M30, one M per block (the DX32 takes one).

  Everything else is refused with the phase that will translate it: `GOTO`/`WHILE` (3), calls (4), other G
  codes and addresses (5).
- **Header:** two kinds of comment lines. `; fb2dx: FILE, FANUC series ...` names the source, and
  `; #1=D20 #2=D22 ...` gives the variable map.
- **Variables:**
  - **Pool:** FANUC variables get pool variables in order of first appearance: D20-D50, then C, A, K, J, I,
    R, F 20-50, eight letters with 31 variables each (none of them used by the built-in cycles).
  - **Reserved:** index 21 of every letter with a second bank, and index 19 of the others, are the word
    carriers. X20, Y20 and Z20 are kept for the current position (phase 5).
  - **Temporaries** come from the same pool, reused from one statement to the next.
- **Expressions:**
  - Every number has a decimal point, with up to 9 decimals; sub-expressions are in parentheses.
  - `SIN COS TAN ACOS ATAN[x] SQRT ABS LN EXP` → `SIN COS TAN ACS ATN SQR ABS LGE E2X`. `AND OR XOR` →
    `AND( , ) LOR( , ) XOR( , )`.
  - **Expanded with `?`/`!`:**
    - `FIX FUP ROUND`: 3.3, exact below 2³²;
    - `ATAN[a]/[b]`: 0..360, or -180..180 with nat=1; the b = 0 case is a branch, never a division;
    - `ASIN`: 270..90 with nat=0.
  - **Others:** `MOD` → `a-b*FIX[a/b]`. `POW` with a whole constant exponent 0-8 → a product; any other
    exponent, and `BIN BCD ADP`, are refused.
- **Conditions:** `IF [c] THEN` becomes `?`, the assignment ending in `!`.
  - A comparison puts the left side in a variable (the DX32 compares a variable with a number or a
    variable); `5. LT #1` is turned round to `#1 GT 5.`.
  - `AND OR XOR` of conditions go through 0/1 flag variables (3.5).
  - Every condition leaves exactly one `?` open.
- **Words:**
  - Constants are written in the unit's format: 4 decimals in inch, 3 in mm. A value that needs more is
    refused, e.g. IS-C values.
  - `dpi=0` numbers are scaled to least increments.
  - A variable or an expression goes through the word letter's carrier (`X21=D20+2.` then `G0X21`).
  - `ROUND` in an address rounds to the least increment.
  - Macro A with `avar=0` multiplies by the increment.
- **Vacant variables:** the evaluator marks each statement that used a vacant variable in a way the DX32 can't
  copy: read as 0 in arithmetic, tested with `EQ`/`NE`, or a word left out. Those statements are refused.
  - **Statements the run never reached:** translated with a `NOTE`, unless they read a variable the run never
    set.
- **Notes in the report:**
  - the PPPL option, when anything is assigned;
  - the LSR range, when `FIX`/`FUP`/`ROUND` are used;
  - G0 with Z and X/Y: the DX32 moves Z first up or last down, FANUC all at once;
  - BOSS 4-7 compatibility mode off.

### 3.10 Phase 3 as built

`fbout.c` translates the jumps and loops of the main program:

| FANUC | DX32 |
|---|---|
| `WHILE [c] DOm` ... `ENDm` | `Lk`, the condition's own blocks, `?c` ... `=Lk`, `!` |
| `DOm` ... `ENDm` (no WHILE) | `Lk` ... `=Lk` |
| `IF [c] GOTO n`, n further down | `?(not c)` ... `!` just before block n |
| `GOTO n`, n further down | a temporary set to 0, `?T<EQ>1.` ... `!` |
| `IF [c] GOTO n`, n further up or this block | `Lk` before block n; `?c`, `=Lk`, `!` |
| `GOTO n`, n further up | `Lk` before block n; `=Lk` |

The condition is negated without a flag where it can be (`EQ`↔`NE`, `GT`↔`LE`, `GE`↔`LT`). The target is found
as the FANUC control finds it: the next block with that N number, then from the top of the program. A
computed target (`GOTO #1`) is refused.

**Leaving a loop.** `IF [c] GOTO n` from inside a loop to the block right after its `END` works by itself:
- The skip that starts at the GOTO's `?` ends at the loop's own `!`, since the DX32 skip counter only counts
  `?` and `!` (`../LANGUAGE.md` section 5).
- Execution then goes on after the loop, and the GOTO's `!` that follows is a no-op.
- This was checked in dxsim before it was relied on.

A jump out of a loop to any other block uses an exit flag (below). A jump into a loop is refused: FANUC
does not allow it either.

**The check before writing.** The output is kept in memory with markers, and nothing is written unless all
of these hold:
1. Every `?` that a jump or a loop opens is matched, by the textual `?`/`!` count, with a `!` after which only
   no-op `!` lines stand before the block where the FANUC jump goes.
2. No skip passes over a label whose `=Lk` comes after the skip, which would leave the label unset.
3. Label regions (from the label to its last `=Lk`) nest without overlapping.

**Label numbers.** The number is the nesting depth: L1 is the outermost. Siblings reuse a number, which is
safe because the DX32 keeps the latest definition of each label. Deeper than 7 is refused, because L8 and L9
belong to the canned cycles. The report notes which labels the program uses: a caller that loops on the same
labels around it would break (phase 4 numbers labels over the whole call tree).

**Exit flags** (added with phase 4). Some jumps leave a structure that does not end at the jump's target:
- a GOTO out of two nested loops;
- a GOTO from a loop to a block further down than the one after its `END`;
- an `M99` inside an `IF..GOTO` region.

These get a flag variable `F` from the pool, set to 0 at the top of the program:
- **At the jump:** `F=1.` (inside `?c` ... `!` for `IF..GOTO`), then `?F<EQ>0.`, which skips to the end of
  the innermost structure around the jump.
- **Guards:** after each enclosing structure that does not hold the target, `?F<EQ>0.` keeps skipping to
  the end of the next one, and the last guard skips to the target.
- **At the target:** `F=0.`

Every region still nests, so the check above verifies these too. A GOTO into a loop is still refused.

### 3.11 Phase 4 as built

`fb2dx` finds every program the main program calls (`callgraph`): `G65 P` and `M98 P`, with constant P
and L (L up to 99).
- **Instances:** each pair of a called program and its local level is one DX32 macro `#n`.
  - `G65` calls at the caller's level + 1, `M98` at the caller's own level, as on FANUC.
  - The same program called at two levels is written twice.
  - A program that is not in the input, or a computed P, is refused. Recursion is refused, and so are
    calls deeper than 8 levels (DX32 6.89); more than 4 gets a NOTE (the manuals' limit).
- **Locals:** each level's `#1-#33` get their own DX32 variables. The map at the top of the output lists
  them as `level n: ...`.
- **Arguments:** the caller assigns them, then calls `=#n`, because `*` can't be used in an expression
  (fact 1). An argument the called program never reads is not assigned.
  - The numbering follows specification I, or II for repeated I J K.
  - Constants without a point are scaled like the address (TENTATIVE, 3.8).
- **Repeats:** `L` repeats the arguments and the call; `M98 P` with the count in front, per the series.
- **Layout:** the macros are defined at the top of the output, `#n; O<number>` ... `$`, then the main
  program.
- **Returns:**
  - `M99` at the end becomes the `$`.
  - An `M99` before the last block is an early return: a forward jump to the `$` (with an exit flag
    when needed).
  - `M30`/`M2` in a called program and `M99` in the main program are refused.
- **Labels over the call tree:** the programs are written callers first. A called macro's labels start
  above the labels its callers have open around the call: `O200` called from inside the main program's
  L1 loop uses L2. The total must stay within L1-L7.
- **Temporaries and carriers** are only used inside one statement, so calls can't disturb them.

### 3.12 Phase 5 as built: arcs, dwell, drilling, tools

The evaluator records, for every NC block it runs, the modal state it ran in (`fbs.modes`): G90 or G91, the
plane, G0/G1 or G2/G3, whether the current position in the arc's plane was known, and a negative R. The
writer refuses a block that ran in two different states.

- **Arcs** (G2/G3 in G17, G18, G19):
  - **G91:** I J K are written as they are, because both controls measure them from the start point. A
    centre word that is left out is written as 0.
  - **G90:** the DX32 wants the absolute centre (fact 13). fb2dx keeps the current position in X20, Y20 and
    Z20. They are set after every block that moves an axis: to the word's value in G90, or by adding it
    in G91. They are set only when the program has a G90 arc. Each centre is then written as
    `I21=X20+(i)` before the arc. A G90 arc at a point where the position isn't known yet (no absolute
    move on those axes since the start) is refused.
  - **R:** written as it is. The DX32 R arc is always the short one (fact 14), so an R below 0 (FANUC's long
    arc) is refused, as is R together with I/J/K.
  - **Refused:** a helical arc (the third axis in the arc block), and centre words of another plane.
  - **G75** (multi-quadrant) is written at the top of the main program when there are arcs.
- **Dwell:** `G4 P<ms>` or `G4 X<s>` becomes `G4Q<s>` (sets the time) and then a lone `G4` (dwells),
  `../LANGUAGE.md` section 11. The time is .01-327.68 s in steps of .01, otherwise it is refused.
- **More G codes passed through:** G2 G3 G18 G19, G54-G59, G80, G94.
- **Word order:** the G codes, then X Y Z, then the rest.
- **Drilling cycles** (G81 G82 G83 G73, G90). The DX32's own cycles work differently (its G81 Z is a depth
  below the current height, `../LANGUAGE.md` 13), so each FANUC hole is written out as moves:
  - rapid to X Y; rapid to R; then
    - G81: feed to Z;
    - G82: feed to Z, then dwell P (ms);
    - G83/G73: a DX32 loop of pecks of Q down from R. G83 goes back to R after each peck, then rapids
      to the last depth less d83 (parameter 5115); G73 retracts by d73 (parameter 5114).
  - Then rapid to the initial level (G98) or to R (G99).

  The cycle values (Z, R, Q, P, the initial level) live in five pool variables, set where the FANUC block
  sets them, so values from variables and macros work.
  - **The initial level** is Z where the cycle starts (the Z position must be known there).
  - **The evaluator decides which blocks are holes:** in cycle mode, a block with X, Y, Z or R
    (TENTATIVE: that is how FANUC describes it).
  - **The peck loop** is a label region of its own (`loop = 2`), numbered with the others.
  - **Peck parameters:** d83 and d73 are machine parameters, so `-fparam d83=`/`d73=` (program units)
    must be given. `-fparam default` does not cover them.
  - **Refused:** G91 cycles; K/L repeats; G84-G89 and G76 (tapping, boring); a block that drills in one
    pass and moves in another.
- **Tools:**
  - `T` is written with `M6` as `T<n>M6`, using the tool the evaluator saw in the spindle at that M6 (an M6
    that changes to different tools in different passes is refused). A `T` without `M6` is left out.
  - **G43 H / G49:** left out, with a NOTE: the DX32 applies the length of the tool in the spindle from its
    table (TENTATIVE). G43 with an H other than that tool is refused.
- **Axis words with no G0-G3 in force** (after G80, or before the first motion code) are refused, because
  FANUC's motion there depends on the control (power-on G0 or G1 is a parameter).
- **Not translated yet**, refused with the reason:
  - G68 rotation, G51 scaling;
  - G28/G30, G10, G52, G53;
  - G84-G89, G76.


### 3.13 Phase 6 as built: `-check` and `-against`

`src/fbcheck.c`. `fb2dx PROG.NC -w OUT.TXT -check` writes the program, then checks it with dxsim, which runs
as a separate program (`system()`; only the files it writes are read, so it works under COMMAND.COM):
1. `dxsim -lint OUT.TXT`: every error is reported, and the result is a STOP.
2. `dxsim OUT.TXT`: the moves in its trace are compared, in order, with the FANUC tool path.
   - **The FANUC tool path** is recorded by the evaluator as it runs: lines, arcs (with the centre, for G17
     I/J arcs), and the drilling holes as `fbout.c` writes them out. It is kept in inches, as dxsim
     prints it.
   - **The DX32 program has to reach every FANUC end point in order.** The only extra moves allowed are the
     two halves of a G0 that the DX32 splits into Z and XY: (the previous XY, the new Z) or (the new XY,
     the previous Z). A G17 arc must also have the same centre (tolerance 0.00015 in, since dxsim prints
     4 decimals).
   - **A difference** is reported with the FANUC line and the line of the output, and the result is a STOP
     ("do not run it").
   - **dxsim's warnings** (for example a feed with no M3) go into the report as NOTEs.

- **dxsim** is looked for next to `fb2dx`: `dxsim`, or `DXSIM.EXE` and then `DXSIM32.EXE` on DOS/Windows.
  `-dxsim PATH` overrides it.
- **`-against FILE`** compares an existing DX32 program, e.g. one edited by hand after translation, with
  the FANUC one, in the same way.
- **Skipped**, with a NOTE: when the unit system of some moves is not known, or the program makes more
  than 16000 moves.

### 3.14 Shop programs: safety lines, argument checks, alarms, G28

What typical FANUC jobs contain, beyond the phases above:
- **Safety lines** (`G17 G21 G40 G49 G80 G90`):
  - G40 is written as G40; G49 is left out (3.12).
  - G15, G50, G50.1, G64, G69 and G13.1 are left out with a NOTE: what they cancel is refused anyway, so
    there is nothing active to cancel.
- **Argument checks**, e.g. `IF [#18 EQ #0] THEN #3000=1 (NO RADIUS)` or `IF [#2 EQ #0] GOTO 900`.
  - **Why they can be folded:** the DX32 has no vacant value. But a program that translates has no
    inputs from outside: system variables are refused, and an unset variable that is read is caught. So
    the evaluator's run, which goes through every call in the input, is the run the machine will make.
  - **How:** the evaluator counts, for every IF and WHILE, how often its condition held and how often it
    didn't. A condition that tests a vacant variable and always came out the same is written as that
    constant, with a NOTE:
    - an IF..THEN that is always true is written as its assignment;
    - one that is always false is left out;
    - an IF..GOTO that is always true becomes a GOTO;
    - one that is always false is no jump.
  - **Refused:** a test that came out both ways (a macro called once with the argument and once without),
    one that never ran, and one that decides a WHILE.
- **Alarms:** `#3000 = n (MESSAGE)` becomes a comment with the number and the message, then `M2`, because a
  FANUC alarm ends the run. `#3006 = n (MESSAGE)` becomes the comment and `M0`. The DX32 shows no message:
  the comment is there for whoever reads the program. The evaluator stops at a #3000 alarm, as the control
  does. The message is the first comment in the block (the reader keeps it).
- **`G91 G28 Z0`** (Z to the reference point) needs `-g28`:
  - `m25` writes the DX32 quill home, M25 (TENTATIVE: check that it goes where FANUC's reference point
    is);
  - `drop` leaves it out, so Z stays where it is.

  Any other G28 is refused. After a G28 the evaluator treats those axes as not known, so a G90 arc or a
  G98 cycle that needs them is refused until an absolute move.

### 3.15 Cutter compensation (G40/G41/G42)

- **How the two controls start compensation:**
  - **DX32** (`CUTCOMP.md`, traced in the 6.89 firmware): the G41/G42 block moves to its point
    uncompensated. The move after it is the approach, which ends on the normal to the element after that.
  - **FANUC start-up type A** (parameter 5003 bit 0 SUP = 0, the factory setting): the G41 block itself is
    the approach, and it ends on the normal to the next block's start.
- **The plain copy gouges.** Copying the G41 block as it is makes the profile's first side the DX32
  approach, cut uncompensated. With a 10 mm tool on a 50 mm square, that side was cut at X0 instead of
  X-5: a gouge of the tool radius, seen in dxsim.
- **So fb2dx writes `G41`/`G42` on a zero move of its own**, just before the FANUC G41 block's move:
  `G91G41X0.` then `G90` (in G91: `G41X0.`; a G0/G1 of the FANUC block is copied into it). That move is
  then the DX32 approach and ends where FANUC type A ends. The zero move is needed: the DX32 acts on
  G41/G42 only in a block with an axis word (0x1cf64, `OPENQ.md` A13). A bare `G41` line, as fb2dx wrote
  first, leaves the turn-on pending, so FANUC's start-up move is made uncompensated and the profile's
  first side becomes the approach: the same gouge as the plain copy.
- **Cancel:** G40 ends the same way on both controls: the last element ends on its normal, then the G40
  block moves uncompensated.
- **Start-up type B** (SUP = 1) starts differently. It is refused unless `-comp dx32` accepts the type-A
  approach. `-fparam sup=0|1` gives the type; `-fparam default` takes type A.
- **D:**
  - The D number must be the tool in the spindle (the evaluator checks it, as for G43 H), because the
    DX32 takes the diameter from its tool table for that tool.
  - D is not written.
- **Outside corners:** the DX32 rounds them with blend arcs (G49, its reset state). FANUC's corner moves
  differ, but only in the air off the part.
- **`-check`** runs dxsim without a tool table, so the diameter is 0, the compensated path is the
  programmed one, and the end points still have to match. dxtest also runs the job with a 10 mm tool and
  checks the offset corners and that the first side is not cut at X0.

### 3.16 More drilling: G91, K/L repeats, G85 G86 G89

- **G91 cycles** follow FANUC:
  - X Y are steps from the last hole;
  - R is measured from the initial level, and Z from R.

  fb2dx writes each hole in G90, with absolute values worked out from the kept position (X20, Y20) and
  the initial level, then switches the DX32 back to G91. A G91 hole needs the X/Y position known. A cycle
  where G90/G91 changes while it is on is refused, because the stored R and Z would mean something else.
- **Repeats:** the block is written out K times, at most 50. In G91 that steps X Y each time; in G90 it
  drills the same spot again. K0 only stores the values.
  - **The letter depends on the series** (TENTATIVE): K on 16, 0i and 30i, L on 0, 6, 10 and 15. When
    the candidate series don't agree, `-series` is needed.
  - A K in a drilling block is a count, so the decimal-point rule (DPI) does not apply to it.
  - Repeats with G83/G73 are refused for now.
- **Boring cycles** (motion only):
  - G85: feed in, feed out to R;
  - G89: the same, with the P dwell at the bottom;
  - G86: feed in, M5, rapid out, then M3 (TENTATIVE: FANUC restarts the spindle in the direction it had).

  Then G98/G99 as for the other cycles.
- **Still refused:** G84/G74 tapping (the spindle has to reverse in step with the feed), G76/G87/G88.

### 3.17 Rotation and local shift (G68, G69, G52)

- **The DX32 side:** the DX32 rotates about X0 Y0 (`G73 A`, cancelled by `G72`) and has a fixture offset
  (`G97 X Y Z`, from the base system). It applies rotation first and the offsets last
  (`../LANGUAGE.md` 12; dxsim does the same).
- **The FANUC side:** G68 X cx Y cy R a rotates about (cx, cy), and G52 shifts the local coordinate
  system. So:
  - FANUC's point is c + R(p − c) + s = R p + (c − R c + s);
  - fb2dx writes `G73 A a`, then `G97` with X = s.x + c.x − (c.x cos a − c.y sin a) and
    Y = s.y + c.y − (c.x sin a + c.y cos a), Z = s.z.
- **State:** the angle, the centre and the shift live in six pool variables, set to 0 at the top. G69
  writes `G72` and clears the rotation; G52 sets the shift for the axes it gives (TENTATIVE: the others
  keep theirs).
- **In the evaluator:** the same transformation is applied to the FANUC tool path, and the program
  coordinates of the tool are recomputed when it changes, because the tool doesn't move. `-check`
  therefore compares dxsim's transformed path with FANUC's.
- **After G68, G69 or G52, X/Y count as not known** until an absolute move. G90 arcs and G91 holes, which
  need the position, are refused until then.
- **Refused:**
  - G68 without X, Y and R (FANUC then takes the current position);
  - G68 outside G17, and in G91;
  - R without a decimal point (its angle unit is not known);
  - G51 scaling.

## 4. Errors and warnings (the report)

One line per finding: input line, output line, kind, text.

| Kind | Examples |
|---|---|
| `CHECK` | `-check`/`-against`: dxsim reads the program cleanly and reaches every FANUC end point in order |
| `DIALECT` | the series decided on and why, or the option that would settle it (3.7) |
| `REFUSED` | `line 40 uses G65 H02 (Macro A only), line 12 uses ATAN[a,b] (30i only)`; `#5061 (probe skip position): system variables don't translate`; `#0 vacant: no DX32 equivalent`; `GOTO 20 jumps into a WHILE loop`; `8 backward targets, 7 labels free (canned cycles use L8, L9)`; `X10 without a decimal point: give -fparam dpi= (3.7)` |
| `NOTE` | `the output needs BOSS 4-7 compatibility mode off`; `FIX built from LSR and a correction (TENTATIVE, bench/BFIX.TXT)`; `EQ on a computed value`; `the output needs the PPPL option (param 285 bit 4)` |
| `MAP` | variable and macro numbers |
| `VACANT` | a vacant variable read where it counts as 0 (evaluator) |
| `FILE`, `RESULT` | the input files, and `ok` or `stopped` at the end |

A refused line stops the translation of that program. Nothing is written for it, so there is no half-translated
output.

## 5. Tests

- **Dialects:**
  - one program per construct in the first table of 3.7, checked to detect the right set of series;
  - one program per meaning item in the second table, checked to stop without its option and to
    translate with each setting;
  - a mixed program, and `G65 H.. P#..` without `-series`;
  - a Macro A program for each H operation.
- Hand-written FANUC programs, one per row of the tables in section 3 and `../ROADMAP.md` section 5, each with
  the expected DX32 output and an expected report.
- One program per `REFUSED` reason.
- **Rotation:**
  - `fbrot` (a rectangle with an I/J arc, rotated 30 degrees about X50 Y20, then a G52 shift) translates with
    `-check`.
  - `fbxrot.txt`, the same output with the angle's sign flipped, is refused at the first rotated move.
  - `fbrotx` checks the refusals.
- **More drilling:** `fbrow` (a G91 G81 row with K5, then G85, G89 and G86) translates with `-series 16` and
  `-check`. dxtest checks its 8 holes and the two feeds back out to R. Without `-series`, K is refused.
- **Cutter compensation:**
  - `fbcomp` (a square profile with G41 D3) translates with `-fparam default` and `-check`, and in dxsim with a
    10 mm tool its sides are offset by 5 mm with no gouge.
  - Without the start-up type it is refused, and with type B it needs `-comp dx32`.
- **Shop programs:**
  - `fbjob` is a typical job: a safety line, `G91 G28 Z0`, `T1 M6`, `G43`, two G65 bolt circles drilled with G81,
    and argument checks with `#3000` alarms. It is translated with `-check`; without `-g28` it is refused.
  - `fbvarg` has an argument check that comes out both ways, and is refused.
- **Phase 6:** every translating entry in `fb.lst` runs with `-check`, and its expected report holds the CHECK
  line. `fbxcen.txt` (one arc centre moved) and `fbxend.txt` (one arc end point moved) are broken copies of the
  `fbarc` output; `-against` must refuse each of them at the right FANUC line.
- **Phase 5, drilling:** `fbdrill` (G81 with G99/G98, G82 with a dwell from a variable depth, G83, G73, two
  tool changes with G43) is translated and run in dxsim. dxtest checks the six holes' positions and depths, the
  three G83 pecks and the return levels. `fbdrillx` checks the refusal without d83/d73.
- **Phase 5, arcs:** `fbarc` is translated and run in dxsim. Its end points are compared with the FANUC
  evaluation, and its arc centres with values worked out by hand. `fbarcx` checks the refusals (unknown
  position, R below 0, helix).
- **Phase 4, built:** `fbbolt` (G65 with arguments), `fbcall` (M98 sharing the caller's locals, G65 inside a
  loop, L repeats, an early M99 inside an IF..GOTO region) and `fbexit` (exit flags) are translated, run in
  dxsim and compared with the FANUC evaluation.
- **Phase 3, built:**
  - `fbloop` (nested WHILE) and `fbgoto` (a backward GOTO loop, forward skips, breaks out of WHILE and DO) are
    translated and run in dxsim, and their end points are compared, in order, with the FANUC evaluation.
  - `fbjmpx` checks the refusals.
- **Phase 2, built:** `fb.lst` entries with `-w` compare the written DX32 program too (`fbstr`, `fbfix`, `fbatan`
  with nat=1, and refusals for `fbthen` and `fbbolt`). `dxtest` runs the written programs in dxsim and checks
  `FIX`/`FUP`/`ROUND` on 15 values (halves, negatives, 0.4999999) and `ATAN`/`ASIN`/`ACOS` in every quadrant
  against values worked out in C.
- **Phase 1, built:** `tests/fb.lst` and `tests/fb*.nc` (16 runs: detection, each `-fparam` stop, Macro A,
  vacant variables, `M98`, specification II, a read error), and 4 hand checks in `dxtest` (bolt circle
  positions, the Macro A loop, specification II numbering, `M98 P30100`).
- Public FANUC examples: bolt circles, rectangular pockets, a family-of-parts program, an engraving macro.
- `FIX`/`ROUND`/`FUP` checked over a table of values (whole numbers, halves, negatives, the values next to
  them) by running the output in dxsim.
- Phase 6 runs all of them through `dxsim -lint` and the path comparison.

## 6. Phases (checklist)

- **0** ☑ `*` in expressions (fact 1) ☑ rounding of the 32-bit functions (fact 2, dxsim changed, `BFIX.TXT`)
  ☑ arcs, word values, units, BOSS 4-7 mode (facts 12-16, from `../LANGUAGE.md`)
- **1** ☑ dialect table `src/fbdial.c` and `-series list` ☑ detection, `-series`, `-fparam` ☑ Macro A reader
  ☑ reader (blocks, `( )` comments, `/` block delete, `%`, `O` numbers) ☑ expression parser ☑ statements
  ☑ evaluator with vacant tracking
- **2** ☑ variable pool and map ☑ PPPL writer ☑ `FIX`/`FUP`/`ROUND`/`ATAN`/`ASIN`/`MOD` expansions ☑ word carriers
  ☑ vacancy checks from the evaluation
- **3** ☑ jumps and loops found (`structure`) ☑ structure check (`check`) ☑ `?`/`!` and label writer
  ☑ jumps out of a loop to other blocks (exit flags)
- **4** ☑ `M98`/`M99` (early returns too) ☑ `G65` with arguments and L ☑ depth and recursion checks ☑ labels over the call tree
- **5** ☑ arcs (G90 absolute centres from a tracked position) ☑ dwell ☑ G81/G82/G83/G73 (as moves) ☑ T, M6, G43/G49
- **6** ☑ `-check` (lint, then the tool path) ☑ `-against` ☑ tests that a wrong program is caught

## 7. Open questions

| ID | Question | How to answer |
|---|---|---|
| Q1 | Fact 2 on the machine | **B:** `../bench/BFIX.TXT` (`OPENQ.md` L8) |
| Q2 | What LSR returns for a negative argument: unsigned 0..2³²−1 (dxsim) or signed | **F:** what 0x25d66 does with d1 after the shift (the conversion back to a float). Only matters for refused values today |
| Q3 | Does G70/G71 convert the values already in variables, or only the numbers read after it? | **F:** the G table entries 70 and 71. Matters only for a program that changes units in the middle, which fb2dx refuses until then |
| Q4 | What happens to a full circle with `R`, or an `R` arc over 179.998° | **F:** 0x1f992. fb2dx writes those with the centre anyway |
| Q5 | ~~Which FANUC models to target first~~ | **Answered by the owner:** all of them, with the series detected or given (section 3.7) |
| Q6 | Every `?` cell and every TENTATIVE cell of the two tables in 3.7, including the parameter numbers, and the TENTATIVE rules in 3.8 | The FANUC operator manuals ("Custom Macro" chapter) for each series. Copies exist online (3.7) on hosts the session's network policy blocks: allowing them, or PDFs in `../manual/`, settles this |

## 8. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-28 | (this commit) | G41/G42 written on a zero move, `G91G41X0.` then `G90`: the DX32 acts on G41 only in a block with an axis word (`OPENQ.md` A13), so the bare `G41` line left FANUC's start-up move uncompensated (a gouge of the tool radius, now shown by dxsim) |
| 2026-10-28 | d6c1d47 | 16-bit DOS review (Watcom could not be run here): the FANUC tool path is kept in 512-move chunks (no object over 64 KB; the static 16000-move copy in fbcheck.c is gone); caps of 16000 statements and 16000 output lines; a 16 KB stack for FB2DX.EXE (`-k16384`); user text in messages limited to 200 characters; `pre[]` holds two lines a word (186 passed) |
| 2026-10-28 | a0e7978 | Rotation and shift (3.17): G68/G69/G52 as G73 A, G72 and G97, applied to the FANUC tool path too; tests `fbrot`, `fbxrot`, `fbrotx` (186 passed) |
| 2026-10-28 | d7f5113 | More drilling (3.16): G91 cycles, K/L repeats by series, G85 G86 G89 written out; test `fbrow` (181 passed) |
| 2026-10-28 | 49b47ab | Cutter compensation (3.15): G41/G42 on a block of their own so FANUC's start-up move is the DX32 approach (type A), D checked against the tool, `-fparam sup`, `-comp dx32` for type B; the gouge of a plain copy found in dxsim and tested against (176 passed) |
| 2026-10-28 | 36ae51d | Shop programs (3.14): safety-line cancel codes, G40, argument checks folded from the evaluation, #3000/#3006 alarms, `-g28 m25|drop`; test `fbjob` (169 passed) |
| 2026-10-28 | 788551b | Phase 6: `-check` and `-against` (`src/fbcheck.c`), the FANUC tool path recorded by the evaluator, broken outputs caught in tests (163 passed). All six phases built |
| 2026-10-28 | 9a5323e | Phase 5 done: G81/G82/G83/G73 written out as moves with a peck loop, -fparam d83/d73, T/M6 from the evaluator, G43/G49 left out, axis words with no motion code refused (161 passed) |
| 2026-10-28 | 8872bf5 | Phase 5, arcs: modal state per block from the evaluator, G90 centres from X20/Y20/Z20, R arcs, G18/G19, dwell, G54-G59 G80 G94 (155 passed) |
| 2026-10-28 | 1408751 | Phase 4 built: G65/M98 as DX32 macros per program and level, caller-side arguments, early M99, labels over the call tree; exit flags for jumps out of nested structures; dxsim path checks for fbbolt, fbcall, fbexit (149 passed) |
| 2026-10-28 | ff2da1d | Phase 3 built for the main program: WHILE/DO/END, GOTO, IF..GOTO as labels and ?/! regions, checked before writing; breaks out of loops; dxsim path checks (142 passed) |
| 2026-10-28 | 7101cae | Phase 2 built for straight-line programs: `-w`, `src/fbout.c`, variable pool and carriers, PPPL expressions, `FIX`/`FUP`/`ROUND`/`ATAN`/`ASIN`/`MOD` expansions checked in dxsim, vacancy refusals (134 passed) |
| 2026-10-27 | da366b4 | Phase 1 built: `fb2dx` reads, detects the series, evaluates (`-eval`, `-parse`, `-series list`); tests `fb.lst` and 4 hand checks (122 passed) |
| 2026-10-27 | 9689d77 | All FANUC series (owner's request): section 3.7, dialect table, series detection, `-series`, `-fparam`, Macro A; M98 shares the caller's locals; Q5 answered, Q6 added |
| 2026-10-27 | ebcc885 | Document created. Phase 0: `*` invalid in PPPL (0x262a6 → 0x24fba); the 32-bit functions round (fmove.l, default FPCR), dxsim changed from truncation, `bench/BFIX.TXT`, test `bfix`; exact `FIX`/`FUP`/`ROUND` built on `LSR` with a correction; word values need same-letter variables, G90 arc centres are absolute |
