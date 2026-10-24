/*
 * dxout.c - trace lines, statistics and plots.
 *
 * Plots are written in two passes over the program: pass 1 only measures the
 * extent, pass 2 draws.  Nothing is kept in memory, which suits 16-bit DOS.
 * Arcs are cut into chords (tolerance dxplot.tol) before projection, so a
 * device only needs straight lines: SVG, HPGL and PostScript here; IBM GDDM
 * (z/OS) through dxgddm.c when built with -DDX_GDDM.
 */
#include <math.h>
#include <string.h>
#include "dxsim.h"

#define PI 3.14159265358979323846

void dxtline(dxsim *s, const char *text)
{
    char where[16];
    if (!s->trace) return;
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

static void proj(const dxplot *pl, const double *p, double *x, double *y)
{
    switch (pl->proj) {
    case DXV_XZ: *x = p[0]; *y = p[2]; break;
    case DXV_YZ: *x = p[1]; *y = p[2]; break;
    case DXV_ISO:
        *x = (p[0] - p[1]) * 0.86602540378;
        *y = (p[0] + p[1]) * 0.5 + p[2];
        break;
    default: *x = p[0]; *y = p[1]; break;
    }
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
            fprintf(pl->fp, "<path class=\"%s\" d=\"M%.3f,%.3f", pen ? "f" : "r", X0, -Y0);
            pl->pen = pen;
        }
        fprintf(pl->fp, " L%.3f,%.3f", X1, -Y1);
        break;
    case DXP_HPGL:
        if (pl->pen != pen) {
            fprintf(pl->fp, "SP%d;%s\n", pen ? 1 : 2, pen ? "LT;" : "LT2,1;");
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
            fprintf(pl->fp, "%s %.2f %.2f moveto\n", pen ? "F" : "R", X0, Y0);
            pl->pen = pen;
        }
        fprintf(pl->fp, "%.2f %.2f lineto\n", X1, Y1);
        break;
#ifdef DX_GDDM
    case DXP_GDDM:
        dxgdlin(pl, pen, X0, Y0, X1, Y1);
        break;
#endif
    default:
        break;
    }
    pl->lx = X1;
    pl->ly = Y1;
}

static void plline(dxsim *s, int pen, const double *a, const double *b)
{
    dxplot *pl = s->plot;
    double x0, y0, x1, y1, X0, Y0, X1, Y1, h;
    if (!pl || pl->dev == DXP_NONE) return;
    if (pen == 0 && !pl->rapids) return;
    proj(pl, a, &x0, &y0);
    proj(pl, b, &x1, &y1);
    if (pl->pass == 1) {
        if (pl->nseg == 0) {
            pl->minx = pl->maxx = x0;
            pl->miny = pl->maxy = y0;
        }
        if (x0 < pl->minx) pl->minx = x0;
        if (x0 > pl->maxx) pl->maxx = x0;
        if (y0 < pl->miny) pl->miny = y0;
        if (y0 > pl->maxy) pl->maxy = y0;
        if (x1 < pl->minx) pl->minx = x1;
        if (x1 > pl->maxx) pl->maxx = x1;
        if (y1 < pl->miny) pl->miny = y1;
        if (y1 > pl->maxy) pl->maxy = y1;
        pl->nseg++;
        return;
    }
    devx(pl, x0, y0, &X0, &Y0);
    devx(pl, x1, y1, &X1, &Y1);
    pl->nseg++;
    if (pen && fabs(X1 - X0) + fabs(Y1 - Y0) < 1e-9 && fabs(a[2] - b[2]) > 1e-9) {
        /* a plunge seen end-on (a drilled hole in the XY view): draw a small x */
        h = pl->mark;
        devline(pl, 1, X0 - h, Y0 - h, X0 + h, Y0 + h);
        devline(pl, 1, X0 - h, Y0 + h, X0 + h, Y0 - h);
        return;
    }
    devline(pl, pen, X0, Y0, X1, Y1);
}

void dxplbeg(dxsim *s)
{
    dxplot *pl = s->plot;
    double w, h, m;
    if (!pl || pl->dev == DXP_NONE || pl->pass != 2) return;
    if (pl->nseg == 0) pl->minx = pl->maxx = pl->miny = pl->maxy = 0.0;
    w = pl->maxx - pl->minx;
    h = pl->maxy - pl->miny;
    if (w < 1e-6) w = 1.0;
    if (h < 1e-6) h = 1.0;
    m = (w > h ? w : h) * 0.05;
    pl->minx -= m;
    pl->miny -= m;
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
                ".f{stroke:#1f4fbf}.r{stroke:#d04020;stroke-dasharray:%.3f,%.3f}"
                ".o{stroke:#999;stroke-width:%.3f}</style>\n",
                (w > h ? w : h) * pl->sc / 700.0, (w > h ? w : h) * pl->sc / 120.0,
                (w > h ? w : h) * pl->sc / 120.0, (w > h ? w : h) * pl->sc / 900.0);
        {
            double ox, oy, z[3] = { 0.0, 0.0, 0.0 }, t = (w > h ? w : h) * 0.03;
            proj(pl, z, &ox, &oy);
            devx(pl, ox, oy, &ox, &oy);
            fprintf(pl->fp, "<path class=\"o\" d=\"M%.3f,%.3f h%.3f M%.3f,%.3f v%.3f\"/>\n",
                    ox - t * pl->sc, -oy, 2 * t * pl->sc, ox, -oy - t * pl->sc, 2 * t * pl->sc);
        }
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
                "0.5 setlinewidth 1 setlinejoin newpath\n");
        break;
#ifdef DX_GDDM
    case DXP_GDDM:
        pl->sc = 1.0;
        pl->ox = pl->oy = 0.0;
        dxgdbeg(pl);
        break;
#endif
    default:
        break;
    }
    pl->mark = (w > h ? w : h) * pl->sc * 0.008;
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
#ifdef DX_GDDM
    case DXP_GDDM:
        dxgdend(pl);
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
    fmtxyz(s, a, g->p1);
    if (!g->arc) {
        len = sqrt((g->p1[0] - g->p0[0]) * (g->p1[0] - g->p0[0]) +
                   (g->p1[1] - g->p0[1]) * (g->p1[1] - g->p0[1]) +
                   (g->p1[2] - g->p0[2]) * (g->p1[2] - g->p0[2]));
        if (pen) {
            sprintf(f, "  F%.1f", g->feed);
            sprintf(buf, "FEED  %s%s", a, f);
            s->lfeed += len;
        } else {
            sprintf(buf, "RAPID %s", a);
            s->lrapid += len;
        }
        if (!g->quiet) dxtline(s, buf);
        bbox(s, g->p0);
        bbox(s, g->p1);
        plline(s, pen, g->p0, g->p1);
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
        sprintf(buf, "ARC%c  %s  %s  %+.3f deg  F%.1f", g->arc == DXM_CW ? '2' : '3',
                a, b, g->sweep * 180.0 / PI, g->feed);
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
            bbox(s, q);
            plline(s, 1, p, q);
            memcpy(p, q, sizeof p);
        }
    }
}

void dxsumm(dxsim *s)
{
    char buf[200], a[80], b[80];
    double p[3];
    FILE *fp = s->trace;
    if (!fp) return;
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
}
