/*
 * dxtest.c - regression tests for dxsim, in C89 so they run where dxsim runs
 * (DOS, Windows 98, Linux, z/OS).
 *
 *   dxtest [-u] [path-to-dxsim]        run from the tests directory
 *
 * 1. "dxsim -check" must parse every block of the built-in cycle text, and
 *    "dxsim -selftest" must pass its cutter-compensation geometry checks.
 * 2. Each program in TESTS.LST (a name, then optional dxsim options) must
 *    reproduce EXPECTED\<name>.OUT, warnings and error reports included.  Numbers may
 *    differ by one digit in the last place (C libraries round halfway cases
 *    differently).  -u rewrites the expected files after a reviewed change.
 * 3. Results worked out by hand from the programmer's manual's figures.
 * 4. The tests together must execute at least 600 built-in cycle blocks.
 *
 * dxsim is started with system() and always writes to a file (-o), so neither
 * shell redirection nor exit codes are needed (COMMAND.COM returns none).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#if defined(__DOS__) || defined(__NT__) || defined(_WIN32) || defined(MSDOS)
#define DEFEXE  "..\\src\\DXSIM.EXE"
#define EXPDIR  "expected\\"
#else
#define DEFEXE  "../dxsim"
#define EXPDIR  "expected/"
#endif

#define TMPOUT  "dxt.out"
#define MAXMV   600
#define MAXCOV  1024
#define PI      3.14159265358979323846

static const char *dxsim = DEFEXE;
static int nfail, nok, update;

typedef struct mv {
    char   k;                   /* R rapid, F feed, 2 ARC2, 3 ARC3, S spiral */
    double x, y, z;
    double cx, cy, r, sw;       /* arcs */
} mv;

static mv mvs[MAXMV];
static int nmv;
static unsigned long cov[MAXCOV];
static int ncov;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (ok) nok++;
    else nfail++;
}

