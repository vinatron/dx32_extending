@echo off
rem PREVIEW.BAT - every test program through the live preview, unattended, checked.
rem
rem Build a preview first, in ..\src:
rem    wmake -f makefile.wat win32w tests32   (DXSIMW.EXE, the Windows window)
rem    wmake -f makefile.wat dos tests        (DXSIM.EXE, DOS VGA)
rem
rem Usage:  PREVIEW [path\preview.EXE]
rem   default: ..\src\DXSIMW.EXE, else ..\src\DXSIM.EXE
rem Each program runs twice (G17 view, all views) with dxsim -Sa: no waits, no keys.
rem DXTEST compares what was drawn with EXPECTED\SCREEN.OUT (moves, line points,
rem points off the drawing area, paced time) and saves every picture in SCR17\ and
rem SCRALL\. Open SCREENS.HTM to look through them. Results go to the screen and
rem to PREVIEW.TXT.

if not exist SCR17\NUL md SCR17
if not exist SCRALL\NUL md SCRALL
if not "%1"=="" goto given
if exist ..\src\DXSIMW.EXE goto win
if exist ..\src\DXSIM.EXE goto dos
echo No preview build found: build win32w or dos in ..\src first.
goto end

:win
DXTEST -sc ..\src\DXSIMW.EXE -bmp > PREVIEW.TXT
goto show

:dos
DXTEST -sc ..\src\DXSIM.EXE -bmp > PREVIEW.TXT
goto show

:given
DXTEST -sc %1 -bmp > PREVIEW.TXT

:show
type PREVIEW.TXT
echo Pictures: SCREENS.HTM
:end
