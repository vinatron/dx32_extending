# Cutter diameter compensation (G40 / G41 / G42, G48 / G49, G44 / G45)

Roadmap item 2. Sources:
- **M:** programmer's manual, chapter 7 (pages 57-67), the G-code notes on pages 19-20, and chapter 8
  (constant surface feed);
- **F:** the 6.89 firmware (`../../BMDC.BIN`), with the disassembler `../tools/m68k_dis.py`.

## 1. Status

| Phase | What | Status |
|---|---|---|
| 0 | Research: manual chapter 7, firmware map, design, test list (this document) | **done** |
| 1 | Firmware trace of the geometry engine (0x2412e ...) and the state machine (0x1d03e): settle the open questions in section 7 | not started |
| 2 | `src/dxcomp.c` geometry: offset line/arc, intersections, blend arcs, with unit tests | not started |
| 3 | Look-ahead pipeline: turn-on, approach, corners, exit, Z-only moves, G48/G49; trace and plot both paths | not started |
| 4 | Rules, errors and warnings; errors reported at the right line despite the look-ahead | not started |
| 5 | G45 constant surface feed, the live preview, `README.md` / `LANGUAGE.md` | not started |
| 6 | Bench check on the machine: the test programs on the control's own graphics, compared with dxsim | not started (needs the owner) |

Until phase 3 is done, `dxsim` keeps today's behaviour: it plots the programmed path and warns once that
cutter compensation is not simulated.

## 2. What the control does

### 2.1 From the manual (CONFIRMED as documentation; behaviour still to be checked on the machine)

- **Codes:**
  - G40 cancel, G41 tool left of the part, G42 tool right. "Left" is seen from behind the tool, looking
    along the direction of motion (M fig. 7-2).
  - With right-hand cutters G41 is climb milling and G42 conventional.
  - G40 is the reset state.
- **Radius:** half the tool diameter of the active tool, from `T__D__`, `T__/tlo/dia` or the tool table.
  - A diameter of 0 means compensation is **not active**, even with G41/G42 (M p.59 note; p.67 factor 10).
  - A negative diameter is an undersized cutter: the offset goes the other way (M p.58).
  - The maximum is ±3.2768 in.
  - The tool must be selected before G41/G42.
- **Plane:** XY only. The control must be in G17 (M p.58; p.16 note 3: no cutter compensation in
  G18/G19). Z is never compensated.
- **Turn-on** (M p.59, fig. 7-3). Three blocks:
  ```
  G0(G1)G41(G42)X__Y__     turn-on point: NOT compensated, the tool goes straight there
  G0(G1)X__Y__             approach point: a linear move
  (G1,G2,G3)X__Y__ ...     first compensated element
  ```
  The approach move ends on the normal to the first element at its start point, at one tool radius from
  the approach point. Turn-on needs G0 or G1 modal.
- **Look-ahead:** the control holds three blocks: the one moving and the next two. The offset of a move
  depends on the next move (M p.59).
- **Corners:** the angle is measured on the workpiece side (M fig. 7-4).
  - **Over 180°, inside corner:** the two offset elements are cut back to their intersection (figs. 7-5 to
    7-8: line-line, line-arc, arc-line, arc-arc).
  - **Under 180°, outside corner:**
    - With **G49** (corner rounding on, the reset state), a blend arc of tool radius around the
      programmed corner joins the two offset elements (figs. 7-9 to 7-12).
    - With **G48**, "adjacent moves are carried out as they are defined" (fig. 7-14 example B).
  - **Exactly 180°:** tangent, no corner.
  - G48/G49 may be switched while compensation is on (factor 13).
  - BOSS 4-7 compatibility mode resets to G48 (M p.67 factor 12, and p.1 note 8).
- **Exit** (M p.66, fig. 7-15):
  ```
  (G1,G2,G3)X__Y__        last compensated element: ends on the normal at its end point, one radius out
  G40X__Y__               turn-off point: NOT compensated, the tool goes straight there
  ```
- **Gouging is not prevented:**
  - an oversize cutter in a notch (fig. 7-13);
  - a step smaller than the cutter radius (fig. 7-14).
