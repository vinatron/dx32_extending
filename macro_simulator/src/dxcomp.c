/*
 * dxcomp.c - cutter diameter compensation (G41/G42), after the 6.89 firmware.
 *
 * This file holds the geometry, written to follow the firmware's engine
 * (features/CUTCOMP.md section 2.3, 0x2412e-0x24e86) formula by formula:
 *
 *   dxcoffl   line offset: the line as A*x+B*y+C=0 moved by r, new end points
 *             are the feet of the perpendiculars (0x24442)
 *   dxcoffa   arc offset: R+r or R-r by side and direction, end points moved
 *             radially; R' < 0 is "Cutter-Comp: Negative Radius" (0x245e2)
 *   dxccorn   corner type from the cross product about the programmed corner
 *             (0x247ea)
 *   dxcxll    line-line intersection (0x2489a)
 *   dxcxla    line-circle intersection (0x24928 / 0x24a5a)
 *   dxcxaa    circle-circle intersection (0x24b84)
 *   dxcpick   choice between two intersection points (0x24d36)
 *   dxcg45    constant surface feed factor (0x24e86)
 *
 * All lengths are inches.  The firmware's tolerance is 1270 units of 10 nm
 * (0x4adc), 0.0005 in; the line-line determinant is compared with the same
 * number in units squared.  dxcself() checks the functions against results
 * worked out by hand (dxsim -selftest, run by tests/dxtest).
 */
#include <math.h>
#include <string.h>
#include "dxsim.h"

#define UPI     2540000.0                   /* firmware units per inch */
#define TOL     (1270.0 / UPI)              /* 0x4adc as a length */
#define TOLDET  (1270.0 / (UPI * UPI))      /* 0x4adc as a determinant (units^2) */

/* ------------------------------------------------------------------ */
/* offsets                                                             */

/*
 * Line p0 -> p1 offset by r to the left (left != 0) or right.  q0/q1 get the
 * feet of the perpendiculars from p0/p1, cf the line coefficients
 * A, B, C, A^2+B^2 of the offset line.  (A, B) is the left normal.
 */
void dxcoffl(const double *p0, const double *p1, double r, int left, double *q0, double *q1,
             double *cf)
{
    double a = p0[1] - p1[1], b = p1[0] - p0[0];
    double c = p0[0] * p1[1] - p0[1] * p1[0];
    double l2 = a * a + b * b, d;
    c += (left ? -1.0 : 1.0) * sqrt(l2) * r;
    cf[0] = a;
    cf[1] = b;
    cf[2] = c;
    cf[3] = l2;
    if (l2 <= 0.0) {
        q0[0] = p0[0]; q0[1] = p0[1];
        q1[0] = p1[0]; q1[1] = p1[1];
        return;
    }
    d = (a * p0[0] + b * p0[1] + c) / l2;
    q0[0] = p0[0] - a * d;
    q0[1] = p0[1] - b * d;
    d = (a * p1[0] + b * p1[1] + c) / l2;
    q1[0] = p1[0] - a * d;
    q1[1] = p1[1] - b * d;
}

/*
 * Arc p0 -> p1 about ctr, radius rad, cw != 0 for G2, offset by r.  The tool
 * is outside (R + r) for G41 CW and G42 CCW, inside (R - r) otherwise.
 * Returns the new radius; < 0 means "Cutter-Comp: Negative Radius" and the
 * points are not moved.
 */
double dxcoffa(const double *p0, const double *p1, const double *ctr, double rad, int cw,
               double r, int left, double *q0, double *q1)
{
    double nr = ((left != 0) == (cw != 0)) ? rad + r : rad - r, f;
    if (nr < 0.0 || rad <= 0.0) return nr;
    f = nr / rad;
    q0[0] = ctr[0] + (p0[0] - ctr[0]) * f;
    q0[1] = ctr[1] + (p0[1] - ctr[1]) * f;
    q1[0] = ctr[0] + (p1[0] - ctr[0]) * f;
    q1[1] = ctr[1] + (p1[1] - ctr[1]) * f;
    return nr;
}

/* ------------------------------------------------------------------ */
/* corners                                                             */

/*
 * The corner between B and C at the programmed point pc: be = B's offset end,
 * cs = C's offset start.  0 tangent (the two within tolerance), 1 outside (a
 * blend arc), 2 inside (an intersection).
 */
