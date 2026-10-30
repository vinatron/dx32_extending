# Open Watcom wmake file for dxsim.  Run from this directory:
#   wmake -f makefile.wat dos      16-bit real-mode DOS, large model  -> DXSIM.EXE    (with -S preview)
#   wmake -f makefile.wat dos32    32-bit DOS extender (DOS/4GW)      -> DXSIM4G.EXE  (with -S preview)
#   wmake -f makefile.wat win32    Windows 95/98/NT console           -> DXSIM32.EXE
#   wmake -f makefile.wat win32w   same, with the -S preview window   -> DXSIMW.EXE
#   wmake -f makefile.wat fb       FANUC macro translator, DOS        -> FB2DX.EXE
#   wmake -f makefile.wat fb32     FANUC macro translator, Windows    -> FB2DX.EXE
#   wmake -f makefile.wat tests    test runner for DOS                -> ..\tests\DXTEST.EXE
#   wmake -f makefile.wat tests32  test runner for Windows            -> ..\tests\DXTEST.EXE
# then: cd ..\tests and RUNTESTS (see RUNTESTS.BAT).
# Checked with Open Watcom 1.9: all build with -za (strict ANSI) and no warnings.
# The DOS targets link graph.lib for the live VGA preview (dxsim -S, dxscreen.c).

SRCW = dxmain.c dxinterp.c dxexpr.c dxmotion.c dxprog.c dxout.c dxcorpus.c dxscreen.c dxparam.c dxcomp.c dxmap.c
SRC = $(SRCW) dxwin.c
FBSRC = fbmain.c fbread.c fbdial.c fbeval.c fbout.c fbcheck.c
OPT = -za -wx -ox -q

all: dos win32 .SYMBOLIC

dos: .SYMBOLIC
	wcl -bt=dos -ml $(OPT) -DDX_SCREEN -fe=DXSIM.EXE $(SRC) graph.lib

dos32: .SYMBOLIC
	wcl386 -bt=dos -l=dos4g $(OPT) -DDX_SCREEN -fe=DXSIM4G.EXE $(SRC) graph.lib

win32: .SYMBOLIC
	wcl386 -bt=nt -l=nt $(OPT) -fe=DXSIM32.EXE $(SRC)

# dxwin.c includes windows.h, which is not strict ANSI: compiled on its own without -za
win32w: .SYMBOLIC
	wcc386 -bt=nt -wx -ox -q -DDX_SCREEN -DDX_WIN -fo=dxwin.obj dxwin.c
	wcl386 -bt=nt -l=nt $(OPT) -DDX_SCREEN -DDX_WIN -fe=DXSIMW.EXE $(SRCW) dxwin.obj

# fb2dx: a 16 KB stack (the default 4 KB is too small for the writer's nested calls)
fb: .SYMBOLIC
	wcl -bt=dos -ml $(OPT) -k16384 -fe=FB2DX.EXE $(FBSRC)

fb32: .SYMBOLIC
	wcl386 -bt=nt -l=nt $(OPT) -fe=FB2DX.EXE $(FBSRC)

tests: .SYMBOLIC
	wcl -bt=dos -ml $(OPT) -k8192 -fe=..\tests\DXTEST.EXE ..\tests\dxtest.c

tests32: .SYMBOLIC
	wcl386 -bt=nt -l=nt $(OPT) -fe=..\tests\DXTEST.EXE ..\tests\dxtest.c

clean: .SYMBOLIC
	rm -f *.obj *.EXE *.err
