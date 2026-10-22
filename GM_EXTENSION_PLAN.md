# Extending the DOS DX32 with New G/M Codes

This is a different track from the LinuxCNC replacement. The goal here is to **keep BXX.EXE and the
BMDC card** and add part-program functions the stock control does not offer. Examples are user I/O
from G-code, and later automatic tool-length and diameter probing. The plan is staged so each phase
ends with something testable on the machine.

Confidence labels follow the repo convention: CONFIRMED = traced in firmware or verified against
MAINTPG; TENTATIVE = inferred, needs a bench test.

---

## 1. Where an extension can live

| Layer | How it is changed | Cost / risk | Use it for |
|---|---|---|---|
| **A. Hidden firmware features** | Set a parameter (mailbox 0x1c / MAINTPG) | None, reversible | Anything the firmware already implements but the machine has switched off |
| **B. Firmware patches** | Patch `BMDC.BIN` on disk; it is loaded into card RAM at every start, so no EPROM work (same approach as `tools/patch_bmdc_ram.py`) | Low; the original file stays as the fallback | New M/G codes, new behaviour inside the motion and I/O tasks |
| **C. DOS-side tools** | New small DOS programs (or a TSR) that use the PC→BMDC mailbox | Medium; they cannot run at the same time as BXX unless written as a TSR | Diagnostics, set-up utilities, anything that needs files or the screen |
| D. Patching BXX.EXE | Binary patch of a 577 KB program with no symbols | High | Avoid unless unavoidable (for example, if BXX must learn about new tool-table data) |

Recommendation: use **A** wherever possible, **B** for new codes, and **C** for bench tools. Leave BXX
untouched. The firmware parses the part-program text itself (letter table 0x1ee84, see
`MOTION_PLANNER.md`), so new codes can be added entirely on the card side.

## 2. Phases

| Phase | Goal | Main work |
|---|---|---|
| **1** | Read the machine I/O state and switch individual outputs on/off | This document, section 3. Mostly layer A: the firmware already has user I/O M-codes |
| 2 | Understand and use the firmware's **M-code macro** mechanism; build a patch framework for new M-codes | Trace `M<n>:` definitions (section 4); find free space in `BMDC.BIN`; hook the unknown-M fallback in the dispatcher `FUN_0002ca1e` |
| 3 | Tool-length probing as a G/M code | Reuse probe move type 5 and the latch at 0xE70 (`BMDC_PROBE_INTERNALS.md`). Solve how a measured length gets into BXX's tool table (`wrTool`/`rdTool` ownership) |
| 4 | Tool-diameter probing, packaging | Probe cycles, `tools/patch_bmdc_*.py`, test part programs, operator notes |

---

## 3. Phase 1: I/O status and output control

### 3.1 The I/O words are firmware parameters (CONFIRMED)

Every machine I/O status/command word is a slot in the firmware parameter table (0x2a536, see
`BMDC_PARAM_TABLE.md`). The PC reads a slot with mailbox command 0x1b and writes it with 0x1c. The
0x1c handler (0x2ad24) has **no write protection**: it stores the value straight to the slot's RAM
address with the slot's type. MAINTPG uses exactly these two commands (its wrappers are at image
offsets 0x7432 and 0x7510, both calling its `doPcomCmd` copy at 0x7326).

Bit names come from `MAINTPG1.DAT`. Bit numbers are bits of the parameter **value** (word bit 10 of
param 161 is bit 2 of byte 0x31ee, because the 68030 is big-endian).

**Status (read):**