int dxccorn(const double *be, const double *cs, const double *pc, int left)
{
    double x1 = be[0] - pc[0], y1 = be[1] - pc[1], x2 = cs[0] - pc[0], y2 = cs[1] - pc[1];
    double cr = x1 * y2 - y1 * x2;
    if (sqrt((cs[0] - be[0]) * (cs[0] - be[0]) + (cs[1] - be[1]) * (cs[1] - be[1])) <= TOL)
        return 0;
    if (!left) cr = -cr;
    return cr < 0.0 ? 1 : 2;
}

/* two offset lines (coefficients from dxcoffl): 1 and the point, 0 if parallel */
int dxcxll(const double *cb, const double *cc, double *x)
{
    double det = cb[0] * cc[1] - cc[0] * cb[1];
    if (fabs(det) <= TOLDET) return 0;
    x[0] = (cb[1] * cc[2] - cc[1] * cb[2]) / det;
    x[1] = (cc[0] * cb[2] - cb[0] * cc[2]) / det;
    return 1;
}

/*
 * A line (coefficients cf) and the circle ctr, rad.  Returns the number of
 * points: 1 tangent (x1), 2 (x1, x2), 0 none.  x1 = foot - h*dir, x2 = foot +
 * h*dir, dir = (B, -A)/|AB|, the firmware's order.
 */
int dxcxla(const double *cf, const double *ctr, double rad, double *x1, double *x2)
{
    double l = sqrt(cf[3]), s, d, h, fx, fy, ux, uy;
    if (l <= 0.0) return 0;
    s = (cf[0] * ctr[0] + cf[1] * ctr[1] + cf[2]) / cf[3];
    fx = ctr[0] - cf[0] * s;            /* foot of the perpendicular from the centre */
    fy = ctr[1] - cf[1] * s;
    d = fabs(cf[0] * ctr[0] + cf[1] * ctr[1] + cf[2]) / l;
    if (fabs(d - rad) <= TOL) {
        x1[0] = x2[0] = fx;
        x1[1] = x2[1] = fy;
        return 1;
    }
    if (d > rad) return 0;
    h = sqrt(rad * rad - d * d);
    ux = cf[1] / l;
    uy = -cf[0] / l;
    x1[0] = fx - h * ux;
    x1[1] = fy - h * uy;
    x2[0] = fx + h * ux;
    x2[1] = fy + h * uy;
    return 2;
}

/* circles b (B, the earlier element) and c: 2 points, or 0 none */
int dxcxaa(const double *cb, double rb, const double *cc, double rc, double *x1, double *x2)
{
    double dx = cc[0] - cb[0], dy = cc[1] - cb[1], d = sqrt(dx * dx + dy * dy), a, h, mx, my;
    double px, py;
    if (d <= 0.0 || d > rb + rc + TOL || d <= fabs(rb - rc) - TOL) return 0;
    a = ((rb + rc) * (rb - rc) + d * d) / (2.0 * d);
    mx = cb[0] + dx / d * a;
    my = cb[1] + dy / d * a;
    h = sqrt(fabs(rb * rb - a * a));
    px = dy / d;                        /* the firmware's perpendicular (0x4b70, 0x4b7c) */
    py = -dx / d;
    x1[0] = mx - h * px;
    x1[1] = my - h * py;
    x2[0] = mx + h * px;
    x2[1] = my + h * py;
    return 2;
}

/*
 * Of two intersection points take the one nearer ref.  When they are equally
 * near (within 2 tol), take the one on the arc's side: the cross product of
 * (other - ctr) and (candidate - ctr), sign by cw.  Returns 1 or 2, 0 for
 * "Cutter-Comp: Cannot Pick Intersection".
 */
int dxcpick(const double *x1, const double *x2, const double *ref, const double *other,
            const double *ctr, int cw)
{
    double d1, d2, cr;
    if (sqrt((x2[0] - x1[0]) * (x2[0] - x1[0]) + (x2[1] - x1[1]) * (x2[1] - x1[1])) <= TOL)
        return 1;
    d1 = sqrt((x1[0] - ref[0]) * (x1[0] - ref[0]) + (x1[1] - ref[1]) * (x1[1] - ref[1]));
    d2 = sqrt((x2[0] - ref[0]) * (x2[0] - ref[0]) + (x2[1] - ref[1]) * (x2[1] - ref[1]));
    cr = d2 - d1;
    if (fabs(cr) <= 2.0 * TOL) {
        cr = (other[0] - ctr[0]) * (x1[1] - ctr[1]) - (other[1] - ctr[1]) * (x1[0] - ctr[0]);
        if (!cw) cr = -cr;
    }
    if (cr > 0.0) return 1;
    if (cr < 0.0) return 2;
    return 0;
}

