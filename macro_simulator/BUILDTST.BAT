@echo off
rem BUILDTST.BAT - build dxsim and fb2dx with Open Watcom and run every test.
rem
rem Usage, from this folder (macro_simulator), with the Watcom environment set:
rem    BUILDTST WIN     Windows 95/98/NT: DXSIM32, DXSIMW, FB2DX, DXTEST (win32 win32w fb32 tests32)
rem    BUILDTST DOS     16-bit DOS: DXSIM (with the VGA preview), FB2DX, DXTEST (dos fb tests)
rem
rem It builds (output in BUILD.TXT), stops if the build fails, then runs
rem    tests\RUNTESTS   traces, plots, fb2dx and the FANUC corpus  -> tests\RESULTS.TXT
rem    tests\PREVIEW    the preview: drawing, BMPs, scripted keys  -> tests\PREVIEW.TXT
rem and shows the last line of each. Leave the mouse and keyboard alone while it runs.

if "%1"=="WIN" goto win
if "%1"=="win" goto win
if "%1"=="DOS" goto dos
if "%1"=="dos" goto dos
echo Usage: BUILDTST WIN   or   BUILDTST DOS
goto end

:win
cd src
wmake -f makefile.wat win32 win32w fb32 tests32 > ..\BUILD.TXT
if errorlevel 1 goto failed
cd ..\tests
call RUNTESTS ..\src\DXSIM32.EXE > NUL
call PREVIEW ..\src\DXSIMW.EXE > NUL
goto summary

:dos
cd src
wmake -f makefile.wat dos fb tests > ..\BUILD.TXT
if errorlevel 1 goto failed
cd ..\tests
call RUNTESTS ..\src\DXSIM.EXE > NUL
call PREVIEW ..\src\DXSIM.EXE > NUL
goto summary

:failed
cd ..
echo The build failed: see BUILD.TXT and the .ERR files in src.
goto end

:summary
echo RUNTESTS (tests\RESULTS.TXT):
find "passed," RESULTS.TXT
find "FAIL" RESULTS.TXT
echo PREVIEW (tests\PREVIEW.TXT; pictures in tests\SCREENS.HTM):
find "passed," PREVIEW.TXT
find "FAIL" PREVIEW.TXT
cd ..
:end