| Param | Address | Type | Content |
|---|---|---|---|
| 150 | 0x31e0 | byte | Machine Faults: b0 drive fault, b1 spindle ctrl, b2 aux ctrl, b3 tool changer, b4 BMDC, b5 power not enabled, b6 spindle not enabled, b7 axis limit |
| 151 | 0x31e1 | byte | Axis limits: b0 X, b1 Y, b2 Z, b3 W, b4 S |
| 152 | 0x31e2 | byte | Aux limits: b0 E-stop, b1 spindle overload, b2 spindle overheat, b3 door |
| 153 | 0x31e3 | byte | Drive faults X/Y/Z/W/S in b0-b4 |
| 154 | 0x31e4 | byte | Machine status: b0 drives on, b1 rotary brake, b2 rotary active, b3 door guard |
| 155 | 0x31e5 | byte | Home switches X/Y/Z/W/S in b0-b4, **b7 touch probe** |
| 156 | 0x31e6 | word | Option status: b8 low air, b9 low lube, b10 index in, b11 index out, b14 lube pressure, b15 ext reset |
| 158 | 0x31e8 | long | Back-end switches: pendant buttons, jog axis select, hold, cycle start (b16-b31) |
| 164 | 0x3252 | byte | Spindle status: ready, zero speed, at speed, home, brake, low/high gear, shot pin |
| 171 | 0x3268 | byte | TLAUF inputs (tool #1, count, arm out/back, unclamped, clamped, arm down/up) |
| 277 | 0x3248 | long | GPAUF1 inputs 0-31 = AUF **long-RX** word 0 |
| 278 | 0x324c | long | GPAUF1 inputs 32-63 = AUF long-RX word 1 (b16-b23 tool-changer switches) |

Params 150-156 are decoded from the AUF **short** RX packet by `FUN_00014f9e`, which is the input path
this machine actually uses (`AUF_PACKET_FORMAT.md`). Whether the AUF also reports the same inputs in
the long RX words 277/278 on a DX32 is **TENTATIVE**; it matters for M5xx/M6xx below.

**Command (write) words, the "semantic" layer:**

| Param | Address | Type | Bits |
|---|---|---|---|
| 160 | 0x31ed | byte | b0 axis drives, b1 rotary brake release, b2 door shot-pin, b6 Z drive, b7 limit override |
| 161 | 0x31ee | word | b8 work light, b9 lube pump, b10 mist, b11 flood, b12 indexer, b13/b14 conveyor fwd/rev, b15 ext reset |
| 163 | 0x3250 | word | spindle: b0 xmsn pin, b1/b2 xmsn gear, b8 drive, b9 high gear, b10 fwd, b11 rev, b14 off light, b15 brake |
| 172 | 0x326a | byte | TLAUF outputs (non-VMC only; VMC uses the pocket command protocol) |

The firmware changes these words with single `bset`/`bclr` instructions when events happen (M-codes,
door open/close, faults). It does **not** rebuild them every tick. 1 = ON: for example the door-close
path restores coolant with `bset #2/#3, 0x31ee` = word bits 10/11 (0xfb1a, 0xfc30).

**Wire-level output words** (copied into the long TX packet `_DAT_00003218`):

| Param | Address | Content |
|---|---|---|
| 279 | 0x31f0 | GPAUF1 outputs 0-31 = long-TX bytes 4-7. Written only by M3xx/M4xx and by the reload at 0x367d4 below (raw-binary search for every access) |
| 280 | 0x31f4 | GPAUF1 outputs 32-63 = long-TX bytes 0-3. MAINTPG names: b0 spindle safe light, b1 spindle fwd, b2/b3 gear, b4 shot pin, b6 lube, b8 door guard, b9 A-brake release, b10 mist (M07), b11 flood (M08), b12/b13 conveyor (M36/M38), b14 index air (M51), **b15 Z-axis brake release**, b25 air blast, b26 drawbar, b27 arm brake, b28/b29 carousel rev/fwd, b30/b31 arm back/out |

Bits of 0x31f4 that the per-cycle I/O mapper drives for this machine's configuration (bits 1, 5, 6, 7,
12-15 per `AUF_PACKET_FORMAT.md`) are re-asserted every cycle, so writing them from outside has no
lasting effect. The other bits keep whatever value they are given, with one exception: the routine at
0x367d4 (taken when its D0 == 3, next to message-tag checks for 'Pars', 'PPPL' and 'auxF') reloads
**both** words wholesale from 0xc53a/0xc53e. Nothing writes those two longs by absolute address, so in
practice this clears every user output. It looks like a reset/abort path (TENTATIVE). Expect M3xx
outputs to drop on that event.

