# DX32 part program and macro language reference

This is the language the BMDC card interprets: Bridgeport's BOSS word-address G-code plus the
**PPPL / 3PL** parametric extension (variables, expressions, conditionals, labels), macros and loops.
It is the specification `dxsim` implements.

**Sources**

| Tag | Source |
|---|---|
| **M** | *DX32 Programmer's Manual*, Bridgeport, February 1994, code 11042638 rev A (`manual/DX32 Programmer's Manual.pdf`); chapter numbers below refer to it |
| **O** | *DX32 Operator's Manual*, October 1997, code 1104-2832 rev A (`manual/DX32 Complete Op Manual.pdf`), appendix D error messages |
| **F** | Firmware **6.89** (repo-root `BMDC.BIN`), traced at the addresses given. The manual predates 6.89; where they differ the firmware is what the machine does |
| **C** | The built-in cycle text of 6.89 (`corpus_689.txt`, 628 non-empty blocks), the language as Bridgeport itself wrote it |

**Confidence**: **CONFIRMED** = stated by the manual and consistent with the firmware, or traced in the
firmware; **TENTATIVE** = inferred, not traced end to end. `dxsim` follows the firmware where the two differ
and says so in its trace.

All addresses are 6.89 file offsets = 68030 addresses (`../decompiled/BMDC.asm`, and `tools/m68k_dis.py` in this
directory: the repo's capstone disassembler extended to decode 68882 instructions and FBcc itself, since
capstone gets their length wrong and loses sync).

---

## 1. Program structure

| Rule | Source | Status |
|---|---|---|
| A program is a sequence of **blocks**, one per line, ended by CR (the manual prints the end of block as `;`). The card stores them NUL-terminated; the loop at 0x1ed12 ends a block at NUL or 0x80 | M ch.2, F | CONFIRMED |
| At most 132 characters per block including CR | M ch.2 | CONFIRMED (manual; not enforced by `dxsim`) |
| `;` starts a comment to the end of the block (0x2031a) | M, F | CONFIRMED |
| `'`, `"` and `` ` `` start a message that runs to the matching quote (0x20376). `'` messages are shown at M0/M1/M6/M20/M21/M26 (PPRINT) | M ch.9, F | CONFIRMED |
| `:n` program number; the rest of the block is ignored (0x202b6) | M, F | CONFIRMED |
| `Nn` sequence number (1-16000000), first word of the block; also a loop end marker | M ch.2 | CONFIRMED |
| `/` as the first character: block delete, skipped when the DELETE option is on | M ch.2 | CONFIRMED |
| `.` as the first character: definition block, also executed during a program search | M ch.2 | CONFIRMED |
| Letters are case-insensitive (`and #$df`). `dxsim` folds to upper case outside comments and messages | F | CONFIRMED |
| Blanks are skipped inside numbers and expressions (0x23b4e, 0x260c4), but the assignment test looks at fixed offsets (section 5), so `X1 = 5` with blanks is not an assignment on the card. The manual's examples are typeset with blanks; `dxsim` removes blanks on load (`-keep` turns that off) | M, F | TENTATIVE (what BXX does on load is not traced) |
| Unknown characters, digits, `+ - . * @` outside a word are ignored (0x1f084, 0x1f086, 0x20464, 0x20466) | F | CONFIRMED |

Word order inside a block matters: the card decodes left to right (`N G X Y Z ... F M`). For example a
G-code that changes the meaning of later words (G73, G92, G81...) must come before those words (M ch.1, F).

## 2. Words and numbers

Address table (M table 2-1, 2-2); inch formats, metric `+4.3` for `+3.4`:

| Address | Meaning | Format / range |
|---|---|---|
| `:` | program number | 5 digits |
| `#` | macro (subroutine) number | 1-99 (manual), 1-200 (F, table 0x3820) |
| `L` | label | 1-9 (10-29 inside an M-macro, F 0x1f4aa) |
| `N` | sequence number | 1-16000000 |
| `G` | preparatory function | 0-199 |
| `X Y Z` | axis position, absolute or incremental (G90/G91) | ±3.4, ±8388.607 |
| `U V W` | incremental X, Y, Z move, whatever G90/G91 says | ±3.4 |
| `I J K` | arc centre / pole centre | ±3.4 |
| `R` | arc radius, pole radius, cycle radius | 3.4 |
| `A` | polar angle (longitude), degrees, absolute or incremental | ±3.3 |
| `B` | incremental polar angle | ±3.3 |
| `E` | colatitude (spherical coordinates) | 3.3 |
| `D` | tool diameter (`T_D_`) | ±3.4 |
| `P` | cycle parameters | ±3.4 |
| `Q` | dwell time (s), cycle parameter | 3.2, .01-327.68 |
| `F` | feed rate, in/min or mm/min (G95: per revolution) | 3.1, .1-250 ipm |
| `S` | spindle speed | 4 |
| `T` | tool number, `T n/tlo/dia` | 1-24 |
| `M` | miscellaneous function, one per block | 0-99 (more on 6.89) |
| `H` | spindle position in degrees (M12) | |

