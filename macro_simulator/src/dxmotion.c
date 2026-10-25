/*
 * dxmotion.c - modal state and geometry (endpoints, arcs, polar moves).
 *
 * Positions are kept in program coordinates in inches (the control stores
 * inch values internally, manual chapter 3 G70/G71).  Output goes through
 * dxtopos(): G73 rotation and scaling, G31/G32 mirror, then the G92, G97 and
 * G54-G59 translations, in the order the manual gives (chapter 4).
 *
 * Not simulated (a warning is printed once): cutter diameter compensation
 * (G41/G42), fillets (G22/G23 are cut as straight lines), spherical E words.
 */
#include <math.h>
#include <string.h>
#include "dxsim.h"

#define PI 3.14159265358979323846
#define EPS 1e-9

static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/* no "-0.0000" in the trace */
static double nz(double v)
{
    return fabs(v) < 5e-5 ? 0.0 : v;
}

double dxunit(const dxsim *s)
{
    return s->metric ? 1.0 / 25.4 : 1.0;
}

void dxmreset(dxsim *s)
{
    s->mot = DXM_RAPID;
    s->abs = 1;
    s->plane = 17;
    s->metric = s->metric0;
    s->g75 = 1;
    s->g9 = 0;
    s->g99 = 0;
    s->ccomp = 40;
    s->mirx = s->miry = 0;
    s->mirx0 = s->miry0 = 0.0;
    s->xform = 0;
    s->rot = 0.0;
    s->scl[0] = s->scl[1] = s->scl[2] = 1.0;
    memset(s->g92, 0, sizeof s->g92);
    memset(s->g97, 0, sizeof s->g97);
    s->work = 54;
    memset(s->cur, 0, sizeof s->cur);
    memcpy(s->tgt, s->cur, sizeof s->cur);
    s->feed = 0.0;
    s->dwell = 0.0;
    s->tool = s->ntool = 0;
    s->pole[0] = s->pole[1] = 0.0;
    s->prad = 0.0;
    s->poleset = 0;
    s->acur = 0.0;
    s->spindle = 0.0;
    s->sdir = 5;
}

/* rotation, scaling and mirror only */
static void xfrm(const dxsim *s, const double *p, double *o)
{
    double x = p[0], y = p[1], z = p[2], c, sn, t;
    if (s->xform) {
        c = cos(s->rot * PI / 180.0);
        sn = sin(s->rot * PI / 180.0);
        t = x * c - y * sn;
        y = x * sn + y * c;
        x = t * s->scl[0];
        y *= s->scl[1];
        z *= s->scl[2];
    }
    if (s->mirx) x = 2.0 * s->mirx0 - x;
    if (s->miry) y = 2.0 * s->miry0 - y;
    o[0] = x;
    o[1] = y;
    o[2] = z;
}

static void offs(const dxsim *s, double *o)
{
    int k, w = s->work - 54;
    for (k = 0; k < 3; k++) {
        o[k] += s->g92[k] + s->g97[k];
        if (w > 0 && w < 7) o[k] += s->wsh[w][k];
    }
}

/* program coordinates -> base part program coordinates */
void dxtopos(dxsim *s, const double *pp, double *base)
{
    xfrm(s, pp, base);
    offs(s, base);
}

void dxmblock(dxsim *s)
{
    memcpy(s->tgt, s->cur, sizeof s->cur);
    s->axset = 0;
    s->ijkset = 0;
    s->rset = 0;
    s->aset = 0;
    s->gmot1 = 0;
    s->g99 = 0;
    s->g73blk = s->g92blk = s->g97blk = 0;
    s->premask = 0;
}

static int axis(int c)
{
    switch (c) {
    case 'X': case 'U': case 'I': return 0;
    case 'Y': case 'V': case 'J': return 1;
    default: return 2;
    }
}

