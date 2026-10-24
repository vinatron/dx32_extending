# BMDC Firmware 6.73 vs 6.89 (and the DOS Software Around Them)

Two complete software sets are in this repo:

| Set | Where | BMDC.BIN | BXX title string | Source |
|---|---|---|---|---|
| **6.73** | `my_tc22_files/` | 253951 bytes, param 0 = 673 | "BOSS DX/32 V4.23" | The TC22 as it runs today |
| **6.89** | repo root | 311290 bytes, param 0 = 689 | "BOSS BPC-MC V4.79.22" | The set all earlier research (`decompiled/`, every other .md) was done on |

Both sets ship the identical `B3T.EXE`, and both `BMDCPRMS.SYS` files hold the same machine
configuration (param 98 = 0x21221220). This page compares the two builds and ends with what an
upgrade of the TC22 to 6.89 involves. The plan is to use 6.89 from now on.

Method: the 6.73 image was not run through Ghidra. Every item below comes from locating the same code
in both images by signature (capstone, `tools/m68k_dis.py` style) and comparing tables, strings and
handlers. Confidence labels as elsewhere: CONFIRMED = seen in both binaries, TENTATIVE = inferred.

---

## 1. Summary

| Area | 6.73 | 6.89 | Upshot for the TC22 |
|---|---|---|---|
| Code size / layout | Image 0x0-0x3dfff, variables 0x51e0-0x9b80, stack 0x3e000-0x3f000 | Image 0x0-0x4bff9, variables 0x7770-0xc62e, stacks 0x4c000-0x4d000 | Every address differs; nothing can be patched across builds by address |
| **Part-program buffer** (512 KB card) | 0x40000-0x7ffff = **256 KB** (TENTATIVE: from the boot RAM-test ranges) | 0x4e000-0x7ffff = **200 KB** | 6.89 holds about 56 KB less program, unless the RAM upgrade (`BMDC_RAM_UPGRADE.md`, patch written for 6.89) is done |
| AUF/TLAUF serial protocol | Packet builders, receivers, hex check, debounce | Same code instruction for instruction | No change for the I/O boards or the LinuxCNC work |
| Hardware support | BMDC3/BMDC4, 68040 FPSP, RAM probe to 16 MB, 8 KB dual-port, PWMINT, BMDC4 spindle DAC | Same, with a few more PWMINT accesses, one more halt site and different cache handling | No hardware change needed |
| G-codes 0-125 | 70 supported | **Identical table** (same codes, same handler grouping) | The G-code set does not change |
| G-word handler | — | Adds a G77/G79 case tied to the tool-change subprogram flag | Minor |
| Letter handlers | All printable ASCII | Adds **`\|`** | New syntax element, purpose not traced |
| M-codes | 0-9, 12-15, 19-22, 25, 26, 30, 36-38, 51, 60-62, 300-663, 900, 901, 999 | Adds **M16, M17, M63, M71, M72** | M63 is the real tool change that a user M6 macro hands off to |
| **User I/O M-codes M300-M663** | Present, **always enabled** | Present, **gated** by param 285 bit 3 "Additional M-codes" (alarm "Additional M-Code Option Not Enabled.") | After the upgrade they must be enabled (param, or the patch in `EXT_CODES.md`) |
| **`M<n>:` user M-code macros** | **Absent** | Present, including a user M6 | The route to probing in `EXT_PROBING.md` exists only in 6.89 |
| Licensed options (param 285) | No option gating found | Rigid tapping, tool-life management, parts counter, additional M-codes, PPPL parametric programming, digitizing; each has an "Option Not Enabled" alarm | Some 6.89 features are off until param 285 is set |
| Mailbox commands (PC → BMDC) | 0x00-0x24 (37) | 0x00-0x2a (43); 0x27 = drive-ready sequence | The 6.89 BXX may use commands 6.73 lacks: upgrade BXX and BMDC together |
| Move types (interpolator) | 19 (0x00-0x12) | 20: adds **0x13**, the drive-enable-path rapid | Goes with the new drive-enable sequencer and mailbox 0x27 |
| Parameter table | 242 entries (0-241) | 288 entries (0-287) | Several numbers changed meaning: section 3 |
| GPAUF expansion boards | One fixed word each way (params 157/162) | Up to 2 boards, 64 in + 64 out each (params 203, 204, 277-284), comms alarm, a monitor/logic feature ("AND operands/states") | 6.89 can drive extra I/O boards |
| Alarms | Servo DAC over/underflow per axis, "Shot Pin Failed" | Drops the DAC messages; adds X/Y/Z/C drive overload, coolant motor overload, CR1 not enabled, spindle not at speed, shot pin not in place, pallet changer time-out, part count, tool life, AutoBlend parser state | Different messages in the alarm log |

## 2. Firmware details (CONFIRMED)

### 2.1 Equivalent addresses

