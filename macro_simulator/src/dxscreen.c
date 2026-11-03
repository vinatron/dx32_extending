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
 * -Sa runs it unattended, for tests: feed-rate pacing is worked out but not
 * waited for, no key is read, and the run ends by itself with a summary line
 * "SCREEN moves .. points .. offscreen .. time .. ms" in the trace (-o).
 * -bmp FILE saves the finished picture as a 16-colour BMP (all builds).
 * -Sk KEYS runs it with scripted keys, for the UI tests: no waits, and every
 * key and every change of the status lines goes into the trace ("UI" lines).
 * A key is given when the preview waits for one (a block-by-block stop, a pause,
 * the end); a key written /K is typed ahead instead, and the next look at the
 * keyboard while the program runs takes it.  s = SPACE, e = ESC, w = close
 * the window, r = cover the window and check that it repaints; the end of the
 * script counts as ESC.  In DXSIMW the keys go through the window's message
 * queue (WM_CHAR, WM_CLOSE), so its own key path is tested too.
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
static int stubx, stuby, stubcol;
static unsigned char stubfb[480][640];  /* the picture, lines only, for -bmp */
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
static short _setcolor(short c) { stubcol = c & 15; return 0; }
static unsigned short _setlinestyle(unsigned short st) { (void)st; return 0; }
static void stubpt(int x, int y)
{
    if (x < 0 || x > 639 || y < 0 || y > 447) fprintf(scl, "offscreen %d %d\n", x, y);
}
static void _moveto(short x, short y) { stubx = x; stuby = y; stubpt(x, y); }
static short _lineto(short x, short y)
{
    int dx = x > stubx ? x - stubx : stubx - x, dy = y > stuby ? y - stuby : stuby - y;
    int sx = x > stubx ? 1 : -1, sy = y > stuby ? 1 : -1, e = dx - dy, px = stubx, py = stuby, e2;
    stubpt(x, y);
    for (;;) {                          /* Bresenham, solid; the clip is the drawing area */
        if (px >= 0 && px < 640 && py >= 0 && py < 448) stubfb[py][px] = (unsigned char)stubcol;
        if (px == x && py == y) break;
        e2 = 2 * e;
        if (e2 > -dy) { e -= dy; px += sx; }
        if (e2 < dx) { e += dx; py += sy; }
    }
    stubx = x;
    stuby = y;
    return 1;
}
static short _getpixel(short x, short y) { return stubfb[y][x]; }
static void _setcliprgn(short a, short b, short c, short d) { (void)a; (void)b; (void)c; (void)d; }
static void _clearscreen(short a) { (void)a; memset(stubfb, 0, sizeof stubfb); }
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
static void delay(unsigned ms) { (void)ms; }
#endif

#define SCW     640
#define SCH     448             /* drawing area; rows 29-30 are the status lines */
#define RAPID   472.0           /* ipm, manual chapter 8; dxemit passes params 7/8 */
#define TICKMS  55              /* one DOS clock tick */

static const unsigned movems[10] = { 0, 2, 5, 10, 20, 50, 100, 200, 400, 800 };
static const short vgacol[DX_NSRC] = { 11, 14, 13, 10, 9, 6, 15, 3, 5, 1, 4, 7 };

static struct {
    int    on;
    int    paced;               /* 1: feed-rate pacing */
    int    speed;               /* per-move pacing: index into movems */
    double factor;              /* feed-rate pacing: 1 = machine speed */
    int    pause, block, quit;
    int    over;                /* block by block: stop only at macro depth <= over; -1 = every block */
    double rate;                /* in/min of the move being drawn */
    long   moves;
    int    unat;                /* -Sa: no waits, no keys */
    const char *bmp;            /* -bmp FILE */
    long   pts, bad, ms;        /* line end points drawn, those off the drawing area, time waited */
    const char *keys;           /* -Sk: the rest of the key script */
    FILE   *log;                /* -Sk: the trace */
    char   last1[80], last2[80];    /* -Sk: the status lines last logged */
    int    first;               /* the first status line is drawn at the first block */
} sc;

