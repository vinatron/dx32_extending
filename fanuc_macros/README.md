# FANUC Custom Macro B examples — conversion-test corpus

These CNC code examples were transcribed mechanically from the user-provided PDF of Peter Smid's *FANUC CNC Custom Macros* (Industrial Press, 2005). The original work is copyrighted by its author/publisher. This directory is intended as a **test fixture for `macro_simulator/src/fb2dx`**, not machine-ready CNC programs. Verify redistribution rights before republishing or reusing outside permitted testing.

## Contents

- `programs/`: individually extracted, `O`-numbered listings with a printed `%` terminator and **no apparent omissions**; `.nc` inputs for parser and conversion testing. A source-complete listing is **not** a guarantee of valid syntax, machining safety or successful conversion.
- `fragments/`: intentionally abbreviated code, error demonstrations, mathematical snippets, listings with editorial ellipses, or code without a reliable program boundary; `.txt` to avoid accidental machine use.
- `manifest.json`: source PDF page numbers (1-indexed), original program numbers, extraction class, features and line counts.

`p254_O8104.nc` means the listing begins on **PDF page 254**; book page numbers are 22 less, so the printed page is 232. Some programs continue onto the next PDF page. Duplicate program numbers or multiple versions from the manual remain separate (`_v2` where necessary).

## Suggested testing

Build the translator as documented in [`../macro_simulator/features/FB2DX.md`](../macro_simulator/features/FB2DX.md) and run it on an input:

```sh
./macro_simulator/fb2dx -series 16 -w /tmp/p254_O8104.dx -o /tmp/p254_O8104.report fanuc_macros/programs/p254_O8104.nc
```

The `-series 16` argument is an **illustration**, not an assertion of the manual's intended controller series. Select the relevant series/parameters for each test. For programs using `G65 Pnnnn` or `M98 Pnnnn`, add the corresponding `O`-numbered macro file(s) as further CLI arguments. Source omissions, proprietary probing M-codes, controller-specific variables and offset behavior may cause intentional refusals. **Never upload or execute extracted programs on a physical machine without manual verification, simulation, bounds checking, tool/setup review and safety approval.**

## Extraction principles

The PDF's fixed-width Courier code blocks were extracted without deliberately correcting numerical data, punctuation, instructions or control flow; marginal explanations were omitted. That means the source's typographical errors and sample-specific assumptions may remain. Examples are indexed by PDF location for rapid comparison with the scanned page. `source_complete` in the manifest only means a printed `%` was present and no omission marker was detected; it is not execution validation.

No FANUC-to-DX32 conversions are asserted to pass by this corpus alone. Use the converter's report and `-check` mode, where available, to determine actual results.