void dxmword(dxsim *s, int li, double v, int flags)
{
    int c = letters[li], k = axis(c);
    double u = dxunit(s);
    (void)flags;
    if (s->g73blk) {
        if (c == 'A') s->rot = s->abs ? v : s->rot + v;
        else if (c == 'X' || c == 'Y' || c == 'Z') {
            if (v < 0.001 || v > 99.999) dxwarn(s, 0, "G73 scale factor outside .001-99.999");
            s->scl[k] = v;
        }
        s->xform = 1;
        return;
    }
    if (s->g92blk || s->g97blk) {
        if (c == 'A' || c == 'B') {
            s->acur = v;
            return;
        }
        if (c == 'X' || c == 'Y' || c == 'Z') {
            s->pre[k] = v;
            s->premask |= 1 << k;
        }
        return;
    }
    switch (c) {
    case 'X': case 'Y': case 'Z':
        s->tgt[k] = s->abs ? v * u : s->tgt[k] + v * u;
        s->axset |= 1 << k;
        break;
    case 'U': case 'V': case 'W':
        s->tgt[k] += v * u;
        s->axset |= 1 << k;
        break;
    case 'I': case 'J': case 'K':
        s->ijk[k] = v * u;
        s->ijkset |= 1 << k;
        break;
    case 'R':
        s->rword = v * u;
        s->rset = 1;
        break;
    case 'A':
        s->araw = v;
        s->aword = s->abs ? v : s->acur + v;
        s->aset = 1;
        break;
    case 'B':
        s->araw = v;
        s->aword = s->acur + v;
        s->aset = 2;
        break;
    default:
        break;
    }
}

/* keep the base position when an offset changes: adjust the program position */
static void keepbase(dxsim *s, const double *before)
{
    double now[3];
    int k;
    dxtopos(s, s->cur, now);
    for (k = 0; k < 3; k++) s->cur[k] += before[k] - now[k];
    memcpy(s->tgt, s->cur, sizeof s->cur);
}

void dxgcode(dxsim *s, int g)
{
    double b[3], m[3];
    switch (g) {
    case 0: case 1: case 2: case 3:
        s->mot = g;
        break;
    case 8:  s->g9 = 0; break;
    case 9:  s->g9 = 1; break;
    case 99: s->g99 = 1; break;
    case 12: case 13: case 22: case 23:
        s->gmot1 = g;
        break;
    case 17: case 18: case 19:
        s->plane = g;
        break;
    case 30:
        dxtopos(s, s->cur, b);
        s->mirx = s->miry = 0;
        keepbase(s, b);
        break;
    case 31: case 32:
        xfrm(s, s->cur, m);
        if (g == 31) {
            s->mirx = 1;
            s->mirx0 = m[0];
        } else {
            s->miry = 1;
            s->miry0 = m[1];
        }
        break;
    case 40:
        s->ccomp = 40;
        break;
    case 41: case 42:
        s->ccomp = g;
        dxwarn(s, 3, "cutter compensation (G41/G42) is not simulated: the plot shows the programmed path");
        break;
    case 44: case 45: case 46: case 47: case 48: case 49: case 94: case 95:
        break;
    case 54: case 55: case 56: case 57: case 58: case 59:
        dxtopos(s, s->cur, b);
        s->work = g;
        keepbase(s, b);
        break;
    case 70: case 71:
        s->metric = g == 71;
        break;
    case 72:
        dxtopos(s, s->cur, b);
        s->xform = 0;
        s->rot = 0.0;
        s->scl[0] = s->scl[1] = s->scl[2] = 1.0;
        keepbase(s, b);
        break;
    case 73:
        s->g73blk = 1;
        s->xform = 1;
        break;
    case 74: s->g75 = 0; break;
    case 75: s->g75 = 1; break;
    case 90: s->abs = 1; break;
    case 91: s->abs = 0; break;
    case 92: s->g92blk = 1; break;
    case 96:
        dxtopos(s, s->cur, b);
        memset(s->g97, 0, sizeof s->g97);
        keepbase(s, b);
        break;
    case 97: s->g97blk = 1; break;
    default:
        dxerror(s, "Parser: Illegal G-Code");
    }
}

/* ------------------------------------------------------------------ */
/* emitting                                                            */

static void line2(dxsim *s, int kind, const double *a, const double *b, int quiet)
{
    dxseg g;
    memset(&g, 0, sizeof g);
    g.quiet = quiet;
    g.kind = kind;
    g.plane = s->plane;
    dxtopos(s, a, g.p0);
    dxtopos(s, b, g.p1);
    g.feed = kind == DXM_RAPID ? 0.0 : s->feed;
    if (fabs(g.p0[0] - g.p1[0]) + fabs(g.p0[1] - g.p1[1]) + fabs(g.p0[2] - g.p1[2]) < EPS)
        return;
    dxemit(s, &g);
}

static void line(dxsim *s, int kind, const double *a, const double *b)
{
    line2(s, kind, a, b, 0);
}

