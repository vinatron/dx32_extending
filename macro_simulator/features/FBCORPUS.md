# fb2dx on the FANUC macro corpus (`../../fanuc_macros`)

A review of the corpus the owner added on 2026-10-30, and of how `fb2dx` does on it. The corpus is code
transcribed from Peter Smid, *FANUC CNC Custom Macros* (Industrial Press, 2005); its `README.md` explains
the extraction and the copyright caveat. It is a test fixture: nothing here is for a machine.

## 1. Status

| Step | Status |
|---|---|
| Read `fanuc_macros/README.md`, `manifest.json`, `verify_corpus.py` | **done** |
| Integrity check (`python3 fanuc_macros/verify_corpus.py`) | **passes**: 143 fixtures, 49 complete programs, 94 fragments |
| Every file through the reader (`fb2dx -parse`) | **done**: every valid block reads (section 3) |
| Every complete program through the translator (`-fparam default -w ... -check`) | **done**: none translates yet (section 4) |
| Regression tests: `tests/corpus.lst`, 41 checks in `RUNTESTS` | **done** |
| fb2dx changes for the common blockers (section 5) | proposed, not started: the owner decides |

## 2. The corpus

- `programs/`: 49 `O`-numbered listings with a `%` end and no apparent omissions. 17 are main programs
  and their subprograms (`O00xx`, `O1001`, `O1002`); the other 32 are macros (`O80xx`, `O81xx`, `O82xx`, `O90xx`).
- `fragments/`: 94 pieces: syntax illustrations, abbreviated listings with `...`, error examples.
- `manifest.json`: PDF page, program number, completeness and features of each file. Feature counts
  over all 143: arithmetic 34, system variables 26, conditions 26, calls 19, alarms 16, offsets 12,
  loops 9, probing 2.
- The file names are long (`p044_O0001.nc`), so 16-bit DOS cannot open them; the DXTEST corpus tests
  are skipped there with a message, and run on Windows and Linux.

## 3. The reader

Every valid block in all 143 files reads. The blocks it cannot read are not FANUC, and a FANUC control
would stop on them too:

| File | Block | What it is |
|---|---|---|
| `programs/p207_O8009.nc` line 3 | `WHILE [#25 LE 360.0]` | the book leaves out `DO1` (its `END1` is there) |
| `programs/p313_O8113.nc` lines 11, 12 | `IF[#16=20.0]GOTO20` | FANUC compares with `EQ`, not `=` (also in `O0033`'s run) |
| `fragments/p205_snippet_119.txt` line 2 | `IF[#1EQ0` | a missing `]` |
| `fragments/p275_O0030.txt` line 3 | `... G49 TO3` | a transcription slip: the letter O for zero in `T03` |
| fragments with `...` or `…` (90 blocks) | | editorial omissions, as the corpus README says |
| `p139/p140` `ROUND[#1]`, `p205` `IF[#1EQ#0]` alone, `p147/p148` `#5=[#1EQ#2]`, `p149` `#[3]` | | notation shown on its own, or a condition assigned to a variable (not FANUC Macro B) |

The first four are worth correcting in the corpus (or noting in its manifest), since the README says
the code was not corrected.

## 4. The translator

`tests/corpus.lst` runs each main program with the programs it calls (`G65`/`M98 Pnnnn` found in the
corpus), 15 runs, and each macro that no corpus program calls, alone, 26 runs: 41 in all, `-fparam default -w ... -check`.
**None translates yet.** Every stop is a refusal with a reason; nothing is translated wrongly.

| Program | Called programs | Result | Blockers |
|---|---|---|---|
| `p044_O0001` |  | refused | G43 H, tool unknown; G28 form; G84 tapping; R (G84) |
| `p045_O0002` | `p045_O1001` | refused | G43 H, tool unknown; L repeat: series; G28 form; G84 tapping; R (G84); L (G84) |
| `p046_O0003` | `p047_O1002` | refused | G43 H, tool unknown; L repeat: series; G84 tapping; R (G84); L (G84); G28 form |
| `p054_O8001` |  | refused | address W; address U; macro alone (M99) |
| `p122_O8004` |  | refused | vacant variable; macro alone (M99) |
| `p151_O0011` |  | refused | G43 H, tool unknown; axis words after G80; G28 form |
| `p152_O0012` |  | refused | G43 H, tool unknown; axis words after G80; G28 form |
| `p153_O0013` |  | refused | G43 H, tool unknown; axis words after G80; G28 form |
| `p154_O0015` |  | refused | G43 H, tool unknown; axis words after G80; G28 form |
| `p155_O0016` |  | refused | G43 H, tool unknown; axis words after G80; G28 form |
| `p156_O0017` | `p156_O8005` | refused | G43 H, tool unknown; axis words after G80; G28 form |
| `p157_O8006` |  | refused | division by 0 |
| `p168_O8007` |  | refused | system variable |
| `p207_O8009` |  | refused | source error |
| `p208_O8011` |  | refused | vacant #0; #[...] indirect; macro alone (M99) |
| `p210_O8013` |  | refused | vacant variable; macro alone (args unset); macro alone (M99) |
| `p211_O8014` |  | refused | vacant variable; macro alone (args unset); macro alone (M99) |
| `p214_O8017` |  | refused | system variable |
| `p214_O8018` |  | refused | system variable |
| `p218_O8019` |  | refused | system variable |
| `p221_O8020` |  | refused | system variable |
| `p240_O8021` |  | refused | units unknown |
| `p241_O8021` |  | refused | units unknown |
| `p245_O8101` |  | refused | vacant variable; axis words after G80; macro alone (M99) |
| `p245_O8101_v2` |  | refused | vacant variable; macro alone (M99) |
| `p247_O8102` |  | refused | vacant variable; axis words after G80; macro alone (M99) |
| `p250_O8103` |  | refused | system variable |
| `p254_O8104` |  | refused | system variable |
| `p257_O8105` |  | refused | system variable |
| `p260_O8106` |  | refused | macro alone (args unset); system variable; macro alone (M99) |
| `p264_O0027` | `p264_O8107` | refused | system variable |
| `p268_O0028` | `p268_O8108` | refused | system variable |
| `p270_O8022` |  | refused | units unknown; macro alone (M99) |
| `p272_O0029` | `p272_O8109` | refused | system variable |
| `p275_O8110` |  | refused | macro alone (args unset); units unknown; macro alone (M99) |
| `p285_O9012` |  | refused | system variable |
| `p285_O9013` |  | refused | system variable |
| `p288_O0031` |  | refused | G13; D (G13); G28 form |
| `p295_O8200` |  | refused | POPEN/DPRNT; vacant variable; macro alone (M99) |
| `p311_O0032` | `p311_O8112` | refused | system variable |
| `p313_O0033` | `p313_O8113` | refused | source error |

**The blockers, by how many programs they stop:**

| Programs | Blocker | Why fb2dx refuses | What would fix it |
|---|---|---|---|
| 15 | system variables: `#4003` (5), `#4001` (3), `#3003` (2), `#3001` (2), `#4006`, `#4214`, `#3004`, `#2404` | no DX32 equivalent | 5.4, 5.5 |
| 12, 4 | a macro alone (`M99` ends a main program; arguments never set) | no caller in the corpus | 5.6 |
| 10 | `G28 Z25.0`, `G28 X.. Y.. Z..` (G90, an intermediate point) | only `G91 G28 Z0` translates | 5.2 |
| 9 | `G43 H01` while the tool in the spindle is not known | the book's programs assume T01 is in the spindle at the start | 5.1 |
| 8 | `G80 Z5.0`: axis words after G80 with no G0/G1 in the block | fb2dx takes G80 to leave no motion code in force | 5.3 |
| 7 | a vacant variable used where the DX32 would use 0 | mostly the macros alone | 5.6 |
| 4 | the unit system is not known (no G20/G21) | | `-mm` or `-inch` already settles it |
| 3 | `G84` tapping, with its `R` and `L` | not translated (FB2DX.md 3.16) | later |
| 2 | `L` as a repeat count | series-dependent | `-series` |
| 1 each | lathe words `U`/`W` (`O8001` is a lathe program); `G13` with `D` (circle cutting, an option on some controls); `POPEN`/`DPRNT`; `#[...]` indirection; a division by an unset argument (`O8006` alone) | out of scope, or no DX32 equivalent | none |

## 5. Proposed fb2dx changes

Each keeps the rule that fb2dx never guesses: where the answer depends on the machine, an option says it.

1. **`-ftool n`: the tool in the spindle at the start.** Smid's programs begin with T01 already in the
   spindle and preselect the next tool (`T02` on the first motion line). With the start tool given,
   `G43 H01` checks out like any other.
2. **`G28` through an intermediate point in G90.** `G28 Z25.0` is a move to Z25 and then to the
   reference point: `G0Z25.` followed by the `-g28` choice (`m25` or `drop`). X/Y reference return
   (`G28 X100.0 Y50.0 Z5.0`) has no DX32 equivalent; an option could make it a move to the intermediate
   point only (`-g28 xy=stay`), otherwise it stays refused.
3. **`-fparam g80=keep|none`: the motion code after G80.** The book writes `G80 Z5.0` and expects a rapid,
   i.e. the 01-group code from before the cycle (G00) still in force. Whether a FANUC control keeps it is
   part of the open per-series question (`FB2DX.md` Q6); with `keep` the evaluator would do that.
4. **Modal information `#4001`-`#4130` (and `#4201`-`#4330`) folded to the value the evaluation saw**,
   as argument checks already are (the run covers every call, and a translated program has no outside
   inputs). `#4001` (motion), `#4003` (G90/G91) and `#4006` (G20/G21) are what the corpus reads. A read
   that sees different values in different passes stays refused.
5. **`#3003`/`#3004` writes** (single-block and feed-hold suppression) left out with a NOTE: they change
   how the operator can stop the program, not the tool path. `#3001` (the clock), `#2xxx` offsets and
   `#5xxx` positions stay refused.
6. **Test callers for the macros.** Most of the book's calls are in fragments with `...`. Small callers
   written for the tests (not from the book), one per macro with typical arguments, would let fb2dx
   translate and `-check` the 26 macros that now run alone.

With 5.1-5.3 the nine simple main programs (`O0001`-`O0003`, `O0011`-`O0017`) should get past these
refusals; the three with G84 tapping (`O0001`-`O0003`) still stop there. 5.4-5.6 open the macros.

## 6. Tests

`tests/corpus.lst` (41 runs, expected reports `tests/expected/k*.out`) is part of `DXTEST`, so
`RUNTESTS` and `make test` check it; each change in section 5 shows up as a reviewed change in those
reports. On 16-bit DOS they are skipped (long file names).

## 7. Change log

| Date | Commit | Change |
|---|---|---|
| 2026-10-30 | (this commit) | Corpus reviewed: integrity, reader, translator; source errors listed; blockers counted; changes proposed; `tests/corpus.lst` |
