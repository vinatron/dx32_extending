/*
 * dxout.c - trace lines, statistics and plots.
 *
 * Plots are written in two passes over the program: pass 1 only measures the
 * extent, pass 2 draws.  Nothing is kept in memory, which suits 16-bit DOS.
 * Arcs are cut into chords (tolerance dxplot.tol) before projection, so a
 * device only needs straight lines: SVG, HPGL and PostScript here, and the live
 * preview (DOS VGA or a Windows window) through dxscreen.c when built with
 * -DDX_SCREEN.
 */
#include <ctype.h>
#include <math.h>
#include <string.h>
#include "dxsim.h"

#define PI 3.14159265358979323846

void dxtline(dxsim *s, const char *text)
{
    char where[16];
    if (!s->trace) return;
    /* -flow alone: only the flow lines, warnings and the end */
    if (s->flow == 1 && !s->inflow && strncmp(text, "WARN", 4) && strncmp(text, "END", 3) &&
        strncmp(text, "STOP", 4))
        return;
    dxbwhere(s, s->start.blk, where);
    fprintf(s->trace, "%-7s %s\n", where, text);
}

static const char *ufmt(const dxsim *s)
{
    return s->dunit > 1.5 ? "%.3f" : "%.4f";
}

/* no "-0.0000" */
static double nz(double v)
{
    return fabs(v) < 5e-5 ? 0.0 : v;
}

static void fmtxyz(const dxsim *s, char *buf, const double *p)
{
    char f[40];
    sprintf(f, "X%s Y%s Z%s", ufmt(s), ufmt(s), ufmt(s));
    sprintf(buf, f, nz(p[0] * s->dunit), nz(p[1] * s->dunit), nz(p[2] * s->dunit));
}

static void paxes(int plane, int *a0, int *a1, int *an)
{
    if (plane == 18) { *a0 = 2; *a1 = 0; *an = 1; }
    else if (plane == 19) { *a0 = 1; *a1 = 2; *an = 0; }
    else { *a0 = 0; *a1 = 1; *an = 2; }
}

/* ------------------------------------------------------------------ */
/* projection and devices                                              */

static void proj(int kind, const double *p, double *x, double *y)
{
    switch (kind) {
    case DXV_XZ: *x = p[0]; *y = p[2]; break;
    case DXV_YZ: *x = p[1]; *y = p[2]; break;
    case DXV_ISO:
        *x = (p[0] - p[1]) * 0.86602540378;
        *y = (p[0] + p[1]) * 0.5 + p[2];
        break;
    default: *x = p[0]; *y = p[1]; break;
    }
}

/*
 * Views.  G17 is the XY plane seen from above (X right, Y up), G18 the XZ
 * plane seen from the front (X right, Z up), G19 the YZ plane seen from the
 * right (Y right, Z up).  "all" puts the three on one sheet as a third-angle
 * drawing, with the iso view in the free corner:
 *
 *      G17 (top)      ISO
 *      G18 (front)    G19 (right)
 *
 * All four share one scale, so X lines up between G17 and G18 and Z between
 * G18 and G19.
 */
static const struct { const char *name; int kind; } vnames[] = {
    { "xy", DXV_XY }, { "g17", DXV_XY }, { "17", DXV_XY },
    { "xz", DXV_XZ }, { "zx", DXV_XZ }, { "g18", DXV_XZ }, { "18", DXV_XZ },
    { "yz", DXV_YZ }, { "g19", DXV_YZ }, { "19", DXV_YZ },
    { "iso", DXV_ISO }, { "all", DXV_ALL }, { "4", DXV_ALL }
};

int dxview(const char *name)
{
    char b[8];
    int i;
    for (i = 0; i < 7 && name[i]; i++) b[i] = (char)tolower((unsigned char)name[i]);
    b[i] = 0;
    if (name[i]) return -1;
    for (i = 0; i < (int)(sizeof vnames / sizeof vnames[0]); i++)
        if (!strcmp(b, vnames[i].name)) return vnames[i].kind;
    return -1;
}

