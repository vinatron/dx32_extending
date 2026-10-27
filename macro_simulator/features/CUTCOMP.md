# Cutter diameter compensation (G40 / G41 / G42, G48 / G49, G44 / G45)

Roadmap item 2. Sources:
- **M:** programmer's manual, chapter 7 (pages 57-67), the G-code notes on pages 19-20, and chapter 8
  (constant surface feed);
- **F:** the 6.89 firmware (`../../BMDC.BIN`), with the disassembler `../tools/m68k_dis.py`.

## 1. Status

| Phase | What | Status |
|---|---|---|
| 0 | Research: manual chapter 7, firmware map, design, test list (this document) | **done** |
| 1 | Firmware trace of the geometry engine (0x2412e ...) and the state machine (0x1d03e): settle the open questions in section 7 | **done**: sections 2.2-2.4 |
| 2 | `src/dxcomp.c` geometry: offset line/arc, intersections, blend arcs, with unit tests | **done**: 31 checks in `dxsim -selftest`, run by `dxtest` |
| 3 | Look-ahead pipeline: turn-on, approach, corners, exit, Z-only moves, G48/G49; trace and plot both paths | **done** |
| 4 | Rules, errors and warnings; errors reported at the right line despite the look-ahead | **done** (one rule left as an open question, 7) |
| 5 | G45 constant surface feed, the live preview, `README.md` / `LANGUAGE.md` | **done** |
| 6 | Bench check on the machine: the test programs on the control's own graphics, compared with dxsim | **waiting for the owner**: bench sheet in section 6 |

Cutter compensation is simulated (phases 0-5). What is left is the bench check on the machine (phase 6).
Until then, every behaviour here rests on the firmware trace and the manual. It is marked CONFIRMED (code)
where it was read from the instructions, but not yet seen on the machine.

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

### 2.2 From the firmware (6.89): codes and state

Everything in this section was traced in the 6.89 code. CONFIRMED means read from the instructions;
TENTATIVE marks an interpretation that the code does not settle on its own.

