# Program flow view

Roadmap item 4: show how a program moves between its parts. The parts are the main program, `#n` macros,
`M<n>:` macros, canned cycles, `=N` loops, `=Ln` jumps and `?…!` conditions. The design is in
`../ROADMAP.md` section 4.

## 1. Status

| Phase | What | Status |
|---|---|---|
| 4a | `-flow` / `-flow+` text trace | **done** |
| 4c | Moves coloured by the macro or cycle that made them; the call stack on the preview's status line; step into / step over in block-by-block mode | not started |
| 4b | Call map (`-map`): boxes and arrows, calls never taken drawn dashed, extra warnings | not started |

This is a dxsim feature, not a control feature, so there is no firmware trace or bench check. It reports
what the interpreter does, at the same call, return, loop, jump and skip points that drive the run.

## 2. Phase 4a: the flow trace (as built)

- **`-flow`:** the trace shows only the flow lines, warnings, stops and the end. Later loop passes are
  folded into one line.
- **`-flow+`:** the same lines inside the full trace, with every loop pass shown.
- **Layout:** each line is indented by the macro depth plus the `=N` loop depth.
- **Location column:** the block that caused the event.

| Kind | When | Text |
|---|---|---|
| `CALL` | `=#n` or an M-macro call | `#n "name" at line L, values v1 v2 ...`. The name is the comment on the definition line (`#3; Peck Cycle`); the values are the `*` parameters passed |
| `RET` | `$` or `\|` | `back to the call at line L` |
| `CYCLE` | a canned cycle runs for a block of the program | `canned cycle G81 (firmware text, driver A20)`, with the pattern for the G18x/G19x drivers. The cycle's own text is not shown |
| `LOOP` | `=Nseq/count` | the start (`blocks up to N30, 2 passes`); `passes 2-n not shown` (`-flow` only); `end of the =N loop, n passes` |
| `JUMP` | `=Ln` | `=L1: back to line L` |
| `SKIP` | a `?` condition that is false | `condition false: lines a-b skipped`, or `the rest of the block` |

- **Repeats:** a line that is identical to the previous one, at the same block, is printed once,
  followed by `(the line above n more times)`.

**An interpreter change made for this.** A block that both ends an `=N` loop and calls a macro or a
canned cycle now counts its loop pass when the call **returns** (`subret`, `sysret`), not when it is made.
The moves are the same; all the existing tests are unchanged. Only the order of the flow lines needed it.
How the firmware's `=N` handler (0x1f732) does this is still open: `OPENQ.md` L4.

**Example** (`tests/flow.txt -flow`):

```
L20     CALL  #1 "Hole pair" at line 5
L6      CALL    #2 "One hole" at line 9, values 0
L13     RET       back to the call at line 6
...
L23     JUMP  =L1: back to line 19
L22     SKIP  condition false: lines 22-24 skipped
L28     LOOP  =N30/2: blocks up to N30, 2 passes
L30     CALL    M100 "Spot" at line 14
L17     RET       back to the call at line 30
L31     LOOP    pass 2 not shown (-flow+ shows every pass)
L31     LOOP    end of the =N loop, 2 passes
L32     CYCLE canned cycle G81 (firmware text, driver A20)
```

## 3. Tests

| Test | Checked |
|---|---|
| `flow -flow` | the expected trace; `dxtest` also counts the events against a hand count: **CALL 7, RET 7, JUMP 1, SKIP 2, LOOP 3, CYCLE 2** |
| `flowall -flow+` | the same program with the full trace and every pass |

## 4. Next

- **4c:**
  - The move records get the call stack at the time of the move. The plot colours moves by the innermost
    macro or cycle, with a key.
  - The preview's status line shows `MAIN 12 > #3 41 > G83`.
  - Block-by-block mode gets `I` (step into) and `O` (step over).
- **4b:** the call map, from a pre-scan of the program (all calls, including the ones never taken) plus
  the counts from the run, drawn as SVG or as an indented text tree. It also warns about:
  - unused macros;
  - nesting over 8 levels;
  - recursion;
  - L8/L9 used where the canned cycles use them.

## 5. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-27 | (this commit) | Phase 4a: `-flow` / `-flow+` with CALL, RET, CYCLE, LOOP, JUMP, SKIP lines, folding, macro names from the definition comment; loop passes on call blocks counted at the return; tests `flow`, `flowall`, event-count check (95 passed) |
