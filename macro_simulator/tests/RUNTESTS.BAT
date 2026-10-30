@echo off
rem RUNTESTS.BAT - run the dxsim regression tests from a DOS or Windows 98 prompt.
rem
rem Build first, in ..\src:
rem    wmake -f makefile.wat dos tests        (16-bit DOS)
rem    wmake -f makefile.wat win32 tests32    (Windows console)
rem
rem Usage:  RUNTESTS [path\DXSIM.EXE] [-u]
rem   default simulator: ..\src\DXSIM.EXE, else ..\src\DXSIM32.EXE
rem   -u rewrites EXPECTED\*.OUT (only after reviewing a change)
rem Results go to the screen and to RESULTS.TXT. The plot files (PLOTS.LST) are
rem checked too; PREVIEW.BAT checks the live preview.

if not "%1"=="" goto given
if exist ..\src\DXSIM.EXE goto dos
if exist ..\src\DXSIM32.EXE goto win
echo DXSIM.EXE not found: build it in ..\src first.
goto end

:dos
DXTEST ..\src\DXSIM.EXE %2 > RESULTS.TXT
goto show

:win
DXTEST ..\src\DXSIM32.EXE %2 > RESULTS.TXT
goto show

:given
DXTEST %1 %2 > RESULTS.TXT

:show
type RESULTS.TXT
:end