| What | Where | Status |
|---|---|---|
| **G40:** if the compensation state 0x32f4 ≠ 0, set it to 12 (exit) | G table 0x205e2 entry 40 → 0x20d00 | CONFIRMED |
| **G41 / G42:** call 0x20c86, then set the side bit 0x4974 bit 1 for **G41** and clear it for G42. The bit is inverted when exactly one of X and Y is mirrored: 0x20c86 sets 0x32e7 bit 4 = 0x32ec bit 3 XOR bit 4, the two mirror flags. Bit 1 set = tool **left** (checked against the offset formula, 2.3) | 0x20d0e, 0x20d46, 0x20c86 | CONFIRMED |
| **Radius:** copied from 0x3d88 (the active tool's radius) to 0x4978, and the state set to 2, **only if the radius ≠ 0**. With 0, compensation stays off | 0x20d28, 0x20d60 | CONFIRMED |
| **Negative radius (undersized cutter):** at turn-on the engine flips the side bit and uses \|r\| | 0x24156-0x24166 | CONFIRMED |
| **G44 / G45:** 0x32ec bit 6 clear / set | 0x20d84, 0x20d8c | CONFIRMED |
| **G48 / G49:** 0x32ec bit 7 set / clear (bit 7 = corner rounding **off**) | 0x20db0, 0x20db8 | CONFIRMED |
| **Tolerance:** 0x4adc = 1270 units = 0.0005 in. It decides tangent corners, coincident points, tangent intersections and parallel lines (determinant) | 0x24144 | CONFIRMED |
| **Coordinates:** the records take the block targets 0x4010.. after G73 rotation and mirror. That fits the side swap under mirror: compensation works in the transformed path | 0x1d822 | CONFIRMED (inference from the mirror swap) |
| `Parser: Cannot Comp "C" Axis` (0x32f46) is raised by the C-word handler while 0x32f5 ≠ 0. That is a different state byte from 0x32f4, so this message is **not** about cutter compensation | 0x1f1f6 | CONFIRMED |
| The built-in canned-cycle text never uses G41/G42; the G170 cycles offset by R1 themselves | `../corpus_689.txt` | CONFIRMED |
| **Motion mode while compensating:** the approach move and every compensated element are made as feed moves: 0x32f1 = 1, 2 or 3, never 0 (rapid). They use the record's feed, the modal F when the block was read. **A G0 block inside compensation is therefore cut at the feed rate.** Only the G40 move gets the programmed mode back (0x1d6d0: 0x32f1 = 0x32f0) | 0x1d16a, 0x1d244, 0x1d6d0 | CONFIRMED (code) |

**Block records.**
- **Size:** 0x84 bytes, copied with `dbra` over 33 longs.
- **Builder:** 0x1d6e4 fills the new record at 0x46e0 from the block just read.
- **Fields** (offsets from the record start):

| Offset | Field |
|---|---|
| +1 | flags: bit 1 line, bit 2 arc, bit 3 CW (G2), bit 4 G45, bit 6 G48 |
| +3 | carried Z-only move: bit 0 present, bit 1 its mode |
| +4 | feed |
| +0x10 / +0x1c | end X / Y |
| +0x28 / +0x34 | arc centre X / Y |
| +0x40 | arc radius, computed |
| +0x4c | Z |
| +0x58 / +0x64 | carried Z / its feed |

- **Working copies:** **A** (0x4764), **B** (0x47e8, the element being finished) and **C** (0x48f0, the
  next one). Raw copies sit at 0x4984/0x4a08; a blend-arc record is at 0x4868.
- **The record counter** 0x4ae8 (1, 2, 3, then shifting) is what makes the two-block look-ahead.

**States** (0x32f4, table 0x1d050).
- **Odd vs even:** odd states mean "more output before the next block is read". The block loop sends them
  straight to the dispatcher (0x1c4d2: `btst #0,0x32f4`). Even states wait for the next block.

| State | Code | Does |
|---|---|---|
| 2 | 0x1d0b0 | end of the G41/G42 block: its move is made **uncompensated**; then state 4 |
| 4 | 0x1d0d8 | approach block: record only (no motion yet), first-record flag set; then state 6 |
| 6 | 0x1d0ee | first compensated element: record only; then state 8 |
| 8 | 0x1d10a | second element: record. The engine runs for the first time. The **approach move** is made to the first element's offset start. Its carried Z goes first (state 7). Then state 9 |
| 9 / 10 | 0x1d1d0 / 0x1d1d8 | each new block: record, engine, then the element B is made: its carried Z first, then B to its trimmed end (line, or arc with its offset centre and radius). Then state 11 if a blend arc is due, else 10 |
| 11 | 0x1d3ee | the blend arc: CW for G41, CCW for G42, centre on the programmed corner, end at C's offset start. If its start and end directions from the corner agree within 1e-4, it is made as a straight line (0x1d326). Then state 10 |
| 12 | 0x1d4a6 | G40 block read: its target is saved. C's carried Z goes first (state 15). Then **C is made to its offset end**, the normal at its end point. Then state 13 |
| 13 | 0x1d61c | a pending Z-only move, if any; then the **G40 move to its programmed point, uncompensated**; state 0 |

### 2.3 From the firmware: the geometry engine (0x2412e-0x24e86)

The engine (0x2424c) runs once per new record. It offsets B and C, then joins B to C.

**Line offset** (0x24442), for a line from (x0,y0) to (x1,y1):
- The line is written as `A·x + B·y + C = 0`, with A = y0 - y1, B = x1 - x0, C = x0·y1 - y0·x1.
  (A,B) is the left normal.
- The line is moved by `C ∓ r·√(A²+B²)`: minus for side bit set (G41, left), plus for G42.
- The new start and end are the **feet of the perpendiculars** from the programmed start and end.
- The coefficients are kept for the intersections: 0x4b70.. for the newer line, 0x4b40.. for the older.

**Arc offset** (0x245e2 for B, 0x246e6 for C):
- **G41 + CW, or G42 + CCW:** the tool is outside, so R' = R + r.
- **G41 + CCW, or G42 + CW:** the tool is inside, so R' = R - r.
- **R' < 0** gives `Cutter-Comp: Negative Radius` (trap data → 0x328ee, at 0x24636 and 0x24660 for B,
  0x2473a and 0x24760 for C). R' = 0 is accepted.
