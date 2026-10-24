# Macro language: draft grammar (pre-manual)

Status: **written before the programmer's manual was available.** It is derived from two
sources only:

1. the built-in cycle text of firmware 6.89 (`corpus_689.txt`, 628 non-empty blocks), and
2. the interpreter code in `BMDC.BIN` 6.89 (addresses below are 6.89 / repo-root `decompiled/`).

Labels follow the repo convention. **CONFIRMED** means traced in the firmware: the parser really
behaves this way. It does not mean the manual agrees with the wording, or that the meaning given
is the official one. **TENTATIVE** means it is inferred from the corpus or from partial tracing.
When the manual arrives this file becomes the skeleton of `LANGUAGE.md`.

`python3 tools/check_grammar.py` checks this grammar against the corpus: 628/628 blocks parse,
with one block flagged (open question 1).

## 1. The corpus

| Item | Value | Confidence |
|---|---|---|
| Text area | 0x26852-0x27b2e, bounds tested by the G handler at 0x2050a | CONFIRMED |
| Table A | 0x26852, 41 longs; `=%n` call target and cycle drivers (state 5, 0x1ca78, index 0..0x28) | CONFIRMED |
| Table B | 0x268f6, 12 longs; `%n` jump target (state 3, 0x1c9f0, index 0..0xb) | CONFIRMED |
| Blocks | NUL-terminated strings from 0x26926; an empty string separates routines | CONFIRMED |

`tools/extract_corpus.py` regenerates `corpus_689.txt` and tags each block with the table entries
that point at it. For 6.73 (`../my_tc22_files/BMDC.BIN`) the bounds differ (see `../VERSIONS.md`);
the extractor takes the image as an argument, but its bounds are the 6.89 ones.

### Entry map

| Entries | Reached by | Contents | Confidence |
|---|---|---|---|
| A1-A9 | `=%@` with 0x3c44 = 1..9, set by G81-G89 (0x20824: 0x3c44 = G-80) | drill/bore cycle bodies: A1 drill (feed to depth, rapid out), A2 + dwell, A3 peck, A4 tap (M13/M14, G47/G46), A5 feed out, A6 orient (M19) and rapid out, A7 sets the peck retract W8, A8 empty | CONFIRMED (dispatch), TENTATIVE (names) |
| A10-A19 | G170-G179: 0x3c48 = G-160 (0x2094a). When a cycle is active, the end of every block calls A[0x3c48] (0x1c842) | pockets, frames, circular pockets, a G73 slot (A19); A18 empty | CONFIRMED (dispatch), TENTATIVE (names) |
| A20 | G81-G89 driver (0x3c48 = 0x14) | `G0U1V1C9`, `=%@`, `&`: position, then run the cycle body | CONFIRMED |
| A24, A26 | G180-G189 and G190-G199 drivers (0x3c48 = 0x18 / 0x1a, 0x3c44 = G-180 / G-190) | hole patterns (line, grid) that call `=%@` at each point | CONFIRMED (dispatch), TENTATIVE (names) |
| A21-A23, A25, A27-A29 | 0x3c48 = 0x17, 0x1c, 0x1d, set by the X/Y/I word handlers (0x213ca, 0x217ea, 0x21abc, 0x21ec6) | pattern and frame variants | TENTATIVE |
| A30-A41 | `=%21`..`=%32` (n + 9, 0x2041c) | shared helpers: depth step, arc corner, approach, ... | CONFIRMED |
| B1-B12 | `%1`..`%12` | tails of the cycles: continue a loop, retract, finish | CONFIRMED |

A40 and A41 point 5 bytes past A11 and A16, skipping their leading `Q8=0`.

## 2. Lexical structure

| Rule | Confidence |
|---|---|
| A program is a sequence of **blocks**. The interpreter loop (0x1ed20) ends a block at NUL or 0x80 | CONFIRMED |
| Letters are case-insensitive (`and #$df` everywhere) | CONFIRMED |
| Spaces and tabs are skipped inside numbers and expressions (0x23b4e, 0x240b2, 0x260c4) | CONFIRMED |
| `;` starts a comment that runs to the end of the block (0x2031a). `;\` followed by digits is also a block-search target | CONFIRMED |
| `"`, `'` and `` ` `` skip to the matching quote: a message or comment (0x20376) | CONFIRMED |
| Unknown characters, digits, `+ - .` and `* @` outside a word are ignored (handlers 0x1f084/0x1f086/0x20464/0x20466) | CONFIRMED |
| How BXX turns CR/LF lines of a part program into NUL blocks | TENTATIVE (not traced) |

