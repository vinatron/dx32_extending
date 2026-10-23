# TC22 Machine Files vs the Repo Originals

Validation of the files in `my_tc22_files/` (copied from the TC22 the Phase 1 tests will run on)
against the binaries the research was done on (repo root). The short answer: **the TC22 runs an
older build of the same firmware and DOS software.** The Phase 1 findings carry over in substance,
but every firmware address and several parameter numbers differ. `PHASE1_TEST_PLAN.md` has been
rewritten for the TC22 values below.

Confidence labels: CONFIRMED = found in the TC22 binary by disassembly; TENTATIVE = inferred.

---

## 1. File-by-file

| File | Repo size / MD5 | TC22 size / MD5 | Verdict |
|---|---|---|---|
| B3T.EXE | 158628 / 4d5e71e4… | 158628 / 4d5e71e4d016f13173eda8e034252ca6 | **Identical** |
| BMDC.BIN | 311290 / 0f00128c… | 253951 / 9b0bd673021d69290646912efd4a1d58 | Different build: rev **6.73** (param 0 = 673) vs repo 6.89 (689). Same code family; everything is relocated (section 2) |
| BXX.EXE | 577322 / 99a320a1… | 403490 / 657801ef0d476af8526cd4157b8c4191 | Older BXX (Copyright 1988-1995). Uses mailbox 0x1b/0x1c like the repo copy. Not analysed further |
| MAINTPG.EXE | 332202 / ec6caa36… | 189182 / 45d19881001e6aff27381887af2e4492 | Older MAINTPG. Has "Edit Machine Parameter" / "Edit File Parameter", "Selected I/O Status", and calls mailbox 0x1b/0x1c |
| BMDCPRMS.SYS | 28038 | 27915 / dffbb44ff68f8d15bc15e40121d1fe7e | Same format and same parameter count (0-226). **Param 98 = 555880992 (0x21221220), identical** machine configuration. Many tuning values differ (accelerations, home offsets, gains, lead-screw values) |
| BDX.EXE | — | 8091 / e8bbaecb057e1231b5e03c2a40704998 | No repo counterpart. Small launcher that draws a "BOSS" banner |
| CFGPRMS.SYS | — | 1053 / c2abf42ff3234f3f83a1b889c4614b10 | No repo counterpart. Text config read by BXX (serial number line "TC-253 7.5K", `C:\MDI.TXT`, a value of 4800, otherwise zeros) |
| MAINTPG.INI | — | 111 / 0321374e1008f92fcce0ab4663eaa285 | No repo counterpart. MAINTPG preferences |
| MAINTPG1.DAT | 37864 | **not supplied** | The TC22 MAINTPG does not reference it; bit names come from the repo copy and are TENTATIVE for this machine |
| mkdir.md | — | — | Placeholder used to create the folder |

## 2. Firmware: how the Phase 1 findings translate (CONFIRMED unless marked)

