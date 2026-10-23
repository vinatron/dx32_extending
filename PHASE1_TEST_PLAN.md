# Phase 1 Test Plan: I/O Status and Output Control

A step-by-step bench test of the Phase 1 findings in `IO_CTL.md` and `EXT_CODES.md`. Each test
says what the research predicts, so a result either confirms a finding or shows where it is wrong.
Fill in the **Result** boxes as you go and bring the filled-in copy back. The results decide what
gets built next (see "What the results decide" at the end).

Nothing here modifies `BMDC.BIN`, BXX or the parameter file on disk. Every change is a live
parameter write that is lost at power-down.

---

## 0. Safety and set-up

**Before you start:**

- [ ] E-stop within reach and tested.
- [ ] Spindle stopped. No tool in the changer arm. Carousel and arm in their home positions.
- [ ] Z axis resting on a block, or its brake known to hold with the drives off.
- [ ] Work area clear. Coolant nozzles pointed somewhere harmless (the coolant pump is the first output tested).
- [ ] Have a pencil copy of this file, or a laptop, to record results.

**Do NOT set any of these bits in any test**. They move or release machine hardware:

| Word | Bit | Function | M-code that would set it |
|---|---|---|---|
| param 280 (0x31f4) | 15 | **Z-axis brake release**. Z can drop | M347 |
| param 280 | 9 | A-axis brake release | M341 |
| param 280 | 8 | Door guard | M340 |
| param 280 | 25-31 | Tool changer: air blast, drawbar, arm brake, carousel, arm | M357-M363 |
| param 280 | 1 | Spindle enable forward | M333 |
| param 160 | 0, 1, 6, 7 | Axis drives, rotary brake, Z drive, limit override | (parameter write) |
| param 163 | any | Spindle command word | (parameter write) |

If anything moves unexpectedly: E-stop first, then note what you did.

**Number conventions used below:**

- Bit n of a parameter value is worth 2^n: bit 9 = 512, bit 10 = 1024, bit 11 = 2048.
- A long parameter with bit 31 set displays as a negative number. That is normal.
- `MAINTPG1.DAT` numbers the bits of 16-bit words b0-b15. `AUF_PACKET_FORMAT.md` noted that MAINTPG's
  display may number them by byte instead. Test 2 settles which. Always record the **numeric value**
  as well as what lights up.

---

## Test 1: Identify the software you are running

**Predicted:** the machine runs the same firmware the research was done on.

1. On the DX32 PC, note the file sizes (from `DIR`) of these files. If you can copy them to a laptop,
   compute their MD5 too.

   | File | Repo size | Repo MD5 | Machine size | Machine MD5 |
   |---|---|---|---|---|
   | BMDC.BIN | 311290 | 0f00128c2ae818fdc2ca1d7fc29c775c | | |
   | BXX.EXE | 577322 | 99a320a18bf36ae70f19683cd489a43b | | |
   | MAINTPG.EXE | 332202 | ec6caa36465b29e9c78dda4edd38dc5a | | |
   | MAINTPG1.DAT | 37864 | a5c882aaf3975eb09b1f19a0b9715490 | | |

2. Start MAINTPG and open the parameter viewer. Read **param 0** (BMDC Executive Revision Level).
   The analysed firmware holds 0x2b1 = **689** there. It may display as 689, 6.89 or similar.

   **Result:** param 0 = ________

If BMDC.BIN differs, stop here and send the machine's copy. Every address in the research is
specific to this one firmware image.

---

## Test 2: Read-only I/O survey (MAINTPG)

**Predicted (`IO_CTL.md` section 1):** the I/O words are live parameters that change as you operate
switches. Params 150-156 always react, because they are decoded from the AUF short packet. It is
**unknown** whether the raw long-packet words 277/278 react on a DX32. This test answers that.

1. Machine powered, drives may stay off. In MAINTPG's parameter viewer, record the resting values:

   | Param | Name | Resting value |
   |---|---|---|
   | 150 | Machine Faults | |
   | 151 | Axis Limits | |
   | 152 | Auxiliary Limits | |
   | 154 | Machine Status | |
   | 155 | Home Switches | |
   | 156 | Option Status | |
   | 158 | Back-End Switch Status | |
   | 160 | Machine Command | |
   | 161 | Option Command | |
   | 163 | Spindle Command | |
   | 164 | Spindle Status | |
   | 171 | TLAUF Inputs | |
   | 277 | GPAUF1 Inputs 0-31 | |
   | 278 | GPAUF1 Inputs 32-63 | |
   | 279 | GPAUF1 Outputs 0-31 | |
   | 280 | GPAUF1 Outputs 32-63 | |
   | 285 | Control Options | |

   **Predicted:** 285 = 0, 279 = 0.