### 3.2 The firmware already has user I/O M-codes (CONFIRMED)

The M-code dispatcher is **`FUN_0002ca1e`**. When a block carries an M word (block flag bit 13, M
number at block offset +6 = `mWord` of `MOVEBUFFER`), it searches a word list and calls the matching
handler from a parallel jump table:

| Machine family (param 98 bits 28-31) | M list | Handlers | Entries |
|---|---|---|---|
| 3 | 0x2d312 | 0x2d572 | 0x11a |
| any other (this DX32 is family 2) | **0x2cbf8** | **0x2ce7e** | 0x124 |
| tool-change block path (0x7780 == 5) | 0x2ce40 / 0x2d546 | same tables | short list: M0-M2, M6, M20-M22, M25, M26, M30, M63 (no M3xx-M6xx) |

M-codes that are not in the list are ignored, unless param 98 bits 0-3 == 1, in which case 0x320b0
passes the number to a device mailbox at 0xbff1/0xbff3 (purpose unknown).

The mill list (this machine) contains M0-M9, M12-M17, M19-M22, M25, M26, M30, M36-M38, M51, M60-M63,
M71, M72, M900, M901, M999 and four blocks of 64:

| Code | Handler | Action |
|---|---|---|
| **M300+n** | 0x30ba8 | set output bit n: n = 0-31 → bit n of **0x31f0** (param 279); n = 32-63 → bit n-32 of **0x31f4** (param 280) |
| **M400+n** | 0x30bf6 | clear the same bit |
| **M500+n** | 0x30c44 | wait (yielding through 0x2c9c6) until input bit n = 1: n = 0-31 → **0x3248** (param 277), 32-63 → **0x324c** (param 278) |
| **M600+n** | 0x30cb2 | wait until input bit n = 0 |

After the bit operation each handler calls 0x30b76. This copies 0x31f4/0x31f0 straight into the long-TX
staging words 0x3218/0x321c, so the output goes out on the next AUF exchange, and snapshots the long-RX
inputs into 0x3220/0x3224.

**They are switched off on this machine.** Each handler starts with:

```
btst  #3, $c519      ; param 285 "Control Options" (long at 0xc516), bit 3 = "Additional M-codes"
bne   ok
tst.b $c5c0          ; M-macro nesting depth (section 4)
bne   ok
trap  #14            ; alarm, data words 0200 0003 3d5b
```

`BMDCPRMS.SYS` stops at parameter 226 and RAM 0xc516 starts as 0, so the option bit is clear (TENTATIVE
until read back on the machine). Parameter 285's bits per MAINTPG1.DAT: b0 rigid tapping, b1 tool life
management, b2 parts counter/cycle timer, **b3 additional M-codes**, b4 parametric part programming,
b5 digitizing. Bits 0, 1, 2 and 4 are tested elsewhere in the firmware (0x20a40, 0xea7a, 0x10866,
0x1edc4). Those are further hidden options worth a later look; parametric programming in particular
could matter for probing.

### 3.3 Ways to enable the I/O M-codes

1. **Live parameter write (try first).** Write param 285 = 8 with MAINTPG's parameter editor
   ("Edit Machine Values") or any mailbox 0x1c client. This is lost on power-down. If BXX reloads
   `BMDC.BIN` and the parameters when it starts, it is also lost when you leave MAINTPG and start BXX.
   Whether BXX reloads a running card is not known yet; this is a bench question.
2. **Call them from inside an M-macro.** A non-zero `0xc5c0` also passes the gate, so M3xx-M6xx inside a
   user macro body work without the option (TENTATIVE, depends on section 4).
