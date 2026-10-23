# Phase 1 Test Plan: I/O Status and Output Control (TC22)

A step-by-step bench test of the Phase 1 findings, written for **the TC22 and its own files in
`my_tc22_files/`** (firmware rev 6.73). The research was first done on the repo-root firmware
(rev 6.89). `TC22_VALIDATION.md` lists every difference; the values below are already the TC22 ones.

Each test says what the research predicts, so a result either confirms a finding or shows where it
is wrong. Fill in the **Result** boxes and bring the filled-in copy back. The results decide what
gets built next (see "What the results decide" at the end).

Nothing here modifies `BMDC.BIN`, BXX or the parameter file on disk. Parameter changes are live
writes that are lost at power-down. **Always use MAINTPG's "Edit Machine Parameter", never "Edit File
Parameter"**, and answer **No** if MAINTPG asks to write changes to the parameter file.

---

## 0. Safety and set-up

**Before you start:**

- [ ] E-stop within reach and tested.
- [ ] Spindle stopped. No tool in the changer arm. Carousel and arm in their home positions.
- [ ] Z axis resting on a block, or its brake known to hold with the drives off.
- [ ] Work area clear. Coolant nozzles pointed somewhere harmless (coolant is the first output tested).
- [ ] A backup copy of `C:\BMDCPRMS.SYS` on a floppy or USB stick.
- [ ] A printed copy of this file, or a laptop, to record results.

**What is not known on this machine:** which AUF terminal each M300-M363 bit drives. The bit names
come from the repo's `MAINTPG1.DAT` (another machine), so treat them as warnings, not facts.
**Do NOT run these M-codes in any test**; on the reference machine they release brakes or move hardware:

| M-code | Bit | Function on the reference machine |
|---|---|---|
| M347 | M332-word bit 15 | **Z-axis brake release**. Z can drop |
| M341 | bit 9 | A-axis brake release |
| M340 | bit 8 | Door guard |
| M333 | bit 1 | Spindle enable forward |
| M356-M363 | bits 24-31 | Tool changer (air blast, drawbar, arm brake, carousel, arm). The firmware also rewrites these bits itself |

Parameter writes: change **only** param 161 in these tests. Do not write 160 (drives, brakes, limit
override), 163 (spindle) or 172 (tool changer).

If anything moves unexpectedly: E-stop first, then note what you did.

**Number conventions:**

- Bit n of a parameter value is worth 2^n: bit 2 = 4, bit 3 = 8, bit 11 = 2048.
- On the TC22, params 156 and 161 are **single bytes** (bits 0-7).
- A long parameter (157, 162) with bit 31 set displays as a negative number. That is normal.
- Always record the **numeric value** as well as what lights up.

---

## Test 1: Confirm the machine runs the files that were analysed

**Predicted:** the files on the machine's C: drive are the ones in `my_tc22_files/`.