2. Operate one input at a time, and note **every** parameter that changes and its new value:

   | Action | Params that changed (old → new) | Did 277 or 278 change? |
   |---|---|---|
   | Press E-stop, then release | | |
   | Open perimeter door, then close | | |
   | Press X home switch by hand (drives off) | | |
   | Press Y home switch by hand | | |
   | Press Z home switch by hand | | |
   | Low air: close the air supply until the switch trips | | |
   | Pendant/console buttons (cycle start, hold), drives off | | |
   | Touch probe tip, if a probe is fitted (predicted: param 155 bit 7 = +128) | | |

   **Predicted:** E-stop changes 152 bit 0. The door changes 152 bit 3 and/or 154 bit 3. The home
   switches change 155 bits 0-2. Low air changes 156 bit 8 (+256). Buttons change 158 bits 28/29.

3. **Key question:** do the same switches also flip bits in **277/278**? Write down which bit each
   one flips. MAINTPG1.DAT predicts these names for param 277: b6 E-stop, b10 low air, b15/b16/b17
   X/Y/Z home, b24 perimeter door.

   **Result:** 277/278 react: YES / NO / PARTLY ____________________

---

## Test 3: Live parameter write

**Predicted (`IO_CTL.md` section 1):** MAINTPG's "Edit Machine Values" writes straight to the
running firmware. In param 161, bit 11 (value 2048) = flood coolant, and 1 = ON.

1. Note the current value of param 161 (from Test 2): V = ______. It is normally 0 with the machine
   idle.
2. Choose "Edit Machine Values" (**not** "Edit File Values"). Write param 161 = **V + 2048**.

   **Predicted:** the flood coolant pump runs within a fraction of a second.

   **Result:** pump runs: YES / NO. MAINTPG shows which bit as set? ______

3. Write param 161 = **V** (the original value). **Predicted:** the pump stops.
4. Optional: repeat with V + 1024 (bit 10 = mist), then restore V.

   **Result:** mist valve: YES / NO

If writing 2048 does nothing, also try **8** once (the byte-numbering hypothesis), then restore V.

   **Result:** 8 did: ____________

---

## Test 4: The I/O M-codes are present but switched off

**Predicted (`EXT_CODES.md` sections 1-2):** with param 285 = 0, M300-M663 raise an alarm instead of
acting.

1. Start BXX normally. In MDI (or a one-line program) run:
   ```
   M343
   ```
   **Predicted:** an alarm or error message and no coolant. Copy the message text exactly.

   **Result:** ____________________________________________

2. As a control, run `M08` then `M09`. **Predicted:** flood on, then off, as normal.

   **Result:** ____________

---

## Test 5: Switch on "Additional M-codes" and check that the setting survives

**Predicted:** param 285 bit 3 (value 8) enables the codes. It is unknown whether the value survives
leaving MAINTPG and starting BXX (BXX might reload the firmware).

1. Exit BXX. In MAINTPG, "Edit Machine Values": write param **285 = 8**. Read it back.

   **Result:** reads back as ______

2. Exit MAINTPG **without powering down**, and start BXX.
3. Run `M343` in MDI.
   - No alarm and the pump runs: the setting survived. Continue with Test 6.
   - The same alarm as in Test 4: BXX reloads the firmware/parameters at start-up. Record that,
     skip to Test 8, and note it under "What the results decide".

   **Result:** survived: YES / NO

4. Optional: did BXX show anything different during start-up (a "loading BMDC" message, a delay)?

   **Result:** ____________

---

## Test 6: Output M-codes

Only if Test 5 worked. Use a short program so each step is visible. Use a dwell of a few seconds
between steps. The dwell units are not confirmed, so adjust `P` until each step lasts 2-5 s, and
note the value.

### 6a. Flood through the M-code (bit not owned by the firmware on this machine)

```
M343      (param 280 bit 11 = flood ON)
G4 P5
M443      (flood OFF)
M30
```

**Predicted:** the flood pump runs during the dwell. Param 280 gains 2048 while it is on.

**Result:** pump: YES / NO. Dwell P5 lasted ______ s