/* plane axes: first, second, normal (CCW turns from first towards second) */
static void paxes(int plane, int *a0, int *a1, int *an)
{
    if (plane == 18) { *a0 = 2; *a1 = 0; *an = 1; }
    else if (plane == 19) { *a0 = 1; *a1 = 2; *an = 0; }
    else { *a0 = 0; *a1 = 1; *an = 2; }
}

static void arcseg(dxsim *s, int plane, const double *a, const double *b,
                   const double *c, double sweep)
{
    dxseg g;
    int mir = s->mirx ^ s->miry;
    memset(&g, 0, sizeof g);
    g.kind = DXM_LINE;
    g.plane = plane;
    dxtopos(s, a, g.p0);
    dxtopos(s, b, g.p1);
    dxtopos(s, c, g.c);
    if (plane != 17) mir = 0;
    g.sweep = mir ? -sweep : sweep;
    g.arc = g.sweep > 0 ? DXM_CCW : DXM_CW;
    g.feed = s->feed;
    if (s->xform && fabs(s->scl[0] - s->scl[1]) > 1e-9)
        dxwarn(s, 4, "arc with unequal X/Y scale factors (G73) is drawn as a circle");
    dxemit(s, &g);
}

/* a spiral or any curve with changing radius: short straight pieces */
static void spiral(dxsim *s, const double *ctr, double r0, double r1, double a0,
                   double sweep, double z0, double z1)
{
    int n, i;
    double p[3], q[3], t, r, ang, b[3], c[3];
    char buf[160];
    n = (int)(fabs(sweep) / (PI / 90.0)) + 1;   /* 2 degree steps */
    q[0] = ctr[0] + r1 * cos(a0 + sweep);
    q[1] = ctr[1] + r1 * sin(a0 + sweep);
    q[2] = z1;
    dxtopos(s, q, b);
    p[0] = ctr[0]; p[1] = ctr[1]; p[2] = z0;
    dxtopos(s, p, c);
    sprintf(buf, "SPIRL X%.4f Y%.4f Z%.4f  CX%.4f Y%.4f R%.4f->%.4f  %+.3f deg  F%.1f",
            nz(b[0] * s->dunit), nz(b[1] * s->dunit), nz(b[2] * s->dunit), nz(c[0] * s->dunit),
            nz(c[1] * s->dunit),
            r0 * s->dunit, r1 * s->dunit, sweep * 180.0 / PI, s->feed);
    dxtline(s, buf);
    p[0] = ctr[0] + r0 * cos(a0);
    p[1] = ctr[1] + r0 * sin(a0);
    p[2] = z0;
    for (i = 1; i <= n; i++) {
        t = (double)i / n;
        r = r0 + (r1 - r0) * t;
        ang = a0 + sweep * t;
        q[0] = ctr[0] + r * cos(ang);
        q[1] = ctr[1] + r * sin(ang);
        q[2] = z0 + (z1 - z0) * t;
        line2(s, DXM_LINE, p, q, 1);
        memcpy(p, q, sizeof p);
    }
}

static double norm2pi(double d, int ccw)
{
    if (ccw) {
        while (d <= 1e-9) d += 2 * PI;
        while (d > 2 * PI + 1e-9) d -= 2 * PI;
    } else {
        while (d >= -1e-9) d -= 2 * PI;
        while (d < -2 * PI - 1e-9) d += 2 * PI;
    }
    return d;
}

static void needfeed(dxsim *s)
{
    if (s->feed <= 0.0) dxerror(s, "Parser: No Feedrate Specified");
    if (s->sdir != 3 && s->sdir != 4 && dxuser(s))
        dxwarn(s, 6, "feed move with no M3/M4 programmed: the spindle must already be on, or the control "
                     "stops with SPINDLE NOT ON CANNOT CUT");
}

static int ijkinc(const dxsim *s);