**Numbers** (0x23b2c, M ch.2): `[+|-]digits[.digits]`; `,` is also accepted as the decimal point (F).
Leading zeros are skipped and do not count as digits. At most 17-18 digits ("Too Many Digits", O).
**Decimal points are required** on dimensional words (M ch.2 note 5) because of the variable rule below.
In **BOSS 4-7 compatibility mode** (0xaef9 bit 2) a number without a point is scaled: /10000 inch,
/1000 metric (0x23cf8), and variable references are off except in the built-in text (0x23eb8).

### The variable-reference rule (CONFIRMED, M ch.10 + F 0x23eb2)

A word whose value is written as a **positive 1- or 2-digit integer without a decimal point** names a
variable, if the index is in range. `X1` is the value of variable X1; `X1.` is one inch.

* M: "A, B, C, D, E, F, I, J, K, P, Q, R, U, V, W, X, Y, Z words without a decimal point are assumed to be
  variables. ... The range of variable subscripts is 1 to 9."
* F 6.89: index 1-19 for every letter, 1-50 for A C D F I J K R X Y Z (second bank at 0x23f0a). Leading
  zeros vanish, so `X01` is X1 and `X0` is the number 0. A negative value (`X-1`) is always a number.
* G, M, N, L, T, `#`, `%` values never name variables (their handlers do not call 0x23eb2).
* The manual's own examples sometimes drop a point (`Y5`, `J2` in figure 5-8); on the card those read
  variables. `dxsim` does the same.

## 3. Variables (CONFIRMED)

| Item | Source | |
|---|---|---|
| Names: letter + index. Each letter has a 228-byte record at **0x4be0 + n·0xe4** of 19 extended floats (index 1-19); A C D F I J K R X Y Z have a second record for 20-50 (X 0x65f0, Y 0x6764, Z 0x68d8, F 0x647c, C 0x6308, R 0x6a4c, A 0x6bc0, I 0x6d34, J 0x6ea8, K 0x701c, D 0x7190) | F | CONFIRMED |
| Manual: indices 1-9 only; "G H L M N S T should not be used as variables" | M ch.10 | older rule; 6.89 allows them in assignments |
| Values are 68882 extended precision (`dxsim`: double). Comparisons are exact: "care should be taken when testing for <EQ>" | M, F | CONFIRMED |
| All variables are global; there is no local scope. Macros pass values with `*` (section 9) | M, F | CONFIRMED |
| **Variables used by the built-in cycles** (M table 10-1): A1 A2, B1-B4, F1-F3, I1-I4, J1-J4, L8 L9 (labels), P1-P7 P9, Q1 Q2 Q9, R1-R4, U1-U7, V1-V7, W1 W3-W8, X1-X3, Y1-Y7, Z1-Z5. A cycle may change them. 6.89 also uses P8, Q7, Q8, C9, Z6, W9 | M, C | CONFIRMED |
| R1 = current tool radius after a milling cycle is called (F: 0x20990 loads it at G170-G179) | M, F | CONFIRMED |

## 4. Expressions and assignment (PPPL)

### Assignment

```
letter digit [digit] "=" expression        e.g.  X1=X1-1.   P6=P6-.001!   W8=Z3
```

* **A block is an assignment when its third or fourth character is `=`** and the first is not `$` or `;`
  (0x1ed88; M: "the third character in an expression block must be an `=`"). The evaluator (0x24ebc)
  reads to the first CR, `!`, `;` or end of block (0x260a0), so a conditional can end on an
  assignment block: `?V4<GT>0` ... `V4=Y4!`.
* An assignment anywhere else in a block is **not** recognised (see open question 1).
* **Licence:** in a user program an assignment needs the **PPPL option**, param 285 bit 4 (0xc519
  bit 4, 0x1edbc); otherwise it traps. Inside the built-in cycles and inside M-macros (0x4088 bit 2) no
  licence is needed. `dxsim -nopppl` simulates a machine without the option.
* Errors (O, appendix D): `PPPL: Bad destination variable letter`, `... index`, `Cannot find equals sign`,
  `Expected Operator`, `Improper Function call`, `Bad PI`, `Syntax Error: Missing Value`,
  `2-Argument Functions Require Parentheses`.

### Grammar (F 0x24fc8, 0x2500a, 0x25042)

```
expr   := term   { ("+" | "-") term }
term   := factor { ("*" | "/") factor }
factor := number | variable | "(" expr ")" | "-" factor | "+" factor | "#PI"
        | func1 factor                       ABS(V3)  SQR2  COS(A1/2.)
        | func2 "(" expr "," expr ")"        DTF(X1,Y1)
```