If the pump does not run, the AUF on this machine takes coolant from the short packet, not the long
one. That is a valid result: record it and continue with 6c.

### 6b. Mist through the M-code

Same as 6a with M342 / M442 (bit 10).

**Result:** mist: YES / NO

### 6c. Map the long-packet outputs to AUF terminals

Watch the **AUF board output LEDs** (or meter the terminals). Step one bit at a time:

```
M3nn
G4 P5
M4nn
G4 P2
```

Test **only** these numbers; the rest are excluded by the safety table:

- M300-M331 (param 279 bits 0-31; nothing in the firmware uses them)
- M332, M334-M339, M342-M346, M348-M356 (param 280 bits 0, 2-7, 10-14, 16-24)

Fill in every number that changes something:

| M-code | Param.bit | AUF terminal / LED that changed | Notes |
|---|---|---|---|
| | | | |
| | | | |
| | | | |

**Predicted for param 280 (MAINTPG1.DAT names):** b0 spindle safe light, b2/b3 gear, b4 shot pin, b6
lube pump, b10 mist, b11 flood, b12/b13 chip conveyor, b14 index air. Bits 1, 5, 6, 7 and 12-15 are
re-driven by the firmware every cycle, so M-codes on those bits may flicker or have no effect. Note
it if so.

### 6d. Do M-code outputs drop on reset?

**Predicted (TENTATIVE):** a reset/abort event clears every M-code output.

1. `M332` (or any bit found in 6c that lights an LED), without the matching M4xx.
2. Press reset (or E-stop then reset) and watch whether the output drops.

**Result:** dropped on: reset ______ / E-stop ______ / program end ______ / stayed on ______

---

## Test 7: Wait-for-input M-codes

Only if Test 5 worked **and** Test 2 found at least one switch that flips a bit in 277/278.

Mapping: input bit n of param 277 → `M5nn` / `M6nn` with nn = n (00-31). Bit n of param 278 →
nn = n + 32.

Example for the door: if Test 2 showed it on 277 bit 24, use M524 / M624.

```
M5nn      (waits here until the input is ON = bit set)
M343      (flood on as a visible marker)
G4 P5
M443
M6nn      (waits here until the input is OFF = bit clear)
M343
G4 P5
M443
M30
```

1. Start with the switch released. **Predicted:** the program stops at M5nn. Operate the switch:
   flood runs. Release it: flood runs again.
2. Check that Hold and Reset can still stop the program while it waits.

**Result:** M5nn waited and released: YES / NO. M6nn: YES / NO. Hold/reset during wait: ______

If Test 2 showed that 277/278 do not react, skip this test: these M-codes cannot see this machine's
inputs as they stand.

---

## Test 8: Clean-up

- [ ] Run M400-M463 for every output you set, or power-cycle.
- [ ] Param 161 back to its original value V.
- [ ] Param 285 back to 0 (or power-cycle; it is not saved to disk).
- [ ] Coolant off, machine back in its normal state.

---

## What the results decide

| Outcome | Next step |
|---|---|
| Test 1 file mismatch | Send the machine's BMDC.BIN; addresses must be re-checked first |
| Test 3 works | Confirmed: the PC can switch any command-word output live, and a DOS I/O utility is worth building |
| Test 5 survives | Enabling the codes can be done with a parameter. Next: find a persistent way (parameter file or patch) |
| Test 5 does not survive | Build the 4-edit gate patch (`tools/patch_bmdc_mcodes.py`, `EXT_CODES.md` section 2) |
| Test 6a/6c map outputs | Record the mapping in `AUF_PACKET_FORMAT.md` and `IO_CTL.md`, mark it CONFIRMED |
| 6a fails (coolant not on long packet) | New firmware M-codes must drive outputs through the command words (params 160/161) instead. Phase 2 work |
| Test 7 works | Wait-for-input is usable as is |
| Test 2 shows 277/278 do not react | Inputs must come from params 150-158. A new wait M-code is needed (Phase 2) |

## Results summary (fill in)

| Test | Pass / Fail / Skipped | Notes |
|---|---|---|
| 1 Software identity | | |
| 2 I/O survey (277/278 react?) | | |
| 3 Live write (flood via 161) | | |
| 4 M343 alarm with option off | | |
| 5 Option survives BXX start | | |
| 6a Flood via M343 | | |
| 6c Output map | | |
| 6d Outputs drop on reset | | |
| 7 Wait-for-input | | |