static int near(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

/* run dxsim with the given arguments; output in TMPOUT */
static void dx(const char *args)
{
    char cmd[256];
    remove(TMPOUT);
    sprintf(cmd, "%.80s %.80s -silent -o %s", dxsim, args, TMPOUT);
    if (system(cmd)) {
        /* COMMAND.COM gives no exit codes, so the output file is what counts */
    }
}

static void chop(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
}

/* last line of TMPOUT */
static void lastline(char *buf, int size)
{
    char line[256];
    FILE *fp = fopen(TMPOUT, "r");
    buf[0] = 0;
    if (!fp) return;
    while (fgets(line, sizeof line, fp)) {
        chop(line);
        if (line[0]) {
            strncpy(buf, line, size - 1);
            buf[size - 1] = 0;
        }
    }
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* trace comparison                                                    */

/* a token as prefix letters + number; returns 1 if it has a number */
static int splitnum(const char *t, char *pre, double *v)
{
    int n = 0;
    char *end;
    while (*t && !(isdigit((unsigned char)*t) || *t == '-' || *t == '+' || *t == '.')) {
        if (n < 15) pre[n++] = *t;
        t++;
    }
    pre[n] = 0;
    if (!*t) return 0;
    *v = strtod(t, &end);
    return end != t && *end == 0;
}

/* equal, or equal token by token with numbers within one unit in the last place */
static int sameline(const char *a, const char *b)
{
    char ta[256], tb[256], pa[16], pb[16];
    char *sa = ta, *sb = tb, *ca, *cb;
    double va, vb;
    if (!strcmp(a, b)) return 1;
    strcpy(ta, a);
    strcpy(tb, b);
    for (;;) {
        while (*sa == ' ') sa++;
        while (*sb == ' ') sb++;
        if (!*sa || !*sb) return !*sa && !*sb;
        ca = sa;
        cb = sb;
        while (*sa && *sa != ' ') sa++;
        while (*sb && *sb != ' ') sb++;
        if (*sa) *sa++ = 0;
        if (*sb) *sb++ = 0;
        if (!strcmp(ca, cb)) continue;
        if (!splitnum(ca, pa, &va) || !splitnum(cb, pb, &vb)) return 0;
        if (strcmp(pa, pb) || fabs(va - vb) > 1.5e-4 + 1e-7 * fabs(va)) return 0;
    }
}

static int copyfile(const char *from, const char *to)
{
    char line[256];
    FILE *a = fopen(from, "r"), *b;
    if (!a) return 0;
    b = fopen(to, "w");
    if (!b) {
        fclose(a);
        return 0;
    }
    while (fgets(line, sizeof line, a)) fputs(line, b);
    fclose(a);
    fclose(b);
    return 1;
}

static int compare(const char *got, const char *exp, char *why)
{
    char la[256], lb[256];
    FILE *a = fopen(got, "r"), *b = fopen(exp, "r");
    int n = 0, ok = 1;
    why[0] = 0;
    if (!a || !b) {
        sprintf(why, "cannot open %s", a ? exp : got);
        if (a) fclose(a);
        if (b) fclose(b);
        return 0;
    }
    for (;;) {
        char *ra = fgets(la, sizeof la, a), *rb = fgets(lb, sizeof lb, b);
        n++;
        if (!ra && !rb) break;
        if (!ra || !rb) {
            sprintf(why, "line %d: one file is shorter", n);
            ok = 0;
            break;
        }
        chop(la);
        chop(lb);
        if (!sameline(la, lb)) {
            sprintf(why, "line %d: got \"%.60s\"", n, la);
            ok = 0;
            break;
        }
    }
    fclose(a);
    fclose(b);
    return ok;
}

static void addcov(void)
{
    char line[256];
    unsigned long off;
    int i;
    FILE *fp = fopen(TMPOUT, "r");
    if (!fp) return;
    while (fgets(line, sizeof line, fp)) {
        if (sscanf(line, "COV %lx", &off) != 1) continue;
        for (i = 0; i < ncov; i++)
            if (cov[i] == off) break;
        if (i == ncov && ncov < MAXCOV) cov[ncov++] = off;
    }
    fclose(fp);
}

/* name: test program without .txt; opts: extra dxsim options from TESTS.LST */
static void runprog(const char *name, const char *opts)
{
    char args[128], exp[96], why[120], what[300];
    sprintf(args, "%.20s.txt %.60s", name, opts);
    dx(args);
    sprintf(exp, "%s%.20s.out", EXPDIR, name);
    if (update) copyfile(TMPOUT, exp);
    sprintf(what, "trace of %s.txt %s matches %s", name, opts, exp);
    if (!compare(TMPOUT, exp, why)) {
        strcat(what, ": ");
        strcat(what, why);
        check(0, what);
    } else check(1, what);
    sprintf(args, "%.20s.txt %.60s -q -cov", name, opts);
    dx(args);
    addcov();
}

/* ------------------------------------------------------------------ */
/* hand-checked results                                                */

/* run a test program (with options) and read its moves */
static void movesx(const char *name, const char *opts)
{
    char args[96], line[256], where[32], kind[16];
    const char *c;
    FILE *fp;
    mv m;
    sprintf(args, "%.20s.txt %.60s", name, opts);
    dx(args);
    nmv = 0;
    fp = fopen(TMPOUT, "r");
    if (!fp) return;
    while (fgets(line, sizeof line, fp) && nmv < MAXMV) {
        if (sscanf(line, "%31s %15s X%lf Y%lf Z%lf", where, kind, &m.x, &m.y, &m.z) != 5) continue;
        if (!strcmp(kind, "RAPID")) m.k = 'R';
        else if (!strcmp(kind, "FEED")) m.k = 'F';
        else if (!strcmp(kind, "ARC2")) m.k = '2';
        else if (!strcmp(kind, "ARC3")) m.k = '3';
        else if (!strcmp(kind, "SPIRL")) m.k = 'S';
        else continue;
        m.cx = m.cy = m.r = m.sw = 0.0;
        c = strstr(line, "CX");
        if (c) sscanf(c, "CX%lf Y%lf R%lf %lf", &m.cx, &m.cy, &m.r, &m.sw);
        mvs[nmv++] = m;
    }
    fclose(fp);
}

static void moves(const char *name)
{
    movesx(name, "");
}

/*
 * The moves kinds[0..n-1] ('F' feed, '2'/'3' arcs, 'R' rapid) ending at the
 * points xy[2i], xy[2i+1], one after the other somewhere in the trace.
 */
static int seq(const char *kinds, const double *xy)
{
    int i, j, n = (int)strlen(kinds);
    for (i = 0; i + n <= nmv; i++) {
        for (j = 0; j < n; j++)
            if (mvs[i + j].k != kinds[j] || !near(mvs[i + j].x, xy[2 * j], 1e-4) ||
                !near(mvs[i + j].y, xy[2 * j + 1], 1e-4))
                break;
        if (j == n) return 1;
    }
    return 0;
}

/* drill plunges: a feed straight down; the pecks of one hole count once */
static int holes(double *hx, double *hy, double *hz, int max)
{
    int i, n = 0, same = 0;
    double lx = 0, ly = 0, lz = 0;
    for (i = 0; i < nmv; i++) {
        int moved = i > 0 && (fabs(mvs[i].x - lx) > 1e-6 || fabs(mvs[i].y - ly) > 1e-6);
        if (moved) same = 0;
        if (i > 0 && mvs[i].k == 'F' && !moved && mvs[i].z < lz - 1e-6) {
            if (same) {
                if (mvs[i].z < hz[n - 1]) hz[n - 1] = mvs[i].z;
            } else if (n < max) {
                hx[n] = mvs[i].x;
                hy[n] = mvs[i].y;
                hz[n] = mvs[i].z;
                n++;
                same = 1;
            }
        }
        lx = mvs[i].x;
        ly = mvs[i].y;
        lz = mvs[i].z;
    }
    return n;
}

static int cmpd(const void *a, const void *b)
{
    double d = *(const double *)a - *(const double *)b;
    return d < 0 ? -1 : d > 0;
}

static void manual(void)
{
    double hx[64], hy[64], hz[64], r[16], ang[16], v[16];
    int n, i, ok, k;
    static const char *const bolt[2] = { "cbolt", "cg181bc" };
    static const double bcy[2] = { 2.0, 3.0 };
    char what[100];

    /* :1341 five concentric circles R1..R5, each 360 degrees */
    moves("m1341");
    for (i = n = 0; i < nmv; i++)
        if (mvs[i].k == '2' && n < 16) {
            r[n] = mvs[i].r;
            ang[n++] = mvs[i].sw;
        }
    qsort(r, n, sizeof r[0], cmpd);
    ok = n == 5;
    for (i = 0; ok && i < 5; i++) ok = near(r[i], i + 1.0, 1e-4) && near(ang[i], -360.0, 1e-3);
    check(ok, "manual :1341 five full circles R1..R5");

    /* chapter 5 G81 example: 4 holes, bottom Z-1.05 */
    moves("mg81");
    n = holes(hx, hy, hz, 64);
    ok = n == 4 && near(hx[0], 1.0, 1e-4) && near(hy[0], .5, 1e-4) && near(hx[1], .5, 1e-4) &&
         near(hy[1], 1.0, 1e-4) && near(hx[2], 2.0, 1e-4) && near(hy[2], 1.0, 1e-4) &&
         near(hx[3], 2.5, 1e-4) && near(hy[3], .5, 1e-4);
    for (i = 0; ok && i < 4; i++) ok = near(hz[i], -1.05, 1e-4);
    check(ok, "manual G81 example: 4 holes at the programmed points, Z-1.05");

    /* fig 5-11 G191: 18 holes .5 deep on the 3 x 1.5 frame */
    moves("cg191");
    n = holes(hx, hy, hz, 64);
    ok = n == 18;
    for (i = 0; ok && i < n; i++)
        ok = hx[i] > 2.5 - 1e-4 && hx[i] < 5.5 + 1e-4 && hy[i] > 2.0 - 1e-4 &&
             hy[i] < 3.5 + 1e-4 && near(hz[i], -0.5, 1e-4);
    check(ok, "fig 5-11 G191 frame: 18 holes .5 deep on the 3 x 1.5 frame");

    /* fig 5-9 G181: 4 holes from (2.5,1.5) to (5.5,3) */
    moves("cg181");
    n = holes(hx, hy, hz, 64);
    ok = n == 4 && near(hx[0], 2.5, 1e-4) && near(hy[0], 1.5, 1e-4) && near(hx[3], 5.5, 1e-4) &&
         near(hy[3], 3.0, 1e-4);
    for (i = 0; ok && i < n; i++) ok = near(hz[i], -0.5, 1e-4);
    check(ok, "fig 5-9 G181 row: 4 holes .5 deep from (2.5,1.5) to (5.5,3)");

    /* fig 5-8 and 5-10 bolt circles: 6 holes at 30..330 degrees on R1.75 */
    for (k = 0; k < 2; k++) {
        moves(bolt[k]);
        n = holes(hx, hy, hz, 64);
        ok = n == 6;
        for (i = 0; ok && i < n; i++) {
            double a = atan2(hy[i] - bcy[k], hx[i] - 2.0) * 180.0 / PI;
            if (a < 0) a += 360.0;
            ang[i] = a;
            ok = near(sqrt((hx[i] - 2.0) * (hx[i] - 2.0) + (hy[i] - bcy[k]) * (hy[i] - bcy[k])),
                      1.75, 2e-4);
        }
        if (ok) qsort(ang, n, sizeof ang[0], cmpd);
        for (i = 0; ok && i < n; i++) ok = near(ang[i], 30.0 + 60.0 * i, 0.01);
        sprintf(what, "%s: 6 holes at 30..330 deg on R1.75", bolt[k]);
        check(ok, what);
    }

    /* fig 10-2 nested =N loops: 6 rows of 7 holes */
    moves("mloop");
    n = holes(hx, hy, hz, 64);
    for (i = k = 0; i < n; i++) {
        int j;
        for (j = 0; j < i; j++)
            if (near(hy[j], hy[i], 1e-4)) break;
        if (j == i) k++;
    }
    check(n == 42 && k == 6, "fig 10-2 nested =N loops: 42 holes in 6 rows");

    /* G83 peck depths */
    moves("cg83");
    for (i = n = 0; i < nmv; i++)
        if (mvs[i].k == 'F' && n < 16) v[n++] = mvs[i].z;
    check(n == 4 && near(v[0], -.2, 1e-4) && near(v[1], -.4, 1e-4) && near(v[2], -.6, 1e-4) &&
          near(v[3], -.8, 1e-4), "G83 peck depths -.2 -.4 -.6 -.8");

    /* macro #3 with seven '*' parameters */
    moves("mmacro");
    check(nmv >= 7 && near(mvs[2].z, -.5, 1e-4) && near(mvs[3].z, .5, 1e-4) &&
          near(mvs[4].z, -.5, 1e-4) && near(mvs[5].z, -.2, 1e-4) && near(mvs[6].z, .7, 1e-4),
          "macro =#3 Z*... takes its 7 parameters in order");

    /* fig 4-10 mirror: four copies about X2 Y1 */
    moves("mmirror");
    {
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (i = n = 0; i < nmv; i++)
            if (mvs[i].k == 'F') {
                n++;
                if (mvs[i].x < x0) x0 = mvs[i].x;
                if (mvs[i].x > x1) x1 = mvs[i].x;
                if (mvs[i].y < y0) y0 = mvs[i].y;
                if (mvs[i].y > y1) y1 = mvs[i].y;
            }
        check(n == 24 && near(x0, .7, 1e-4) && near(x1, 3.3, 1e-4) && near(y0, .25, 1e-4) &&
              near(y1, 1.75, 1e-4), "fig 4-10 mirror: shape copied into four quadrants about X2 Y1");
    }

    /* fig 3-3: centre, radius and incremental arc forms give the same path */
    moves("marcs");
    {
        mv a[8];
        for (i = n = 0; i < nmv; i++)
            if ((mvs[i].k == '2' || mvs[i].k == '3') && n < 8) a[n++] = mvs[i];
        ok = n == 6;
        for (i = 2; ok && i < 6; i++) {
            const mv *p = &a[i], *q = &a[i % 2];
            ok = p->k == q->k && near(p->x, q->x, 1e-4) && near(p->y, q->y, 1e-4) &&
                 near(p->cx, q->cx, 1e-4) && near(p->cy, q->cy, 1e-4) && near(p->r, q->r, 1e-4) &&
                 near(p->sw, q->sw, 1e-3);
        }
        check(ok, "fig 3-3: I/J, R and G91 arc forms agree");
    }

    /* cutter compensation (features/CUTCOMP.md section 5), worked out by hand */
    {
        static const double sq[] = { 0, .25, 1.5, .25, 1.75, 0, 1.75, -1.5, 1.5, -1.75,
                                     0, -1.75, -.25, -1.5, -.25, .1, -1, 1 };
        static const double tri[] = { -.973607, .111803, -.223607, 1.611803, .223607, 1.611803,
                                      .973607, .111803, .75, -.25, -.85, -.25, -1.75, -1 };
        static const double ins[] = { 2, .25, 2.775255, .25, 3.75, 1.224745, 3.75, 2.75,
                                      .25, 2.75, .25, 1, 1, -1 };
        movesx("ccsq", "-t 1=.5");
        check(seq("FF2F2F2FR", sq), "fig 7-15 square, G41 .5 dia: .25 outside, R.25 blends, exit at (-.25,.1)");
        movesx("cctri", "-t 1=.5");
        check(seq("FF2F2FR", tri), "fig 7-15 triangle, G41 .5 dia: offset edges and blend arcs");
        movesx("ccinside", "-t 1=.5");
        check(seq("FF2FFFF", ins), "cutter comp inside corners: line-arc, arc-line, line-line intersections");
        movesx("ccarcarc", "-t 1=.5");
        for (i = 0; i < nmv && mvs[i].k != '3'; i++) {
        }
        ok = i < nmv && near(sqrt(mvs[i].x * mvs[i].x + mvs[i].y * mvs[i].y), 4.75, 1e-4) &&
             near(sqrt((mvs[i].x - 3) * (mvs[i].x - 3) + (mvs[i].y - 2) * (mvs[i].y - 2)), 1.75, 1e-4);
        check(ok, "cutter comp arc-arc inside corner: the point is on both offset circles (R4.75, R1.75)");
    }
}

static void exprs(void)
{
    static const char *const e[] = { "1+2*3", "(1+2)*3", "-2*3", "SIN(30)", "ATN(1)",
        "SQR(16)+ABS(-2)", "DTF(3,4)", "#PI", "10/4", "2-3-4", "ABS(-3)-1", 0 };
    static const double want[] = { 7.0, 9.0, -6.0, 0.5, 45.0, 6.0, 5.0, PI, 2.5, -5.0, 2.0 };
    char args[64], line[256], what[100];
    int i;
    for (i = 0; e[i]; i++) {
        sprintf(args, "-e \"%s\"", e[i]);
        dx(args);
        lastline(line, sizeof line);
        sprintf(what, "expression %s = %.7g", e[i], want[i]);
        check(line[0] && strncmp(line, "error", 5) && near(atof(line), want[i], 1e-7), what);
    }
}

int main(int argc, char **argv)
{
    char line[256], name[80], what[100];
    FILE *lst;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) update = 1;
        else dxsim = argv[i];
    }
    printf("dxtest: using %s\n", dxsim);

    dx("-check");
    lastline(line, sizeof line);
    sprintf(what, "-check: %.80s", line);
    check(strstr(line, ", 0 failed") != 0, what);

    /* the cutter-compensation geometry against results worked out by hand (dxcomp.c) */
    dx("-selftest");
    {
        FILE *fp = fopen(TMPOUT, "r");
        int nok2 = 0, nbad = 0;
        if (fp) {
            while (fgets(line, sizeof line, fp)) {
                if (!strncmp(line, "ok", 2)) nok2++;
                else if (!strncmp(line, "FAIL", 4)) {
                    chop(line);
                    printf("  %s\n", line);
                    nbad++;
                }
            }
            fclose(fp);
        }
        sprintf(what, "-selftest: cutter-compensation geometry, %d checks, %d failed", nok2 + nbad, nbad);
        check(nok2 >= 31 && nbad == 0, what);
    }

    lst = fopen("tests.lst", "r");
    if (!lst) {
        printf("dxtest: run me in the tests directory (tests.lst not found)\n");
        return 2;
    }
    while (fgets(name, sizeof name, lst)) {
        char *opts;
        chop(name);
        if (!name[0] || name[0] == ';') continue;
        opts = strchr(name, ' ');           /* "ELINT -lint": options after the name */
        if (opts) *opts++ = 0;
        runprog(name, opts ? opts : "");
    }
    fclose(lst);

    manual();
    exprs();

    sprintf(what, "built-in cycle coverage: %d of 628 non-empty blocks executed", ncov);
    check(ncov >= 600, what);
    remove(TMPOUT);

    printf("%d passed, %d failed%s\n", nok, nfail, update ? " (expected files rewritten)" : "");
    return nfail ? 1 : 0;
}