/*
 * G45 feed factor on an offset arc of programmed radius rad: (R + s*r)/R,
 * s = +1 where the offset arc grows (G41 CW, G42 CCW).  A blend arc uses
 * rad = 2r, i.e. 1.5.
 */
double dxcg45(double rad, double r, int left, int cw)
{
    double s = ((left != 0) == (cw != 0)) ? r : -r;
    return rad > 0.0 ? (rad + s) / rad : 1.0;
}

/* ------------------------------------------------------------------ */
/* the look-ahead (state machine 0x1d03e, features/CUTCOMP.md 2.2-2.4)  */

#define PI2     6.28318530717958647692

void dxcreset(dxsim *s)
{
    s->cstate = 0;
    s->cn = 0;
    s->czp = 0;
    s->cused = 0;
    s->cblks = 0;
    s->g48 = s->compat ? 1 : 0;         /* BOSS 4-7 mode resets to G48 (M p.67, 12) */
    s->g45 = 1;                         /* G45 is the reset state (M p.70) */
    dxtopos(s, s->cur, s->ctp);
}

/* an error that belongs to an element read earlier */
static void errat(dxsim *s, long blk, const char *msg)
{
    if (blk >= 0) {
        s->start.blk = blk;
        s->bt = dxbtext(s, blk);
    }
    s->wstart = 0;
    dxerror(s, msg);
}

/*
 * One move of the tool centre from where it is (ctp) to p1, traced at block
 * blk.  arc: DXM_CW / DXM_CCW about c; psweep is the programmed sweep, used
 * only to tell a full circle from a zero-length arc.  cdc: 1 element, 2 blend.
 */
static void out(dxsim *s, long blk, int kind, int arc, const double *c, const double *p1,
                double feed, int cdc, double psweep)
{
    dxseg g;
    long sb = s->start.blk;
    const char *sbt = s->bt;
    memset(&g, 0, sizeof g);
    g.kind = kind;
    g.plane = 17;
    g.cdc = cdc;
    g.feed = feed;
    memcpy(g.p0, s->ctp, sizeof g.p0);
    memcpy(g.p1, p1, sizeof g.p1);
    if (arc) {
        double a0 = atan2(g.p0[1] - c[1], g.p0[0] - c[0]);
        double a1 = atan2(g.p1[1] - c[1], g.p1[0] - c[0]), sw = a1 - a0;
        int same = fabs(g.p0[0] - g.p1[0]) + fabs(g.p0[1] - g.p1[1]) < 1e-9;
        if (same) sw = fabs(psweep) > 3.14159 ? (arc == DXM_CCW ? PI2 : -PI2) : 0.0;
        else if (arc == DXM_CCW) { while (sw <= 0.0) sw += PI2; while (sw > PI2) sw -= PI2; }
        else { while (sw >= 0.0) sw -= PI2; while (sw < -PI2) sw += PI2; }
        if (sw != 0.0) {
            g.arc = arc;
            g.c[0] = c[0];
            g.c[1] = c[1];
            g.c[2] = g.p0[2];
            g.sweep = sw;
        }
    }
    if (!g.arc && fabs(g.p0[0] - g.p1[0]) + fabs(g.p0[1] - g.p1[1]) + fabs(g.p0[2] - g.p1[2]) < 1e-9)
        return;
    if (blk >= 0) {
        s->start.blk = blk;
        s->bt = dxbtext(s, blk);
    }
    dxemit(s, &g);
    s->start.blk = sb;
    s->bt = sbt;
    memcpy(s->ctp, p1, sizeof s->ctp);
}

/* offset an element: start q0, end q1, line coefficients cf, new radius */
static double offs1(dxsim *s, const dxcel *r, double *q0, double *q1, double *cf)
{
    double nr = 0.0;
    if (r->arc) {
        nr = dxcoffa(r->p0, r->p1, r->c, r->rad, r->cw, s->crad, s->cleft, q0, q1);
        if (nr < 0.0) errat(s, r->blk, "Cutter-Comp: Negative Radius");     /* 0x24632 */
    } else dxcoffl(r->p0, r->p1, s->crad, s->cleft, q0, q1, cf);
    return nr;
}