| Item | Repo build (6.89) | TC22 build (6.73) |
|---|---|---|
| Entry point / stack top | 0xc630 / 0x4d000 | 0x9b80 / 0x3f000 |
| UART ISR / probe ISR | 0x1111c / 0x11cd6 | 0xe02c / 0xed1c |
| Parameter table | 0x2a536, 288 entries | **0x22156, 242 entries (0-241)**. Params 194-219 not connected; **277-285 do not exist** |
| Param read/write handlers (0x1b/0x1c) | 0x2abf6 / 0x2ad24 | 0x22702 / 0x22830, same logic, no write protection |
| Mailbox dispatcher (+0x400) | 0x27c02 | around 0x1fff4 (same +0x400/+0x402 handshake) |
| Machine config (param 98) RAM | 0xaf58 | 0x8670 |
| Mode word ("5" = tool-change block path) | 0x7780 | 0x51f0 |
| Letter handler table / M-word handler | 0x1ee84 / 0x1f4e8 | 0x18472 / 0x18a3c |
| M-code dispatcher | `FUN_0002ca1e` (test at 0x2ca7c) | test at **0x23e6a**, same structure |
| M list / handler table (this machine family) | 0x2cbf8 / 0x2ce7e, 0x124 entries | **0x23f84 / 0x241f4, 0x11f entries** |
| M-codes present | M0-9, 12-17, 19-22, 25, 26, 30, 36-38, 51, 60-63, 71, 72, 300-363, 400-463, 500-563, 600-663, 900, 901, 999 | Same **minus M16, M17, M63, M71, M72** |
| M300 / M400 / M500 / M600 handlers | 0x30ba8 / 0x30bf6 / 0x30c44 / 0x30cb2 | **0x2706a / 0x2709a / 0x270ca / 0x2711a** |
| "Additional M-codes" gate | param 285 bit 3, or inside an M-macro | **No gate at all.** The handlers go straight to the bit operation, so M300-M663 should work as delivered |
| Outputs M300-M331 | 0x31f0 = param 279 | **0x2bee = param 162** "User Command Bits Register" |
| Outputs M332-M363 | 0x31f4 = param 280 | **0x2bf2, not a parameter**. Its top byte (bits 24-31, M356-M363) is rewritten from ~0x970b by the firmware task at 0xd6de |
| Firmware bit ops on the long-TX primary word | many bsets/bclrs of 0x31f6/0x31f7 | **None**: in this build only the M-codes and 0xd6de write 0x2bf2. Machine outputs (coolant etc.) go out through the command bytes instead |
| Long-TX staging | 0x3218/0x321c | 0x2c16/0x2c1a (bytes read by the builder at 0x12820) |
| Inputs M500-M531 | 0x3248 → snapshot 0x3220 = param 277 | 0x2c46 → **snapshot 0x2c1e = param 157** "User Status Byte". The snapshot is refreshed **only when an I/O M-code runs** (0x27050), so param 157 in MAINTPG can be stale |
| Inputs M532-M563 | 0x324c → 0x3224 = param 278 | 0x2c4a → 0x2c22, not a parameter |
| `M<n>:` user M-code macros | present (definition lists 0x3b44/0x3b6c, depth 0xc5c0) | **Absent.** The M-word handler has no definition mode, and there is no depth counter |

## 3. Parameters 150-172 on the TC22

| Param | Address | Type | Note vs repo build |
|---|---|---|---|
| 150-155 | 0x2be0-0x2be5 | byte | Same meanings (faults, limits, aux limits, drive faults, machine status, home switches) |
| 156 | 0x2be6 | **byte** | Option status. Repo has a word; the TC22 byte holds what is the high byte there (low air = bit 0, low lube = bit 1, TENTATIVE) |
| 157 | 0x2c1e | long | **User status** = long-RX input snapshot for M500-M531 |
| 158 | 0x2be7 | long | Back-end switch status |
| 159 | 0x2beb | byte | Front-end switch status |
| 160 | 0x2bec | byte | Machine command |
| 161 | 0x2bed | **byte** | Option command. **bit 2 = mist (4), bit 3 = flood (8)**: CONFIRMED from the TC22 M7/M8/M9 handlers (0x261ee, 0x26232, 0x2626e). In the repo build these are word bits 10/11 (1024/2048). Saved value 2 = bit 1 = lube pump (TENTATIVE) |
| 162 | 0x2bee | long | **User command bits** = outputs M300-M331 |
| 163, 164 | 0x2c4e, 0x2c50 | word, byte | Spindle command / status |
| 166-172 | 0x2c5e-0x2c68 | byte | Tool-changer registers, TLAUF inputs (171), TLAUF outputs (172) |

## 4. What this changes in the plan

- **Phase 1 gets simpler:** no option to enable and no gate patch. `EXT_CODES.md` section 2 applies
  to the repo build only.
- **Phase 1 test values change:** flood is param 161 + 8, the user I/O words are 157/162, and there is
  no param 285. Done in `PHASE1_TEST_PLAN.md`.
- **Phase 2/3 change:** the TC22 has no user M-code macros and no M63, so the "user M6 macro, then
  probe" route in `EXT_PROBING.md` is not available. New behaviour (including probing) will need
  firmware patch code in the TC22 image. Free space and hook points must be found in **this**
  image, not the repo one.
- From now on, all firmware work for this machine should use `my_tc22_files/BMDC.BIN`. The repo
  analysis (`decompiled/BMDC.*`) remains a good map of the code, because the structure matches.
  A Ghidra export of the TC22 image would make that work faster.
