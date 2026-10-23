# Extension G/M Codes

G/M codes that exist in the firmware but are switched off, the patches that change G/M-code
behaviour, and the M-code macro mechanism that new codes will build on. Part of the track in
`GM_EXTENSION_PLAN.md`; the I/O words these codes act on are described in `IO_CTL.md`.
Confidence labels follow the repo convention: CONFIRMED = traced in firmware or verified against
MAINTPG; TENTATIVE = inferred, needs a bench test.


> **TC22 (firmware rev 6.73):** M300-M663 exist there too (handlers 0x2706a/0x2709a/0x270ca/0x2711a)
> but have **no gate**, so section 2 (enabling) does not apply. The `M<n>:` macro mechanism of
> section 3 and the codes M16, M17, M63, M71 and M72 are **absent** in that build. See `TC22_VALIDATION.md`.

---

## 1. The M-code dispatcher and the user I/O M-codes (CONFIRMED)

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
tst.b $c5c0          ; M-macro nesting depth (section 3)
bne   ok
trap  #14            ; alarm, data words 0200 0003 3d5b
```

`BMDCPRMS.SYS` stops at parameter 226 and RAM 0xc516 starts as 0, so the option bit is clear (TENTATIVE
until read back on the machine). Parameter 285's bits per MAINTPG1.DAT: b0 rigid tapping, b1 tool life
management, b2 parts counter/cycle timer, **b3 additional M-codes**, b4 parametric part programming,
b5 digitizing. Bits 0, 1, 2 and 4 are tested elsewhere in the firmware (0x20a40, 0xea7a, 0x10866,
0x1edc4). Those are further hidden options worth a later look; parametric programming in particular
could matter for probing.

## 2. Enabling the I/O M-codes

1. **Live parameter write (try first).** Write param 285 = 8 with MAINTPG's parameter editor
   ("Edit Machine Values") or any mailbox 0x1c client. This is lost on power-down. If BXX reloads
   `BMDC.BIN` and the parameters when it starts, it is also lost when you leave MAINTPG and start BXX.
   Whether BXX reloads a running card is not known yet; this is a bench question.
2. **Call them from inside an M-macro.** A non-zero `0xc5c0` also passes the gate, so M3xx-M6xx inside a
   user macro body work without the option (TENTATIVE, depends on section 3).
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

## 3. The M-code macro mechanism (Phase 2 leads, TENTATIVE)

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

## 4. Modifications log

| Change | Status |
|---|---|
| Gate bypass for M300-M663 (four `66 06` → `60 14` edits, section 2 option 3) | Designed, not built. Planned as `tools/patch_bmdc_mcodes.py` |
| New M-codes via the unknown-M fallback 0x2cb0e or unused list entries | Not started (Phase 2) |