## 3. Numbers and units

```
number := [ "+" | "-" ] ( digits [ ("." | ",") [digits] ] | ("." | ",") digits )
```

* `,` is accepted as a decimal point; leading zeros are skipped; at most 18 digits (0x23b2c). CONFIRMED
* Implied-decimal mode (0xaef9 bit 2): a number without a decimal point is divided by 10000 (inch)
  or 1000 (metric, 0x4089 bit 2) (0x23cf8). The parameter that sets 0xaef9 bit 2 has not been
  identified. CONFIRMED (mechanism)
* Axis values are scaled to internal units of 1/2 540 000 inch = 10 nm (inch) or 1/100 000 mm
  (metric) (0x23fe0). The simulator works in program units. CONFIRMED

## 4. Variables

```
var := letter index
index := 1..19                 for B E G H L M N O P Q S T U V W   (19 x 12-byte slots)
       | 1..50                 for A C D F I J K R X Y Z           (19 slots + 31-slot bank)
```

* Each letter has a 0xe4-byte record at **0x4be0 + (letter-'A') * 0xe4**, holding 19 extended
  floats (index 1-19). Eleven letters (A C D F I J K R X Y Z) have a second bank for indices 20-50
  (0x23f0a; X → 0x65f0, Y → 0x6764, Z → 0x68d8, F → 0x647c, C → 0x6308, R → 0x6a4c, A → 0x6bc0,
  I → 0x6d34, J → 0x6ea8, K → 0x701c, D → 0x7190). The assignment parser marks the same eleven
  letters (0x7762 bit 0). CONFIRMED
* In the corpus: P1-P9, Q1-Q9, R1-R4, U1-U7, V1-V7, W1-W8, X1-X3, Y1-Y7, Z1-Z6, A1-A2, B1-B4,
  I1-I4, J1-J3, F1-F3. The cycle parameters arrive in these variables (for example Z2 = depth,
  Z3 = peck, P1/P2 = counts, X2/Y2 = sizes); the mapping to the G-code words comes from the
  manual. TENTATIVE
* Variables are global; there is no local scope. TENTATIVE (no frame save seen)

## 5. Words and the variable-reference rule

```
word := letter value                (letter not L; L is the label letter)
value := number | var-index | "*" [number]
```

The letter handlers read the value with 0x23aa0 and then call 0x23eb2. **If the value was written
as a 1-2 digit integer, positive, with no decimal point, and it is in the letter's index range,
the word uses the variable's contents, not the number.** So `G91G1U1F3` moves by the value of U1
at the feed in F3, while `W.02`, `G90A90.` and `W-.02` are literal numbers. CONFIRMED (0x23bf2,
0x23ee6). The same rule makes `X1` mean variable X1 and `X1.` mean one unit.

* In implied-decimal mode, variable references are disabled outside the built-in text (0x23eb8).
  CONFIRMED
* Whether the reference rule also applies in normal (decimal-point) user programs, which would make
  `X1` dangerous, is the most important question for the manual (open question 2). The code path
  has no such restriction.
* G and M numbers go through the same reader, but whether `G1` is ever read as variable G1 is not
  traced: the G handler converts with `fmove.l` before any lookup. TENTATIVE
* **U, V, W** jump into the X, Y, Z handler tails (0x215e4, 0x21938, 0x21c4c). They are the
  incremental forms of X, Y, Z. CONFIRMED (shared code), TENTATIVE (incremental meaning)
* `*` passes parameters into a called subprogram or M-macro. In a block that makes a call
  (0x32e3 bit 0 already set), `X*1.5` pushes 1.5 on the parameter list (0x3368, 12 bytes per entry)
  and the word itself does nothing (0x32e8 bit 0). In the called body, a bare `X*` takes the next
  value (0x336c). CONFIRMED (mechanism), TENTATIVE (usage)

## 6. Assignment and expressions

A block is an **assignment** when it starts with `letter digit =` or `letter digit digit =`
(0x1ed88). The evaluator at 0x24ebc consumes up to `!`, `;`, CR or the end of the block
(0x260a0). CONFIRMED.

