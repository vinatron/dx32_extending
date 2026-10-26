/*
 * dxscreen.c - live preview on a VGA screen (DOS, Open Watcom graph.lib).
 *
 * Built only with -DDX_SCREEN (makefile.wat targets "dos" and "dos32"); the
 * program is then run with -S.  It works like the control's F5 PREVIEW:
 * the tool path is drawn while the program runs.
 *
 *   SPACE  pause / continue          B  block by block on / off
 *   + -    faster / slower           F  pacing: per move <-> feed rate
 *   ESC    stop
 *
 * Pacing "per move" waits a fixed time after each move (speed 0 = none).
 * Pacing "feed rate" draws each move at the programmed feed (rapids at
 * 472 ipm, manual chapter 8) times a speed factor; the DOS clock ticks 18.2
 * times a second, so the timing is approximate.
 *
 * -DDX_WIN (DXSIMW.EXE) draws in a Windows window instead, through dxwin.c.
 * -DDX_SCSTUB replaces graph.lib, conio and delay() with a logger so this file
 * can be compiled and exercised on any system (tests); DXSC_KEYS holds the
 * keys to "press".
 */
#ifdef DX_SCREEN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dxsim.h"

#if defined(DX_WIN)
#include "dxgraph.h"            /* the same calls on Win32 GDI (dxwin.c) */
#elif !defined(DX_SCSTUB)
#include <graph.h>
#include <conio.h>
#include <i86.h>
#else
/* ---- stand-ins for graph.lib, conio and delay() ---- */
#define _VRES16COLOR 18
#define _DEFAULTMODE (-1)
#define _GCLEARSCREEN 0
static FILE *scl;
static long stubms, stubpix, stubbad;
static int stubx, stuby;
static const char *stubkeys, *stubcomma;
static void stubopen(void)
{
    if (!scl) scl = fopen("dxscreen.log", "w");
    if (!stubkeys) stubkeys = getenv("DXSC_KEYS");
    if (!stubkeys) stubkeys = "";
    stubcomma = strchr(stubkeys, ',');
    if (!stubcomma) stubcomma = stubkeys + strlen(stubkeys);
}
static short _setvideomode(short m) { stubopen(); fprintf(scl, "mode %d\n", m); return 1; }
static short _setcolor(short c) { (void)c; return 0; }
static unsigned short _setlinestyle(unsigned short st) { (void)st; return 0; }
static void stubpt(int x, int y)
{
    stubpix++;
    if (x < 0 || x > 639 || y < 0 || y > 447) {
        stubbad++;
        fprintf(scl, "offscreen %d %d\n", x, y);
    }
}
static void _moveto(short x, short y) { stubx = x; stuby = y; stubpt(x, y); }
static short _lineto(short x, short y) { stubpt(x, y); stubx = x; stuby = y; return 1; }
static void _setcliprgn(short a, short b, short c, short d) { (void)a; (void)b; (void)c; (void)d; }
static void _clearscreen(short a) { (void)a; }
static short _settextcolor(short c) { (void)c; return 0; }
static void _settextposition(short r, short c) { (void)r; (void)c; }
static void _outtext(char *t) { fprintf(scl, "text %s\n", t); }
/* keys before the first ',' are seen by kbhit() (typed ahead); the rest only by getch() */
static int kbhit(void) { return *stubkeys != 0 && stubkeys < stubcomma; }
static int getch(void)
{
    int c;
    while (*stubkeys == ',') stubkeys++;
    c = (unsigned char)*stubkeys;
    if (c) stubkeys++;
    else c = 27;
    fprintf(scl, "key %d\n", c);
    return c;
}
static void delay(unsigned ms) { stubms += ms; }
#endif

#define SCW     640
#define SCH     448             /* drawing area; rows 29-30 are the status lines */
#define RAPID   472.0           /* ipm, manual chapter 8; dxemit passes params 7/8 */
#define TICKMS  55              /* one DOS clock tick */