3. **Firmware patch (persistent).** Make the gate's first branch skip straight to the handler body.
   Each handler has the same layout, so the patch is four 2-byte edits:

   | File offset | Original | Patched | Effect |
   |---|---|---|---|
   | 0x30bb0 | `66 06` | `60 14` | M3xx: `bra` to 0x30bc6 |
   | 0x30bfe | `66 06` | `60 14` | M4xx: `bra` to 0x30c14 |
   | 0x30c4c | `66 06` | `60 14` | M5xx: `bra` to 0x30c62 |
   | 0x30cba | `66 06` | `60 14` | M6xx: `bra` to 0x30cd0 |

   Changing `bne` to `bra` with the original displacement would be wrong: the target 0x30bb8 is a
   second `bne` that still sees the `btst` flags, so it would fall into the alarm. Setting a non-zero
   default for 0xc519 in the image is also less certain, because nothing guarantees the variable area
   is not cleared during initialization. (The boot RAM test at 0xc78a restores what it tests, so it is
   not the problem.) Not built yet; it would become `tools/patch_bmdc_mcodes.py` next to the RAM patch.

### 3.4 Mailbox protocol for a DOS-side I/O tool (CONFIRMED from B3T and MAINTPG)

Dual-port RAM is at PC segment D400 (68030 0x01200000). The ISA side sees 68030 words with the bytes
already in PC order, so a 16-bit read or write needs no swapping. A 32-bit value is two words, with the
**high word at the lower address**.

```
dp[0x404] = param_number                      ; word
; write only: dp[0x406..0x411] = 12-byte packed decimal, 6 words, high word of each long first
dp[0x400] = 0x8000 | cmd                      ; 0x1b read, 0x1c write
wait until (dp[0x400] & 0x8000) == 0          ; B3T timeout 0x6e
wait until (dp[0x402] & 1) == 0               ; busy (mcsPcomBusy), timeout 0x157c
error = dp[0x402] & 2                         ; mcsPcomError, also set for an index past the table
; read only: value at dp[0x406..0x411]
```

68882 packed-decimal real (96 bits, three longs):

```
long0: [SM][SE][Y][Y] [E2 E1 E0: 3 BCD exponent digits] [E3: 4 bits] [8 zero bits] [D16: integer digit]
long1: D15..D8   (8 BCD fraction digits)
long2: D7..D0
value = (-1)^SM * D16.D15...D0 * 10^((-1)^SE * E2E1E0)
```

Examples: 8 = `00000008 00000000 00000000`; 3072 = 3.072e3 = `00300003 07200000 00000000`.
The firmware converts with `fmove.w/.l`, so a long parameter with bit 31 set is written as a negative
number (BMDCPRMS.SYS shows the same convention: param 163 = -16384).

A write is a whole-word replacement and the PC cannot do it atomically. That is harmless for 0x31f0,
which only M-codes touch. For the event-driven command words (160/161/163), a read-modify-write that
loses a race against the firmware (door logic, M09, a fault) can undo the firmware's change, and nothing
re-asserts it. For those words, write only with the machine idle, or use the M-codes instead. Mailbox
command 0x20 (`setCoolant`) is the firmware's own coolant entry point; its argument is a word at +0x404.

MAINTPG and BXX are both plain DOS programs that own the mailbox, so a standalone tool runs instead of
BXX, not beside it. A TSR that shares the mailbox with BXX is possible but only worth building if
Phase 3 needs live PC-side help.

### 3.5 Bench procedure for Phase 1

Machine in a safe state: E-stop within reach, spindle stopped, no tool in the changer arm, Z resting
on something or with its brake known to hold. **Do not touch 0x31f4 bit 15 (Z-axis brake release),
bit 9 (A brake), bit 8 (door guard) or bits 25-31 (tool changer) during these tests.**

1. **Read-only survey with MAINTPG.** Record the live values of 150-156, 158, 160, 161, 163, 164,
   171, 277-280 and 285, and the firmware revision (param 0). Then operate switches one at a time:
   door, a home switch pressed by hand, low-air (by bleeding the regulator), E-stop. Note which bits
   change in **both** the decoded words (150-156) and the raw long-RX words (277/278). This settles
   whether M5xx/M6xx can see the main inputs on a DX32.
2. **Confirm live writes.** In MAINTPG write param 161 = 2048 (flood, b11) and check that the coolant
   pump runs. Then write 0. This proves MAINTPG's "Edit Machine Values" path uses mailbox 0x1c.
3. **Enable additional M-codes** (param 285 = 8). Read it back. Exit MAINTPG, start BXX, and find out
   whether the value survived (for example with a one-line program, step 4).