/* one of two intersection points (0x24d36) */
static void pick(dxsim *s, long blk, int n, const double *x1, const double *x2, const double *ref,
                 const double *other, const double *ctr, int cw, double *x)
{
    int k = n == 1 ? 1 : dxcpick(x1, x2, ref, other, ctr, cw);
    if (!k) errat(s, blk, "Cutter-Comp: Cannot Pick Intersection");          /* 0x24dd4 */
    x[0] = k == 1 ? x1[0] : x2[0];
    x[1] = k == 1 ? x1[1] : x2[1];
}

/* the fallback blend must start one radius from the corner (0x24cec) */
static void dummy(dxsim *s, const dxcel *c, const double *cs, const double *pc)
{
    double d = sqrt((cs[0] - pc[0]) * (cs[0] - pc[0]) + (cs[1] - pc[1]) * (cs[1] - pc[1]));
    if (fabs(d - s->crad) > TOL) errat(s, c->blk, "Cutter-Comp: Dummy Radius Won't Fit");
}

/*
 * The corner B -> C (engine 0x2424c): be gets B's end, cs C's offset start.
 * Returns 1 when a blend arc from be to cs follows.
 */
static int corner(dxsim *s, const dxcel *b, const dxcel *c, double *be, double *cs)
{
    double b0[2], cb[4], c1[2], cc[4], rb, rc, x1[2], x2[2], x[2];
    int t, n, blend = 0, g48;
    rb = offs1(s, b, b0, be, cb);
    rc = offs1(s, c, cs, c1, cc);
    t = dxccorn(be, cs, b->p1, s->cleft);
    if (t == 0) return 0;                           /* tangent */
    g48 = b->g48 && !(b->arc && c->arc);            /* no G48 case for arc -> arc */
    if (t == 1 && !g48) return 1;                   /* outside: blend arc */
    if (!b->arc && !c->arc) {                       /* 0x2489a */
        if (dxcxll(cb, cc, x)) { be[0] = x[0]; be[1] = x[1]; }
        else blend = 1;
    } else if (!b->arc) {                           /* line -> arc, 0x24928 */
        n = dxcxla(cb, c->c, rc, x1, x2);
        if (n) {
            pick(s, c->blk, n, x1, x2, be, cs, c->c, !c->cw, x);
            be[0] = x[0]; be[1] = x[1];
        } else {
            blend = 1;
            dummy(s, c, cs, b->p1);
        }
    } else {
        if (!c->arc) n = dxcxla(cc, b->c, rb, x1, x2);   /* arc -> line, 0x24a5a */
        else n = dxcxaa(b->c, rb, c->c, rc, x1, x2);     /* arc -> arc, 0x24b84 */
        if (n) {
            pick(s, c->blk, n, x1, x2, cs, be, b->c, b->cw, x);
            be[0] = x[0]; be[1] = x[1];
        } else {
            blend = 1;
            dummy(s, c, cs, b->p1);
        }
    }
    /* under G48 the firmware sets "no blend" after the intersection call, even
       when it fell back to a blend (0x24364, 0x24306) */
    return g48 ? 0 : blend;
}

/* a carried Z-only move: first, at the element's start (2.4) */
static void emitz(dxsim *s, const dxcel *r)
{
    double p[3];
    if (!r->zc) return;
    memcpy(p, s->ctp, sizeof p);
    p[2] = r->zz;
    out(s, r->zblk, r->zrap ? DXM_RAPID : DXM_LINE, 0, 0, p, r->zf, 1, 0.0);
}

static void gouge(dxsim *s, long blk)
{
    long sb = s->start.blk;
    s->start.blk = blk;
    dxwarn(s, 20, "gouge: the tool-centre path runs backwards, the tool is bigger than the feature "
           "(manual figs. 7-13, 7-14; the control does not check this)");
    s->start.blk = sb;
}