1. On the DX32 PC, note the sizes (from `DIR C:\`). If you can, copy the files to a laptop and compute
   their MD5 too.

   | File | Expected size | Expected MD5 | Machine size | Machine MD5 |
   |---|---|---|---|---|
   | BMDC.BIN | 253951 | 9b0bd673021d69290646912efd4a1d58 | | |
   | BXX.EXE | 403490 | 657801ef0d476af8526cd4157b8c4191 | | |
   | MAINTPG.EXE | 189182 | 45d19881001e6aff27381887af2e4492 | | |
   | BMDCPRMS.SYS | 27915 (changes when parameters are saved) | dffbb44ff68f8d15bc15e40121d1fe7e | | |

2. Start MAINTPG. It should report "BMDC version ... running" (or load it). Open the parameter viewer
   and read **param 0**. Predicted: **673** (it may display as 6.73).

   **Result:** MAINTPG message ____________  param 0 = ________

If BMDC.BIN differs, stop and send the machine's copy; every address in the research depends on it.

---

## Test 2: Read-only I/O survey (MAINTPG)

**Predicted (`TC22_VALIDATION.md` section 3):** params 150-164 are live and change as you operate
switches.

1. Machine powered, drives may stay off. Record the resting values. The "Saved" column is the value
   in the TC22 `BMDCPRMS.SYS`, for comparison:

   | Param | Name | Saved | Resting value now |
   |---|---|---|---|
   | 150 | Machine Faults | 0 | |
   | 151 | Axis Limits | 0 | |
   | 152 | Auxiliary Limits | 0 | |
   | 153 | Drive Faults | 0 | |
   | 154 | Machine Status | 1 | |
   | 155 | Home Switches | -125 | |
   | 156 | Option Status (byte) | 12 | |
   | 157 | User Status (M500 inputs) | 0 | |
   | 158 | Back-End Switches | 0 | |
   | 160 | Machine Command | 65 | |
   | 161 | Option Command (byte) | 2 | |
   | 162 | User Command Bits (M300 outputs) | 0 | |
   | 163 | Spindle Command | 1280 | |
   | 164 | Spindle Status | -91 | |
   | 171 | TLAUF Inputs | 42 | |

   **Predicted:** 162 = 0. Param 157 may be anything: it only refreshes when an I/O M-code runs.

2. Operate one input at a time and note **every** parameter that changes:

   | Action | Params that changed (old → new) |
   |---|---|
   | Press E-stop, then release | |
   | Open perimeter door, then close | |
   | Press X home switch by hand (drives off) | |
   | Press Y home switch by hand | |
   | Press Z home switch by hand | |
   | Low air: close the air supply until the switch trips | |
   | Cycle start / hold buttons (drives off) | |
   | Touch probe tip, if a probe is fitted (predicted: param 155 +128, bit 7) | |

   **Predicted:** E-stop changes 152 bit 0. The door changes 152 bit 3 and/or 154 bit 3. The home
   switches change 155 bits 0-2. Low air and low lube change 156 bits 0/1 (TENTATIVE).

3. Also try MAINTPG's **"Selected I/O Status"** screen and note what it shows.

   **Result:** ____________________________________________

---

## Test 3: Live parameter write (flood coolant)

**Predicted:** "Edit Machine Parameter" writes straight to the running firmware. On the TC22, param
161 bit 3 (value **8**) = flood and bit 2 (value **4**) = mist, with 1 = ON. Confirmed from the
firmware's own M7/M8/M9 code.

1. Note the current value of param 161 from Test 2: V = ______ (saved value 2 = lube pump bit).
2. "Edit Machine Parameter": write param 161 = **V + 8**.

   **Predicted:** the flood pump runs.

   **Result:** pump runs: YES / NO

3. Write param 161 = **V**. **Predicted:** the pump stops.
4. Optional: V + 4 (mist), then back to V.

   **Result:** mist: YES / NO

---

## Test 4: Do the I/O M-codes run without any set-up?

**Predicted:** the TC22 firmware has **no enable option** for M300-M663 (unlike the repo build,
which needs param 285). They should run straight away.

1. Start BXX. Control test first: `M08`, then `M09`. **Predicted:** flood on, then off.

   **Result:** ____________

2. Run a harmless user-output code and its clear, in MDI or a two-line program:
   ```
   M300
   M400
   ```
   **Predicted:** no alarm.

   **Result:** alarm? NO / YES, text: ____________________________________________

---

## Test 5: Output M-codes and the user output word

Use short programs so each step is visible. The dwell units are not confirmed: adjust `P` until each
dwell lasts 2-5 s, and note the value.

### 5a. M300-M331 set param 162

```
M300      (param 162 bit 0)
M305      (param 162 bit 5)
M30
```

Then (BXX exited, power still on) open MAINTPG and read param 162. Then return to BXX and run
`M400` / `M405` to clear the bits.

**Predicted:** 162 = 33 (bits 0 and 5) after the first program, and 0 after clearing. If MAINTPG
reloads the BMDC when it starts (watch for "Loading BMDC"), the value will read 0 regardless. Note
that, because it also tells us whether MAINTPG and BXX can hand over a running card.

**Result:** 162 = ______  MAINTPG reloaded BMDC: YES / NO

### 5b. Map the user outputs to AUF terminals

Watch the **AUF board output LEDs** (or meter the terminals). Step one code at a time:

```
M3nn
G4 P5
M4nn
G4 P2
```

Test these numbers only:

- M300-M331 (param 162 bits 0-31)
- M332, M334-M339, M342-M355 (second word bits 0, 2-7, 10-23)

Fill in every number that changes something:

| M-code | Bit | AUF terminal / LED that changed | Notes |
|---|---|---|---|
| | | | |
| | | | |
| | | | |

**Predicted:** unknown. In this firmware build nothing but these M-codes writes the user output words,
so either they reach spare AUF terminals, or the AUF ignores them on this machine. Both are useful
results. A good first candidate is **M343**, which is flood on the reference machine.

### 5c. Do user outputs drop on reset or program end?

1. Turn on one output found in 5b (M3nn) without the matching M4nn.
2. Then in turn: end the program (M30), press reset, E-stop and reset. Note when the output drops.

**Result:** dropped on: M30 ______ / reset ______ / E-stop ______ / stayed on ______

---

## Test 6: Wait-for-input M-codes

**Predicted:** `M5nn` waits until bit nn of the long-RX input word is 1, and `M6nn` until it is 0.
nn = 00-31 is param 157 bit nn; 32-63 is the second input word. **Unknown:** which switch is which
bit, or whether the AUF reports the main switches in this word at all.

1. **Find the bits.** In BXX, run `M400` (any I/O M-code refreshes the snapshot in param 157), exit
   to MAINTPG, and read param 157. Repeat with one switch held (door open, E-stop pressed). If 157
   changes, the changed bit is the nn to use. (If MAINTPG reloads the BMDC, see 5a, this step will
   not work; skip to 2 with the reference-machine guesses: 06 E-stop, 10 low air, 24 door.)

   | Switch state | Param 157 |
   |---|---|
   | All at rest | |
   | Door open | |
   | E-stop pressed | |

2. **Wait test** with a bit from step 1:
   ```
   M5nn      (waits here until the input bit is 1)
   M08
   G4 P5
   M09
   M6nn      (waits here until the input bit is 0)
   M08
   G4 P5
   M09
   M30
   ```
   **Predicted:** the program stops at M5nn until you operate the switch, runs flood, then waits at
   M6nn until you release it.

3. Check that **Hold and Reset can still stop the program while it is waiting.** Do this first,
   before relying on a wait in a real program.

**Result:** M5nn: ______  M6nn: ______  Hold/reset during wait: ______

---

## Test 7: Clean-up

- [ ] Clear every user output you set (M400-M463), or power-cycle.
- [ ] Param 161 back to its original value V.
- [ ] Coolant off, machine back in its normal state.
- [ ] Compare `C:\BMDCPRMS.SYS` with your backup (size/date) to make sure nothing was saved.

---

## What the results decide

| Outcome | Next step |
|---|---|
| Test 1 mismatch | Send the machine's files; addresses must be re-checked first |
| Test 3 works | Confirmed: the PC can switch command-byte outputs live, and a DOS I/O utility is worth building |
| Test 4 no alarm | Confirmed: the user I/O M-codes are available as delivered on the TC22 |
| Test 4 alarm | The TC22 has a gate the analysis missed; send the alarm text |
| 5b finds terminals | Record the map in `IO_CTL.md` / `AUF_PACKET_FORMAT.md` as CONFIRMED |
| 5b finds nothing | User outputs need new firmware code that drives the command bytes (params 160/161). Phase 2 work in the TC22 image |
| Test 6 works | Wait-for-input usable as is |
| Test 6 never sees the switches | A wait M-code on params 150-158 is needed (Phase 2) |

## Results summary (fill in)

| Test | Pass / Fail / Skipped | Notes |
|---|---|---|
| 1 Files and revision | | |
| 2 I/O survey | | |
| 3 Flood via param 161 + 8 | | |
| 4 M-codes run without set-up | | |
| 5a Param 162 shows M300 bits | | |
| 5b Output map | | |
| 5c Outputs drop on reset / M30 | | |
| 6 Wait-for-input | | |
