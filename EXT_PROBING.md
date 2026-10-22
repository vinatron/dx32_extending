# Probing Extensions

Plans and findings for tool-length and tool-diameter probing as G/M codes on the stock DOS DX32.
Part of the track in `GM_EXTENSION_PLAN.md`. The existing probe internals (BXX probe mode, firmware
probe cycle, position latch, offsets) are in `BMDC_PROBE_INTERNALS.md`; the macro mechanism a probing
cycle would hang off is in `EXT_CODES.md` section 3.
Confidence labels follow the repo convention: CONFIRMED = traced in firmware or verified against
MAINTPG; TENTATIVE = inferred, needs a bench test.

---

## 1. Leads (Phase 3)

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

## 2. Proposed shape (not started)

1. Tool length: user M6 macro (`EXT_CODES.md` section 3) → real tool change → new probe M-code that
   queues a type 5 move down Z onto a fixed tool setter, reads the latch at 0xE70, and computes
   length = latched Z − setter reference.
2. Storing the result: either in the firmware tool table (only useful if BXX reads it back with
   `rdTool`) or through a DOS-side helper. Decide after the `rdTool` question above is answered.
3. Tool diameter: X/Y touches against the setter (or a rotating-spindle edge find), same storage
   question.
4. Bench check before any motion: watch param 155 bit 7 (`IO_CTL.md` section 1) while tapping the
   setter.

## 3. Modifications log

| Change | Status |
|---|---|
| (none yet) | |