static const char *const vlabel[DXV_N] = { "G17 XY", "G18 XZ", "G19 YZ", "ISO" };

/* a projected point of view v in drawing units */
static void vproj(const dxplot *pl, int v, const double *p, double *x, double *y)
{
    proj(pl->vproj[v], p, x, y);
    *x += pl->vsx[v];
    *y += pl->vsy[v];
}

/* end of pass 1: place the views on the sheet and set the drawing extent */
static void layout(dxplot *pl)
{
    int k;
    double w[DXV_N], h[DXV_N], m0, W, D, H, IW, IH, g, cw, ch;
    if (!pl->bset)
        for (k = 0; k < DXV_N; k++) pl->bminx[k] = pl->bmaxx[k] = pl->bminy[k] = pl->bmaxy[k] = 0.0;
    for (k = 0; k < DXV_N; k++) {
        w[k] = pl->bmaxx[k] - pl->bminx[k];
        h[k] = pl->bmaxy[k] - pl->bminy[k];
    }
    if (pl->proj != DXV_ALL) {
        k = pl->proj;
        pl->nview = 1;
        pl->vproj[0] = k;
        pl->vsx[0] = pl->vsy[0] = 0.0;
        pl->minx = pl->bminx[k];
        pl->maxx = pl->bmaxx[k];
        pl->miny = pl->bminy[k];
        pl->maxy = pl->bmaxy[k];
        return;
    }
    /* a flat part has no height in G18/G19: give each view a minimum size */
    m0 = w[DXV_XY];
    if (h[DXV_XY] > m0) m0 = h[DXV_XY];
    if (h[DXV_XZ] > m0) m0 = h[DXV_XZ];
    m0 = m0 > 1e-6 ? m0 * 0.1 : 0.1;
    W = w[DXV_XY] > m0 ? w[DXV_XY] : m0;
    D = h[DXV_XY] > m0 ? h[DXV_XY] : m0;
    H = h[DXV_XZ] > m0 ? h[DXV_XZ] : m0;
    IW = w[DXV_ISO] > m0 ? w[DXV_ISO] : m0;
    IH = h[DXV_ISO] > m0 ? h[DXV_ISO] : m0;
    cw = D > IW ? D : IW;           /* right column */
    ch = D > IH ? D : IH;           /* top row */
    g = (W + cw > H + ch ? W + cw : H + ch) * 0.08;
    pl->nview = 4;
    /* view 0 (G17) is the one paced in the live preview */
    pl->vproj[0] = DXV_XY;
    pl->vsx[0] = (W - w[DXV_XY]) / 2 - pl->bminx[DXV_XY];
    pl->vsy[0] = H + g + (ch - h[DXV_XY]) / 2 - pl->bminy[DXV_XY];
    pl->vlx[0] = 0.0;
    pl->vly[0] = H + g + ch;
    pl->vproj[1] = DXV_XZ;
    pl->vsx[1] = pl->vsx[0];        /* same X as G17 */
    pl->vsy[1] = (H - h[DXV_XZ]) / 2 - pl->bminy[DXV_XZ];
    pl->vlx[1] = 0.0;
    pl->vly[1] = H;
    pl->vproj[2] = DXV_YZ;
    pl->vsx[2] = W + g + (cw - w[DXV_YZ]) / 2 - pl->bminx[DXV_YZ];
    pl->vsy[2] = pl->vsy[1];        /* same Z as G18 */
    pl->vlx[2] = W + g;
    pl->vly[2] = H;
    pl->vproj[3] = DXV_ISO;
    pl->vsx[3] = W + g + (cw - w[DXV_ISO]) / 2 - pl->bminx[DXV_ISO];
    pl->vsy[3] = H + g + (ch - h[DXV_ISO]) / 2 - pl->bminy[DXV_ISO];
    pl->vlx[3] = W + g;
    pl->vly[3] = H + g + ch;
    pl->minx = 0.0;
    pl->miny = 0.0;
    pl->maxx = W + g + cw;
    pl->maxy = H + g + ch;
}