/* G0/G1/G2/G3/G12/G13 with A or B: polar coordinates around the pole */
static void polar(dxsim *s)
{
    double r0, r1, a0, a1, sweep, ctr[3], e[3];
    int ccw;
    dxmpole(s);
    if (!s->poleset) dxerror(s, "polar move (A/B word) without a pole (R I J)");
    ctr[0] = s->pole[0];
    ctr[1] = s->pole[1];
    ctr[2] = s->cur[2];
    r0 = sqrt((s->cur[0] - ctr[0]) * (s->cur[0] - ctr[0]) +
              (s->cur[1] - ctr[1]) * (s->cur[1] - ctr[1]));
    a0 = r0 > EPS ? atan2(s->cur[1] - ctr[1], s->cur[0] - ctr[0]) : s->acur * PI / 180.0;
    r1 = s->prad;

    if (s->gmot1 == 12 || s->gmot1 == 13) {     /* helix / spiral: A = degrees of travel */
        needfeed(s);
        ccw = s->gmot1 == 13;
        sweep = fabs(s->araw) * PI / 180.0;
        if (!ccw) sweep = -sweep;
        a1 = a0 + sweep;
        s->acur = a1 * 180.0 / PI;
    } else if (s->mot == DXM_CW || s->mot == DXM_CCW) {
        needfeed(s);
        ccw = s->mot == DXM_CCW;
        a1 = s->aword * PI / 180.0;
        sweep = norm2pi(a1 - a0, ccw);
        a1 = a0 + sweep;
        s->acur = s->aword;
    } else {                                    /* G0 / G1 to a polar point */
        a1 = s->aword * PI / 180.0;
        e[0] = ctr[0] + r1 * cos(a1);
        e[1] = ctr[1] + r1 * sin(a1);
        e[2] = s->tgt[2];
        if (s->mot == DXM_LINE) needfeed(s);
        line(s, s->mot == DXM_RAPID ? DXM_RAPID : DXM_LINE, s->cur, e);
        memcpy(s->cur, e, sizeof e);
        s->acur = s->aword;
        return;
    }
    e[0] = ctr[0] + r1 * cos(a1);
    e[1] = ctr[1] + r1 * sin(a1);
    e[2] = s->tgt[2];
    if (fabs(r1 - r0) > 1e-6) {
        spiral(s, ctr, r0, r1, a0, sweep, s->cur[2], e[2]);
    } else {
        arcseg(s, 17, s->cur, e, ctr, sweep);
    }
    memcpy(s->cur, e, sizeof e);
}

/* G2/G3 with an end point */
static void arc(dxsim *s)
{
    int a0, a1, an, k, ccw = s->mot == DXM_CCW;
    double c[3], sx, sy, ex, ey, dx, dy, d, h, r0, r1, sweep;
    char buf[100];

    paxes(s->plane, &a0, &a1, &an);
    sx = s->cur[a0]; sy = s->cur[a1];
    ex = s->tgt[a0]; ey = s->tgt[a1];
    memcpy(c, s->cur, sizeof c);
    if (s->rset && !(s->ijkset & ((1 << a0) | (1 << a1)))) {
        /* 0x1f992: radius programming, always the arc of 180 degrees or less */
        dx = ex - sx;
        dy = ey - sy;
        d = sqrt(dx * dx + dy * dy);
        if (d < EPS || s->rword < 0) dxerror(s, "Parser: Bad Arc Radius Or Endpoint");
        h = s->rword * s->rword - d * d / 4.0;
        if (h < -1e-8) dxerror(s, "Parser: Bad Arc Radius Or Endpoint");
        h = h > 0 ? sqrt(h) : 0.0;
        if (ccw) {
            c[a0] = sx + dx / 2.0 - dy / d * h;
            c[a1] = sy + dy / 2.0 + dx / d * h;
        } else {
            c[a0] = sx + dx / 2.0 + dy / d * h;
            c[a1] = sy + dy / 2.0 - dx / d * h;
        }
    } else if (s->g75) {
        for (k = 0; k < 3; k++) {
            if (k == an) continue;
            if (s->ijkset & (1 << k)) c[k] = ijkinc(s) ? s->cur[k] + s->ijk[k] : s->ijk[k];
        }
    } else {
        /* G74: unsigned distances; pick the signs that give a <= 90 degree arc */
        double best = 1e30, bc0 = sx, bc1 = sy, ci, cj, err, sw;
        int i;
        for (i = 0; i < 4; i++) {
            ci = sx + ((i & 1) ? -1 : 1) * fabs(s->ijk[a0]);
            cj = sy + ((i & 2) ? -1 : 1) * fabs(s->ijk[a1]);
            r0 = sqrt((sx - ci) * (sx - ci) + (sy - cj) * (sy - cj));
            r1 = sqrt((ex - ci) * (ex - ci) + (ey - cj) * (ey - cj));
            sw = norm2pi(atan2(ey - cj, ex - ci) - atan2(sy - cj, sx - ci), ccw);
            err = fabs(r0 - r1) + (fabs(sw) > PI / 2 + 1e-6 ? 1e3 : 0.0);
            if (err < best) { best = err; bc0 = ci; bc1 = cj; }
        }
        c[a0] = bc0;
        c[a1] = bc1;
    }
    r0 = sqrt((sx - c[a0]) * (sx - c[a0]) + (sy - c[a1]) * (sy - c[a1]));
    r1 = sqrt((ex - c[a0]) * (ex - c[a0]) + (ey - c[a1]) * (ey - c[a1]));
    if (r0 < EPS) dxerror(s, "Parser: Bad Arc Radius Or Endpoint");
    if (fabs(r0 - r1) > 0.0005 + 1e-4 * r0) {
        sprintf(buf, "arc end point is not on the circle (radius %.4f at start, %.4f at end)", r0, r1);
        dxwarn(s, 0, buf);
    }
    sweep = norm2pi(atan2(ey - c[a1], ex - c[a0]) - atan2(sy - c[a1], sx - c[a0]), ccw);
    c[an] = s->cur[an];
    arcseg(s, s->plane, s->cur, s->tgt, c, sweep);
}