| Item | 6.73 | 6.89 |
|---|---|---|
| Entry point | 0x9b80 | 0xc630 |
| UART ISR / probe ISR | 0xe02c / 0xed1c | 0x1111c / 0x11cd6 |
| Mailbox jump (dispatch) / table | 0x20052 / 0x20112 | 0x27c64 / 0x27d3e |
| Parameter table / rd / wr | 0x22156 / 0x22702 / 0x22830 | 0x2a536 / 0x2abf6 / 0x2ad24 |
| Letter table / G handler / G table | 0x18472 / 0x1962c / 0x196ce | 0x1ee84 / 0x204f6 / 0x205e2 |
| M-word letter handler | 0x18a3c | 0x1f4e8 |
| M dispatcher test / M list / handlers | 0x23e6a / 0x23f84 / 0x241f4 | 0x2ca7c / 0x2cbf8 / 0x2ce7e |
| M300 / M400 / M500 / M600 | 0x2706a / 0x2709a / 0x270ca / 0x2711a | 0x30ba8 / 0x30bf6 / 0x30c44 / 0x30cb2 |
| Move-type table | 0x12d20 | 0x16d74 |
| Packet builders | around 0xfe66 | 0x13d50 / 0x13da2 |
| Param 98 RAM / mode word | 0x8670 / 0x51f0 | 0xaf58 / 0x7780 |
| User output words (M300-M331 / M332-M363) | 0x2bee / 0x2bf2 | 0x31f0 / 0x31f4 |
| User input words (M500-M531 / M532-M563) | 0x2c46 → 0x2c1e / 0x2c4a → 0x2c22 | 0x3248 → 0x3220 / 0x324c → 0x3224 |

### 2.2 Mailbox commands

Same numbering for 0x00-0x24 (B3T, identical in both sets, uses them). Handlers whose code shape
changed between builds: 0x01 doMove, 0x0c setHold, 0x0d rdProbe, 0x0e resetPgm, 0x13 clrPartPgm,
0x14 wrPartPgm, 0x16 executeBlock, 0x1a getPreviewBlk, 0x1c wrParam, 0x21 setBreakPoint,
0x23 wrDNCpgmBlk. The rest have the same instruction sequence. Only 6.89 has 0x25, 0x26,
0x27 (drive-ready, `HOMING_SEQUENCE.md`), 0x28, 0x29 and 0x2a.

### 2.3 User I/O M-codes

The handler bodies are the same: set or clear bit n of the output words, or poll the input snapshot.
6.89 adds the `btst #3,0xc519 / tst.b 0xc5c0 / trap #14` prologue to each one (`EXT_CODES.md`
section 1). In 6.73, the output word for M300-M331 is parameter 162 and the input snapshot for
M500-M531 is parameter 157. In 6.89 those moved to 279 and 277; 157 and 162 became not connected.

### 2.4 Output word ownership

In 6.89 the per-cycle I/O mapper sets and clears individual bits of the long-TX primary word 0x31f4
(coolant, conveyor, brakes, ...). In 6.73 nothing but the M-codes and one task at 0xd6de (top byte
only) writes the equivalent word 0x2bf2. 6.73 sends the machine functions through the command bytes
instead. This is why `PHASE1_TEST_PLAN.md` treats the 6.73 output-to-terminal map as unknown.

## 3. Parameter changes (CONFIRMED from both tables and both MAINTPG1.DAT files)

`BMDCPRMS.SYS` has the same format in both sets (file version "3.2", parameters 0-226). Parameter
numbers that change type or meaning:

| Param | 6.73 | 6.89 | Action when moving the TC22 file to 6.89 |
|---|---|---|---|
| 46 | Advanced Servo Options (long) | not connected | None (the TC22 file has 0) |
| 77-81 | Spindle-range #4-#8 max RPM | Integral following-error terms (gain, counter, normal gain, magnitude limit, loss) | Keep 0 (the TC22 file has 0; both files have 0) |
| 128 | Max S-axis PWM deviation | Z-axis velocity balance flag | Keep 0 |
| **156** | Option Status, **byte** | Option Status, **word** | Value × 256 (TC22 12 → 3072; the 6.89 file holds 3072). Status word, refreshed at run time |
| 157 | GPAUF1 32 input bits (long) | not connected | Drop (0) |
| **161** | Option Command, **byte** (flood = 8) | Option Command, **word** (flood = 2048) | Value × 256 (TC22 2 → 512 = lube pump) |
| 162 | GPAUF1 32 output bits (long) | not connected | Drop (0) |
| 193 | (unused) | Spindle ramp-up max time | Both files: 500 |
| **194-201** | Unused (NOVRAM/tool-changer-2 registers; TC22 file: 0) | **Spindle gear-shift supervision**: shot-pin make, switch settling, range-switch make, gear settling, shot-pin settling, back-out times, max retries, max shifting torque | **Must be set.** The 6.89 file holds 200, 5, 600, 100, 100, 100, 12, 640. With 0 the gear-shift time-outs would trip at once (TENTATIVE) |
| 202-205 | Unused | Spindle temp gain, number of GPAUF boards, GPAUF use, Z brake timer | 0, as in the 6.89 file |
| 227-241 | Present | Same meaning | Not in the SYS file |
| 242-287 | — | New: spindle timers and counters, temperature compensation, cycle time, peck clearance, pallet clearances, GPAUF1/2 I/O words, **285 Control Options** | Not in the SYS file. Firmware defaults (0) unless BXX sets them |