static void status(dxsim *s, const char *msg);

void dxscinit(int paced, int unattended, const char *bmp, const char *keys)
{
    sc.on = 1;
    sc.unat = unattended;
    sc.bmp = bmp;
    sc.keys = keys;
    if (unattended) paced = 1;
    sc.paced = paced;
    sc.speed = paced ? 0 : 4;
    sc.factor = 1.0;
    sc.over = -1;
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

/* the drawing calls, counted the same way in every build (the -Sa summary) */
static void pt(int x, int y)
{
    sc.pts++;
    if (x < 0 || x > SCW - 1 || y < 0 || y > SCH - 1) sc.bad++;
}

static void mv(int x, int y)
{
    pt(x, y);
    _moveto((short)x, (short)y);
}

static void ln(int x, int y)
{
    pt(x, y);
    _lineto((short)x, (short)y);
}

static void scwait(unsigned ms)
{
    sc.ms += ms;
    if (!sc.unat && !sc.keys) delay(ms);
}

/* -Sk: a line of the UI log */
static void uilog(const char *what, const char *text)
{
    if (sc.log) fprintf(sc.log, "UI    %s%s\n", what, text);
}

/* -Sk: r in the script (not a key; done where the preview next waits for one): cover the window and
   check that it repaints */
static void repaint(void)
{
    char buf[64];
#ifdef DX_WIN
    sprintf(buf, "%ld pixels differ", dxwcheck());
#else
    strcpy(buf, "0 pixels differ");     /* VGA: nothing covers the screen */
#endif
    uilog("repaint: ", buf);
}

static void scripted(void)
{
    while (*sc.keys == 'r') {
        sc.keys++;
        repaint();
    }
}

static int skbhit(void)
{
    if (!sc.keys) return kbhit();
    return *sc.keys == '/';
}

static int sgetch(void)
{
    int c;
    char name[8];
    if (!sc.keys) return getch();
    scripted();
    if (*sc.keys == '/') sc.keys++;
    c = (unsigned char)*sc.keys;
    if (c) sc.keys++;
    switch (c) {
    case 0: strcpy(name, "(end)"); c = 27; break;
    case 's': strcpy(name, "SPACE"); c = ' '; break;
    case 'e': strcpy(name, "ESC"); c = 27; break;
    case 'w': strcpy(name, "CLOSE"); break;
    default: name[0] = (char)c; name[1] = 0; break;
    }
    uilog("key ", name);
#ifdef DX_WIN
    c = dxwpost(c);                     /* through the window: WM_CHAR, or WM_CLOSE for w */
#else
    if (c == 'w') c = 27;               /* no window: closing it is ESC */
#endif
    return c;
}

/* read waiting keys; while paused, wait for the next one */
static void poll(dxsim *s)
{
    if (sc.unat) return;
    while (skbhit()) key(s, sgetch());
    while (sc.pause && !sc.quit) {
        status(s, "PAUSED - SPACE continues");
        key(s, sgetch());
    }
}

/* the call stack, e.g. "MAIN 20 > #1 6 > #2 11": caller lines, then the current line */
static void stack(dxsim *s, char *out)
{
    char part[24];
    int k;
    long b;
    strcpy(out, "MAIN");
    for (k = 0; k < s->gdepth; k++) {
        b = s->gret[k].off ? s->gret[k].blk : s->gret[k].blk - 1;
        if (s->gmac[k]) sprintf(part, " %ld > M%d", dxbline(s, b), s->gmnum[k]);
        else sprintf(part, " %ld > #%d", dxbline(s, b), s->gslot[k]);
        if (strlen(out) + strlen(part) < 60) strcat(out, part);
    }
    if (s->sdepth > 0) {
        sprintf(part, " > G%d", s->cycg);
        if (strlen(out) + strlen(part) < 60) strcat(out, part);
    } else if (s->start.blk < s->corp0) {
        sprintf(part, " %ld", dxbline(s, s->start.blk));
        if (strlen(out) + strlen(part) < 60) strcat(out, part);
    }
}

static void status(dxsim *s, const char *msg)
{
    char l1[200], l2[200], where[64];
    const char *t = s->bt ? s->bt : "";
    double p[3];
    stack(s, where);
    dxtopos(s, s->cur, p);
    sprintf(l1, "%-.30s  %-34.34s %.40s", where, t, msg ? msg : "");
    if (sc.paced)
        sprintf(l2, "X%9.4f Y%9.4f Z%9.4f F%7.1f  feed x%-6g SPC B F +- ESC",
                p[0], p[1], p[2], s->feed, sc.factor);
    else
        sprintf(l2, "X%9.4f Y%9.4f Z%9.4f F%7.1f  move %3ums SPC B F +- ESC",
                p[0], p[1], p[2], s->feed, movems[sc.speed]);
    l1[79] = l2[79] = 0;
    if (sc.keys && (strcmp(l1, sc.last1) || strcmp(l2, sc.last2))) {
        size_t n;
        strcpy(sc.last1, l1);
        strcpy(sc.last2, l2);
        for (n = strlen(l1); n && l1[n - 1] == ' '; n--) l1[n - 1] = 0;
        for (n = strlen(l2); n && l2[n - 1] == ' '; n--) l2[n - 1] = 0;
        uilog("1 ", l1);
        uilog("2 ", l2);
    }
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
    if (sc.first) {                         /* not in dxscbeg: the state is still the measuring pass's */
        sc.first = 0;
        status(s, "RUN");
    }
    if (sc.quit || sc.unat) return;
    poll(s);
    if (sc.block && s->start.blk < s->corp0 && (sc.over < 0 || s->gdepth <= sc.over)) {
        int c;
        sc.over = -1;                       /* O applies to the block it is pressed at */
        s->bt = dxbtext(s, s->start.blk);
        status(s, "BLOCK: key=next I=into O=over");
        c = sgetch();
        if (c == 'o' || c == 'O') sc.over = s->gdepth;      /* skip the calls this block makes */
        else if (c == 'i' || c == 'I') sc.over = -1;
        else key(s, c);
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
    sc.log = sc.keys ? s->trace : 0;
    s->blkhook = blkhook;
    s->bt = "";
    sc.first = 1;
}

/* part zero of one view: a small grey cross */
void dxscmark(dxplot *pl, double x, double y)
{
    (void)pl;
    if (x < 6 || x >= SCW - 6 || y < 6 || y >= SCH - 6) return;
    _setcolor(8);
    _setlinestyle(0xffff);
    mv((int)(x - 6), (int)(SCH - 1 - y));
    ln((int)(x + 6), (int)(SCH - 1 - y));
    mv((int)x, (int)(SCH - 1 - y - 6));
    ln((int)x, (int)(SCH - 1 - y + 6));
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

/* -pc: a line of the key at text row row */
void dxsclegend(int row, int colour, const char *text)
{
    char buf[40];
    sprintf(buf, "%.38s", text);
    _settextcolor(vgacol[colour % DX_NSRC]);
    _settextposition((short)row, 1);
    _outtext(buf);
}

static void draw(int pen, double x0, double y0, double x1, double y1)
{
    /* light red rapid, green programmed path (cutter compensation); feeds 10+i: colour i of -pc,
       light cyan for the main program */
    _setcolor((short)(pen == 2 ? 2 : pen >= 10 ? vgacol[(pen - 10) % DX_NSRC] : pen ? 11 : 12));
    _setlinestyle((unsigned short)(pen ? 0xffff : 0xf0f0));
    mv((int)floor(x0 + 0.5), (int)(SCH - 1 - floor(y0 + 0.5)));
    ln((int)floor(x1 + 0.5), (int)(SCH - 1 - floor(y1 + 0.5)));
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
        scwait((unsigned)(ms / n));
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
        scwait(movems[sc.speed]);
        status(s, sc.block ? "BLOCK" : "RUN");
    } else if ((sc.moves & 15) == 0 || sc.paced) {
        status(s, sc.block ? "BLOCK" : "RUN");
    }
    poll(s);
}

/* -bmp: the whole 640x480 screen, status lines included, as a 4-bit BMP (rows bottom up) */
static int savebmp(const char *name)
{
    static const unsigned char pal[16][3] = {   /* the VGA colours, as R G B */
        { 0, 0, 0 }, { 0, 0, 170 }, { 0, 170, 0 }, { 0, 170, 170 }, { 170, 0, 0 }, { 170, 0, 170 },
        { 170, 85, 0 }, { 170, 170, 170 }, { 85, 85, 85 }, { 85, 85, 255 }, { 85, 255, 85 },
        { 85, 255, 255 }, { 255, 85, 85 }, { 255, 85, 255 }, { 255, 255, 85 }, { 255, 255, 255 }
    };
    unsigned char h[118], row[320];
    unsigned long size = 118UL + 320UL * 480UL;
    int i, x, y;
    FILE *fp = fopen(name, "wb");
    if (!fp) return 0;
    memset(h, 0, sizeof h);
    h[0] = 'B';
    h[1] = 'M';
    for (i = 0; i < 4; i++) {
        h[2 + i] = (unsigned char)(size >> (8 * i));    /* file size, little-endian */
        h[22 + i] = (unsigned char)(480UL >> (8 * i));  /* height */
        h[34 + i] = (unsigned char)((size - 118UL) >> (8 * i));
    }
    h[10] = 118;                        /* offset of the pixels */
    h[14] = 40;                         /* BITMAPINFOHEADER */
    h[18] = (unsigned char)(640 & 255);
    h[19] = (unsigned char)(640 >> 8);
    h[26] = 1;                          /* planes */
    h[28] = 4;                          /* bits per pixel */
    h[46] = 16;                         /* colours used */
    for (i = 0; i < 16; i++) {          /* the palette: B G R 0 */
        h[54 + 4 * i] = pal[i][2];
        h[55 + 4 * i] = pal[i][1];
        h[56 + 4 * i] = pal[i][0];
    }
    fwrite(h, 1, sizeof h, fp);
    _setcliprgn(0, 0, SCW - 1, 479);    /* reading outside the clip gives nothing */
    for (y = 479; y >= 0; y--) {
        for (x = 0; x < 640; x += 2)
            row[x / 2] = (unsigned char)(((_getpixel((short)x, (short)y) & 15) << 4) |
                                         (_getpixel((short)(x + 1), (short)y) & 15));
        fwrite(row, 1, sizeof row, fp);
    }
    _setcliprgn(0, 0, SCW - 1, SCH - 1);
    return fclose(fp) == 0;
}

void dxscend(dxsim *s)
{
    char msg[100];
    int bmpok = 1;
    if (s->stop == 2) sprintf(msg, "ERROR %.20s: %.40s", s->eloc, s->emsg);
    else strcpy(msg, sc.unat ? "END" : sc.quit ? "STOPPED - any key" : "END - any key");
    status(s, msg);
    if (sc.bmp) bmpok = savebmp(sc.bmp);
    if (!sc.unat) {
        while (skbhit()) sgetch();
        sgetch();
    }
    _setvideomode(_DEFAULTMODE);
    if (!bmpok) fprintf(stderr, "dxsim: cannot write %s\n", sc.bmp);
    if ((sc.unat || sc.keys) && s->trace)
        fprintf(s->trace, "SCREEN  moves %ld points %ld offscreen %ld time %ld ms\n", sc.moves, sc.pts,
                sc.bad, sc.ms);
#ifdef DX_SCSTUB
    fprintf(scl, "moves %ld pixels %ld offscreen %ld delay %ld ms\n", sc.moves, sc.pts, sc.bad, sc.ms);
    fclose(scl);
    scl = 0;
#endif
}

#else

/* ISO C forbids an empty file */
int dxscnone;

#endif
