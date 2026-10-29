/*
 * dxgraph.h - the part of Open Watcom's graph.lib, conio and delay() that the
 * live preview (dxscreen.c) uses, implemented on Win32 GDI by dxwin.c.
 * Used when dxscreen.c is built with -DDX_WIN (DXSIMW.EXE, Windows 95/98/NT).
 * The macros give the functions our own names, so they cannot clash with the
 * C library's kbhit()/getch().
 */
#ifndef DXGRAPH_H
#define DXGRAPH_H

#define _VRES16COLOR    18
#define _DEFAULTMODE    (-1)
#define _GCLEARSCREEN   0

#define _setvideomode   dxwmode
#define _setcolor       dxwcolor
#define _setlinestyle   dxwlstyl
#define _moveto         dxwmove
#define _lineto         dxwline
#define _setcliprgn     dxwclip
#define _clearscreen    dxwclear
#define _settextcolor   dxwtcol
#define _settextposition dxwtpos
#define _outtext        dxwtext
#define _getpixel       dxwgetpx
#define kbhit           dxwkbhit
#define getch           dxwgetch
#define delay           dxwdelay

short dxwmode(short mode);
short dxwcolor(short c);
unsigned short dxwlstyl(unsigned short style);
void  dxwmove(short x, short y);
short dxwline(short x, short y);
void  dxwclip(short x0, short y0, short x1, short y1);
void  dxwclear(short area);
short dxwtcol(short c);
void  dxwtpos(short row, short col);
void  dxwtext(char *text);
short dxwgetpx(short x, short y);
int   dxwkbhit(void);
int   dxwgetch(void);
void  dxwdelay(unsigned ms);

#endif