- The start and end move radially: `P' = P + (P - centre)·(±r/R)`.

**Corner B→C** (0x247ea):
- **Tangent:** B's offset end and C's offset start within 0.0005 in of each other. Nothing is added. The
  block is then made with G99, deceleration override, automatically (0x1d2e6), which is the manual's
  factor 9.
- **Otherwise,** the sign of the cross product `(B'end - P) × (C'start - P)` (P = the programmed corner),
  with the side, decides the corner type:
  - **outside:** a blend arc is set up (0x24834). Its feed is B's feed; under G45 it is ×1.5 (0x24e86
    with radius 2r).
  - **inside:** marked for intersection (0x486d bit 5).
- **Then, by element types** (0x242e8-0x24404):

| B → C | Inside corner | Outside corner, G49 (default) | Outside corner, G48 |
|---|---|---|---|
| line → line | intersection of the offset lines (0x2489a). Parallel (determinant ≤ tol): blend instead | blend arc | **intersection**: sharp corner (0x2489a) |
| line → arc | line-arc intersection (0x24928) | blend arc | **intersection** (0x24928) |
| arc → line | arc-line intersection (0x24a5a) | blend arc | **intersection** (0x24a5a) |
| arc → arc | circle-circle intersection (0x24b84) | blend arc | **blend arc**: the code has no G48 case for arc → arc |

- **The intersection becomes B's new end.** C's start is left as computed; with no blend, the next record
  starts from B's end.
- **When an intersection does not exist** (line too far from the circle, circles apart or one inside the
  other, beyond the tolerance), the engine falls back to a **blend arc** (0x24834). It then checks that
  C's offset start is one radius from the corner, within the tolerance: 0x24cec, using 0x24e32. If not,
  it stops with `Cutter-Comp: Dummy Radius Won't Fit` (0x3290b, at 0x24d22). With the offsets above this
  holds by construction, so the error marks an inconsistent state rather than an everyday case
  (TENTATIVE: no input found yet that raises it).
- **Choosing one of two intersection points** (0x24d36):
  - If the two candidates are within the tolerance, take either.
  - Otherwise take the one **nearer the reference point**: B's offset end for line → arc, C's offset start
    for arc → line and arc → arc (0x24e4c).
  - If both are equally near (within 2 × tolerance), take the one on the side of the arc's direction:
    the cross product about the arc centre, with the CW flag.
  - If that cross product is exactly 0: `Cutter-Comp: Cannot Pick Intersection` (0x3292f, at 0x24dd4).
- **Gouging is not detected:** an intersection behind the start of an element is used as it is, and the
  tool path reverses (manual figs. 7-13, 7-14).

**G45 constant surface feed** (0x24e86):
- **Arcs:** `F' = F · (R ± r) / R`, with + where the offset arc grows (G41 CW, G42 CCW). This is the
  manual's tool-path radius / part radius.
- **Blend arcs:** the formula is used with R = 2r, so the blend is cut at **1.5 × F**.

### 2.4 Z-only moves during compensation (CONFIRMED)

- **Marking:** with the state ≥ 3, a block with Z and no X/Y sets 0x32e6 bit 6 (Z handler 0x21d14).
- **Saving:** the record builder (0x1d6e4) saves that Z and its feed (0x3df4/0x3e00) and returns without
  making a record (`addq #4,a7`). The block produces no motion yet.
- **Carrying:** the next X/Y block's record carries it (+3 bit 0, Z at +0x58, feed at +0x64).
- **Making it:** when that element is made, the carried Z is made **first, as its own straight Z move**,
  at the element's start point (states 7, 9, 15, 13). This happens after any blend arc of the previous
  corner.