4. **Test program** (MDI or a short program). Flood is a harmless first target on a bit the mapper
   does not own on this machine:
   ```
   M343      (0x31f4 bit 11 = flood, set)
   G4 P2
   M443      (clear)
   M300      (0x31f0 bit 0, set: watch the AUF output LEDs for anything that changes)
   G4 P2
   M400
   ```
   If M343 does nothing, the AUF on this machine takes coolant from the short TX packet instead. In that
   case, map which long-TX bits do reach physical terminals by stepping M300-M331 and M332-M363 (skipping
   the bits listed above) and watching the AUF LEDs. The result goes into `AUF_PACKET_FORMAT.md`, which
   currently marks the long-TX→terminal mapping as untraced.
5. **Wait-for-input:** `M5nn` / `M6nn` against a switch found in step 1, for example a spare AUF input
   wired to a push button. If the program hangs on M5xx forever, the bit is not in the long RX word on
   this machine.

Results of steps 1, 3 and 4 decide Phase 2: if the native M-codes reach the outputs that matter, the
Phase 1 deliverable is only the enable patch plus a short operator note.

---

## 4. Leads for Phase 2: the M-code macro mechanism (TENTATIVE)

The M letter handler (0x1f4e8) has a second mode that is active while 0xaefb bit 5 is set (apparently
a pre-scan/load pass). A word written as **`M<n>:`** stores n in a definition list at 0x3b44 (count at
0x3b80, room for about 0x77 entries) and records the position through 0x200c4. At run time, an
`M<n>` that matches a list entry does the following:

* sets 0x32e3 bit 0 (call pending) and increments the nesting depth **0xc5c0**;
* at depth 0, saves the dwell slot 0x3fcc → 0x3fd0, and unless 0xc539 bit 3 is set, sets 0x32e3 bit 4
  and 0x4088 bit 2;
* M6 matching a definition sets 0xc539 bit 2. On return, the routine at 0x20176 decrements 0xc5c0;
  when that was the last level of an M6 macro it queues **M63** (M60 on family 3) with the T number,
  so the real tool change still happens after the user's macro.

Defining M900-M907 or M974/M975 sets configuration bits 0xc525 b0-b7 and 0xc524 b3/b2 instead of only
listing them, which looks like reserved hook numbers. The S/T path at 0x1fc6e keeps a second
definition list at 0x3b6c (count 0x3b84) and loads the number into the carousel target 0xaecc.

So the stock firmware seems to support **user-defined M-code subprograms, including a user M6**. That
is the natural home for a probing cycle: `M6` → user macro → tool change → probe move → store length.
Phase 2 work:

1. Find the return code (what reaches 0x20176) and the exact definition syntax, and check how BXX
   passes such programs (macros must be in the program buffer the firmware scans).
2. Try a minimal macro on the machine (`M1000:` body ending with the return code; then call `M1000`).
3. Find free space in `BMDC.BIN` for patch code. The image ends at 0x4bffa and the stacks start at
   0x4d000. Then decide whether new codes go into the dispatcher's unknown-M fallback (0x2cb0e) or
   replace unused list entries (for example M71/M72 if unused on this machine).

## 5. Leads for Phase 3: probing

* Probe move type 5 (executor 0x1aa46) and the latch ISR 0x11cd6 → 0xE70 exist; there are no
  G-code probing cycles (`MOTION_PLANNER.md`). A new M-code handler can queue a type 5 block through
  0x2b4d6 and read the latch, the same way the mailbox `probePoint` (0x2b0a8) does.
* The tool table lives on both sides. BXX writes it to the card with `wrTool` (mailbox 0x10) and reads
  it with `rdTool` (0x11), and `dpMSS` mirrors the active tool length and diameter (`msToolLen`,
  `msToolDia` at dual-port +0x480+0xdc/+0xe8). A firmware-side measurement only sticks if BXX reads it
  back. Finding out whether BXX ever calls `rdTool` and saves the result is the key Phase 3 question
  on the DOS side.
* Param 155 bit 7 (touch probe switch) gives a polled view of the probe input for testing without
  moving an axis.
