# Open Watcom wmake file for dxsim.  Run from this directory:
#   wmake -f makefile.wat dos      16-bit real-mode DOS, large model  -> DXSIM.EXE
#   wmake -f makefile.wat dos32    32-bit DOS extender (DOS/4GW)      -> DXSIM4G.EXE
#   wmake -f makefile.wat win32    Windows 95/98/NT console           -> DXSIM32.EXE
#   wmake -f makefile.wat tests    test runner for DOS                -> ..\tests\DXTEST.EXE
#   wmake -f makefile.wat tests32  test runner for Windows            -> ..\tests\DXTEST.EXE
# then: cd ..\tests and RUNTESTS (see RUNTESTS.BAT).
# Checked with Open Watcom 1.9: all three build with -za (strict ANSI) and no warnings.

SRC = dxmain.c dxinterp.c dxexpr.c dxmotion.c dxprog.c dxout.c dxcorpus.c
OPT = -za -wx -ox -q

all: dos win32 .SYMBOLIC

dos: .SYMBOLIC
	wcl -bt=dos -ml $(OPT) -fe=DXSIM.EXE $(SRC)

dos32: .SYMBOLIC
	wcl386 -bt=dos -l=dos4g $(OPT) -fe=DXSIM4G.EXE $(SRC)

win32: .SYMBOLIC
	wcl386 -bt=nt -l=nt $(OPT) -fe=DXSIM32.EXE $(SRC)

tests: .SYMBOLIC
	wcl -bt=dos -ml $(OPT) -fe=..\tests\DXTEST.EXE ..\tests\dxtest.c

tests32: .SYMBOLIC
	wcl386 -bt=nt -l=nt $(OPT) -fe=..\tests\DXTEST.EXE ..\tests\dxtest.c

clean: .SYMBOLIC
	rm -f *.obj *.EXE *.err