static void devx(const dxplot *pl, double x, double y, double *dx, double *dy)
{
    *dx = (x - pl->minx) * pl->sc + pl->ox;
    *dy = (y - pl->miny) * pl->sc + pl->oy;
}

static void svgclose(dxplot *pl)
{
    if (pl->pen >= 0) fprintf(pl->fp, "\"/>\n");
    pl->pen = -1;
}

/* one straight line in device coordinates */
static void devline(dxplot *pl, int pen, double X0, double Y0, double X1, double Y1)
{
    switch (pl->dev) {
    case DXP_SVG:
        if (pl->pen != pen || fabs(X0 - pl->lx) > 1e-6 || fabs(Y0 - pl->ly) > 1e-6) {
            svgclose(pl);
            fprintf(pl->fp, "<path class=\"%s\" d=\"M%.3f,%.3f", pen == 2 ? "p" : pen ? "f" : "r",
                    X0, -Y0);
            pl->pen = pen;
        }
        fprintf(pl->fp, " L%.3f,%.3f", X1, -Y1);
        break;
    case DXP_HPGL:
        if (pl->pen != pen) {
            fprintf(pl->fp, "SP%d;%s\n", pen == 2 ? 3 : pen ? 1 : 2, pen ? "LT;" : "LT2,1;");
            pl->pen = pen;
            pl->lx = pl->ly = -1e30;
        }
        if (fabs(X0 - pl->lx) > 0.5 || fabs(Y0 - pl->ly) > 0.5)
            fprintf(pl->fp, "PU%.0f,%.0f;", X0, Y0);
        fprintf(pl->fp, "PD%.0f,%.0f;\n", X1, Y1);
        break;
    case DXP_PS:
        if (pl->pen != pen || fabs(X0 - pl->lx) > 1e-4 || fabs(Y0 - pl->ly) > 1e-4) {
            if (pl->pen >= 0) fprintf(pl->fp, "stroke\n");
            fprintf(pl->fp, "%s %.2f %.2f moveto\n", pen == 2 ? "P" : pen ? "F" : "R", X0, Y0);
            pl->pen = pen;
        }
        fprintf(pl->fp, "%.2f %.2f lineto\n", X1, Y1);
        break;
    default:
        break;
    }
    pl->lx = X1;
    pl->ly = Y1;
}

static void extent(dxplot *pl, const double *p)
{
    int k;
    double x, y;
    for (k = 0; k < DXV_N; k++) {
        proj(k, p, &x, &y);
        if (!pl->bset) {
            pl->bminx[k] = pl->bmaxx[k] = x;
            pl->bminy[k] = pl->bmaxy[k] = y;
            continue;
        }
        if (x < pl->bminx[k]) pl->bminx[k] = x;
        if (x > pl->bmaxx[k]) pl->bmaxx[k] = x;
        if (y < pl->bminy[k]) pl->bminy[k] = y;
        if (y > pl->bmaxy[k]) pl->bmaxy[k] = y;
    }
    pl->bset = 1;
}

static void plline(dxsim *s, int pen, const double *a, const double *b)
{
    dxplot *pl = s->plot;
    double x0, y0, x1, y1, X0, Y0, X1, Y1, h, len;
    int v;
    if (!pl || pl->dev == DXP_NONE) return;
    if (pen == 0 && !pl->rapids) return;
    if (pl->pass == 1) {
        extent(pl, a);
        extent(pl, b);
        pl->nseg++;
        return;
    }
    pl->nseg++;
    len = sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) +
               (b[2] - a[2]) * (b[2] - a[2]));
    (void)len;                  /* only the live preview paces by length */
    for (v = 0; v < pl->nview; v++) {
        vproj(pl, v, a, &x0, &y0);
        vproj(pl, v, b, &x1, &y1);
        devx(pl, x0, y0, &X0, &Y0);
        devx(pl, x1, y1, &X1, &Y1);
        if (fabs(X1 - X0) + fabs(Y1 - Y0) < 1e-9) {
            if (pen != 1 || fabs(a[2] - b[2]) < 1e-9) continue;
            /* a plunge seen end-on (a drilled hole in the G17 view): draw a small x */
            h = pl->mark;
#ifdef DX_SCREEN
            if (pl->dev == DXP_SCREEN) {
                dxscline(s, pl, 1, X0 - h, Y0 - h, X0 + h, Y0 + h, v ? 0.0 : len);
                dxscline(s, pl, 1, X0 - h, Y0 + h, X0 + h, Y0 - h, 0.0);
                continue;
            }
#endif
            devline(pl, 1, X0 - h, Y0 - h, X0 + h, Y0 + h);
            devline(pl, 1, X0 - h, Y0 + h, X0 + h, Y0 - h);
            continue;
        }
#ifdef DX_SCREEN
        if (pl->dev == DXP_SCREEN) {
            /* only the first view is paced; the others (and the programmed path) at once */
            dxscline(s, pl, pen, X0, Y0, X1, Y1, v || pen == 2 ? 0.0 : len);
            continue;
        }
#endif
        devline(pl, pen, X0, Y0, X1, Y1);
    }
}