Usual precedence, left to right: `2-3-4` = -5, `10/4*2` = 5. Unary minus binds to the next factor. A
one-argument function applies to the **next factor**, so `ABS(V3)-Y7` is `|V3| - Y7` and `SQR2` is √2.
The manual recommends parentheses around every sub-expression (ch.10). (CONFIRMED)

### Functions

Name table 0x26302 (4-byte entries), handlers from 0x253f2. The manual lists only SIN COS SQR TAN ATN ABS
DTF and #PI; 6.89 has 36.

| Function | Meaning (68882 op) | Status |
|---|---|---|
| `ABS SQR` | absolute value, square root | CONFIRMED |
| `SIN COS TAN` | argument in **degrees** | CONFIRMED (M, F) |
| `ATN ASN ACS` | result in degrees; ATN in -90..+90 | CONFIRMED |
| `LOG ALG` | log10, 10^x | CONFIRMED (F) |
| `LGE E2X` | natural log, e^x | CONFIRMED (F) |
| `HSN HCS HTN` | sinh, cosh, tanh of (x·π/180) | CONFIRMED (F) |
| `HAT` | atanh, result ·180/π | CONFIRMED (F) |
| `#PI` | 3.14159265 (FMOVECR #0) | CONFIRMED |
| `DTF(a,b)` | √(a²+b²) | CONFIRMED |
| `NOT(x)` | 32-bit NOT; `FFB(x)` = number of the highest set bit | CONFIRMED (F) |
| `XOR AND LOR (a,b)` | 32-bit xor, and, or | CONFIRMED (F) |
| `LSR LSL ROR ROL (n,v)` | shift/rotate **v by n**: the first argument is the count (FP7/FP6 order at 0x253c8) | CONFIRMED (F) |
| `SET CLR TST (n,v)` | set / clear / test bit n of v | CONFIRMED (F) |
| (all 32-bit functions) | arguments are converted with `fmove.l fp,dn` under the default FPCR: **rounded to nearest, ties to even**, not truncated (`LSR(0,2.7)` = 3, `LSR(0,2.5)` = 2, `NOT(-1.5)` = 1). Values ≥ 2³¹ have 2³¹ taken off first (LSR 0x25d66, NOT 0x25aa6); FPCR is only changed inside library routines (0x374d6+) | TENTATIVE (F); `bench/BFIX.TXT` |
| `MRG(a,b)` | (a and mask) or (b and not mask) (0x25b5c). The mask is entry 64 of the variable table below (0x764c): 0 at power-up, so `MRG(a,b)` = b until a program sets it with `WRI(64,m)` | CONFIRMED (F) |
| `RDI(n) GET(n)` | read entry n of the variable table below: RDI as a signed 32-bit number (entries 55 and 56 as reals), GET as unsigned (0x255a8, 0x2566a) | CONFIRMED (F); `dxsim -i n=v` gives the value an entry holds at the start |
| `RDP(n) WRP(n,v)` | read / write user byte n at 0xac06[n-1] | CONFIRMED (F) |
| `WRI(n,v) PUT(n,v)` | write v to entry n if the entry is writable, and return v (signed / unsigned); a read-only entry is left alone (0x25724, 0x259b8) | CONFIRMED (F); `dxsim` traces each write as `IO` |

### The variable table (`RDI GET WRI PUT`, CONFIRMED F)

The four functions do not address I/O channels: n picks an entry of a table of 151 (address, type) pairs
at 0x263a2, and the function reads or writes the 32-bit word at that address. n is rounded; below 0 or
above 150 it becomes 150. Type 1 entries can be written, type 0 entries only read. Types 2 and 3 would
raise `PPPL: User IO BUT User AUF not operating` when the user AUF is absent (0x32a4/0x32a8 = 0xbad),
but no entry in 6.89 has them, so that message cannot occur. Reading or writing entries 0-19 first
refreshes the AUF copies (0x30b8e: 0x3248 → 0x3220, 0x324c → 0x3224, 0x31f4 → 0x3218, 0x31f0 →
0x321c).

| n | Address | W | What it is |
|---|---|---|---|
| 0, 1 | 0x3220, 0x3224 | | AUF inputs: the long-RX primary and secondary words (`../AUF_PACKET_FORMAT.md`) |
| 2-9 | 0x3228-0x3244 | | not identified |
| 10, 11 | 0x31f0, 0x31f4 | yes | AUF outputs: the secondary and primary output words. Whether a written bit survives the next servo tick is open (OPENQ L9) |
| 12-19 | 0x31f8-0x3214 | yes | not identified |
| 20-34 | 0xfd8-0x1010 | | machine parameters 17 onward (axis scaling, section 14a) |
| 55, 56 | 0xae2e, 0xae3a | | reals: RDI returns them as they are; GET reads the first 4 bytes |
| 64 | 0x764c | yes | the `MRG` mask |
| 65, 68, 148 | 0x01200554, 0x01200558, 0x01200738 | yes | dual-port RAM words shared with the PC |
| 78-81 | 0xaf34, 0xaf10, 0xaf38, 0xaf14 | | spindle range 1 and 2 minimum and maximum (OPENQ A5) |
| 93 | 0xaef8 | yes | machine flags (bit 2 probe, bit 7 FPU flag, bit 6 zeroes C9 in the G81 driver) |
| 102-105 | 0xc5a0-0xc5ac | yes | WRI also appends the low byte to a queue (0xc548, 32 bytes; 0xc569, 16; 0xc57a, 20; 0xc58f, 16); 255 is not queued; a full queue raises the message at 0x33b27, `Number of AND operands has exceeded the max.` |
| 137 | 0x765c | yes | WRI also appends the byte to a 256-byte buffer at 0x7660; 255 clears it |
| 144 | 0x3c60 | | the block-search target |
| 149 | 0x765c | yes | WRI also builds a three-part message at 0x33c79 (first write: `LO/HI` or `HI/LO` by v = 1; then two numbers) and copies it to 0x99e0 |
| 135, 138-140 | 0 | | address 0 (the reset stack pointer) |

The writable flag of every entry is in `uiowr()` in `src/dxexpr.c`. `dxsim` keeps the values written in the run (a later RDI/GET returns them, and the
`MRG` mask is entry 64), starts each entry at its `-i` value or 0, and does not model the queues,
the buffer at 0x7660, the message of entry 149 or what the AUF does with entries 10 and 11.

## 5. Conditionals (CONFIRMED, M ch.10 + F)

```
"?" variable "<" op ">" (number | variable)      op: EQ NE LT LE GT GE
...blocks...
"!"
```

* `?` reads the left variable (0x1ffc6, stored at 0x3cec), `<` the two operator letters (0x20010),
  `>` the right side and compares (0x2021c, table "EQNELTLEGTGE" at 0x20256). Exact `FCMP`.
* The left side must be a variable, the right side a number or a variable; no expressions.
* **If the condition is false, everything up to the matching `!` is skipped**: the skip counter 0x32f6
  counts `?` up and `!` down (0x1ed44), across any number of blocks, so conditions nest (M: "Conditionals
  may be nested as many times as required"). A true condition does nothing; its `!` is a no-op.
* The skipped part can end in the middle of a block; the rest of that block executes.
* Quirk (F): a right side written as a bare 1-2 digit integer goes through the variable rule with the
  **left-hand letter**: `?Q1<LT>5` compares Q1 with Q5. Write `5.` for the number. TENTATIVE (follows from
  the code; the manual and the built-in text always use a point or `0`).
* `Parser: Illegal Condition Code` for an unknown operator (O).

## 6. Labels and jumps (CONFIRMED)

| Syntax | Meaning | Source |
|---|---|---|
| `Ln` | first word of a block: defines label n (1-9) at that block. Inside an M-macro 10-29 are allowed | M ch.10, F 0x1f48e |
| `=Ln` | jump back to label n (an undefined label: `Parser: Call Of Undefined Loop Or Macro`) | M, F 0x1ca36 |

Labels are one global table (0x3c74): a macro that defines L1 overwrites the caller's L1. The built-in
cycles use **L8 and L9** (M: "Do not use these labels when looping back over blocks containing milling
and drilling canned cycles").

## 7. Repetitive programming: `=N` loops (CONFIRMED, M ch.10)

```
=Nseq/count      run the following blocks up to and including block Nseq, count times (1-16384)
```

How the firmware does it (CONFIRMED, F):
- **Starting a loop:** the `/count` handler (0x20468, with the `=` and N flags from the N handler 0x1f732)
  pushes the loop onto a stack of five entries at 0x3b88:
  - the end N (0x3b8c);
  - the passes left (0x3bd4; a count below 1 counts as 1);
  - the return point (0x3bf8);
  - the macro parameter pointer (0x3bb0).

  It also clears the block's own N flag, so the `=N` block itself does not end a loop.
- **Too deep:** with five loops open, the push traps (0x204a6) with `Parser: #14 Internal Addressing Error`.
  **6.89 allows five levels**; the manual says four. dxsim follows the firmware (tests `eloop5`, `eloop6`;
  the bench program `bench/BLOOP.TXT` confirms it on the machine).
- **Ending a loop:** at the end of every block that has an N number (0x1cc5a), while it matches the
  innermost loop's end N:
  - one pass is counted;
  - if passes are left, the loop goes back to its return point;
  - if not, the loop is dropped and the next one out is checked against the same block.

  So nested loops can share their end block, as the manual says.
- **Detail dxsim does not copy:** the return point is the text right after `/count`. Words written after
  the count in the same block would run again on every pass; dxsim starts each pass at the next block.
  The manual and the built-in text always put `=Nseq/count` on a block of its own.

## 8. Macros (subroutines) `#n ... $` (CONFIRMED, M ch.10 + F)

```
#3; Peck Cycle          definition: #n starts it, $ ends it
G1Z*F*                  '*' = the next value passed by the call
G0Z*
$
=#3Z*-.5F*10.Z*.5       call: '=' then #n, then the values in order (letter*value)
```

* Definition (0x200ba without `=`): records the position after `#n` in the table 0x3820 and skips the
  body to `$` or `|`. Defining again replaces the old body (M note 5). A macro cannot be defined inside
  another (M note 6). `dxsim` also registers all definitions before the run so a call may precede its
  definition (TENTATIVE: the card's load pass).
* Call (`=#n`, 0x200c4): pushes the return block and the parameter frame (0x32fc/0x3320, 8 levels in 6.89;
  the manual and the operator's manual say 4).
* Parameters: in the calling block every `letter*value` appends to the list (0x3368) and does nothing
  else; inside the macro each `letter*` takes the next value (0x336c). Up to 12 active values (M note 8).
* Return: `$` (0x2016c).

## 9. M-code macros `M<n>: ... |` (6.89, F)

```
M100:  comment           definition: the M word followed by ':'
...body...
|                        return
M100 X*1.5               call: any M100 in a program; '*' values as for #n
```

* The M handler (0x1f4e8) in the load pass (0xaefb bit 5) registers `M<n>:` in the list 0x3b44 and stores
  the body in slot 199-i of the same table as `#n` macros (0x1f632). M900-M907, M974, M975 and M6 set
  extra flags (hooks; `../EXT_CODES.md` section 3).
* A call sets call-pending (0x32e3 bit 0) and the nesting count 0xc5c0; at the first level it also allows
  assignments without PPPL (0x4088 bit 2). `|` (0x20176, the letter-table entry for `|`) returns; this
  answers Phase 2 item 1 of `../EXT_CODES.md` section 3 ("find the return code"); after a user M6 macro the real tool
  change M63 is queued.
* The operator's manual mentions a separate MACROs file, validated with START, and "Loops and Macros
  Illegal in DNC". `dxsim -M file` loads such a file. TENTATIVE.

## 10. The cycle calls `%` and `&` (internal, F)

The built-in cycle text uses three more characters. They are not in the manual; user programs should not
need them.

| Syntax | Meaning | Firmware |
|---|---|---|
| `=%n` | call table A entry **n+9** (n = 1..32), 5 levels (stack 0x3c1c) | 0x203fe, 0x20422 |
| `=%@` | call the current cycle body, table A entry 0x3c44 (G81→A1 ... G89→A9) | 0x203dc |
| `%n` | jump to table B entry n (1..12), no return | 0x20406 |
| `&` | return from a `%` call; at level 0 the cycle is over | 0x1cabe |

## 11. Preparatory functions (G codes)

M table 3-1 and 3-2. Groups: G codes of different groups may share a block. Reset state: G0 G8 G17 G30 G40
G45 G49 G70/71 G72 G75 G90 G94 G96. Unknown: `Parser: Illegal G-Code`. Last column: `dxsim` support.

| G | Meaning | dxsim |
|---|---|---|
| 0 | rapid; Z up first then XY, or XY then Z down | yes |
| 1 | linear feed (needs F: `No Feedrate Specified`) | yes |
| 2 / 3 | arc CW / CCW: I J K centre or R radius (≤ 180°, F 0x1f992) | yes |
| 4 | dwell: `G4Q__` or `G4/__` defines, a lone `G4` dwells; also at the bottom of G82/G84/G89 | yes (traced) |
| 8 / 9 / 99 | deceleration override off / on (modal) / this block only | recorded |
| 12 / 13 | helix CW / CCW in polar coordinates: `A` = degrees of travel, `Z` = end depth, `R` = new radius (spiral) | yes |
| 17 / 18 / 19 | plane XY / ZX / YZ | yes |
| 22 / 23 | fillet CW / CCW at the corner X Y, radius R; the next block must be G1 X Y. **Overwrites X1-X4 Y1-Y4 U1 U2 V1 V2 I1 J1 R2 R3 B1-B3 P2-P4** | yes, as the firmware does it (`features/FILLET.md`) |
| 30 / 31 / 32 | mirror off / X / Y, about the point where it is programmed | yes |
| 40 / 41 / 42 | cutter compensation off / left / right | yes, as the 6.89 firmware does it (`features/CUTCOMP.md`) |
| 44 / 45, 48 / 49 | constant surface feed off / on (reset G45); corner rounding off / on (reset G49; G48 in BOSS 4-7 mode) | yes, with cutter compensation |
| 54-59 | work shifts (operator's manual appendix C) | yes (offsets 0) |
| 70 / 71 | inch / metric input | yes |
| 72 / 73 | transformation off / on: `G73A_` rotate about X0Y0, `G73X_Y_Z_` scale | yes |
| 74 / 75 | single-quadrant / multi-quadrant arcs | yes |
| 77 / 78 / 79 | zig-zag face, pocket, bore mill (A27-A29) | yes, firmware text |
| 80 | cancel Z cycle (also selects G0) | yes |
| 81-89 | Z cycles, see section 13 | yes, firmware text |
| 90 / 91 | absolute / incremental | yes |
| 92 | set the current point's coordinates (no motion) | yes |
| 94 / 95 | feed per minute / per revolution | recorded |
| 96 / 97 | fixture offset off / `G97X_Y_Z_` on | yes |
| 170-179 | milling cycles (178 does not exist) | yes, firmware text |
| 181-189 / 191-199 | Z cycle on a row / bolt circle / frame of holes | yes, firmware text |

Inside the built-in text the firmware also uses G46/G47 (tapping) and G99 on arcs.

## 12. Geometry

* **Absolute / incremental:** `X Y Z` follow G90/G91; `U V W` are always incremental (M table 4-1).
* **Arc centres** (M ch.3, F 0x21dba): with G75 and G90, `I J K` are the absolute centre; with G91 they
  are the signed distance from the start point; with G74 unsigned distances, the control picks the
  quadrant. **Inside the built-in cycle text I/J/K are always incremental**, whatever G90/G91 says
  (0x21e0a: 0x3c40 ≠ 0). A full circle: end point = start point.
* **Radius arcs** (`G2/G3 X Y R`): always the short arc (≤ 179.998°). Centre = mid point ± the
  perpendicular, right of the chord for G2, left for G3 (0x1f992). (CONFIRMED)
* **Polar coordinates** (M ch.4): `R` = radius, `I J` = pole (absolute in G90, incremental in G91), `A` =
  angle from +X, counter-clockwise positive. A block with only `R I J` defines the pole. `G0/G1 ... A_`
  moves to the polar point; `G2/G3 A_` moves around the pole to angle A (same angle: full circle);
  `B` is an incremental angle. `G92A0` presets the angle register.
* **Transformations** apply in the order rotation, scaling, mirror, translation (M ch.4). G92 changes the
  coordinates of the current point; G97 X Y Z sets the fixture offset from the base system; G96 cancels it.
* **Units:** the control works in inches internally (F: 2 540 000 counts per inch, 100 000 per mm, 0x23fe0);
  variables hold program-unit numbers.

## 13. Canned cycles

All canned cycles are programs in the built-in text (`corpus_689.txt`) run by the same interpreter. The
cycle G code arms a **capture** state (0x32e3 bit 5): while it is set, the words of the block load cycle
variables instead of moving. At the end of the block the **driver** in table A runs (0x1c842), then
returns to the next block. (CONFIRMED, F)

### Which G code runs what (0x3c44 = body, 0x3c48 = driver entry)

| G code | 0x3c44 | driver | notes |
|---|---|---|---|
| G81-G89 | 1-9 | A20 `G0U1V1C9 =%@ &`: rapid to the hole, run body A1-A9 | modal until G80 or G0-G3 |
| … second `X` or `Y` | | A23 row (X X or Y Y), B6 for Y | M "Row drilling - incremental distance" |
| … `A` | | A21 polar hole; second `A`: A22 bolt circle | |
| G170-G179 | - | A10-A19 (G = 160 + entry) | one block |
| G181-G189 | 1-9 | A24 row with P holes; second `A`: A25 bolt circle | |
| G191-G199 | 1-9 | A26 frame | |
| G77 / G78 / G79 | - | A27 / A28 / A29 | |

A modal Z cycle re-arms on any later block with an X, Y or A word (0x213b0), unless the block runs inside a
cycle. G0-G3 and G80 cancel it (0x20b56) when not inside a cycle.

### Word → variable rules (F, per word handler)

| Word | In a cycle block | Handler |
|---|---|---|
| 1st `X` / `Y` (or `U` / `V`) | X1/Y1 = absolute target; U1/V1 = target − current position | 0x21650 / 0x21984 |
| 2nd `X` | X2 (G81-89: switches to the row driver A23; G78: also Y2 = X2, X3 = 0) | 0x21776 |
| 3rd `X` | X3 | |
| 2nd and later `Y` | Y2 (G81-89: row driver) | 0x21a7c |
| `Z` in G81-G89 | 1st: Z2 = Z3 = Z4 (depth, incremental); 2nd: Z3 = Z4 (first peck); 3rd: Z4 (later pecks) | 0x21c02, 0x21d2a |
| `Z` in G170-G199, G77-79 | 1st: Z1 = clearance plane (absolute/incremental); 2nd: Z2 = Z3 = Z4 (depth); 3rd: Z3 = Z4 (step); 4th: Z4 | 0x21ca0, 0x21d2a |
| `W` | Z1 = current Z + W | 0x21c4c |
| 1st `A` (or `B`) | A1 = angle, B1 = A1 − current angle | 0x222a8 |
| 2nd `A` | A2 (bolt circle span); B1 adjusted ±360 to the direction of A2 | 0x22300 |
| `R` | G170-G179: R2 (fillet / circle radius); with G181/G191: the pole radius | 0x1f8f8 |
| `J` | G79: J1 | 0x21ec6 |
| `F` | 1st: feed, F1 = F2 = F, F3 = F/2 (G173, G174, G179: F2 = F3 = F/2); 2nd: F2; 3rd: F3 | 0x1f344 |
| `P` | P1, P2, P3, then P4 (in **every** block, not only cycles) | 0x1f81a |
| `Q` | 1st: Q1 = Q2; 2nd: Q2 (dwell times); after G4: the dwell | 0x1f894 |

Set by the cycle G code itself: G81-89: X1 X2 Y1 Y2 U1 V1 = 0, Z6 = peck clearance (param 272), C9.
G170-179: R1 = tool radius, R2 = Z2 = Z3 = 0, P8 = machine family flag (0x20920), P9 = 1 if G91 was
active (B12 restores it). G181-199: F1 = current feed, W9 = param 272.

### Formats (M chapters 5 and 6)

```
G81..G89 X_ Y_ Z(depth) [Z(peck1) Z(peck)] F_          Z depth incremental, unsigned
G8x X_ X_(increment) Z_ F_   /  Y_ Y_ ...  /  A_ A_ ...  row along X, Y or a bolt circle
G181..G189 X_ Y_ Z(clear) X_ Y_(to last hole) Z(depth) P(holes) F_
G181..G189 A_ Z(clear) A_(span) Z(depth) P(holes) F_  [R_ I_ J_]
G191..G199 X_ Y_ Z(clear) X_ Y_(frame) Z(depth) P(holes X) P(holes Y) F_
G170/G171 X Y Z  X Y(length,width) R(fillet) Z(depth) [Z(step)] P(clearance) F P(finish) [F F]
G172      X Y Z  X Y R Z [Z] P(stepover) P(clearance) F P(finish) [F F]
G173/G174 X Y Z  X Y Z [Z] P(overlap) P(clearance) F [F]
G175/G176 X Y Z  R Z [Z] P(clearance) F P(finish) [F F]
G177      X Y Z  R Z [Z] P(stepover) P(clearance) P(finish) F [F F]
G179      X Y Z  P(length) P(diameter) P(angle) Z [Z] F [F]
G77 X_ Y_ Y_(stepover) F_      G78 X X [X] Y [Y] F [F]      G79 J_ F_
```

### Notes from the cycle text (C)

* G87 retracts **0.05 in** after each peck (W8 = .05; 1.27 when Q7 ≠ 0); the manual says .01".
* G83/G87 re-enter each peck at the previous depth less Z6 = param 272 (peck clearance).
* G88 has an empty body (A8 is `&`) and so does G178 (A18); the manual lists neither.

## 14. Miscellaneous functions (M ch.9)

M0 stop, M1 optional stop, M2/M30 end and rewind, M3/M4/M5 spindle, M6 tool change, M7/M8/M9 coolant,
M12/M15 closed/open loop spindle, M13/M14 spindle direction (tapping), M19 orient, M20/M21/M22/M26 as
M0/M1/M2/M6 with a move to the clear point, M25 quill home, M28/M29 rigid tapping, M51 index table.
One M per block. `dxsim` traces them; it does not move to the clear point or apply tool length offsets.

## 14a. Travel limits (CONFIRMED, F)

The parser checks the end point of every block that moves, before the move is queued. The block can be in
the user program or in the built-in cycle text. The check is FUN_000231f0, called from the move finalizer
at 0x231d2 unless 0x4089 bit 3 is set. **G54-G59 set that bit** (0x20dc0), so a block that contains a work
shift is not checked; the bit is cleared at each block start. It compares the target in
machine units (0x40b2 X, 0x40be Y, 0x40ca Z, 0x40d6 4th axis, 0x40e2 S) with params 17-26 (0xfd8-0xffc).
Each violation is a `trap #14` with its message:

| Check | Message (6.89 address) |
|---|---|
| X < param 17 / X > param 18 | `Parser: Commanded X Axis Move Too Far Minus` / `... Positive` (0x331fc / 0x33228) |
| Y < 19 / Y > 20 | `... Y Axis ...` (0x33257 / 0x33283) |
| Z < 21 / Z > 22 | `... Z Axis ...` (0x332b2 / 0x332de) |
| 4th axis < 23 / > 24 | `... W Axis ...` (0x3330d / 0x33339) |
| S axis < 25 / > 26 | `... S Axis ...` (0x33368 / 0x33394) |

* **Z, 4th axis and S are checked only if** the top nibble of **param 98** (0xaf58, the 'Master'
  configuration, `bfextu {0:4}`) is not 0, or **param 62** (Z velocity gain, 0x2988) is not 0 (CONFIRMED).
  Both shipped `BMDCPRMS.SYS` files have param 98 = 0x21221220 (nibble 2): Z is checked. `dxsim -P` reads
  both and shows the result as `zcheck` in the trace header.
* **Z limit off:** if param 21 is -2147483648 (0x80000000), Z is not checked (0x23280).
* **Above the Z positive limit,** only when that nibble is 0 (not on the shipped files), the move is refused only if it also goes
  higher than Z already is (0x2758) by more than 254 units, i.e. 0.0001 in (0x232ae). A move at a height
  that was already reached is allowed.
* **Between end points:** the parser doesn't look there. The operator's manual (appendix D) lists the
  same messages with `dpCom:` for a limit the interpolator catches during the move. `dxsim` reports an arc
  that bulges past a limit that way.
* **The same check also refuses a block with no feed rate:** `ftst 0x40a6` / `Parser: No Feedrate
  Specified` (0x333c3).
* **Units** (`../MOTION_PLANNER.md`):
  * distances: 1 unit is 10 nm, so 2,540,000 units per inch;
  * rates: units per 4 ms tick, so 169.333 = 1 ipm. Param 7 is 79,925 = 472 ipm.

**Spindle (F, TENTATIVE in detail).** The spindle-speed code at 0x34620 clamps the commanded speed:
* down to 0xaf34, whose parameter isn't identified;
* up to param 75 (0xaf14, range #2 maximum), with a machine message.

`dxsim -P` warns above the highest of params 74-76.

## 15. Open questions

All open questions, with how to answer each one (firmware trace, bench test, or a BXX trace), are in
`features/OPENQ.md`. The list below is the language part of it (L1-L7 there).


1. **`?Q7<NE>0W8=1.27!`** (G87 body, block 0x2757b): by context it means W8 = 1.27 when Q7 ≠ 0, but the
   assignment test (0x1ed88) only looks at the start of a block. Read literally, `W8` is a W word (capture
   or a Z move) and `=1.27` is ignored. Q7 is 0 unless a program sets it, so the block is normally skipped.
   `dxsim` follows the firmware literally.
2. 6.89 differs from the 1994 manual in several limits: variable indices (1-19/50 vs 1-9), macro nesting
   (8 vs 4), 36 functions vs 7, M-macros. Bench tests should confirm the 6.89 behaviour.
3. `C9` in the G81 driver (`G0U1V1C9`) (answered, `features/OPENQ.md` A7): 0x20882 sets C9 = (4th-axis
   position 0x40d6 − 0xacae) / 250000, or 0 without a 4th axis, and the C word handler (0x1f0f6) turns it
   back into a 4th-axis target. It holds the C axis; it never moves X, Y or Z. `dxsim` ignores C words.
4. The load pass that registers `#n` definitions was not traced. (The `=N` loop handler is answered: section 7.)
5. (Answered: the `MRG` mask and the RDI/WRI table are in section 4.) Whether WRI(11) reaches the AUF outputs: OPENQ L9.
6. What BXX does to a program on load (blanks, CR/LF, `%` tape leaders).

## 16. Firmware index (6.89)

| What | Address |
|---|---|
| block loop, assignment test, skip counter | 0x1ed12, 0x1ed88, 0x1ed44 |
| letter/character handler table (by ASCII) | 0x1ee84 |
| G table (G0-G125) / G170-G199 | 0x205e2 / 0x208f8 |
| number reader / word value / variable rule | 0x23b2c / 0x23aa0 / 0x23eb2 |
| assignment / expression / functions | 0x24ebc / 0x24fc8 / names 0x26302, code 0x253f2 |
| `?` `<` `>` compare | 0x1ffc6, 0x20010, 0x2001a, 0x2021c |
| `#` `$` `|` `%` `&` `=` `:` `;` quotes | 0x200ba, 0x2016c, 0x20176, 0x203c8, 0x2045a, 0x2036e, 0x202b6, 0x2031a, 0x20376 |
| L, M, N, P, Q, R, F handlers | 0x1f410, 0x1f4e8, 0x1f732, 0x1f7a4, 0x1f864, 0x1f8b2, 0x1f2c0 |
| X, Y, Z, I, J, A, B handlers | 0x21446, 0x217f4, 0x21ac6, 0x21dba, 0x21e9e, 0x2213a, 0x22070 |
| end of block: cycle driver call, state dispatch | 0x1c7f0, 0x1c892 (states 0x1c8a0) |
| cycle text and tables A / B | 0x26926-0x27b2e, 0x26852 / 0x268f6 |
| travel-limit check, its call in the move finalizer, messages | 0x231f0, 0x231d2, 0x331fc-0x333c3 |
| spindle speed clamp | 0x34620 |
| fillets: G22/G23, state 0x32f5, arithmetic, moves | 0x20bf6/0x20c18, 0x1c53e-0x1c69c, 0x1d89e/0x1d946, 0x1dc80/0x1dd3e |
| cutter compensation: G40-G49, states 0x32f4, engine | 0x20d00-0x20db8, 0x1d03e, 0x2412e-0x24e86 |