- **Only one fits:** there is one slot, which is the manual's "Z-only moves must be separated by an X/Y
  move" (factor 7).

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
- **dxcomp follows the firmware's model step by step** (2.2-2.4): the A/B/C records, the states, the
  order of emission, the 0.0005 in tolerance, and the same formulas. Where the manual and the code differ,
  the code wins, and the difference is noted here.

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
    normal to the second element's start normal: CW for G41, CCW for G42.
  - **G48:** the offset elements meet at their intersection (a sharp corner), except arc → arc, which is
    always blended. If there is no intersection, a blend arc is used (2.3).
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
4. **Z-only blocks:** they are carried with the next X/Y element and made first, as their own Z move at
   that element's start point (2.4). A second Z-only block before any X/Y move overwrites the slot, as in
   the firmware, and is reported (section 4).
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
| `Cutter-Comp: Dummy Radius Won't Fit` | the fallback blend (inside corner with no intersection) does not start one radius from the corner (2.3) |

**Warnings** cover what the manual forbids. The firmware has no message for these, so the result on the
machine is undefined. dxsim keeps going with the most likely behaviour and says so:

| Warning | Manual |
|---|---|
| G41/G42 with tool diameter 0: compensation is not active | p.59, factor 10 |
| G41/G42 outside G17, or with G2/G3 modal | p.58, p.59 |
| G41↔G42 while compensation is on (ignored: dxsim keeps the first; the firmware would start a new turn-on, 0x20d0e) | factor 1 |
| two co-linear moves in a row (the firmware treats them as tangent and adds G99; the manual forbids them) | factor 5 |
| fewer than 5 blocks between G41/G42 and G40 (4 in BOSS 4-7 mode). dxsim makes what it holds: only an approach → as programmed; approach + one element → approach, then the element to its offset end | factor 6 |
| two Z-only moves without an X/Y move between them | factor 7 |
| an M code while compensation is on | factor 8 |
| G92, G73, or a helix (G12/G13) while compensation is on | p.21, p.33, p.15 |
| a canned cycle while compensation is on: dxsim leaves the cycle's moves **uncompensated** (what the control does is not traced) | ch.6 |
| an approach that is an arc (the firmware makes it a straight line anyway) | p.59 |
| the tool-centre path reverses on an element, or a blend loops more than half a turn: **gouge**, the tool is bigger than the feature (figs. 7-13, 7-14). The firmware does not check this (2.3) | p.65 |
| the program ends with compensation on: the held elements are made as if G40 came, without the move back | (dxsim) |

**Reporting the right line.**
- Because of the look-ahead, the block being read is usually not the one at fault.
- Each `dxcel` keeps its block. `errat()` in `dxcomp.c` reports an error against the element at fault, and
  every delayed move is traced with its own block's line.
- The control may show the block it was reading at the time. That is for the bench check.

## 5. Tests

Each test program has an expected trace, and the key points are also checked by hand in `dxtest.c`, like
the existing manual figures.

| Test (8.3) | Program | Result (hand-checked items in **bold** are also asserted by `dxtest`) |
|---|---|---|
| `ccsq` | M fig. 7-15 example 1: the 1.5 in square, G41, .5 dia | **Approach to (0,.25); edges at Y.25, X1.75, Y-1.75, X-.25; R.25 CW blends at (1.5,0), (1.5,-1.5), (0,-1.5); last element to (-.25,.1); G40 rapid to (-1,1).** Blends at F30 (G45 ×1.5) |
| `cctri` | M fig. 7-15 example 2: triangle | **(-.9736,.1118), (-.2236,1.6118), blend to (.2236,1.6118), (.9736,.1118), blend to (.75,-.25), (-.85,-.25)** |
| `ccentry` | M fig. 7-3: entry with a line, then with an arc. The figure's arc numbers are not on one circle, so the test uses R2.5 about (6.5,0) | approach to (3.85,.2) on the normal of the 3-4-5 line; approach to (3.75,0) radial for the arc, which is offset to R2.75 (G41 + CW = outside, F22 by G45) |
| `ccnotch` | M fig. 7-13, .25 and 1.0 dia | .25: arcs offset to R.875, no warning. 1.0: the offset circles (R.5) do not meet, the firmware falls back to a 286° blend round the notch: gouge warning |
| `ccstep` | M fig. 7-14 example A (G49) and B (G48), 1.0 dia | A: the step's element runs backwards, gouge warning. B: G48 makes the outside corner sharp (1.5,.5) |
| `ccinside` | inside corners: line → arc, arc → line, line → line | **(2.7753,.25), arc to (3.75,1.2247), (3.75,2.75), (.25,2.75), exit (.25,1)** |
| `ccarcarc` | arc → arc inside corner | **the intersection lies on both offset circles (R4.75, R1.75)** |
| `ccneg` | inside arc R.2, .5 dia | error `Cutter-Comp: Negative Radius` at the arc's line |
| `cczero` | G41 with diameter 0 | warning; the programmed path is cut |
| `ccmirror` | ccsq with X mirrored (G31) | the side is swapped: the mirrored square is still cut from outside |
| `ccz` | a Z-only move inside compensation | made at its own feed (F5) after the corner blend, before the next element |
| `ccwarn` | co-linear moves, an M code, two Z-only moves, the end of the program without G40 | the warning lines; the first Z-only move is lost; the held elements are made at the end |