/* view names and part-zero crosses, in device units */
static void decor(dxplot *pl, double w, double h)
{
    int v;
    double ox, oy, X, Y, t, fs, z[3] = { 0.0, 0.0, 0.0 };
    t = (w > h ? w : h) * 0.02 * pl->sc;
    fs = (w > h ? w : h) * 0.022 * pl->sc;
    if (pl->dev == DXP_SVG)
        fprintf(pl->fp, "<style>text{font:%.3fpx sans-serif;fill:#555}</style>\n", fs);
    if (pl->dev == DXP_PS)
        fprintf(pl->fp, "/Helvetica findfont 9 scalefont setfont\n");
    for (v = 0; v < pl->nview; v++) {
        vproj(pl, v, z, &ox, &oy);
        devx(pl, ox, oy, &X, &Y);
        switch (pl->dev) {
        case DXP_SVG:
            fprintf(pl->fp, "<path class=\"o\" d=\"M%.3f,%.3f h%.3f M%.3f,%.3f v%.3f\"/>\n",
                    X - t, -Y, 2 * t, X, -Y - t, 2 * t);
            break;
        case DXP_PS:
            fprintf(pl->fp, "0.6 setgray [] 0 setdash %.2f %.2f moveto %.2f 0 rlineto "
                    "%.2f %.2f moveto 0 %.2f rlineto stroke 0 setgray\n",
                    X - t, Y, 2 * t, X, Y - t, 2 * t);
            break;
#ifdef DX_SCREEN
        case DXP_SCREEN:
            dxscmark(pl, X, Y);
            break;
#endif
        default:
            break;
        }
        /* the label sits just above the view's top left corner */
        if (pl->nview > 1) {
            X = pl->vlx[v];
            Y = pl->vly[v];
            devx(pl, X, Y, &X, &Y);
        } else {
            devx(pl, pl->minx, pl->maxy, &X, &Y);
            Y -= fs * 1.4;
        }
        switch (pl->dev) {
        case DXP_SVG:
            fprintf(pl->fp, "<text x=\"%.3f\" y=\"%.3f\">%s</text>\n", X, -Y - fs * 0.3,
                    vlabel[pl->vproj[v]]);
            break;
        case DXP_PS:
            fprintf(pl->fp, "%.2f %.2f moveto (%s) show\n", X, Y + 3.0, vlabel[pl->vproj[v]]);
            break;
        case DXP_HPGL:
            fprintf(pl->fp, "SP1;PU%.0f,%.0f;LB%s\003;\n", X, Y + fs * 0.3, vlabel[pl->vproj[v]]);
            break;
#ifdef DX_SCREEN
        case DXP_SCREEN:
            dxsclabel(pl, vlabel[pl->vproj[v]], X, Y);
            break;
#endif
        default:
            break;
        }
    }
}

