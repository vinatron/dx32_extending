/*
 * dxgddm.c - IBM GDDM plot device (z/OS).  NOT BUILT BY DEFAULT and NOT TESTED.
 *
 * Compile with -DDX_GDDM and link with the GDDM stubs to draw the tool path on
 * a 3270 graphics terminal instead of writing a file.  dxout.c already hands
 * this device straight lines in a device space where 1 unit = 1 inch of part,
 * origin at the lower left corner of the drawing (dxplot.minx/miny).
 *
 * The calls are the GDDM base API (FSINIT, GSFLD, GSWIN, GSCOL, GSLT, GSMOVE,
 * GSLINE, FSFRCE, ASREAD, FSTERM).  The C binding (header name, float vs
 * double arguments, lower or upper case entry names) must be checked against
 * the GDDM Base Application Programming Reference for the installed release
 * before the first build; the DXG_ macros below are the only place to change.
 */
#ifdef DX_GDDM

#include "dxsim.h"

/* ---- binding: adjust to the installed GDDM C interface ---- */
#include "admucdef.h"           /* TODO: check the header name */
#define DXG_INIT()              fsinit()
#define DXG_FIELD()             gsfld(1, 1, 32, 80)
#define DXG_WINDOW(x0, x1, y0, y1) gswin((float)(x0), (float)(x1), (float)(y0), (float)(y1))
#define DXG_COLOUR(c)           gscol(c)
#define DXG_LTYPE(t)            gslt(t)
#define DXG_MOVE(x, y)          gsmove((float)(x), (float)(y))
#define DXG_LINE(x, y)          gsline((float)(x), (float)(y))
#define DXG_SHOW()              fsfrce()
#define DXG_WAIT(a, b, c)       asread(a, b, c)
#define DXG_TERM()              fsterm()

#define COL_FEED   1            /* blue */
#define COL_RAPID  2            /* red */
#define LT_SOLID   0
#define LT_DASH    2

void dxgdbeg(dxplot *p)
{
    double w = p->maxx - p->minx, h = p->maxy - p->miny;
    DXG_INIT();
    DXG_FIELD();
    /* keep the aspect ratio: make the window square around the drawing */
    if (w > h) h = w;
    else w = h;
    DXG_WINDOW(0.0, w, 0.0, h);
    p->pen = -1;
}

void dxgdlin(dxplot *p, int pen, double x0, double y0, double x1, double y1)
{
    if (pen != p->pen) {
        DXG_COLOUR(pen ? COL_FEED : COL_RAPID);
        DXG_LTYPE(pen ? LT_SOLID : LT_DASH);
        p->pen = pen;
    }
    DXG_MOVE(x0, y0);
    DXG_LINE(x1, y1);
}

void dxgdend(dxplot *p)
{
    long type, mod, count;
    (void)p;
    DXG_SHOW();
    DXG_WAIT(&type, &mod, &count);      /* wait for ENTER */
    DXG_TERM();
}

#endif
