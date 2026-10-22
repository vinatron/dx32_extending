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
| **1** | Read the machine I/O state and switch individual outputs on/off | `IO_CTL.md`, `EXT_CODES.md` sections 1-2. Mostly layer A: the firmware already has user I/O M-codes |
| 2 | Understand and use the firmware's **M-code macro** mechanism; build a patch framework for new M-codes | Trace `M<n>:` definitions (`EXT_CODES.md` section 3); find free space in `BMDC.BIN`; hook the unknown-M fallback in the dispatcher `FUN_0002ca1e` |
| 3 | Tool-length probing as a G/M code (`EXT_PROBING.md`) | Reuse probe move type 5 and the latch at 0xE70 (`BMDC_PROBE_INTERNALS.md`). Solve how a measured length gets into BXX's tool table (`wrTool`/`rdTool` ownership) |
| 4 | Tool-diameter probing, packaging | Probe cycles, `tools/patch_bmdc_*.py`, test part programs, operator notes |

---

## 3. Document map

| File | Contents |
|---|---|
| `IO_CTL.md` | I/O status and command words as firmware parameters, output-word ownership, mailbox 0x1b/0x1c protocol and packed-decimal format, Phase 1 bench procedure |
| `EXT_CODES.md` | M-code dispatcher `FUN_0002ca1e`, hidden user I/O M-codes M300-M663 and their gate (param 285 bit 3), the gate patch, the M-macro mechanism, log of G/M-code modifications |
| `EXT_PROBING.md` | Probing leads and the proposed tool-length/diameter cycle, log of probing modifications |

## 4. Change log

| Date | Change |
|---|---|
| 2026-10-22 | Plan written. Phase 1 research: I/O words mapped to parameters 150-172/277-285; M-code dispatcher and user I/O M-codes M300-M663 found; gate (param 285 bit 3) and bypass patch bytes identified; M-macro mechanism and probing leads recorded. No firmware or binary modified. `tools/m68k_dis.py` now finds `BMDC.BIN` relative to the script |
| 2026-10-22 | Split into `IO_CTL.md`, `EXT_CODES.md` and `EXT_PROBING.md`; this file keeps the plan and index |
