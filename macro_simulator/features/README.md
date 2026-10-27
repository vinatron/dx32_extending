# dxsim feature library

There is one document per feature while it is being built. `../ROADMAP.md` is the ordered list; the
documents here hold the detail.

| File | Feature | Roadmap | Status |
|---|---|---|---|
| `OPENQ.md` | **Open questions register**: every open question, with how to answer it (firmware trace, bench test, BXX trace); the bench session | all | kept current |
| `FILLET.md` | Fillets (G22/G23) | item 3 | **Simulated** (phases 0-3 done); bench check waits for the owner |
| `CUTCOMP.md` | Cutter diameter compensation (G40/G41/G42, G48/G49, G44/G45) | item 2 | **Simulated** (phases 0-5 done); phase 6, the bench check on the machine, waits for the owner |

## How a feature document is kept

- **One file per feature**, with an 8.3 name, so it can be read on the DOS machine too.
- **Fixed sections**, in this order:
  1. **Status:** the phase table, kept current. Each phase is *not started*, *in progress*, *done*, or
     *blocked*, with a reason.
  2. **What the control does:** the manual and the firmware, each fact marked CONFIRMED or TENTATIVE, with
     the source: manual page or figure, or firmware address. This is the specification. If a later trace
     changes it, the change is made here first.
  3. **Design:** how dxsim implements it. That covers files, data structures, and how it fits the existing
     code.
  4. **Errors and warnings:** what dxsim reports, and why.
  5. **Tests:** the test list, with how each expected result is worked out.
  6. **Phases:** a checklist per phase.
  7. **Open questions:** what is not known yet, and what would settle it (a firmware trace, or a bench run
     on the machine).
  8. **Change log:** date, commit and what changed. There is one entry per commit that touches the feature.
- **Updated in the same commit** as the code it describes, so the document and the code never disagree.
- **When the feature is finished,** the user-facing part goes into `../README.md` and the language facts
  into `../LANGUAGE.md`. The document stays here as the design record, marked *done*, and its row in
  `../ROADMAP.md` is marked done.