void dxplbeg(dxsim *s)
{
    dxplot *pl = s->plot;
    double w, h, m;
    if (!pl || pl->dev == DXP_NONE || pl->pass != 2) return;
    layout(pl);
    w = pl->maxx - pl->minx;
    h = pl->maxy - pl->miny;
    if (w < 1e-6) { w = 1.0; pl->minx -= 0.5; pl->maxx += 0.5; }
    if (h < 1e-6) { h = 1.0; pl->miny -= 0.5; pl->maxy += 0.5; }
    m = (w > h ? w : h) * 0.05;
    pl->minx -= m;
    pl->miny -= m;
    pl->maxx += m;              /* keep the bounds and w, h consistent (screen preview) */
    pl->maxy += m;
    w += 2 * m;
    h += 2 * m;
    pl->pen = -1;
    pl->nseg = 0;
    pl->lx = pl->ly = -1e30;
    switch (pl->dev) {
    case DXP_SVG:
        pl->sc = 100.0;     /* 100 user units per inch */
        pl->ox = pl->oy = 0.0;
        fprintf(pl->fp, "<?xml version=\"1.0\"?>\n");
        fprintf(pl->fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" "
                "viewBox=\"0 %.3f %.3f %.3f\">\n",
                w >= h ? 900.0 : 900.0 * w / h, w >= h ? 900.0 * h / w : 900.0,
                -h * pl->sc, w * pl->sc, h * pl->sc);
        fprintf(pl->fp, "<style>path{fill:none;stroke-linejoin:round;stroke-width:%.3f}"
                ".f{stroke:#1f4fbf}.p{stroke:#2a9d3a;stroke-width:%.3f}"
                ".r{stroke:#d04020;stroke-dasharray:%.3f,%.3f}"
                ".o{stroke:#999;stroke-width:%.3f}</style>\n",
                (w > h ? w : h) * pl->sc / 700.0, (w > h ? w : h) * pl->sc / 1400.0,
                (w > h ? w : h) * pl->sc / 120.0,
                (w > h ? w : h) * pl->sc / 120.0, (w > h ? w : h) * pl->sc / 900.0);
        break;
    case DXP_HPGL:
        pl->sc = 1016.0;    /* plotter units per inch */
        pl->ox = pl->oy = 0.0;
        fprintf(pl->fp, "IN;IP;SC;PA;\n");
        break;
    case DXP_PS:
        pl->sc = 540.0 / w < 720.0 / h ? 540.0 / w : 720.0 / h;
        if (pl->sc > 72.0) pl->sc = 72.0;    /* never larger than full size */
        pl->ox = 36.0;
        pl->oy = 36.0;
        fprintf(pl->fp, "%%!PS-Adobe-3.0 EPSF-3.0\n%%%%BoundingBox: 0 0 %.0f %.0f\n",
                w * pl->sc + 72.0, h * pl->sc + 72.0);
        fprintf(pl->fp, "%%%%Creator: dxsim %s\n%%%%EndComments\n", DX_VERSION);
        fprintf(pl->fp, "/F { stroke 0.12 0.31 0.75 setrgbcolor [] 0 setdash } def\n"
                "/R { stroke 0.82 0.25 0.12 setrgbcolor [3 3] 0 setdash } def\n"
                "/P { stroke 0.16 0.62 0.23 setrgbcolor [] 0 setdash } def\n"
                "0.5 setlinewidth 1 setlinejoin newpath\n");
        break;
#ifdef DX_SCREEN
    case DXP_SCREEN:
        dxscbeg(s, pl);         /* sets the scale: pixels per inch */
        break;
#endif
    default:
        break;
    }
    pl->mark = (w > h ? w : h) * pl->sc * 0.008;
    decor(pl, w, h);
    pl->pen = -1;
}

