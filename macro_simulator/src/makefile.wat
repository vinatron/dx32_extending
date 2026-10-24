# Open Watcom wmake file for dxsim.  Run from this directory:
#   wmake -f makefile.wat dos      16-bit real-mode DOS, large model  -> DXSIM.EXE
#   wmake -f makefile.wat dos32    32-bit DOS extender (DOS/4GW)      -> DXSIM4G.EXE
#   wmake -f makefile.wat win32    Windows 95/98/NT console           -> DXSIM32.EXE
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

clean: .SYMBOLIC
	rm -f *.obj *.EXE *.err