```
assignment := var "=" expr
expr   := term { ("+" | "-") term }                            0x24fc8
term   := factor { ("*" | "/") factor }                        0x2500a
factor := number | var | "(" expr ")" | ("-" | "+") factor     0x25042, char table 0x262a6
        | "#PI"                                                0x2511a (FMOVECR #0)
        | func1 "(" expr ")" | func2 "(" expr "," expr ")"     name table 0x26302, ops 0x253f2
```

The usual precedence applies: `*` and `/` bind tighter than `+` and `-`, all left-associative, and
unary minus binds to the following factor. CONFIRMED

**Licence gate:** outside the built-in text (0x4088 bit 2) an assignment traps unless param 285
bit 4 (0xc519 bit 4, **PPPL**) is set (0x1edbc). The first call of an M-macro also sets 0x4088
bit 2, unless 0xc539 bit 3 is set (0x1f6be), so assignments may work inside M-macros without PPPL.
CONFIRMED (code), TENTATIVE (consequence)

### Functions (code = 4 x table index)

| Name | Meaning (68882 op) | Confidence |
|---|---|---|
| `ABS` `SQR` | absolute value, square root | CONFIRMED |
| `SIN` `COS` `TAN` | argument in **degrees** (x pi / 180) | CONFIRMED |
| `ATN` `ASN` `ACS` | result in degrees | CONFIRMED |
| `LOG` `ALG` | log10, 10^x | CONFIRMED |
| `LGE` `E2X` | ln, e^x | CONFIRMED |
| `HSN` `HCS` `HTN` | sinh, cosh, tanh of (x pi / 180) | CONFIRMED |
| `HAT` | atanh, result x 180 / pi | CONFIRMED |
| `NOT` | 32-bit bitwise NOT | CONFIRMED |
| `FFB` | number of the highest set bit (31 - BFFFO) | CONFIRMED |
| `RDI` `RDP` `GET` | machine reads (input, parameter, ...); `RDI` takes a channel 0..0x96 checked against a table at 0x263a2 | TENTATIVE |
| `DTF(a,b)` | sqrt(a² + b²) | CONFIRMED |
| `WRI` `WRP` `PUT` | machine writes (two arguments) | TENTATIVE |
| `XOR` `AND` `LOR` `LSR` `LSL` `ROR` `ROL` `SET` `CLR` `TST` `MRG` | two-argument bit operations on 32-bit integers | TENTATIVE (by name; handlers 0x25b5c-0x2600e not decoded) |

The corpus uses only `ABS`.

## 7. Conditionals

```
condition := "?" var "<" op ">" ( number | var )        op := EQ | NE | LT | LE | GT | GE
endif := "!"
```

* `?` reads the left-hand variable (0x1ffc6, stores it at 0x3cec), `<` stores the two operator
  letters (0x20010), and `>` reads the right-hand side, stores it at 0x3cf8 and compares (0x2021c).
  Operators come from the table "EQNELTLEGTGE" at 0x20256. The compare is an exact 68882 `FCMP` of
  left minus right, with no tolerance. CONFIRMED
* The left side must be a variable. The right side is a literal number or a variable. No
  expressions. CONFIRMED
* A false condition increments the skip counter 0x32f6. While it is non-zero the loop (0x1ed44)
  discards characters across block boundaries, counting `?` up and `!` down, so **`?...!` nests and
  can span any number of blocks**. A true condition changes nothing, and its `!` is a no-op
  (0x20004). CONFIRMED
* Quirk: a right side written as a bare small integer (`?Q1<LT>5`) goes through the variable rule
  with the left-hand letter's record still in A0, so it would compare Q1 with **Q5**. The corpus
  only writes `0` or decimals there. TENTATIVE (follows from the code; check on the machine)

## 8. Flow control

