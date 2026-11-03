# BXX "9 EXIT TO DOS" and NEXT.BAT

Symptom: after launching NC-VIEW (runs `NC`) or WINDOWS (runs `WIN`) from BXX and coming back,
**9 EXIT TO DOS** runs that same program again instead of leaving you at the DOS prompt.

Status: mechanism CONFIRMED by disassembly of both BXX builds. On the machine the real cause is RUN.BAT (see the last section), and the fix is a batch-file edit. The optional patch
(`tools/patch_bxx_exit_dos.py`) verified by disassembly only. It has not been run on the machine yet.

Addresses below are **file offsets** in the EXE, header included. For 4.79 the code is in Ghidra
segment `1129` (Ghidra address = `1129:(file − 0x3C00 − 0x1290)`). Ghidra never decompiled
this part of the segment, so none of it is in `decompiled/BXX.c`. Disassemble it with capstone
or `objdump -b binary -m i8086`.

## How BXX launches the other applications

BXX doesn't run NC, WIN, MAINTPG and so on itself. Each launcher in the main menu function writes
a one-line batch file and then quits BXX:

```c
sprintf(buf, "%s:\\NEXT.BAT", drive);   // drive = DS:0xC7DA (4.79) / DS:0xA252 (4.23)
f = fopen(buf, "wb");
fputs("call EZ-VIEW", f);               // the line differs per launcher
if (f) fclose(f);
quit_flag = 1;                          // [bp-0x2CA] (4.79) / [bp-0xBC] (4.23)
goto quit;                              // saves PTS.DAT etc. and returns to DOS
```

Key dispatch is the compare chain at 4.79 0x9141 (key code in `[bp-0x272]`):

| Key | Menu label | Line written to NEXT.BAT | 4.79 launcher |
|-----|------------|--------------------------|---------------|
| F2  | NC-VIEW    | `call EZ-VIEW`           | 0x6C3E |
| F3  | WINDOWS    | `call EZ-WNDO`           | 0x6CC5 |
| F4  | EZ-SPS     | `call EZ-SPS`            | 0x6D4C |
| F5  | MaintPg    | `call MaintPg.bat`       | 0x6DD3 |
| F6  | dataNET    | `call datanet`           | 0x6E5A |
| F7  | (probe)    | `call EZ-MZR`, only if the probe is ON | 0x6EE1 |
| 9   | EXIT TO DOS | nothing (the bug)       | 0x764C (prompt), 0x76CB (Y test) |

The batch file that started BXX runs `NEXT.BAT` once BXX has quit. EZ-VIEW.BAT and the others
(not in this repo) run the program and then start BXX again.

## Why Exit to DOS re-runs the last program

The <Y> branch of the "EXIT TO DOS? <Y>= EXIT" prompt (4.79: 0x76CB, 4.23: 0x9B1B) only does
`quit_flag = 1; goto quit;`. It **never writes NEXT.BAT**. BXX never deletes NEXT.BAT either, so the
file still contains the last launcher's line. The outer batch file then runs it, and you end up in NC or WIN again.

The only other place that writes NEXT.BAT is the 4.79 "BMDC communication failure" exit (0x9E93).
It truncates the file to a single `"\n"` before `exit(1)`. So the original authors did intend an empty
NEXT.BAT to mean "just go to DOS", but they never did the same for the Exit-to-DOS menu item. 4.23 doesn't even have
the comm-failure clear.

## The patch

`tools/patch_bxx_exit_dos.py` makes <Y> write `"\n"` to NEXT.BAT (the same thing the comm-failure path
writes) before quitting. It works by sending <Y> through the NC-VIEW launcher with a different string:

```
handler (29 bytes, was the cmp/je/jmp test for 'y'/'Y'):
    mov ax,[bp+key] ; cmp ax,'y' ; je + ; cmp ax,'Y' ; jne <other keys>
 +: mov si,"\n"           ; jmp launcher      <- EXIT TO DOS
    mov si,"call EZ-VIEW" ; jmp launcher      <- NC-VIEW (menu dispatch jump retargeted here)
    nop nop nop
launcher: mov ax,"call EZ-VIEW"  ->  mov ax,si ; nop
```

