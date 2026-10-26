# Handover from the G/M-code track to dxsim

The G/M-code track (`../GM_EXTENSION_PLAN.md`, `../EXT_CODES.md`, `../EXT_PROBING.md`, `../IO_CTL.md`)
adds an entry here in the same PR as any merged change that affects what a part program can do. The dxsim
agent validates each entry, adds it to the simulator and fills in **Outcome**. The newest entry goes at the
top. The process is described in the repo-root `CLAUDE.md`, "Coordination with the macro simulator".

## Entry format

```
### <date> - <short title>   (PR #n, commits abc1234..def5678)

What changed:   new or changed G/M codes, M-macros, probing cycles, I/O bits, parameters, messages
Files:          documents and files changed (EXT_PROBING.md section 4, PROBE.MAC, ...)
Firmware:       addresses of the code involved, and any patch (address, old bytes, new bytes), 6.89
Status:         CONFIRMED (traced or run on the machine) or TENTATIVE, per item
Bench results:  what was run on the machine and what happened (PHASE1_TEST_PLAN.md table, or "not run")
Example:        a short part program that uses it, and what the machine should do

Outcome (dxsim): validated / disagreement found / simulated in commit ..., tests added: ...
```

## Entries

None yet. The extension work starts after the first dxsim PR is merged and the `PHASE1_TEST_PLAN.md`
bench results are in.