- **Rules** (M p.67 "Factors affecting use of cutter compensation"):
  1. G41↔G42 cannot be changed, and the amount cannot be changed, without G40 first.
  2. G45 constant surface feed (the reset state; G44 turns it off) scales the feed on arcs by
     tool-path radius / part radius.
  3. A block without tool movement cannot be programmed with compensation on.
  4. Blocks shorter than .25 s may dwell.
  5. Co-linear moves cannot be programmed.
  6. At least 5 blocks are needed: G41/G42, three compensated points, then G40. BOSS 4-7 mode needs 4.
  7. Z moves are not compensated. Several Z-only moves must be separated by an X/Y move.
  8. No M codes from the block after G41/G42 to the block after G40.
  9. G99 is generated automatically for tangent moves, and G99 in the program is stripped.
  10. The diameter must be non-zero.
  11. G48 / G49, as above.
- **Other chapters:**
  - no compensation with helical interpolation (M p.15 note 1);
  - none with G79, G77 or G78 (M ch.6);
  - no G92 while compensation is on (M p.21 note 4);
  - G73 must be set before G41/G42 and cancelled after G40 (M p.33 note 4);
  - polar data may be used with compensation (M ch.4, polar coordinates note 4).

### 2.2 From the firmware (6.89)

| What | Where | Status |
|---|---|---|
| G40: if the compensation state 0x32f4 ≠ 0, set it to 12 (exit) | G table 0x205e2 entry 40 → 0x20d00 | CONFIRMED |
| G41 / G42: call 0x20c86, then set or clear the side bit 0x4974 bit 1 (G41 clears it, G42 sets it). The bit is **inverted when exactly one axis is mirrored**: 0x20c86 sets 0x32e7 bit 4 = 0x32ec bit 3 XOR bit 4, the X and Y mirror flags (TENTATIVE: which bit is X) | 0x20d0e, 0x20d46, 0x20c86 | CONFIRMED (code); mirror reading TENTATIVE |
| The radius is copied from 0x3d88 (the active tool's radius) to 0x4978, and the state set to 2 (turn-on), **only if the radius ≠ 0**: zero leaves compensation off, as the manual says | 0x20d28, 0x20d60 | CONFIRMED |
| G44 / G45: 0x32ec bit 6 clear / set (constant surface feed) | 0x20d84, 0x20d8c | CONFIRMED |
| G48 / G49: 0x32ec bit 7 set / clear (bit 7 = corner rounding **off**) | 0x20db0, 0x20db8 | CONFIRMED |
| Compensation state machine: 16 states, `jmp` through table 0x1d050, indexed by 0x32f4 | 0x1d03e | CONFIRMED (structure) |
| States seen: 0 normal; 2 turn-on, then 4; 4 → 6 and 6 → 8 build the first look-ahead records; 7-10 the running states, which take records from two buffers at 0x4768.. and 0x47ec.. (stride 0x84: two blocks of look-ahead); 11; 12 exit (set by G40); 13; 15 | 0x1d0b0-0x1d6dc | TENTATIVE (meaning of each state) |
| Block record builder 0x1d6e4 copies into the record at 0x46e1..: flags (bit 4 G45, bit 6 G48, bit 1 linear, bit 2 arc, bit 3 CW), end point X/Y (0x4010/0x401c), arc centre (0x4058/0x4064), arc radius (computed, 0x4720), Z (0x472c). It then calls the geometry engine 0x2412e | 0x1d6e4-0x1d868 | CONFIRMED (fields), TENTATIVE (names) |
| **Z-only move during compensation:** the Z handler sets 0x32e6 bit 6 (0x21d14) when the state is ≥ 3. The record builder then saves that Z and its feed (0x3df4/0x3e00) and **carries them into the next X/Y record** (0x46e3 bit 0, Z at 0x4738) instead of making a block of its own. There is room for one such Z, hence the manual's rule that Z-only moves need an X/Y move between them | 0x21d0c, 0x1d6e4 | CONFIRMED (code), TENTATIVE (exact timing of the Z move) |
| Geometry engine: functions 0x2412e-0x24e0a | 0x2412e | to trace (phase 1) |
| Error `Cutter-Comp: Negative Radius` (0x328ee), raised at 0x24638, 0x2465e, 0x2473c, 0x24762: an offset arc whose radius would be ≤ 0, e.g. an inside arc smaller than the tool | 0x328ee | message CONFIRMED, cause TENTATIVE |
| Error `Cutter-Comp: Dummy Radius Won't Fit` (0x3290b), raised at 0x24d28 | 0x3290b | message CONFIRMED, cause to trace (probably the G49 blend or the approach) |
| Error `Cutter-Comp: Cannot Pick Intersection` (0x3292f), raised at 0x24dda: an inside corner whose offset elements do not intersect | 0x3292f | message CONFIRMED, cause TENTATIVE |
| Error `Parser: Cannot Comp "C" Axis` (0x32f46) from 0x1f202 | 0x1f202 | CONFIRMED (message); context to trace |
| The built-in canned-cycle text never uses G41/G42; the G170 cycles offset by R1 themselves | `../corpus_689.txt` | CONFIRMED |

## 3. Design

### 3.1 Where it sits

Motion today goes `dxmotion.c` (line, arc, polar, spiral) → `dxtopos()` (rotation, scale, mirror, offsets)
→ `dxemit()` (trace, travel check, plot). Compensation becomes a stage between `dxtopos()` and
`dxemit()`, in a new file **`src/dxcomp.c`**:

```
dxmotion.c  --dxseg (programmed, base coordinates)-->  dxcomp.c  --dxseg (tool centre)-->  dxemit()
```

- With G40 and no compensation pending, `dxcomp` passes each segment straight through, so nothing changes
  for programs that don't use compensation.
- The offset is computed **after** the transformations, in base part coordinates. With exactly one axis
  mirrored the side is swapped, as the firmware does (2.2). Rotation and uniform scale keep the geometry,
  so offsetting afterwards gives the same result. A non-uniform G73 scale turns arcs into ellipses; that
  case is reported (section 4) rather than guessed.

### 3.2 Data

```c
typedef struct dxcel {          /* one programmed element in the look-ahead */
    dxseg  g;                   /* line or arc, base coordinates, programmed path */
    long   blk;                 /* source block (errors, trace location) */
    int    zonly;               /* a Z-only move carried with this element (firmware 0x46e3 bit 0) */
    double z;                   /* its Z */
    int    g48, g45;            /* modal state when the block was read */
} dxcel;
```

The state lives in `dxsim`:
- `cstate`, following the firmware's states where they are understood;
- `cside` (+1 left, -1 right, after mirror);
- `crad`, the radius fixed at turn-on;
- a ring of 3 `dxcel`: the element being executed plus two read ahead, as on the control;
- the last tool-centre point emitted.

### 3.3 Geometry (pure functions, unit-tested on their own)

- **Offsetting:**
  - a line is offset by `side * r` along its left normal;
  - an arc keeps its centre, and its radius becomes `R + r` or `R - r`, depending on side and direction.
    A result ≤ 0 is the firmware error `Cutter-Comp: Negative Radius`.
- **Corner classification:** compare the direction leaving the first element with the direction entering
  the second, using the tangents at the corner; arcs use the tangent at the shared point. The sign of the
  cross product, together with the side, gives inside, outside or tangent, with a tolerance (1e-6 rad) for
  tangent.
- **Inside corner:** intersect the two offset elements. There are three cases: line-line, line-arc and
  arc-arc, with the arc-line case mirrored. Of the candidates, take the one nearest the programmed corner
  that lies on both elements' forward parts. If there is none, that is `Cutter-Comp: Cannot Pick
  Intersection`.
- **Outside corner:**
  - **G49:** a blend arc with centre at the programmed corner and radius r, from the first element's end
    normal to the second element's start normal, turning the same way as the corner.
  - **G48:** TENTATIVE until phase 1. The working assumption is that the two offset elements are extended
    to meet in a sharp corner, lines along their direction and arcs along their tangent. If they don't
    meet, they are joined by a straight line.
- **Approach** (turn-on): from the turn-on point to `approach point + side * r * left normal of the first
  element at its start`.
- **Exit:** the last element ends at `end + side * r * left normal at its end`, then a straight move to the
  G40 point.

### 3.4 Pipeline

1. **G41/G42 block:** it moves without compensation (`dxemit` directly) and arms the turn-on. The radius
   is fixed here.
2. **Approach block:** it is held. The next block is read. The approach end is computed from the
   first element, and the approach move is emitted.
3. **Running:** each new element is pushed into the ring. When the ring holds the next element, the
   current one is emitted with its trimmed end, or with its end and a blend arc. Emission therefore runs
   one block behind the reading, as the two-block look-ahead does on the control.
4. **Z-only blocks:** they are carried with the next X/Y element, as in the firmware. A second Z-only
   block before any X/Y move is reported (section 4).
5. **G40 block:** the ring is flushed, the last element is given its exit normal, and the G40 move is
   emitted without compensation.
6. **End of program** (M2/M30, or end of text) with compensation still on: the ring is flushed as if
   G40 had been programmed, with a warning.

### 3.5 Output

- **Trace:** the moves are the **tool-centre path**, which is what the machine does. They are tagged `CDC`,
  e.g. `L7  FEED  X1.7500 Y0.0000 Z0.0000  F20.0  CDC`. Blend arcs get their own line, `ARC2 ... CDC blend`.
  The location column shows the block that produced the move, even though it is emitted one block later.
- **Plot and preview:** the tool-centre path is drawn as usual (feed blue/cyan, rapid red). While
  compensation is on, the **programmed path** is also drawn as a thin green line, so both can be seen.
  This works in all views, including `-v all`.
- **Travel check:** `-zero` checks the tool-centre path.

## 4. Errors and warnings

**Errors** use the firmware's messages and stop the run:

| Message | When |
|---|---|
| `Cutter-Comp: Negative Radius` | the offset of an arc has radius ≤ 0 |
| `Cutter-Comp: Cannot Pick Intersection` | an inside corner whose offset elements don't meet |
| `Cutter-Comp: Dummy Radius Won't Fit` | to be defined by the phase 1 trace |

**Warnings** cover what the manual forbids. The firmware has no message for these, so the result on the
machine is undefined. dxsim keeps going with the most likely behaviour and says so:

| Warning | Manual |
|---|---|
| G41/G42 with tool diameter 0: compensation is not active | p.59, factor 10 |
| G41/G42 outside G17, or with G2/G3 modal | p.58, p.59 |
| G41↔G42 or a new diameter while compensation is on (ignored) | factor 1 |
| block without movement while compensation is on | factor 3 |
| two co-linear moves in a row | factor 5 |
| fewer than 5 blocks between G41/G42 and G40 (4 in BOSS 4-7 mode) | factor 6 |
| two Z-only moves without an X/Y move between them | factor 7 |
| an M code while compensation is on | factor 8 |
| G92, a G73 change, a helix (G12/G13), or G77/G78/G79 while compensation is on | ch.4, ch.6 |
| the tool-centre path reverses on an element: **gouge**, the tool is bigger than the feature (figs. 7-13, 7-14) | p.65 |
| the program ends with compensation on | (dxsim) |

**Reporting the right line.** Because of the look-ahead, the block being read is not the one at fault.
Each `dxcel` keeps its block, and a new `dxerrat(s, blk, msg)` reports an error against a given block.

## 5. Tests

Each test program has an expected trace, and the key points are also checked by hand in `dxtest.c`, like
the existing manual figures.

| Test (8.3) | Program | Expected result, worked out by hand |
|---|---|---|
| `ccsq` | M fig. 7-15 example 1: the 1.5 in square, G41, .5 dia | The centre path runs .25 outside. Approach ends at (0, .25); edges at Y.25, X1.75, Y-1.75, X-.25; G49 blend arcs of R.25 round (1.5,0), (1.5,-1.5) and (0,-1.5); the last element ends at (-.25, .1); then G40 to (-1, 1) |
| `cctri` | M fig. 7-15 example 2 (triangle) | worked out when the test is written |
| `ccarc` | M fig. 7-15 example 3 (R.75 arc) | arc offset to R1.0 / R.5 depending on side |
| `ccentry` | M fig. 7-3, both versions: first element a line, then an arc | approach end on the normal of the first element |
| `ccnotch` | M fig. 7-13 with .25 and 1.0 dia | .25: no warning. 1.0: gouge warning |
| `ccstep` | M fig. 7-14 example A (G49) and B (G48) | the step smaller than the radius: gouge warning; B shows the G48 corner |
| `ccinside` | line-line, line-arc, arc-arc inside corners | intersection points |
| `ccneg` | inside arc R.2 with a .5 dia tool | error `Cutter-Comp: Negative Radius` at the arc's line |
| `ccmirror` | ccsq with one axis mirrored | the side flips: the path runs inside the square |
| `ccwarn` | each warning in section 4 once | the warning lines |
| `cczero` | G41 with diameter 0 | warning; programmed path |

Unit checks in `dxtest.c`: the geometry functions with known answers (offset, intersections, blend
direction).

## 6. Phases (checklist)

**Phase 1: firmware trace**
- [ ] Geometry engine 0x2412e-0x24e0a: which function offsets, intersects and blends; how G48 joins an
  outside corner.
- [ ] When each `Cutter-Comp:` error is raised.
- [ ] What each state of 0x1d03e does (turn-on, approach, running, exit); confirm that the approach and
  exit match the manual.
- [ ] When the carried Z-only move happens: at the start of the next element, or with it.
- [ ] Whether the offset is applied before or after G73/mirror. The firmware works on 0x4010.. after
  transformation: check.
- [ ] The context of `Parser: Cannot Comp "C" Axis`.
- [ ] Record the results in section 2.2 and close section 7's items.

**Phase 2: geometry**
- [ ] `src/dxcomp.c` with offset, classify, intersect and blend functions; add it to `Makefile` and
  `makefile.wat`.
- [ ] Unit checks in `dxtest.c`.

**Phase 3: pipeline**
- [ ] `dxcel` ring, turn-on/approach/running/exit, Z-only carry, G48/G49.
- [ ] Trace tags, the programmed path in plots and the preview.
- [ ] Tests `ccsq`, `cctri`, `ccarc`, `ccentry`, `ccinside`, `ccmirror`.

**Phase 4: rules**
- [ ] The errors and warnings of section 4, `dxerrat`.
- [ ] Tests `ccneg`, `ccnotch`, `ccstep`, `ccwarn`, `cczero`.

**Phase 5: finishing**
- [ ] G45 feed scaling on arcs: trace F and preview pacing.
- [ ] Remove the "not simulated" warning; update `README.md`, `LANGUAGE.md` and `ROADMAP.md`.
- [ ] Watcom builds, `make test`, `make test-screen`.

**Phase 6: machine**
- [ ] A short bench sheet for the owner: run `ccsq`, `ccentry`, `ccstep` B and `ccneg` on the control and
  compare its graphics and messages with dxsim. The results flip TENTATIVE items to CONFIRMED.

## 7. Open questions

1. **G48 outside corners:** extended sharp corner, or something else? `Dummy Radius Won't Fit` may belong
   here. Phase 1.
2. **Where the carried Z-only move goes** in the next element: before the X/Y motion, or blended into it?
   Phase 1, then the machine.
3. **Negative diameter:** does the firmware flip the side, or use `|r|` on the other side?
   `Cutter-Comp: Negative Radius` might also be raised for a negative tool radius. Phase 1.
4. **Mirror flags:** which of 0x32ec bits 3/4 is X. Only the XOR matters for the side.
5. **Non-uniform G73 scale with arcs:** what does the control do? dxsim will warn.
6. **Behaviour after a manual-forbidden case** (M codes, co-linear moves): only the machine can show
   it. dxsim warns.

## 8. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-26 | (this commit) | Phase 0: manual chapter 7 summarised; firmware map (G40-G49 handlers, side bit and mirror XOR, zero radius = off, state machine 0x1d03e, record builder 0x1d6e4, Z-only carry, the three `Cutter-Comp:` messages); design, test list and phases |