`si` is saved in the function prologue and never used as a register variable in this function. The
apparent `si` operands in a linear disassembly are 8087-emulator operand bytes after `int 0x3x`.
No far call or relocated word moves. The launcher also sets a "handled" flag
(`[bp-0x236]` / `[bp-0x8]`), which doesn't matter because BXX is quitting anyway.

| Build | File | Patch sites (handler / launcher string / dispatch) | `"\n"` string |
|-------|------|------------------------------------------------------|---------------|
| 4.79.22 (577322 B, md5 99a320a1…) | repo `BXX.EXE` | 0x76CB / 0x6C7B / 0x916E | DS:0x14A8 |
| 4.23 (403490 B, md5 657801ef…) | `my_tc22_files/BXX.EXE` | 0x9B1B / 0x8C6B / 0xAB40 | DS:0x1D8A |

```bash
python3 tools/patch_bxx_exit_dos.py --check BXX.EXE
python3 tools/patch_bxx_exit_dos.py BXX.EXE BXX_NEW.EXE      # then copy over BXX.EXE on the machine (keep the original)
```

### Bench check

1. Run NC-VIEW, quit NC and get back to BXX. Choose 9 EXIT TO DOS and answer Y. You should land at the DOS prompt
   and `type NEXT.BAT` should show an empty line.
2. NC-VIEW, WINDOWS and the other launchers still work as before.
3. If Exit to DOS now brings BXX straight back up, the outer batch file loops on its own. Check
   AUTOEXEC.BAT and the batch file that starts BXX.

## Actual cause on the machine: Windows 98 Setup disabled the DEL in RUN.BAT (CONFIRMED)

AUTOEXEC.BAT runs `RUN`, and RUN.BAT is the loop that runs BXX and then NEXT.BAT:

```bat
@ECHO OFF
:LOOP
BDX
FONTLOAD \DOS\BXT.FNT
ECHO Loading Operator Interface...
BXX %1
IF NOT EXIST C:\NEXT.BAT GOTO SKIP
CALL C:\NEXT.BAT
rem - By Windows Setup - DEL C:\NEXT.BAT     <- the original line, commented out by Win98 Setup
GOTO LOOP
:SKIP
```

`EZ-VIEW.BAT` is just `NC`, and `EZ-WNDO.BAT` is just `WIN`. They return to RUN, which loops back to BXX.
BXX was designed so that each run either writes a fresh NEXT.BAT (an app launch) or writes nothing
(Exit to DOS). RUN.BAT then has to delete the file once it has run it. Without the `DEL`, NEXT.BAT
lasts forever, so Exit to DOS finds it, runs the old app and loops back into BXX. Leaving BXX
would then always relaunch the last app, which matches the symptom.

**Fix (no EXE patch needed):** use `dos_batch/RUN.BAT`. It has DOS line endings, the `DEL` is
restored, and it adds one line before `:LOOP` that clears any NEXT.BAT left over from an
earlier session:

```bat
@ECHO OFF
IF EXIST C:\NEXT.BAT DEL C:\NEXT.BAT
:LOOP
BDX
FONTLOAD \DOS\BXT.FNT
ECHO Loading Operator Interface...
BXX %1
IF NOT EXIST C:\NEXT.BAT GOTO SKIP
CALL C:\NEXT.BAT
DEL C:\NEXT.BAT
GOTO LOOP
:SKIP
```

The `DEL` comes after `CALL` returns, so NEXT.BAT is no longer running when it is deleted. The 4.79 comm-failure path writes
a one-line NEXT.BAT, so the loop runs that blank line, deletes the file and restarts BXX, which is
what "Hit any key to restart machine" implies. If Windows Setup runs again, check whether it has
commented the line out again.

With RUN.BAT fixed, the BXX patch above is optional. It only helps if NEXT.BAT can be left over by
something other than this loop.
