# I/O Control on the Stock DX32 (DOS + BMDC)

How to read the machine I/O state and switch individual outputs without changing BXX. Part of the
track in `GM_EXTENSION_PLAN.md`; the G/M codes that drive I/O from a part program are in
`EXT_CODES.md`. Confidence labels follow the repo convention: CONFIRMED = traced in firmware or verified against
MAINTPG; TENTATIVE = inferred, needs a bench test.


> **TC22 (the machine under test, firmware rev 6.73):** the addresses and several parameter numbers
> below are for the repo-root firmware (rev 6.89). On the TC22: params 156/161 are bytes (flood =
> param 161 bit 3 = 8, mist = bit 2 = 4), the user I/O words are param **157** (inputs) and **162**
> (outputs), and params 277-285 do not exist. See `TC22_VALIDATION.md` sections 2-3.

---

## 1. The I/O words are firmware parameters (CONFIRMED)

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
the long RX words 277/278 on a DX32 is **TENTATIVE**; it matters for M5xx/M6xx (`EXT_CODES.md`).

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

## 2. Mailbox protocol for a DOS-side I/O tool (CONFIRMED from B3T and MAINTPG)

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

## 3. Bench procedure (Phase 1)

The full test plan with result tables is `PHASE1_TEST_PLAN.md`; this is the short version.

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
3. **Enable additional M-codes** (param 285 = 8, see `EXT_CODES.md` section 2). Read it back. Exit MAINTPG, start BXX, and find out
   whether the value survived (for example with a one-line program, step 4).
4. **Test program** (codes in `EXT_CODES.md` section 1) (MDI or a short program). Flood is a harmless first target on a bit the mapper
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