/* an element to its end e (XY); always a feed move (0x32f1 = 1, 2, 3) */
static void emitel(dxsim *s, const dxcel *r, const double *e)
{
    double p[3], f = r->feed;
    if (!r->arc) {
        double dx = e[0] - s->ctp[0], dy = e[1] - s->ctp[1];
        if ((dx * (r->p1[0] - r->p0[0]) + dy * (r->p1[1] - r->p0[1])) < 0.0 &&
            sqrt(dx * dx + dy * dy) > TOL)
            gouge(s, r->blk);
    } else if (fabs(r->sweep) < PI2 - 1e-6) {
        double a0 = atan2(s->ctp[1] - r->c[1], s->ctp[0] - r->c[0]);
        double sw = atan2(e[1] - r->c[1], e[0] - r->c[0]) - a0;
        if (r->cw) { while (sw >= 0.0) sw -= PI2; } else { while (sw <= 0.0) sw += PI2; }
        if (fabs(sw) > fabs(r->sweep) + 1e-3 && fabs(sw) > 3.14159) gouge(s, r->blk);
    }
    p[0] = e[0];
    p[1] = e[1];
    p[2] = r->p1[2];
    if (r->arc && r->g45) f *= dxcg45(r->rad, s->crad, s->cleft, r->cw);    /* 0x24e86 */
    if (f <= 0.0) errat(s, r->blk, "Parser: No Feedrate Specified");
    out(s, r->blk, DXM_LINE, r->arc ? (r->cw ? DXM_CW : DXM_CCW) : 0, r->c, p, f, 1, r->sweep);
}

/* the blend arc round B's programmed end, to C's offset start (state 11) */
static void emitbl(dxsim *s, const dxcel *b, const double *cs)
{
    double p[3], u0, v0, u1, v1, l, f = b->feed * (b->g45 ? 1.5 : 1.0);
    const double *pc = b->p1;
    p[0] = cs[0];
    p[1] = cs[1];
    p[2] = s->ctp[2];
    u0 = s->ctp[0] - pc[0]; v0 = s->ctp[1] - pc[1];
    l = sqrt(u0 * u0 + v0 * v0); if (l > 0) { u0 /= l; v0 /= l; }
    u1 = cs[0] - pc[0]; v1 = cs[1] - pc[1];
    l = sqrt(u1 * u1 + v1 * v1); if (l > 0) { u1 /= l; v1 /= l; }
    /* a blend of more than half a turn loops round the corner */
    if ((s->cleft ? -1.0 : 1.0) * (u0 * v1 - v0 * u1) < 0.0) gouge(s, b->blk);
    /* 0x1d326: both ends in the same direction from the corner: a straight line */
    if (fabs(u0 - u1) < 1e-4 && fabs(v0 - v1) < 1e-4)
        out(s, b->blk, DXM_LINE, 0, 0, p, f, 2, 0.0);
    else
        out(s, b->blk, DXM_LINE, s->cleft ? DXM_CW : DXM_CCW, pc, p, f, 2, 0.0);
}

/* B (crec[1]) with C (crec[2]) read: B to its end, then the blend if any */
static void doelem(dxsim *s)
{
    double be[2], cs[2];
    const dxcel *b = &s->crec[1], *c = &s->crec[2];
    int bl;
    if (!b->arc && !c->arc) {               /* manual p.67, factor 5 */
        double ux = b->p1[0] - b->p0[0], uy = b->p1[1] - b->p0[1];
        double vx = c->p1[0] - c->p0[0], vy = c->p1[1] - c->p0[1];
        double lu = sqrt(ux * ux + uy * uy), lv = sqrt(vx * vx + vy * vy);
        if (lu > 0 && lv > 0 && fabs(ux * vy - uy * vx) / (lu * lv) < 1e-9 && ux * vx + uy * vy > 0)
            dxwarn(s, 19, "two co-linear moves in a row while compensating (manual p.67, factor 5)");
    }
    bl = corner(s, b, c, be, cs);
    emitz(s, &s->crec[1]);
    emitel(s, &s->crec[1], be);
    if (bl) emitbl(s, &s->crec[1], cs);
}

/* the approach (A, crec[0]) to B's offset start: a feed move at A's feed */
static void doappr(dxsim *s)
{
    double q0[2], q1[2], cf[4], p[3];
    offs1(s, &s->crec[1], q0, q1, cf);
    emitz(s, &s->crec[0]);
    p[0] = q0[0];
    p[1] = q0[1];
    p[2] = s->crec[0].p1[2];
    if (s->crec[0].feed <= 0.0) errat(s, s->crec[0].blk, "Parser: No Feedrate Specified");
    out(s, s->crec[0].blk, DXM_LINE, 0, 0, p, s->crec[0].feed, 1, 0.0);
}