Fig. 7-15 example 3 (the R.75 arc) is not used: the printed program is garbled (`G2X0Y.7510J0`).

**Unit checks (phase 2, done):**
- `dxsim -selftest` runs `dxcself()` in `src/dxcomp.c`. It checks 31 results worked out by hand:
  - line offsets, both sides and oblique;
  - arc offsets inside and outside, and the negative-radius case;
  - corner types;
  - line-line, line-circle (two points, tangent, none) and circle-circle intersections;
  - the choice of intersection point;
  - the G45 factors, including ×1.5 on blends.
- `dxtest` runs it, so it also runs on DOS and Windows 98 through `RUNTESTS`.

## 6. Phases (checklist)

**Phase 1: firmware trace**
- [x] Geometry engine 0x2412e-0x24e0a: which function offsets, intersects and blends; how G48 joins an
  outside corner.
- [x] When each `Cutter-Comp:` error is raised.
- [x] What each state of 0x1d03e does (turn-on, approach, running, exit); confirm that the approach and
  exit match the manual.
- [x] When the carried Z-only move happens: at the start of the next element, or with it.
- [x] Whether the offset is applied before or after G73/mirror. The firmware works on 0x4010.. after
  transformation: check.
- [x] The context of `Parser: Cannot Comp "C" Axis`.
- [x] Record the results in section 2.2 and close section 7's items.

**Phase 2: geometry**
- [x] `src/dxcomp.c` with offset, classify, intersect and blend functions; add it to `Makefile` and
  `makefile.wat`.
- [x] Unit checks in `dxtest.c`.

**Phase 3: pipeline**
- [x] `dxcel` records A/B/C, turn-on/approach/running/exit, Z-only carry, G48/G49 (`dxcmove`, `dxcgcode`,
  `dxcblk`, `dxcfin` in `src/dxcomp.c`; hooks in `dxmotion.c` and `dxinterp.c`).
- [x] Trace tags `CDC` / `CDC blend`; the programmed path in green in plots (SVG/HPGL/PS) and the preview.
- [x] Tests `ccsq`, `cctri`, `ccentry`, `ccinside`, `ccarcarc`, `ccmirror`, `ccz`.

**Phase 4: rules**
- [x] The errors and warnings of section 4, errors at the element's own line.
- [x] Tests `ccneg`, `ccnotch`, `ccstep`, `ccwarn`, `cczero`.

**Phase 5: finishing**
- [x] G45 feed scaling on arcs and blends: in the trace's F, so the preview's feed pacing uses it.
- [x] The "not simulated" warning removed; `README.md`, `LANGUAGE.md`, `ROADMAP.md` updated.
- [x] Watcom builds (dos, dos32, win32, win32w, DXTEST), `make test` (83 passed), `make test-screen`
  (the cc tests run with their tool options).

**Phase 6: machine (bench sheet for the owner)**

Set tool 1 to the diameter given, part zero anywhere safe, and Z well clear of the table (the programs cut at
Z0 or Z-.1). Load each program and look at it in the control's F5 PREVIEW, or dry-run it, then compare with
`DXSIM PROG.TXT -t 1=D -S` or the trace:

| # | Program | Tool 1 dia | Look for | Result |
|---|---|---|---|---|
| 1 | `tests/CCSQ.TXT` | .5 | rounded corners .25 outside the square; approach ends at (0,.25) | |
| 2 | `tests/CCENTRY.TXT` | .5 | approach on the normal of the first element; blend at (8,3) | |
| 3 | `tests/CCSTEP.TXT` | 1.0 | example A: the tool backs up at the step (gouge); example B: sharp corner at (1.5,.5) | |
| 4 | `tests/CCNEG.TXT` | .5 | the control stops with `Cutter-Comp: Negative Radius` (which line does it show?) | |
| 5 | `tests/CCZ.TXT` | .5 | the Z move happens at (1.75,0) after the corner, not before | |
| 6 | `tests/CCNOTCH.TXT` | T1 .25, T2 1.0 | with T2 the tool loops round the notch | |

- [ ] Results filled in; differences recorded in section 2 and fixed in `dxcomp.c`.

## 7. Open questions

Closed by phase 1:
- **G48 outside corners:** intersection, except arc → arc, which is always blended (2.3).
- **The carried Z-only move:** made first, as its own move, at the next element's start (2.4).
- **Negative diameter:** side flipped, \|r\| used (2.2).
- **`Dummy Radius Won't Fit`:** the consistency check after the fallback blend (2.3).
- **Coordinates:** after G73 and mirror (2.2).

Still open:
1. **Mirror flags:** which of 0x32ec bits 3/4 is X. Only the XOR matters for the side.
2. **Non-uniform G73 scale with arcs:** what the control does. dxsim will warn.
3. **Behaviour in cases the manual forbids** (M codes, Z-only pairs): only the machine can show it.
   dxsim warns and does what the code would do.
6. **Blocks without a move** (an F, S or T word alone, an assignment or a condition) while compensating:
   the firmware's dispatcher may build a zero-length record for them. That is not traced. dxsim ignores
   them and warns only for M codes (factor 8).
7. **G41/G42 again while compensating, and canned cycles while compensating:** not traced. dxsim warns,
   keeps the first G41/G42, and leaves cycle moves uncompensated.
4. **An input that raises `Dummy Radius Won't Fit`:** none found yet. dxsim makes the same check, so a
   case would show up in testing.
5. **Gouging:** the manual's figs. 7-13 and 7-14 are the bench test (phase 6). dxsim reproduces the
   firmware's arithmetic, so the paths should match the control's graphics.

## 8. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-26 | 100c783 | Phase 0: manual chapter 7 summarised; firmware map (G40-G49 handlers, side bit and mirror XOR, zero radius = off, state machine 0x1d03e, record builder 0x1d6e4, Z-only carry, the three `Cutter-Comp:` messages); design, test list and phases |
| 2026-10-27 | 1c9e4e1 | Phase 1: geometry engine and state machine traced. G41 sets the side bit (left); negative radius flips the side; line/arc offset formulas; corner classification; the intersection/blend table including G48 (arc → arc always blended); intersection choice; the three error conditions; G45 ×(R±r)/R and ×1.5 on blends; automatic G99 on tangent corners; the Z-only carry made as its own move; the full state sequence for turn-on, approach, running and exit. `Cannot Comp "C" Axis` is not about cutter compensation |
| 2026-10-27 | 5664f35 | Phase 2: `src/dxcomp.c` geometry (`dxcoffl`, `dxcoffa`, `dxccorn`, `dxcxll`, `dxcxla`, `dxcxaa`, `dxcpick`, `dxcg45`), written from the firmware formulas; `dxsim -selftest` with 31 hand-checked results, run by `dxtest`. Also found: G0 inside compensation is cut at feed (2.2) |
| 2026-10-27 | (this commit) | Phases 3-5: the look-ahead pipeline after the firmware states (turn-on, approach, two-block look-ahead, blends, intersections, G48, Z-only carry, exit), errors at the element's line, the manual's rules as warnings plus gouge detection, G45 feeds, `CDC` trace tags, the programmed path in green. 12 test programs (`cc*.txt`) and 4 hand checks in `dxtest`; 83 passed. Phase 6 bench sheet added |
