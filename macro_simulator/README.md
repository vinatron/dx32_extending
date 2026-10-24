# Macro Language Simulator

A PC-side simulator for the BMDC part-program macro language: the variables, arithmetic and
conditionals that the DX32 firmware interprets on the card. The goal is to try macro programs
(including future probing cycles, see `../EXT_PROBING.md`) on a laptop before they go near the
machine.

Target firmware: **6.89** (repo-root `BMDC.BIN`). The TC22 is moving to 6.89 (`../VERSIONS.md`).
6.89 is also the build that has `M<n>:` user M-code macros (`../EXT_CODES.md` section 3) and the
licensed "PPPL" parametric option (param 285 bit 4).

## Layout

| Path | Contents |
|---|---|
| `manual/` | The Bridgeport programmer's manual (PDF), the primary specification. Upload it here |
| `README.md` | This file: scope, sources, what is already known |

Code, tests and the extracted corpus will be added by the simulator project.

## Sources for the language definition

1. **The programmer's manual** in `manual/`: syntax, variables, operators, conditionals, subprograms.
2. **The firmware's own built-in cycles.** 6.89 stores its canned cycles (pockets, frames, bolt
   circles, peck/clearance moves, ...) as macro-language text. The G-word handler (0x204f6) tests
   the program pointer 0xaeb2 against the bounds **0x26852 and 0x27b2e**. 0x26852 starts with a
   table of 32-bit entry pointers into the text, which runs to 0x27b2e. About 400 statements, for
   example:

   ```
   ?Z4<LT>Z3          conditional: if Z4 < Z3 ...
   Z3=Z4!             assignment; '!' appears to end the conditional statement
   P4=R1+P3           arithmetic on letter+digit variables
   Y6=ABS(V3)-Y7      functions and expressions
   R3=R2-(R1+P2)      parentheses
   G91G1U1W1F3!       motion using variables as words
   G3G99U1V1I1J0F1&   '&' suffix (meaning to be found)
   ?Q7<NE>0W8=1.27!   condition and assignment on one line
   ```

   Comparison operators seen: `<LT> <GT> <EQ> <NE> <LE> <GE>`. Variable letters seen: A B F I J P Q
   R U V W X Y Z, digits 1-9. Extract with a simple printable-string scan of `BMDC.BIN` between the
   two bounds. This is a free regression corpus: the simulator must parse all of it.
3. **The interpreter itself** in the firmware: letter table 0x1ee84 (indexed by ASCII; `?`, `<`, `=`,
   `(`, `|` etc. have handlers), G table 0x205e2, M handler 0x1f4e8 with the `M<n>:` definition mode.
   Addresses and tooling are in `../MOTION_PLANNER.md`, `../EXT_CODES.md` and `../CLAUDE.md`
   (`tools/m68k_dis.py`, `decompiled/BMDC.asm`).

All findings above are TENTATIVE until checked against the manual.