/* G40 (or the end of the program): the last element to its offset end (state 12) */
static void doexit(dxsim *s)
{
    double q0[2], q1[2], cf[4], p[3];
    dxcel *last = 0;
    if (s->cn < 3) dxwarn(s, 7, "cutter compensation needs at least 5 blocks (G41/G42, approach, two "
                          "elements, G40): the control's result is undefined");
    if (s->cn == 1) {                       /* only the approach: made as programmed */
        emitz(s, &s->crec[0]);
        p[0] = s->crec[0].p1[0];
        p[1] = s->crec[0].p1[1];
        p[2] = s->crec[0].p1[2];
        out(s, s->crec[0].blk, DXM_LINE, 0, 0, p, s->crec[0].feed, 1, 0.0);
    } else if (s->cn == 2) {
        doappr(s);
        last = &s->crec[1];
    } else if (s->cn == 3) last = &s->crec[2];
    if (last) {
        offs1(s, last, q0, q1, cf);
        emitz(s, last);
        emitel(s, last, q1);
    }
    if (s->czp) {                           /* state 13: a Z-only move just before G40 */
        memcpy(p, s->ctp, sizeof p);
        p[2] = s->czz;
        out(s, s->czb, s->czrap ? DXM_RAPID : DXM_LINE, 0, 0, p, s->czf, 1, 0.0);
        s->czp = 0;
    }
    s->cn = 0;
}

/* a move from dxmotion.c: straight to the output, or into the look-ahead */
void dxcmove(dxsim *s, const dxseg *g)
{
    dxcel *r;
    dxseg h;
    if (s->cstate == 12) {                  /* the G40 block's move, from the tool centre */
        doexit(s);
        s->cstate = 0;
        h = *g;
        memcpy(h.p0, s->ctp, sizeof h.p0);
        dxemit(s, &h);
        memcpy(s->ctp, g->p1, sizeof s->ctp);
        return;
    }
    if (s->cstate < 4) {
        dxemit(s, g);
        memcpy(s->ctp, g->p1, sizeof s->ctp);
        return;
    }
    if (!dxuser(s)) {                       /* built-in cycle text: not compensated */
        dxwarn(s, 8, "canned cycle with cutter compensation on: the cycle's moves are not compensated");
        h = *g;
        memcpy(h.p0, s->ctp, sizeof h.p0);
        dxemit(s, &h);
        memcpy(s->ctp, g->p1, sizeof s->ctp);
        return;
    }
    dxprog(s, g);                           /* the programmed path, drawn thin */
    s->cused = 1;
    if (fabs(g->p0[0] - g->p1[0]) + fabs(g->p0[1] - g->p1[1]) < 1e-9) {
        /* Z only: carried with the next element (0x21d14, 0x1d6e4) */
        if (s->czp)
            dxwarn(s, 9, "two Z-only moves without an X/Y move between them while compensating: "
                   "the first is lost (one slot, manual p.67 factor 7)");
        s->czp = 1;
        s->czz = g->p1[2];
        s->czrap = g->kind == DXM_RAPID;
        s->czf = s->feed;
        s->czb = s->start.blk;
        return;
    }
    r = s->cstate == 4 ? &s->crec[0] : s->cstate == 6 ? &s->crec[1] : &s->crec[2];
    if (s->cstate == 10) {
        s->crec[1] = s->crec[2];
        r = &s->crec[2];
    }
    memset(r, 0, sizeof *r);
    memcpy(r->p0, g->p0, sizeof r->p0);
    memcpy(r->p1, g->p1, sizeof r->p1);
    r->arc = g->arc != 0;
    r->cw = g->arc == DXM_CW;
    r->c[0] = g->c[0];
    r->c[1] = g->c[1];
    r->rad = sqrt((g->p0[0] - g->c[0]) * (g->p0[0] - g->c[0]) +
                  (g->p0[1] - g->c[1]) * (g->p0[1] - g->c[1]));
    r->sweep = g->sweep;
    r->feed = s->feed;                      /* the modal F, also for a G0 block */
    r->g45 = s->g45;
    r->g48 = s->g48;
    r->blk = s->start.blk;
    if (s->czp) {
        r->zc = 1;
        r->zz = s->czz;
        r->zf = s->czf;
        r->zrap = s->czrap;
        r->zblk = s->czb;
        s->czp = 0;
    }
    if (g->arc && g->plane != 17) dxwarn(s, 10, "cutter compensation works in G17 (XY) only");
    s->cblks++;
    switch (s->cstate) {
    case 4:
        if (r->arc) dxwarn(s, 11, "the approach move after G41/G42 must be a straight line (manual p.59)");
        s->cn = 1;
        s->cstate = 6;
        break;
    case 6:
        s->cn = 2;
        s->cstate = 8;
        break;
    case 8:
        s->cn = 3;
        doappr(s);
        doelem(s);
        s->cstate = 10;
        break;
    default:
        doelem(s);
        break;
    }
}

