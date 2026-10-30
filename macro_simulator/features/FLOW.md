# Program flow view

Roadmap item 4: show how a program moves between its parts. The parts are the main program, `#n` macros,
`M<n>:` macros, canned cycles, `=N` loops, `=Ln` jumps and `?…!` conditions. The design is in
`../ROADMAP.md` section 4.

## 1. Status

| Phase | What | Status |
|---|---|---|
| 4a | `-flow` / `-flow+` text trace | **done** |
| 4c | Moves coloured by the macro or cycle that made them; the call stack on the preview's status line; step into / step over in block-by-block mode | **done** |
| 4b | Call map (`-map`): boxes and arrows, calls never taken drawn dashed, extra warnings | **done** |

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

## 3. Phase 4c (as built)

**`-pc`: colour by source.** Feed moves are coloured by what made them. The innermost source counts:

| Source | Key text |
|---|---|
| main program | `main program` |
| a `#n` macro | `#n "name"` (the name is the definition line's comment) |
| an M-macro | `M100 "name"` |
| a canned cycle | `canned cycle G81`, or `canned cycle G81 in #1 "name"` when a macro called it |

- **Colours:** pass 1 of the plot registers the sources in order of first use (12 colours; the 12th is
  shared by any further sources). Pass 2 draws with them.
- **The key:** printed in a strip under the drawing that `dxplbeg()` keeps free.
- **Unchanged:** rapids stay red and dashed; the programmed path under cutter compensation stays green.
- **Devices:**

| Device | Colours | Key |
|---|---|---|
| SVG | `style="stroke:..."` | coloured text |
| PostScript | `setrgbcolor` | coloured text |
| HPGL | pens 4-8 | `LB` labels |
| Live preview | 12 VGA colours | text rows above the status lines |

**Live preview:**
- **Status line:** shows the call stack, e.g. `MAIN 21 > #1 5` (inside macro #1, called at line 21, now at
  its line 5) or `MAIN 23 > M100 12`. Inside a cycle it ends in `> G81`.
- **Block by block** (B): it stops at every program block, macro bodies included ("step into").
  - **O** at a stop runs the calls that block makes without stopping ("step over"). It applies to that
    block only.
  - **I**, or any other key, goes on to the next block.

**Also added:** a warning for `N20=#1`. On the control it is an assignment to variable N20 (the `=` is
the 4th character), not a macro call. The run then stops with `Parser: #16 Internal Addressing Error`, as
the control does. Test `encall`.

## 4. Tests

| Test | Checked |
|---|---|
| `flow -flow` | the expected trace; `dxtest` also counts the events against a hand count: **CALL 7, RET 7, JUMP 1, SKIP 2, LOOP 3, CYCLE 2** |
| `flowall -flow+` | the same program with the full trace and every pass |
| `flowpc -pc` | `dxtest` checks the SVG key: **3 entries, including "canned cycle G81 in #1"**; `make test-screen` runs it on the preview stub. Step into/over checked by hand with the stub's scripted keys |
| `encall` | the `N20=#1` warning and the control's error |

## 5. Phase 4b: the call map (as built)

`-map FILE` writes the map after the run: an SVG diagram if FILE ends in `.svg`, otherwise an indented
text tree. `src/dxmap.c` builds it from two sources:
- **A scan of the program text** (`dxmapscan`), which finds:
  - every `#n` and `M<n>:` definition;
  - every call site (`=#n`, or `M<n>` of a defined M-macro) and every canned-cycle G code, each with the
    part of the program it sits in (the main program, or the body of a definition);
  - labels L8/L9.
- **The run** (`dxmapcall`, at the same call points as `-flow`): how often each call was made, and the
  deepest nesting.

**Text:**
```
main program
  #1 "Hole pair"  (line 20): 2 times
    #2 "One hole"  (lines 6, 7): 4 times
  M100 "Spot"  (lines 26, 30): 2 times
  canned cycle G81  (line 31): 2 times
deepest nesting in this run: 2 levels (6.89 allows 8)
```

**SVG:**
- **Layout:** the main program on the left, callees in columns by their shortest call depth.
- **Arrows:** labelled with the count and the calling lines. A call never reached is dashed; a macro that
  calls itself gets a loop.
- **Boxes:** a red box for a macro that is called but never defined, a grey dashed box for one that is
  never called. The warnings are listed under the diagram.

**Warnings** (in the map, not in the trace):
- a macro defined but never called;
- a call to a macro that is never defined;
- recursion, a chain of calls in the text that comes back to itself;
- nesting deeper than 8 in the text;
- label L8 or L9 in a program that uses canned cycles, which use those labels too (manual ch.10).

**Limitation:** the counts are per caller and callee, not per call line. M100 above shows lines 26 and 30
with 2 calls, both from line 30 (line 26 is in a skipped branch).

## 6. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-27 | 4dfddc0 | Phase 4a: `-flow` / `-flow+` with CALL, RET, CYCLE, LOOP, JUMP, SKIP lines, folding, macro names from the definition comment; loop passes on call blocks counted at the return; tests `flow`, `flowall`, event-count check (95 passed) |
| 2026-10-27 | 3e1cfae | Phase 4c: `-pc` colours by source with a key (SVG, PS, HPGL, preview); call stack on the preview's status line; O = step over, I = step into; the `N..=#` warning; tests `flowpc`, `encall`, key check (98 passed) |
| 2026-10-27 | (this commit) | Phase 4b: `-map` call map, text tree or SVG diagram, from a scan of the text and the run's counts; map warnings (never called, never defined, recursion, depth, L8/L9 with cycles); tests `fmapw` and two map checks (101 passed). Item 4 is complete |