| Syntax | Meaning | Firmware | Confidence |
|---|---|---|---|
| `Ln` | define label n at the current block (n 1-9; 10-29 only inside an M-macro) | 0x1f48e, table 0x3c74 | CONFIRMED |
| `=Ln` | go to label n (backward jump; an undefined label traps) | 0x1f410 → state 4, 0x1ca36 | CONFIRMED |
| `#n` ... `$` | define subprogram n (1-200): record the start and skip to `$` or `\|` | 0x2012e, table 0x3820 | CONFIRMED |
| `=#n` | call subprogram n (8 levels, stack 0x32fc) | 0x200c4 → state 1, 0x1c8c0 | CONFIRMED |
| `$` | return from a subprogram | state 2, 0x1c99c | CONFIRMED |
| `M<n>:` ... `\|` | define an M-macro; slots from 199 down in the same table (0x1f632) | 0x1f566 | CONFIRMED |
| `M<n>` (defined) | call the macro (sets 0x32e3 bit 0, depth 0xc5c0 + 1) | 0x1f666 | CONFIRMED |
| `\|` | return from an M-macro (depth - 1; after a user M6, queues the real M63) | 0x20176 | CONFIRMED |
| `=%n` | call table A entry n + 9 (5 levels, stack 0x3c1c) | 0x203fe, 0x20422 | CONFIRMED |
| `=%@` | call the current cycle body A[0x3c44] | 0x203dc | CONFIRMED |
| `%n` | jump to table B entry n, no return | 0x20406 → state 3 | CONFIRMED |
| `&` | return from a `%` call; at depth 0 it ends the cycle | state 6, 0x1cabe | CONFIRMED (return), TENTATIVE (end of cycle) |
| `:n` | sequence/target number, compared with the search target 0x3c60 | 0x202b6 | CONFIRMED (code), TENTATIVE (meaning) |

`=` itself (0x2036e) only sets 0x32e3 bit 0, "call pending". It turns the next `L`, `#` or `%`
into a go-to or a call. Labels are a single global table: a nested routine that defines `L8`
overwrites the caller's `L8`. TENTATIVE (follows from the shared table)

Definitions (`#n`, `M<n>:`) are recorded in a pre-scan pass (0xaefb bit 5), so they may appear
anywhere in the program. TENTATIVE

## 9. Open questions for the manual

1. **`?Q7<NE>0W8=1.27!`** (block 0x2757b, cycle A7/G87). By its context this sets W8 = 1.27 when Q7
   is non-zero (the metric twin of `W8=.05`). But the assignment test only looks at the start of a
   block. Traced literally, the firmware reads `W8` as a W word (an incremental Z move by W8), then
   `=` sets call-pending, and the digits are ignored. Either the manual allows assignments after a
   condition and the firmware does it some way not yet traced, or this is a latent bug. The
   simulator will offer both readings.
2. Does the variable-reference rule (section 5) apply in ordinary user programs? Does a program
   need `X1.` for one inch?
3. The official names and parameter words of G170-G199 and of the A21-A29 pattern cycles, and how
   the G-code words map to the P/Q/R/X/Y/Z variables the cycles read.
4. The arguments and meaning of `RDI RDP GET WRI WRP PUT`, and of the bit operations.
5. Whether `M<n>:` macros and `#n` subprograms need PPPL, and the exact syntax the manual gives.
6. The meanings of `C9` in `G0U1V1C9`, `G72`/`G73` and `G92` inside A19, and the `R`/`A` polar words
   with `G13` in A17.
7. The block separator in user programs (CR/LF, `;`) and line-length limits.

## 10. Proposed simulator plan (to confirm before coding)

1. `LANGUAGE.md` from the manual. This draft supplies the firmware cross-references.
2. `dx32macro/` package, pure Python 3, standard library only:
   * `lexer.py` / `parser.py`: blocks → AST, following sections 2-8; syntax errors carry the block
     offset or line number;
   * `interp.py`: variable store with the 19/50 index ranges; the expression evaluator with all
     math functions; the `?`/`!` skip counter (a faithful nesting model); labels, `#`/`$`,
     `M<n>:`/`|`, `%`/`&` with the stack depths the firmware enforces; parameter passing with `*`;
   * modal state: G0/G1/G2/G3, G90/G91, U/V/W incremental, feed, G99 one-shot; arcs from I/J as
     endpoints plus centre, with no timing;
   * the built-in cycles loaded from `corpus_689.txt`, so that `G81 ... X Y` runs the firmware's own
     text through the A20 driver;
   * a trace as text and JSON: one line per move (type, from → to, centre, feed) and per assignment;
   * options for the open questions (`--quirk-midblock-assign`, `--strict-var-ref`).
3. `tests/`: every corpus block parses; each built-in cycle runs with a sample parameter set and
   gives the expected move list (hand-checked); programs from the manual's examples.
4. README: how to run it (`python3 -m dx32macro prog.txt --trace`).