/* G40-G49 (0x20d00-0x20db8) */
void dxcgcode(dxsim *s, int g)
{
    double r;
    switch (g) {
    case 40:
        if (s->cstate) s->cstate = 12;
        break;
    case 41: case 42:
        if (s->cstate >= 4 && s->cstate <= 10) {
            dxwarn(s, 12, "G41/G42 while compensation is on is ignored: G40 first (manual p.67, 1)");
            break;
        }
        r = s->tool >= 1 && s->tool < DX_NTOOL ? s->tools[s->tool].dia / 2.0 : 0.0;
        if (r == 0.0) {
            dxwarn(s, 13, "G41/G42 with tool diameter 0: cutter compensation is not active "
                   "(manual p.59)");
            break;
        }
        if (s->plane != 17) {
            dxwarn(s, 10, "cutter compensation works in G17 (XY) only");
            break;
        }
        s->cleft = (g == 41) ^ (s->mirx ^ s->miry);     /* 0x20c86 */
        if (r < 0.0) {                                  /* 0x24156 */
            s->cleft = !s->cleft;
            r = -r;
        }
        s->crad = r;
        s->cstate = 2;
        break;
    case 44: s->g45 = 0; break;
    case 45: s->g45 = 1; break;
    case 48: s->g48 = 1; break;
    case 49: s->g48 = 0; break;
    default: break;
    }
}

/* end of a block */
void dxcblk(dxsim *s)
{
    double p[3];
    if (s->cstate == 2) {                   /* the turn-on block has moved uncompensated */
        s->cstate = 4;
        s->cn = 0;
        s->czp = 0;
        s->cblks = 0;
    } else if (s->cstate == 12) {           /* G40 without a move: back to the programmed point */
        doexit(s);
        s->cstate = 0;
        dxtopos(s, s->cur, p);
        out(s, -1, s->mot == DXM_RAPID ? DXM_RAPID : DXM_LINE, 0, 0, p, s->feed, 0, 0.0);
    }
    s->cused = 0;
}

/* end of the program with compensation still on */
void dxcfin(dxsim *s)
{
    if (s->cstate < 4) return;
    dxwarn(s, 14, "the program ends with cutter compensation on (no G40)");
    if (s->cstate != 12 || s->cn) doexit(s);
    s->cstate = 0;
}

/* ------------------------------------------------------------------ */
/* self test: results worked out by hand                               */

static int nfail;

static void chk(FILE *fp, const char *what, double got, double want)
{
    int ok = fabs(got - want) <= 1e-9;
    if (!ok) nfail++;
    fprintf(fp, "%s cutcomp %s = %.6f (want %.6f)\n", ok ? "ok  " : "FAIL", what, got, want);
}