static const unsigned movems[10] = { 0, 2, 5, 10, 20, 50, 100, 200, 400, 800 };

static struct {
    int    on;
    int    paced;               /* 1: feed-rate pacing */
    int    speed;               /* per-move pacing: index into movems */
    double factor;              /* feed-rate pacing: 1 = machine speed */
    int    pause, block, quit;
    double rate;                /* in/min of the move being drawn */
    long   moves;
} sc;

static void status(dxsim *s, const char *msg);

void dxscinit(int paced)
{
    sc.on = 1;
    sc.paced = paced;
    sc.speed = paced ? 0 : 4;
    sc.factor = 1.0;
}

/* 0x1b = ESC */
static void key(dxsim *s, int c)
{
    switch (c) {
    case ' ': sc.pause = !sc.pause; break;
    case 'b': case 'B': sc.block = !sc.block; break;
    case 'f': case 'F': sc.paced = !sc.paced; break;
    case '+': case '=':
        if (sc.paced) { if (sc.factor < 64.0) sc.factor *= 2.0; }
        else if (sc.speed > 0) sc.speed--;
        break;
    case '-': case '_':
        if (sc.paced) { if (sc.factor > 1.0 / 64) sc.factor /= 2.0; }
        else if (sc.speed < 9) sc.speed++;
        break;
    case 27:
        sc.quit = 1;
        s->stop = 1;
        break;
    default: break;
    }
}

/* read waiting keys; while paused, wait for the next one */
static void poll(dxsim *s)
{
    while (kbhit()) key(s, getch());
    while (sc.pause && !sc.quit) {
        status(s, "PAUSED - SPACE continues");
        key(s, getch());
    }
}

static void status(dxsim *s, const char *msg)
{
    char l1[200], l2[200], where[16];
    const char *t = s->bt ? s->bt : "";
    double p[3];
    dxbwhere(s, s->start.blk, where);
    dxtopos(s, s->cur, p);
    sprintf(l1, "%-7s %-50.50s %.60s", where, t, msg ? msg : "");
    if (sc.paced)
        sprintf(l2, "X%9.4f Y%9.4f Z%9.4f F%7.1f  feed x%-6g SPC B F +- ESC",
                p[0], p[1], p[2], s->feed, sc.factor);
    else
        sprintf(l2, "X%9.4f Y%9.4f Z%9.4f F%7.1f  move %3ums SPC B F +- ESC",
                p[0], p[1], p[2], s->feed, movems[sc.speed]);
    l1[79] = l2[79] = 0;
    while (strlen(l1) < 79) strcat(l1, " ");
    while (strlen(l2) < 79) strcat(l2, " ");
    _settextcolor(15);
    _settextposition(29, 1);
    _outtext(l1);
    _settextposition(30, 1);
    _outtext(l2);
}

/* before each block (dxsim.blkhook): block-by-block stops before program blocks */
static void blkhook(dxsim *s)
{
    if (sc.quit) return;
    poll(s);
    if (sc.block && s->start.blk < s->corp0) {
        s->bt = dxbtext(s, s->start.blk);
        status(s, "BLOCK - any key: next");
        key(s, getch());
        if (sc.pause) poll(s);
    }
}

void dxscbeg(dxsim *s, dxplot *pl)
{
    double w = pl->maxx - pl->minx, h = pl->maxy - pl->miny;
    double sx = (SCW - 8) / w, sy = (SCH - 8) / h;
    pl->sc = sx < sy ? sx : sy;
    pl->ox = (SCW - w * pl->sc) / 2.0;
    pl->oy = (SCH - h * pl->sc) / 2.0;
    _setvideomode(_VRES16COLOR);
    _clearscreen(_GCLEARSCREEN);
    _setcliprgn(0, 0, SCW - 1, SCH - 1);
    s->blkhook = blkhook;
    s->bt = "";
    status(s, "RUN");
}