The SYS comment lines at 193-205 still carry the old (6.73-era) descriptions even in the 6.89 file;
trust the values and `MAINTPG1.DAT`, not those comments.

## 4. DOS software

| Item | 6.73 set | 6.89 set |
|---|---|---|
| BXX.EXE | 403490 bytes, "BOSS DX/32 V4.23", Copyright 1988-1995 | 577322 bytes, "BOSS BPC-MC V4.79.22" |
| MAINTPG.EXE | 189182 bytes | 332202 bytes, contains the constant 689.0 (TENTATIVE: exact firmware-version check) |
| MAINTPG1.DAT | 2c51cfeaaf615e0a2960871a65f46250, titles up to 258 | a5c882aaf3975eb09b1f19a0b9715490, titles up to 285 |
| Files the newer programs reference that are **not in this repo** | — | BXX: `MACROS.EMB`, `DELAY.TIM`, `CHECKSUM.OFS`, `XTHERM.DAT`, `TEMPER.TMP`, `BXT_GHLP.MSG/.NDX`. MAINTPG: `ERRMSG.DAT`, `XTHERM.DAT`, `MPERR1.MSG`, `MPERR2.NDX`, `MPUSE1.MSG`, `MPUSE2.NDX`, `FILENAME.SYS`, `PRINT.PLT` |
| Both | `BMDCPRMS.SYS`, `LEADCOMP.DAT`, `CFGPRMS.SYS`, `C:\BMDC.BIN` | same |

Both BXX versions print "INCOMPATIBLE SOFTWARE VERSIONS DETECTED". Mixing an old BXX with the new
firmware (or the reverse) should be expected to fail.

## 5. Upgrading the TC22 to 6.89

Firmware is loaded from disk at every start, so the upgrade is a file swap and rollback is a file
restore. Nothing is flashed.

**Before:**

1. Run the read-only parts of `PHASE1_TEST_PLAN.md` (Tests 1-3) on **6.73** first. A recorded
   baseline makes it possible to tell an upgrade problem from a machine problem.
2. Back up the whole `C:\` root of the DX32 PC (or at least every file named in section 4) to a
   separate disk. Keep it as the 6.73 rollback set.
3. Find the missing 6.89 support files (section 4, last row). The machine the 6.89 set came from
   should have them. Without them, BXX/MAINTPG may refuse to start or lose help screens and thermal
   compensation; which ones are mandatory is unknown.

**Swap (all at once):** `BMDC.BIN`, `BXX.EXE`, `MAINTPG.EXE`, `MAINTPG1.DAT` and the support files
from the 6.89 set. Keep the TC22's own `LEADCOMP.DAT`, `CFGPRMS.SYS` and a **converted**
`BMDCPRMS.SYS`:

- start from the TC22 file (its tuning, travel limits, home offsets and gains belong to this machine);
- apply section 3: 156 × 256, 161 × 256, 157/162 → 0, and 194-201 from the 6.89 file (or values
  that suit the TC22's spindle);
- keep param 0 at "3.2".

A small script can do this conversion and print a diff for review. Not written yet; see "Next steps".

**After:**

1. Boot BXX and check that it starts without "INCOMPATIBLE SOFTWARE VERSIONS" or "Wrong Param File
   Version", and that MAINTPG reports BMDC version 6.89.
2. Re-run `PHASE1_TEST_PLAN.md` Tests 1-3 with the 6.89 values (flood = param 161 + 2048) and compare
   with the 6.73 baseline.
3. Home the axes, run a known program in air (no tool, Z raised), then a spindle gear change, before
   cutting anything.
4. Enable additional M-codes (param 285 = 8, or the gate patch) to get M300-M663 back.

**What changes in the repo after the upgrade:** the 6.89 research (`IO_CTL.md`, `EXT_CODES.md`,
`EXT_PROBING.md`, `decompiled/`) applies directly again. `TC22_VALIDATION.md` and the TC22 values in
`PHASE1_TEST_PLAN.md` become the record of the old state. The test plan needs a 6.89 edition: the
version of it in PR #2 had the 6.89 values.

## 6. Open items

- What the `|` letter, the G77/G79 special case and mailbox commands 0x25, 0x26, 0x28-0x2a do.
- Which options BXX 4.79 turns on in param 285, and from which file ("version config file" strings).
- Whether the 689.0 constant in MAINTPG is an exact version check.
- Whether 6.73 implements rigid tapping, tool life and parts counting ungated, or not at all.
- The meaning of the "BPC-MC" product string vs "DX/32", given that param 98 is identical.