int dxcself(FILE *fp)
{
    double p0[2], p1[2], q0[2], q1[2], cf[4], cg[4], x[2], x2[2], c[2], nr;
    int n;
    nfail = 0;
    /* line along +X, r .25: G41 moves it to Y.25, G42 to Y-.25 */
    p0[0] = 0; p0[1] = 0; p1[0] = 1.5; p1[1] = 0;
    dxcoffl(p0, p1, 0.25, 1, q0, q1, cf);
    chk(fp, "line G41 start Y", q0[1], 0.25);
    chk(fp, "line G41 end X", q1[0], 1.5);
    dxcoffl(p0, p1, 0.25, 0, q0, q1, cf);
    chk(fp, "line G42 end Y", q1[1], -0.25);
    /* oblique line (0,0)-(3,4), G41 r 1: left normal (-.8,.6) */
    p1[0] = 3; p1[1] = 4;
    dxcoffl(p0, p1, 1.0, 1, q0, q1, cf);
    chk(fp, "oblique G41 end X", q1[0], 2.2);
    chk(fp, "oblique G41 end Y", q1[1], 4.6);
    /* arc R1 about 0,0 from (1,0) to (0,1) CCW: G41 inside (.75), G42 outside (1.25) */
    p0[0] = 1; p0[1] = 0; p1[0] = 0; p1[1] = 1; c[0] = 0; c[1] = 0;
    nr = dxcoffa(p0, p1, c, 1.0, 0, 0.25, 1, q0, q1);
    chk(fp, "arc G41 CCW radius", nr, 0.75);
    chk(fp, "arc G41 CCW end Y", q1[1], 0.75);
    nr = dxcoffa(p0, p1, c, 1.0, 0, 0.25, 0, q0, q1);
    chk(fp, "arc G42 CCW radius", nr, 1.25);
    nr = dxcoffa(p0, p1, c, 1.0, 1, 0.25, 1, q0, q1);
    chk(fp, "arc G41 CW radius", nr, 1.25);
    nr = dxcoffa(p0, p1, c, 0.2, 0, 0.25, 1, q0, q1);
    chk(fp, "arc R.2 inside r.25 (negative radius)", nr, -0.05);
    /* corners of the 1.5 square, G41: +X then -Y is a right turn = outside */
    p0[0] = 1.5; p0[1] = 0.25; p1[0] = 1.75; p1[1] = 0; c[0] = 1.5; c[1] = 0;
    chk(fp, "corner right turn G41 (1 = outside)", dxccorn(p0, p1, c, 1), 1);
    chk(fp, "corner right turn G42 (2 = inside)", dxccorn(p0, p1, c, 0), 2);
    p1[0] = 1.5; p1[1] = 0.2502;
    chk(fp, "corner within tolerance (0 = tangent)", dxccorn(p0, p1, c, 1), 0);
    /* inside corner: +X then +Y, G41 r .25: offset lines Y.25 and X.75 meet at (.75,.25) */
    p0[0] = 0; p0[1] = 0; p1[0] = 1; p1[1] = 0;
    dxcoffl(p0, p1, 0.25, 1, q0, q1, cf);
    p0[0] = 1; p0[1] = 0; p1[0] = 1; p1[1] = 1;
    dxcoffl(p0, p1, 0.25, 1, q0, q1, cg);
    n = dxcxll(cf, cg, x);
    chk(fp, "line-line intersect found", n, 1);
    chk(fp, "line-line X", x[0], 0.75);
    chk(fp, "line-line Y", x[1], 0.25);
    chk(fp, "parallel lines (0 = none)", dxcxll(cf, cf, x), 0);
    /* line Y.25 and circle R1 about 0,0: x = -/+ sqrt(1 - .0625) */
    c[0] = 0; c[1] = 0;
    n = dxcxla(cf, c, 1.0, x, x2);
    chk(fp, "line-circle points", n, 2);
    chk(fp, "line-circle Y", x[1], 0.25);
    chk(fp, "line-circle |X|", fabs(x[0]), sqrt(0.9375));
    chk(fp, "line-circle tangent (R .25)", dxcxla(cf, c, 0.25, x, x2), 1);
    chk(fp, "line-circle none (R .2)", dxcxla(cf, c, 0.2, x, x2), 0);
    /* circles R1 at 0,0 and R1 at 1,0 meet at (.5, +-sqrt(.75)) */
    p0[0] = 0; p0[1] = 0; p1[0] = 1; p1[1] = 0;
    n = dxcxaa(p0, 1.0, p1, 1.0, x, x2);
    chk(fp, "circle-circle points", n, 2);
    chk(fp, "circle-circle X", x[0], 0.5);
    chk(fp, "circle-circle |Y|", fabs(x[1]), sqrt(0.75));
    chk(fp, "circle-circle apart (0)", dxcxaa(p0, 0.4, p1, 0.4, x, x2), 0);
    /* pick: the point nearer the reference */
    x[0] = 0.5; x[1] = 0.866; x2[0] = 0.5; x2[1] = -0.866; c[0] = 0.4; c[1] = 0.9;
    chk(fp, "pick nearer (1)", dxcpick(x, x2, c, p1, p0, 0), 1);
    c[1] = -0.9;
    chk(fp, "pick nearer (2)", dxcpick(x, x2, c, p1, p0, 0), 2);
    /* G45 */
    chk(fp, "G45 R1 r.25 outside", dxcg45(1.0, 0.25, 1, 1), 1.25);
    chk(fp, "G45 R1 r.25 inside", dxcg45(1.0, 0.25, 1, 0), 0.75);
    chk(fp, "G45 blend arc (R = 2r)", dxcg45(0.5, 0.25, 1, 1), 1.5);
    fprintf(fp, "cutcomp geometry: %d failed\n", nfail);
    return nfail;
}