static void linear(dxsim *s)
{
    double mid[3];
    if (s->gmot1 == 22 || s->gmot1 == 23)
        dxwarn(s, 5, "fillets (G22/G23) are not simulated: cut as a straight line to the corner");
    if (s->mot == DXM_RAPID && s->gmot1 == 0) {
        /* G0: Z up first, then XY; or XY first, then Z down (manual chapter 3) */
        int xy = fabs(s->tgt[0] - s->cur[0]) > EPS || fabs(s->tgt[1] - s->cur[1]) > EPS;
        int z = fabs(s->tgt[2] - s->cur[2]) > EPS;
        if (xy && z) {
            memcpy(mid, s->cur, sizeof mid);
            if (s->tgt[2] > s->cur[2]) mid[2] = s->tgt[2];
            else { mid[0] = s->tgt[0]; mid[1] = s->tgt[1]; }
            line(s, DXM_RAPID, s->cur, mid);
            line(s, DXM_RAPID, mid, s->tgt);
        } else line(s, DXM_RAPID, s->cur, s->tgt);
        return;
    }
    needfeed(s);
    line(s, DXM_LINE, s->cur, s->tgt);
}

/*
 * 0x21dba: I/J/K are incremental from the start point inside the built-in cycle
 * text (0x3c40 != 0), in G91 and in G74; otherwise (G90 G75) absolute.
 */
static int ijkinc(const dxsim *s)
{
    return s->sdepth > 0 || !s->abs || !s->g75;
}

/* R I J words define the polar pole and radius (also in a cycle block) */
void dxmpole(dxsim *s)
{
    if (s->ijkset & 3) {
        s->pole[0] = ijkinc(s) ? s->cur[0] + s->ijk[0] : s->ijk[0];
        s->pole[1] = ijkinc(s) ? s->cur[1] + s->ijk[1] : s->ijk[1];
        s->poleset = 1;
    }
    if (s->rset) s->prad = s->rword;
}

void dxmend(dxsim *s)
{
    double b[3];
    int k;
    double u = dxunit(s);

    if (s->g92blk) {                    /* new program coordinates of the current point */
        double now[3];
        dxtopos(s, s->cur, b);
        for (k = 0; k < 3; k++)
            if (s->premask & (1 << k)) s->cur[k] = s->pre[k] * u;
        dxtopos(s, s->cur, now);
        for (k = 0; k < 3; k++)
            if (s->premask & (1 << k)) s->g92[k] += b[k] - now[k];
        memcpy(s->tgt, s->cur, sizeof s->cur);
        return;
    }
    if (s->g97blk) {
        dxtopos(s, s->cur, b);
        for (k = 0; k < 3; k++)
            if (s->premask & (1 << k)) s->g97[k] = s->pre[k] * u;
        keepbase(s, b);
        return;
    }
    if (s->gmot1 == 12 || s->gmot1 == 13 || s->aset) {
        polar(s);
        return;
    }
    if (!s->axset) {
        dxmpole(s);                     /* pole definition: R__I__J__ */
        return;
    }
    if (s->mot == DXM_CW || s->mot == DXM_CCW) {
        if (s->gmot1 == 22 || s->gmot1 == 23) linear(s);
        else {
            needfeed(s);
            arc(s);
        }
    } else linear(s);
    memcpy(s->cur, s->tgt, sizeof s->cur);
}
