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