/* part zero of one view: a small grey cross */
void dxscmark(dxplot *pl, double x, double y)
{
    (void)pl;
    if (x < 6 || x >= SCW - 6 || y < 6 || y >= SCH - 6) return;
    _setcolor(8);
    _setlinestyle(0xffff);
    _moveto((short)(x - 6), (short)(SCH - 1 - y));
    _lineto((short)(x + 6), (short)(SCH - 1 - y));
    _moveto((short)x, (short)(SCH - 1 - y - 6));
    _lineto((short)x, (short)(SCH - 1 - y + 6));
}

/* a view name with its bottom left corner near device point x, y (8x16 text cells) */
void dxsclabel(dxplot *pl, const char *text, double x, double y)
{
    int row = (int)((SCH - 1 - y) / 16.0), col = (int)(x / 8.0) + 1;
    char buf[16];
    (void)pl;
    if (row < 1) row = 1;
    if (row > SCH / 16) row = SCH / 16;
    if (col < 1) col = 1;
    if (col > 80 - (int)strlen(text)) col = 80 - (int)strlen(text);
    sprintf(buf, "%.15s", text);
    _settextcolor(7);
    _settextposition((short)row, (short)col);
    _outtext(buf);
}

static void draw(int pen, double x0, double y0, double x1, double y1)
{
    _setcolor((short)(pen ? 11 : 12));          /* light cyan feed, light red rapid */
    _setlinestyle((unsigned short)(pen ? 0xffff : 0xf0f0));
    _moveto((short)floor(x0 + 0.5), (short)(SCH - 1 - floor(y0 + 0.5)));
    _lineto((short)floor(x1 + 0.5), (short)(SCH - 1 - floor(y1 + 0.5)));
}

/* one straight piece in device units; len = its 3-D length in inches */
void dxscline(dxsim *s, dxplot *pl, int pen, double x0, double y0, double x1, double y1, double len)
{
    double ms;
    long n, i;
    if (sc.quit) return;
    if (!sc.paced || len <= 0.0) {
        draw(pen, x0, y0, x1, y1);
        return;
    }
    ms = len / (sc.rate > 0 ? sc.rate : RAPID) * 60000.0 / sc.factor;
    n = (long)(ms / TICKMS);
    if (n < 1) n = 1;
    if (n > 200) n = 200;
    for (i = 0; i < n && !sc.quit; i++) {
        double a = (double)i / n, b = (double)(i + 1) / n;
        draw(pen, x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * b, y0 + (y1 - y0) * b);
        delay((unsigned)(ms / n));
        poll(s);
    }
    (void)pl;
}

/* before a move is drawn: its rate (in/min) for the pacing, feed or rapid */
void dxscrate(double feedipm)
{
    sc.rate = feedipm;
}

/* after each move */
void dxscmove(dxsim *s)
{
    if (sc.quit) return;
    sc.moves++;
    if (!sc.paced && movems[sc.speed]) {
        delay(movems[sc.speed]);
        status(s, sc.block ? "BLOCK" : "RUN");
    } else if ((sc.moves & 15) == 0 || sc.paced) {
        status(s, sc.block ? "BLOCK" : "RUN");
    }
    poll(s);
}

void dxscend(dxsim *s)
{
    char msg[100];
    if (s->stop == 2) sprintf(msg, "ERROR %.20s: %.40s", s->eloc, s->emsg);
    else strcpy(msg, sc.quit ? "STOPPED - any key" : "END - any key");
    status(s, msg);
    while (kbhit()) getch();
    getch();
    _setvideomode(_DEFAULTMODE);
#ifdef DX_SCSTUB
    fprintf(scl, "moves %ld pixels %ld offscreen %ld delay %ld ms\n", sc.moves, stubpix, stubbad,
            stubms);
    fclose(scl);
    scl = 0;
#endif
}

#else

/* ISO C forbids an empty file */
int dxscnone;

#endif