void dxplend(dxsim *s)
{
    dxplot *pl = s->plot;
    if (!pl || pl->dev == DXP_NONE || pl->pass != 2) return;
    switch (pl->dev) {
    case DXP_SVG:
        svgclose(pl);
        fprintf(pl->fp, "</svg>\n");
        break;
    case DXP_HPGL:
        fprintf(pl->fp, "PU;SP0;\n");
        break;
    case DXP_PS:
        fprintf(pl->fp, "stroke\nshowpage\n%%%%EOF\n");
        break;
#ifdef DX_SCREEN
    case DXP_SCREEN:
        dxscend(s);
        break;
#endif
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* segments                                                            */

static void bbox(dxsim *s, const double *p)
{
    int k;
    if (!s->bset) {
        memcpy(s->bmin, p, sizeof s->bmin);
        memcpy(s->bmax, p, sizeof s->bmax);
        s->bset = 1;
        return;
    }
    for (k = 0; k < 3; k++) {
        if (p[k] < s->bmin[k]) s->bmin[k] = p[k];
        if (p[k] > s->bmax[k]) s->bmax[k] = p[k];
    }
}

void dxemit(dxsim *s, const dxseg *g)
{
    char a[80], b[80], buf[220], f[24];
    int pen = g->kind == DXM_RAPID ? 0 : 1;
    double len;
    double tol = s->plot && s->plot->tol > 0 ? s->plot->tol : 0.001;

    s->nmoves++;
    dxtravel(s, g->p1, g->p0[2], 0);        /* the block's end point, as the parser checks it */
#ifdef DX_SCREEN
    if (s->plot && s->plot->dev == DXP_SCREEN && s->plot->pass == 2) {
        double xy = sqrt((g->p1[0] - g->p0[0]) * (g->p1[0] - g->p0[0]) +
                         (g->p1[1] - g->p0[1]) * (g->p1[1] - g->p0[1]));
        double dz = fabs(g->p1[2] - g->p0[2]), t = xy / s->rapid;
        if (dz / s->zrapid > t) t = dz / s->zrapid;
        /* a rapid takes as long as its slower axis needs (params 7 and 8) */
        dxscrate(pen ? g->feed * dxunit(s) : t > 0 ? sqrt(xy * xy + dz * dz) / t : s->rapid);
    }
#endif
    fmtxyz(s, a, g->p1);
    if (!g->arc) {
        len = sqrt((g->p1[0] - g->p0[0]) * (g->p1[0] - g->p0[0]) +
                   (g->p1[1] - g->p0[1]) * (g->p1[1] - g->p0[1]) +
                   (g->p1[2] - g->p0[2]) * (g->p1[2] - g->p0[2]));
        if (pen) {
            sprintf(f, "  F%.1f", g->feed);
            sprintf(buf, "FEED  %s%s%s", a, f, g->cdc == 2 ? "  CDC blend" : g->cdc ? "  CDC" : "");
            s->lfeed += len;
        } else {
            sprintf(buf, "RAPID %s%s", a, g->cdc ? "  CDC" : "");
            s->lrapid += len;
        }
        if (!g->quiet) dxtline(s, buf);
        bbox(s, g->p0);
        bbox(s, g->p1);
        plline(s, pen, g->p0, g->p1);
#ifdef DX_SCREEN
        if (s->plot && s->plot->dev == DXP_SCREEN && s->plot->pass == 2) dxscmove(s);
#endif
        return;
    }
    {
        int a0, a1, an, i, n;
        double r, st, dth, p[3], q[3], th;
        paxes(g->plane, &a0, &a1, &an);
        r = sqrt((g->p0[a0] - g->c[a0]) * (g->p0[a0] - g->c[a0]) +
                 (g->p0[a1] - g->c[a1]) * (g->p0[a1] - g->c[a1]));
        st = atan2(g->p0[a1] - g->c[a1], g->p0[a0] - g->c[a0]);
        len = sqrt(r * g->sweep * r * g->sweep +
                   (g->p1[an] - g->p0[an]) * (g->p1[an] - g->p0[an]));
        s->lfeed += len;
        sprintf(f, "%s", ufmt(s));
        sprintf(b, "C%c%s %c%s R%s", "XYZ"[a0], f, "XYZ"[a1], f, f);
        sprintf(buf, b, nz(g->c[a0] * s->dunit), nz(g->c[a1] * s->dunit), r * s->dunit);
        strcpy(b, buf);
        sprintf(buf, "ARC%c  %s  %s  %+.3f deg  F%.1f%s", g->arc == DXM_CW ? '2' : '3',
                a, b, g->sweep * 180.0 / PI, g->feed,
                g->cdc == 2 ? "  CDC blend" : g->cdc ? "  CDC" : "");
        dxtline(s, buf);
        dth = r > tol ? 2.0 * acos(1.0 - tol / r) : PI / 4;
        if (dth > PI / 8) dth = PI / 8;
        n = (int)(fabs(g->sweep) / dth) + 1;
        if (n > 2000) n = 2000;
        memcpy(p, g->p0, sizeof p);
        bbox(s, p);
        for (i = 1; i <= n; i++) {
            th = st + g->sweep * i / n;
            q[a0] = g->c[a0] + r * cos(th);
            q[a1] = g->c[a1] + r * sin(th);
            q[an] = g->p0[an] + (g->p1[an] - g->p0[an]) * i / n;
            if (i == n) memcpy(q, g->p1, sizeof q);
            else dxtravel(s, q, p[2], 1);   /* between the end points: the interpolator stops it */
            bbox(s, q);
            plline(s, 1, p, q);
            memcpy(p, q, sizeof p);
        }
#ifdef DX_SCREEN
        if (s->plot && s->plot->dev == DXP_SCREEN && s->plot->pass == 2) dxscmove(s);
#endif
    }
}

/*
 * The programmed path while cutter compensation is on: a thin green line under
 * the tool-centre path (pen 2).  Not traced, not in the extent summary.
 */
void dxprog(dxsim *s, const dxseg *g)
{
    int a0, a1, an, i, n;
    double r, st, dth, p[3], q[3], th, tol;
    if (!s->plot || s->plot->dev == DXP_NONE) return;
    if (!g->arc) {
        plline(s, 2, g->p0, g->p1);
        return;
    }
    tol = s->plot->tol > 0 ? s->plot->tol : 0.001;
    paxes(g->plane, &a0, &a1, &an);
    r = sqrt((g->p0[a0] - g->c[a0]) * (g->p0[a0] - g->c[a0]) +
             (g->p0[a1] - g->c[a1]) * (g->p0[a1] - g->c[a1]));
    st = atan2(g->p0[a1] - g->c[a1], g->p0[a0] - g->c[a0]);
    dth = r > tol ? 2.0 * acos(1.0 - tol / r) : PI / 4;
    if (dth > PI / 8) dth = PI / 8;
    n = (int)(fabs(g->sweep) / dth) + 1;
    if (n > 2000) n = 2000;
    memcpy(p, g->p0, sizeof p);
    for (i = 1; i <= n; i++) {
        th = st + g->sweep * i / n;
        q[a0] = g->c[a0] + r * cos(th);
        q[a1] = g->c[a1] + r * sin(th);
        q[an] = g->p0[an] + (g->p1[an] - g->p0[an]) * i / n;
        if (i == n) memcpy(q, g->p1, sizeof q);
        plline(s, 2, p, q);
        memcpy(p, q, sizeof p);
    }
}

void dxsumm(dxsim *s)
{
    char buf[200], a[80], b[80];
    double p[3];
    FILE *fp = s->trace;
    if (!fp) return;
    dxspan(s, fp);
    fprintf(fp, "------- summary\n");
    fprintf(fp, "moves   %ld (rapid %.4f, feed %.4f %s)\n", s->nmoves,
            s->lrapid * s->dunit, s->lfeed * s->dunit, s->dunit > 1.5 ? "mm" : "in");
    if (s->bset) {
        fmtxyz(s, a, s->bmin);
        fmtxyz(s, b, s->bmax);
        fprintf(fp, "extent  %s .. %s\n", a, b);
    }
    dxtopos(s, s->cur, p);
    fmtxyz(s, a, p);
    sprintf(buf, "final   %s  (%s, %s, T%d)", a, s->abs ? "G90" : "G91",
            s->mot == 0 ? "G0" : s->mot == 1 ? "G1" : s->mot == 2 ? "G2" : "G3", s->tool);
    fprintf(fp, "%s\n", buf);
    if (s->nwarn) fprintf(fp, "warnings %ld\n", s->nwarn);
}
