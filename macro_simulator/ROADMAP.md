# dxsim roadmap

Features planned after PR #5 (the first version) is merged, in the agreed order. Each one is a separate PR,
and all the work stays in `macro_simulator/`. The owner can change the order.

| # | Feature | Status |
|---|---|---|
| 1 | Machine parameters (`-P BMDCPRMS.SYS`) | planned |
| 2 | Cutter diameter compensation (G41/G42) | planned |
| 3 | Fillets and corner radii (G22/G23) | planned |
| 4 | Program flow view: main program → macros → cycles | planned (design below) |
| 5 | z/OS port with GDDM plots | deferred ("way down the road") |

A finished feature leaves this file, and its description moves into `README.md` and `LANGUAGE.md`.

---

## 1. Machine parameters

The aim is to check how a program behaves on one particular machine. Only the parameters that change what
a part program does on paper are read. The rest of the file is ignored on purpose.

**Read**
- **Travel limits:** parameters 17-26, in units of 2,540,000 per inch.
- **Maximum traverse rate:** parameters 7, 8 and 47.
  - These replace the fixed 472 ipm rapid and the feed clamp.
  - The units still have to be traced in the firmware. Until then they are TENTATIVE, and dxsim keeps
    472 ipm.
- **Spindle range maxima.** These replace the S 1-4200 range from the manual.
- **Option word:** parameter 156.
- **Peck clearance:** parameter 272. This replaces `-peck` when the file holds it.

**Ignored:** acceleration, servo gains, following error, encoders, homing, gear shifting and I/O. Those are
machine dynamics, not the language. They can't be checked without the machine, and a wrong model would give
false confidence.

**Options**
- `-P file` reads the parameter file. The format is the same for 6.73 and 6.89, so the TC22's own file
  works as is.
- `-model "VMC-1000/22"` applies that section of `BMDCPRMS.INI` on top of the file. See
  `../BMDCPRMS_INI.md`.
- `-zero X..,Y..,Z..` gives the machine position of part zero. Travel limits are measured from home, so
  without this option only the part's overall size can be checked against the travel.

**Errors and tests**
- Over-travel is an error, reported like the other errors: the line, the block, and a `^` under the axis
  word. A move that only goes over inside a canned cycle is reported against the calling line.
- The tests run both the TC22 file and the 6.89 file.
- Without `-P`, nothing changes.

## 2. Cutter diameter compensation (G41/G42)

- Offset the programmed path by the tool radius, using the control's start-up, corner and cancel rules.
  The rules come from the programmer's manual, cross-checked against the firmware.
- Report the control's errors, such as an arc smaller than the tool radius or an interference.
- The plot shows both the programmed path and the tool-centre path.

## 3. Fillets and corner radii (G22/G23)

- Insert the fillet arc between the two moves, instead of cutting straight to the corner as dxsim does
  today.
- Report the control's error when the radius doesn't fit.
- Works together with item 2.

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

## 5. z/OS port with GDDM plots

Deferred by the owner until the DOS/Windows 98 version is settled. The plot-device layer (`src/dxout.c`)
is where a GDDM device would go. Everything else is already portable C89, including the EBCDIC-safe letter
lookups.
