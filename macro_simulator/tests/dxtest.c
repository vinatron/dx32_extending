/*
 * dxtest.c - regression tests for dxsim, in C89 so they run where dxsim runs
 * (DOS, Windows 98, Linux, z/OS).
 *
 *   dxtest [-u] [path-to-dxsim]        run from the tests directory
 *   dxtest [-u] -sc PREVIEW [-bmp]     only the preview tests (5), with a preview build
 *
 * 1. "dxsim -check" must parse every block of the built-in cycle text, and
 *    "dxsim -selftest" must pass its cutter-compensation geometry checks.
 * 2. Each program in TESTS.LST (a name, then optional dxsim options) must
 *    reproduce EXPECTED\<name>.OUT, warnings and error reports included.  Numbers may
 *    differ by one digit in the last place (C libraries round halfway cases
 *    differently).  -u rewrites the expected files after a reviewed change.
 * 3. Results worked out by hand from the programmer's manual's figures.
 * 4. The tests together must execute at least 600 built-in cycle blocks.
 * 5. PLOTS.LST: plot files (SVG, HPGL, PostScript) must match EXPECTED\<name>.<ext>, numbers
 *    within one unit of their last printed digit.  With -sc, every TESTS.LST program runs
 *    through the preview (dxsim -Sa, views g17 and all) and its SCREEN line must match
 *    EXPECTED\SCREEN.OUT (line points and time within 1%); -bmp also saves each picture
 *    in SCR17\ and SCRALL\ and lists them in SCREENS.HTM.  UI.LST then runs the preview with
 *    scripted keys (dxsim -Sk) and the trace, keys and status lines included, must match
 *    EXPECTED\<name>.OUT.
 * 6. CORPUS.LST: fb2dx on the FANUC macro corpus (../../fanuc_macros): the reports must match
 *    EXPECTED\<name>.OUT.  Skipped where the long file names cannot be opened (16-bit DOS).
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
#define DEFFB   "..\\src\\FB2DX.EXE"
#define EXPDIR  "expected\\"
#define DIRSEP  "\\"
#else
#define DEFEXE  "../dxsim"
#define DEFFB   "../fb2dx"
#define EXPDIR  "expected/"
#define DIRSEP  "/"
#endif

#define TMPOUT  "dxt.out"
#define MAXMV   600
#define MAXCOV  1024
#define PI      3.14159265358979323846

static const char *dxsim = DEFEXE;
static const char *fb2dx = DEFFB;
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

/* run dxsim (or the preview build exe) with the given arguments; output in TMPOUT */
static void dxe(const char *exe, const char *args)
{
    char cmd[300];
    remove(TMPOUT);
    sprintf(cmd, "%.80s %.150s -silent -o %s", exe, args, TMPOUT);
    if (system(cmd)) {
        /* COMMAND.COM gives no exit codes, so the output file is what counts */
    }
}

static void dx(const char *args)
{
    dxe(dxsim, args);
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

    /* -flow (features/FLOW.md): the events of tests/flow.txt, counted by hand:
       #1 called twice (label loop), #2 four times, M100 once (pass 2 folded) */
    {
        static const char *const kinds[] = { "CALL", "RET", "JUMP", "SKIP", "LOOP", "CYCLE" };
        static const int want[] = { 7, 7, 1, 2, 3, 2 };
        int got[6] = { 0, 0, 0, 0, 0, 0 };
        char line[256], where[32], kind[16];
        FILE *fp;
        dx("flow.txt -flow");
        fp = fopen(TMPOUT, "r");
        if (fp) {
            while (fgets(line, sizeof line, fp))
                if (sscanf(line, "%31s %15s", where, kind) == 2)
                    for (k = 0; k < 6; k++)
                        if (!strcmp(kind, kinds[k])) got[k]++;
            fclose(fp);
        }
        for (k = 0, ok = 1; k < 6; k++)
            if (got[k] != want[k]) ok = 0;
        sprintf(what, "-flow events: CALL %d RET %d JUMP %d SKIP %d LOOP %d CYCLE %d", got[0], got[1],
                got[2], got[3], got[4], got[5]);
        check(ok, what);
    }

    /* -pc: the key of flowpc.txt names the three sources in order of first use */
    {
        char line[256];
        int nkey = 0, incyc = 0;
        FILE *fp;
        remove("dxt.svg");
        dx("flowpc.txt -pc -p dxt.svg");
        fp = fopen("dxt.svg", "r");
        if (fp) {
            while (fgets(line, sizeof line, fp))
                if (strstr(line, "<text") && strstr(line, "style=\"fill:")) {
                    nkey++;
                    if (strstr(line, "canned cycle G81 in #1")) incyc = 1;
                }
            fclose(fp);
        }
        remove("dxt.svg");
        check(nkey == 3 && incyc, "-pc key: main program, canned cycle G81 in #1, M100");
    }

    /* -map (features/FLOW.md 4b): the text map of flow.txt and the warnings of fmapw.txt */
    {
        char line[256];
        int okc = 0, nw = 0, rec = 0, nev = 0;
        FILE *fp;
        remove("dxt.map");
        dx("flow.txt -map dxt.map");
        fp = fopen("dxt.map", "r");
        if (fp) {
            while (fgets(line, sizeof line, fp))
                if (strstr(line, "#2 \"One hole\"  (lines 6, 7): 4 times")) okc = 1;
            fclose(fp);
        }
        check(okc, "-map: #2 called from lines 6 and 7, 4 times in the run");
        remove("dxt.map");
        dx("fmapw.txt -map dxt.map");
        fp = fopen("dxt.map", "r");
        if (fp) {
            while (fgets(line, sizeof line, fp)) {
                if (!strncmp(line, "WARN", 4)) nw++;
                if (strstr(line, "(recursive call)")) rec = 1;
                if (strstr(line, "#7  (line 18): never reached")) nev = 1;
            }
            fclose(fp);
        }
        remove("dxt.map");
        check(nw == 4 && rec && nev, "-map warnings: never called, never defined, recursion, L8 with cycles");
    }

    /* fillets G22/G23 (features/FILLET.md), worked out by hand */
    {
        static const double f1[] = { 2.366200, .5, 2.574200, .611300 };
        static const double sq[] = { 1.5, 0, 2, .5, 2, 1.5, 1.5, 2, 0, 2 };
        movesx("ffig37", "");
        for (i = n = 0; i < nmv; i++)
            if ((mvs[i].k == '2' || mvs[i].k == '3') && near(mvs[i].r, .25, 1e-4)) n++;
        check(n == 7 && seq("F3", f1), "fig 3-7 fillets: 7 arcs of R.25; fillet 1 from (2.3662,.5) to (2.5742,.6113)");
        movesx("fsquare", "");
        check(seq("F3F3F", sq), "fillets on horizontal/vertical lines: tangent points (1.5,0) (2,.5) (2,1.5) (1.5,2)");
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


/* ------------------------------------------------------------------ */
/* fb2dx, the FANUC macro translator: FB.LST lists its test programs   */

static void fb(const char *args)
{
    char cmd[256];
    remove(TMPOUT);
    sprintf(cmd, "%.80s %.100s -silent -o %s", fb2dx, args, TMPOUT);
    if (system(cmd)) {
        /* the report file is what counts */
    }
}

/* CORPUS.LST: fb2dx on the FANUC macro corpus (../../fanuc_macros): the reports must match */
static void corpus(void)
{
    char line[400], cmd[480], first[200], exp[64], why[120], what[300];
    char *rest;
    FILE *lst = fopen("corpus.lst", "r"), *fp;
    if (!lst) return;
    while (fgets(line, sizeof line, lst)) {
        chop(line);
        if (!line[0] || line[0] == ';') continue;
        rest = strchr(line, ' ');
        if (!rest) continue;
        *rest++ = 0;
        while (*rest == ' ') rest++;
        if (sscanf(rest, "%199s", first) != 1) continue;
        fp = fopen(first, "r");
        if (!fp) {                              /* 16-bit DOS: no long names */
            printf("skip  the corpus tests: %s cannot be opened (long file names, or no fanuc_macros)\n", first);
            break;
        }
        fclose(fp);
        remove(TMPOUT);
        remove("dxtk.txt");                     /* the -w file of the list */
        sprintf(cmd, "%.80s %.300s -silent -o %s", fb2dx, rest, TMPOUT);
        if (system(cmd)) {
            /* the report file is what counts */
        }
        sprintf(exp, "%s%.8s.out", EXPDIR, line);
        if (update) copyfile(TMPOUT, exp);
        sprintf(what, "corpus %.8s (%.60s) matches %.30s", line, strrchr(first, '/') ? strrchr(first, '/') + 1 : first,
                exp);
        if (!compare(TMPOUT, exp, why)) {
            strcat(what, ": ");
            strcat(what, why);
            check(0, what);
        } else check(1, what);
    }
    fclose(lst);
    remove("dxtk.txt");
}

static void runfb(const char *name, const char *prog, const char *opts)
{
    char args[160], exp[96], why[120], what[300];
    {
        const char *w = strstr(opts, "-w ");
        char file[40];
        if (w && sscanf(w + 3, "%30s", file) == 1) remove(file);       /* no stale program */
    }
    sprintf(args, "%.20s.nc %.100s", prog, opts);
    fb(args);
    sprintf(exp, "%s%.20s.out", EXPDIR, name);
    if (update) copyfile(TMPOUT, exp);
    sprintf(what, "fb2dx %s.nc %s matches %s", prog, opts, exp);
    if (!compare(TMPOUT, exp, why)) {
        strcat(what, ": ");
        strcat(what, why);
        check(0, what);
    } else check(1, what);

    /* -w FILE: the DX32 program it wrote must match expected/FILE.out too */
    {
        const char *w = strstr(opts, "-w ");
        char file[40], *dot;
        if (!w) return;
        sscanf(w + 3, "%30s", file);
        strcpy(exp, EXPDIR);
        strcat(exp, file);
        dot = strrchr(exp, '.');
        if (dot) strcpy(dot, ".out");
        {
            FILE *fp = fopen(file, "r"), *ep;
            if (!fp) {              /* refused: nothing may be written, and nothing expected */
                if (update) remove(exp);
                ep = fopen(exp, "r");
                if (ep) fclose(ep);
                sprintf(what, "fb2dx %s.nc: refused, so %s is not written", prog, file);
                check(!ep, what);
                return;
            }
            fclose(fp);
        }
        if (update) copyfile(file, exp);
        sprintf(what, "fb2dx %s.nc: the DX32 program %s matches %s", prog, file, exp);
        if (!compare(file, exp, why)) {
            strcat(what, ": ");
            strcat(what, why);
            check(0, what);
        } else check(1, what);
    }
}

/* run the DX32 program fb2dx wrote in dxsim and read FANUC variable #n from the
   variable dump, through the "; #n=D20" map at the top of the program */
static int fbget(const char *file, int n, double *v)
{
    char line[256], want[16], name[16];
    FILE *fp = fopen(file, "r");
    name[0] = 0;
    if (!fp) return 0;
    sprintf(want, "#%d=", n);
    while (fgets(line, sizeof line, fp) && line[0] == ';') {
        char *p = strstr(line, want);
        if (p && (p == line || p[-1] == ' ')) {
            sscanf(p + strlen(want), "%10s", name);
            break;
        }
    }
    fclose(fp);
    if (!name[0]) return 0;
    fp = fopen(TMPOUT, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof line, fp)) {
        char nm[16];
        double x;
        if (sscanf(line, "%10s = %lf", nm, &x) == 2 && !strcmp(nm, name)) {
            *v = x;
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

/* the X, Y (and Z) of the NC lines in the fb2dx trace */
static int fbxy(double *x, double *y, double *z, int max)
{
    char line[256];
    int n = 0;
    FILE *fp = fopen(TMPOUT, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof line, fp) && n < max) {
        char *px, *py, *pz;
        if (!strstr(line, " NC ")) continue;
        px = strstr(line, " X");
        py = strstr(line, " Y");
        pz = strstr(line, " Z");
        if (!px) continue;
        x[n] = atof(px + 2);
        y[n] = py ? atof(py + 2) : 0;
        z[n] = pz ? atof(pz + 2) : 0;
        n++;
    }
    fclose(fp);
    return n;
}

/* the end points of the moves in an fb2dx -eval trace (modal X Y Z), in inches */
static int fbmoves(double *x, double *y, double *z, int max, double scale)
{
    char line[256], *p;
    double cx = 0, cy = 0, cz = 0;
    int n = 0;
    FILE *fp = fopen(TMPOUT, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof line, fp) && n < max) {
        int any = 0;
        if (!strstr(line, " NC ")) continue;
        for (p = strtok(line + 13, " \n"); p; p = strtok(0, " \n")) {
            if (*p == 'X') { cx = atof(p + 1) * scale; any = 1; }
            if (*p == 'Y') { cy = atof(p + 1) * scale; any = 1; }
            if (*p == 'Z') { cz = atof(p + 1) * scale; any = 1; }
        }
        if (!any || (n ? near(x[n - 1], cx, 1e-6) && near(y[n - 1], cy, 1e-6) && near(z[n - 1], cz, 1e-6)
                       : near(cx, 0, 1e-6) && near(cy, 0, 1e-6) && near(cz, 0, 1e-6)))
            continue;                       /* no movement (the start is X0 Y0 Z0) */
        x[n] = cx;
        y[n] = cy;
        z[n] = cz;
        n++;
    }
    fclose(fp);
    return n;
}

/* translate PROG.nc, run the DX32 program in dxsim, and compare its moves with the
   FANUC evaluation: the end points in order (mm programs: scale 1/25.4) */
static void fbpath(const char *prog, const char *wname, double scale, const char *what)
{
    static double fx[MAXMV], fy[MAXMV], fz[MAXMV];
    char args[120], msg[200];
    int n, i, j = 0, ok = 1;
    sprintf(args, "%.20s.nc -eval -w %.20s.txt", prog, wname);
    fb(args);
    n = fbmoves(fx, fy, fz, MAXMV, scale);
    movesx(wname, "");
    for (i = 0; i < nmv && ok; i++) {
        double lx = i ? mvs[i - 1].x : 0, ly = i ? mvs[i - 1].y : 0, lz = i ? mvs[i - 1].z : 0;
        if (near(mvs[i].x, lx, 1e-4) && near(mvs[i].y, ly, 1e-4) && near(mvs[i].z, lz, 1e-4)) continue;
        if (j < n && near(mvs[i].x, fx[j], 1e-4) && near(mvs[i].y, fy[j], 1e-4) && near(mvs[i].z, fz[j], 1e-4))
            j++;
        else if (!(j < n && mvs[i].k == 'R' &&                /* a G0 split in Z and XY */
                   ((near(mvs[i].x, fx[j], 1e-4) && near(mvs[i].y, fy[j], 1e-4) && near(mvs[i].z, lz, 1e-4)) ||
                    (near(mvs[i].x, lx, 1e-4) && near(mvs[i].y, ly, 1e-4) && near(mvs[i].z, fz[j], 1e-4)))))
            ok = 0;
    }
    sprintf(msg, "%s: %d FANUC end points, the DX32 program in dxsim reaches %d of them in order", what, n, j);
    check(ok && j == n && n > 0, msg);
}

static void fbhand(void)
{
    double x[16], y[16], z[16];
    int n, i, ok;
    char what[120];

    /* bolt circle: centre 10,20, R30, 6 holes from 15 degrees */
    fb("fbbolt.nc -eval");
    n = fbxy(x, y, z, 16);
    ok = n == 7;
    for (i = 1; i < n && ok; i++) {
        double a = (15.0 + 60.0 * (i - 1)) * PI / 180.0;
        ok = near(x[i], 10 + 30 * cos(a), 1e-5) && near(y[i], 20 + 30 * sin(a), 1e-5);
    }
    check(ok, "fb2dx bolt circle: 6 holes on R30 about X10 Y20, 60 degrees apart from 15");

    /* Macro A loop: X0..X40 in 10 mm steps, from 10000 least increments */
    fb("fbmacroa.nc -eval -series a -fparam avar=0 -fparam is=b");
    n = fbxy(x, y, z, 16);
    ok = n == 5;
    for (i = 0; i < n && ok; i++) ok = near(x[i], 10.0 * i, 1e-9);
    check(ok, "fb2dx Macro A: H01/H02/H84 loop gives X0 to X40 in 10 mm steps");

    /* argument specification II: I J K groups go to #4-#6, #7-#9, #10 */
    fb("fbspec2.nc -eval");
    n = fbxy(x, y, z, 16);
    check(n == 1 && x[0] == 2 && y[0] == 5 && z[0] == 8, "fb2dx G65 specification II: #4=2 #7=5 #10=8");

    /* FIX FUP ROUND built on LSR, run in dxsim, against C over a table of values */
    {
        static const double tv[] = { 2.5, 3.5, 2.7, 2.2, -2.5, -3.5, -2.7, -2.2, 0, 3, -3, .5, -.5,
            12345.5, .4999999 };
        int bad = 0;
        fb("fbfix.nc -w fbwfix.txt");
        dx("fbwfix.txt -d");
        for (i = 0; i < 15; i++) {
            double a = fabs(tv[i]), f = floor(a), c = ceil(a), r = floor(a + .5), got[3];
            double sg = tv[i] < 0 ? -1 : 1;
            if (!fbget("fbwfix.txt", 101 + i, &got[0]) || !fbget("fbwfix.txt", 121 + i, &got[1]) ||
                !fbget("fbwfix.txt", 141 + i, &got[2]) || got[0] != sg * f || got[1] != sg * c ||
                got[2] != sg * r) {
                printf("  %g: FIX %g FUP %g ROUND %g\n", tv[i], got[0], got[1], got[2]);
                bad++;
            }
        }
        check(!bad, "fb2dx FIX/FUP/ROUND from LSR, run in dxsim: 15 values incl. halves and negatives");
    }

    /* ATAN[a]/[b] in all quadrants and on the axes, ASIN and ACOS, NAT=0 */
    {
        static const double want[] = { 45, 135, 225, 315, 90, 270, 180, 0, 30, 330, 120 };
        int bad = 0;
        double v;
        fb("fbatan.nc -fparam nat=0 -w fbwatan.txt");
        dx("fbwatan.txt -d");
        for (i = 0; i < 11; i++)
            if (!fbget("fbwatan.txt", 1 + i, &v) || !near(v, want[i], 1e-4)) bad++;
        check(!bad, "fb2dx ATAN[a]/[b] 0..360 on all quadrants and axes, ASIN 270..90, ACOS, run in dxsim");
    }

    /* phase 3: loops and jumps, run in dxsim against the FANUC evaluation */
    fbpath("fbloop", "fbwloop", 1 / 25.4, "fb2dx nested WHILE loops (3 x 2 grid)");
    fbpath("fbgoto", "fbwgoto", 1 / 25.4, "fb2dx backward GOTO loop, forward skips, breaks out of WHILE and DO");
    fbpath("fbexit", "fbwexit", 1 / 25.4, "fb2dx GOTO out of two loops and past blocks after a loop (exit flags)");

    /* phase 4: calls, run in dxsim against the FANUC evaluation */
    fbpath("fbbolt", "fbwbolt", 1 / 25.4, "fb2dx G65 bolt circle (arguments set by the caller)");
    fbpath("fbcall", "fbwcall", 1 / 25.4, "fb2dx M98 sharing locals, G65 in a loop, L repeats, early M99");

    /* phase 5: arcs.  The end points against the FANUC evaluation, and the centres by hand:
       (0,0), (-10,10) for the R arc (the short one), (-5,0) with J left out, (5,0) in G91,
       (5,0) for the full circle */
    fbpath("fbarc", "fbwarc", 1 / 25.4, "fb2dx arcs: G90 and G91, I J and R, a full circle, G18, dwell");
    {
        static const double cx[] = { 0, -10, -5, 5, 5 }, cy[] = { 0, 10, 0, 0, 0 };
        int na = 0, bad = 0;
        for (i = 0; i < nmv; i++)
            if (mvs[i].k == '2' || mvs[i].k == '3') {
                if (na < 5 && !(near(mvs[i].cx, cx[na] / 25.4, 1e-4) && near(mvs[i].cy, cy[na] / 25.4, 1e-4))) bad++;
                na++;
            }
        check(na == 6 && !bad, "fb2dx arcs: the DX32 centres (absolute in G90) are the FANUC ones");
    }

    /* phase 5: drilling cycles written out as moves, run in dxsim: hole positions and depths
       worked out by hand, the G99/G98 return levels, the G83 pecks of 4 to Z-10 */
    {
        static const double ex[] = { 10, 20, 30, 10, 20, 30 }, ey[] = { 10, 10, 10, 30, 30, 30 },
            ez[] = { -5, -5, -5, -3, -10, -6 };
        double hx[16], hy[16], hz[16];
        int n, bad = 0, pk = 0, after20 = -1, after30 = -1;
        fb("fbdrill.nc -fparam d83=.5 -fparam d73=.2 -w fbwdrill.txt");
        movesx("fbwdrill", "");
        n = holes(hx, hy, hz, 16);
        for (i = 0; i < n && i < 6; i++)
            if (!near(hx[i], ex[i] / 25.4, 1e-4) || !near(hy[i], ey[i] / 25.4, 1e-4) || !near(hz[i], ez[i] / 25.4, 1e-4))
                bad++;
        sprintf(what, "fb2dx G81/G82/G83/G73 in dxsim: %d holes at the FANUC positions and depths", n);
        check(n == 6 && !bad, what);
        for (i = 1; i < nmv; i++) {
            /* the feeds at X20 Y30: the pecks reach -2, -6, -10 */
            if (mvs[i].k == 'F' && near(mvs[i].x, 20 / 25.4, 1e-4) && near(mvs[i].y, 30 / 25.4, 1e-4)) pk++;
            /* the first rapid up after the bottoms of the holes at Y10 */
            if (mvs[i].k == 'R' && mvs[i - 1].k == 'F' && near(mvs[i].y, 10 / 25.4, 1e-4)) {
                if (near(mvs[i].x, 20 / 25.4, 1e-4)) after20 = near(mvs[i].z, 2 / 25.4, 1e-4);
                if (near(mvs[i].x, 30 / 25.4, 1e-4)) after30 = near(mvs[i].z, 20 / 25.4, 1e-4);
            }
        }
        check(pk == 3 && after20 == 1 && after30 == 1,
              "fb2dx drilling: G83 in 3 pecks, G99 back to R, G98 back to the initial level");
    }

    /* G91 G81 with K5, then G85 G89 G86: 8 holes; G85 and G89 feed back out to R (Z2) */
    {
        static const double ex[] = { 10, 20, 30, 40, 50, 10, 20, 30 }, ez[] = { -5, -5, -5, -5, -5, -6, -6, -6 };
        double hx[16], hy[16], hz[16];
        int n, bad = 0, out = 0;
        fb("fbrow.nc -series 16 -w fbwrow.txt");
        movesx("fbwrow", "");
        n = holes(hx, hy, hz, 16);
        for (i = 0; i < n && i < 8; i++)
            if (!near(hx[i], ex[i] / 25.4, 1e-4) || !near(hz[i], ez[i] / 25.4, 1e-4) ||
                !near(hy[i], (i < 5 ? 0 : 20) / 25.4, 1e-4)) bad++;
        for (i = 1; i < nmv; i++)
            if (mvs[i].k == 'F' && mvs[i].z > mvs[i - 1].z + 1e-4 && near(mvs[i].z, 2 / 25.4, 1e-4)) out++;
        sprintf(what, "fb2dx G91 K5 row and G85/G89/G86 in dxsim: %d holes, %d feeds back out to R", n, out);
        check(n == 8 && !bad && out == 2, what);
    }

    /* G41 with a 10 mm tool in dxsim: the approach ends at (-5,-5), the first side is cut at X-5
       (not at X0: the gouge a plain copy of the G41 block gives), the far side at X55 */
    {
        static const double px[] = { -5, -5, 55 }, py[] = { -5, 50, 0 };
        int found = 0, gouge = 0, k;
        fb("fbcomp.nc -fparam default -w fbwcomp.txt");
        movesx("fbwcomp", "-t 3=.3937");
        for (k = 0; k < 3; k++)
            for (i = 0; i < nmv; i++)
                if (mvs[i].k == 'F' && near(mvs[i].x, px[k] / 25.4, 2e-4) && near(mvs[i].y, py[k] / 25.4, 2e-4)) {
                    found++;
                    break;
                }
        for (i = 0; i < nmv; i++)
            if (mvs[i].k == 'F' && near(mvs[i].x, 0, 1e-4) && mvs[i].y > 0) gouge++;
        check(found == 3 && !gouge, "fb2dx G41 in dxsim with a 10 mm tool: the sides are offset by 5 mm, no gouge");
    }

    /* M98 P30100: three passes, the subprogram adds 1 to the caller's #1 */
    fb("fbm98.nc -eval -series 0");
    n = fbxy(x, y, z, 16);
    sprintf(what, "fb2dx M98 P30100 on series 0: 3 passes share #1, X%g", n ? x[0] : -1.0);
    check(n == 1 && x[0] == 3, what);
}

/* ------------------------------------------------------------------ */
/* plot files (PLOTS.LST) and the preview (-sc)                        */

typedef struct ptok {
    int    num;                 /* a number: v, and dec digits after the point */
    double v;
    int    dec;
    int    c;                   /* otherwise the character */
} ptok;

/* the next token of a plot file; 0 at the end. CR is skipped, so CR LF and LF compare equal */
static int ptoken(FILE *fp, ptok *t, long *line)
{
    char buf[40];
    int c, d, n = 0, dot = 0, isnum;
    do c = getc(fp); while (c == '\r');
    if (c == EOF) return 0;
    if (c == '\n') (*line)++;
    if (c == '-' || c == '.') {
        d = getc(fp);
        if (d != EOF) ungetc(d, fp);
        isnum = isdigit(d) || (c == '-' && d == '.');
    } else isnum = isdigit(c);
    t->num = isnum;
    t->c = c;
    t->dec = 0;
    if (!isnum) return 1;
    buf[n++] = (char)c;
    if (c == '.') dot = 1;
    for (;;) {
        c = getc(fp);
        if (isdigit(c) || (c == '.' && !dot)) {
            if (c == '.') dot = 1;
            else if (dot) t->dec++;
            if (n < 38) buf[n++] = (char)c;
        } else {
            if (c != EOF) ungetc(c, fp);
            break;
        }
    }
    buf[n] = 0;
    t->v = atof(buf);
    return 1;
}

/* the same text, numbers printed the same way and within one unit of their last digit */
static int plotcmp(const char *got, const char *exp, char *why)
{
    FILE *a = fopen(got, "rb"), *b = fopen(exp, "rb");
    long la = 1, lb = 1;
    ptok ta, tb;
    int ra, rb, ok = 1;
    why[0] = 0;
    if (!a || !b) {
        sprintf(why, "cannot open %s", a ? exp : got);
        if (a) fclose(a);
        if (b) fclose(b);
        return 0;
    }
    for (;;) {
        ra = ptoken(a, &ta, &la);
        rb = ptoken(b, &tb, &lb);
        if (!ra && !rb) break;
        if (!ra || !rb) {
            sprintf(why, "line %ld: one file is shorter", la);
            ok = 0;
            break;
        }
        if (ta.num != tb.num || (!ta.num && ta.c != tb.c) ||
            (ta.num && (ta.dec != tb.dec || fabs(ta.v - tb.v) > pow(10.0, -tb.dec) * 1.001))) {
            if (ta.num) sprintf(why, "line %ld: got %.*f, expected %.*f", la, ta.dec, ta.v, tb.dec, tb.v);
            else sprintf(why, "line %ld: the text differs", la);
            ok = 0;
            break;
        }
    }
    fclose(a);
    fclose(b);
    return ok;
}

static void plots(void)
{
    char line[200], plot[16], args[200], exp[64], why[120], what[300];
    char *prog, *ext, *opts;
    FILE *lst = fopen("plots.lst", "r");
    if (!lst) return;
    while (fgets(line, sizeof line, lst)) {
        chop(line);
        if (!line[0] || line[0] == ';') continue;
        prog = strchr(line, ' ');
        if (!prog) continue;
        *prog++ = 0;
        while (*prog == ' ') prog++;
        ext = strchr(prog, ' ');
        if (!ext) continue;
        *ext++ = 0;
        while (*ext == ' ') ext++;
        opts = strchr(ext, ' ');
        if (opts) {
            *opts++ = 0;
            while (*opts == ' ') opts++;
        } else opts = "";
        sprintf(plot, "dxt.%.3s", ext);
        remove(plot);
        sprintf(args, "%.20s.txt %.100s -p %s", prog, opts, plot);
        dx(args);
        sprintf(exp, "%s%.8s.%.3s", EXPDIR, line, ext);
        if (update) copyfile(plot, exp);
        sprintf(what, "plot %s.txt %s -p .%s matches %s", prog, opts, ext, exp);
        if (!plotcmp(plot, exp, why)) {
            strcat(what, ": ");
            strcat(what, why);
            check(0, what);
        } else check(1, what);
        remove(plot);
    }
    fclose(lst);
}

/* a SCREEN line against the expected one: moves and points off the area exactly, line points and
   time within 1% (they depend on where truncations fall, which can differ between FPUs) */
static int samescreen(const char *got, const char *exp)
{
    char ng[16], ne[16], vg[8], ve[8];
    long mg, pg, og, tg, me, pe, oe, te;
    if (sscanf(got, "%15s %7s moves %ld points %ld offscreen %ld time %ld", ng, vg, &mg, &pg, &og, &tg) != 6 ||
        sscanf(exp, "%15s %7s moves %ld points %ld offscreen %ld time %ld", ne, ve, &me, &pe, &oe, &te) != 6)
        return 0;
    return !strcmp(ng, ne) && !strcmp(vg, ve) && mg == me && og == oe && labs(pg - pe) <= 2 + pe / 100 &&
           labs(tg - te) <= 2 + te / 100;
}

/* -sc: every TESTS.LST program through the preview build, unattended (dxsim -Sa) */
static void screens(const char *exe, int bmp)
{
    static const char *const view[2] = { "g17", "all" };
    static const char *const dir[2] = { "SCR17", "SCRALL" };
    char name[200], args[200], line[256], got[200], exp[200], bmpf[32], what[400];
    char *opts;
    FILE *lst = fopen("tests.lst", "r"), *ef, *gf, *html = 0, *fp;
    int v, found;
    if (!lst) {
        printf("dxtest: run me in the tests directory (tests.lst not found)\n");
        return;
    }
    ef = fopen(EXPDIR "screen.out", "r");
    gf = fopen("screen.got", "w");
    if (bmp) {
        html = fopen("screens.htm", "w");
        if (html) fprintf(html, "<html><head><title>dxsim previews</title></head><body>\n"
                          "<h1>dxsim -Sa previews: G17 and all views</h1>\n<table border=1>\n");
    }
    while (fgets(name, sizeof name, lst)) {
        chop(name);
        if (!name[0] || name[0] == ';') continue;
        opts = strchr(name, ' ');
        if (opts) *opts++ = 0;
        else opts = "";
        if (strstr(opts, "-lint")) continue;
        if (html) fprintf(html, "<tr><td>%s %s</td>", name, opts);
        for (v = 0; v < 2; v++) {
            bmpf[0] = 0;
            if (bmp) {
                sprintf(bmpf, "%s%s%.8s.bmp", dir[v], DIRSEP, name);
                remove(bmpf);
            }
            sprintf(args, "%.20s.txt %.80s -Sa -v %s%s%s", name, opts, view[v], bmp ? " -bmp " : "", bmpf);
            dxe(exe, args);
            sprintf(got, "%-8.8s %-4s (no SCREEN line)", name, view[v]);
            fp = fopen(TMPOUT, "r");
            if (fp) {
                while (fgets(line, sizeof line, fp))
                    if (!strncmp(line, "SCREEN", 6)) {
                        chop(line);
                        sprintf(got, "%-8.8s %-4s %.120s", name, view[v], line + 8);
                    }
                fclose(fp);
            }
            if (gf) fprintf(gf, "%s\n", got);
            exp[0] = 0;
            if (ef && fgets(exp, sizeof exp, ef)) chop(exp);
            found = 1;
            if (bmp) {
                fp = fopen(bmpf, "rb");
                found = fp != 0;
                if (fp) fclose(fp);
                if (html) fprintf(html, "<td><img src=\"%s/%.8s.bmp\" width=320 height=240></td>", dir[v], name);
            }
            sprintf(what, "preview %s%s", got, found ? "" : " (no BMP written: does the folder exist?)");
            if (!update && strcmp(got, exp))
                sprintf(what + strlen(what), "; expected: %.150s", exp[0] ? exp : "(nothing)");
            check((update || samescreen(got, exp)) && strstr(got, " offscreen 0 ") && found, what);
        }
        if (html) fprintf(html, "</tr>\n");
    }
    fclose(lst);
    if (ef) fclose(ef);
    if (gf) fclose(gf);
    if (html) {
        fprintf(html, "</table></body></html>\n");
        fclose(html);
    }
    if (update) copyfile("screen.got", EXPDIR "screen.out");
    remove("screen.got");

    /* UI.LST: the keys, through scripted key presses (dxsim -Sk); the trace has every key and status line */
    lst = fopen("ui.lst", "r");
    if (lst) {
        char *prog, *keys;
        while (fgets(name, sizeof name, lst)) {
            char why[120];
            chop(name);
            if (!name[0] || name[0] == ';') continue;
            prog = strchr(name, ' ');
            if (!prog) continue;
            *prog++ = 0;
            while (*prog == ' ') prog++;
            keys = strchr(prog, ' ');
            if (!keys) continue;
            *keys++ = 0;
            while (*keys == ' ') keys++;
            opts = strchr(keys, ' ');
            if (opts) {
                *opts++ = 0;
                while (*opts == ' ') opts++;
            } else opts = "";
            sprintf(args, "%.20s.txt %.60s -Sk %.60s", prog, opts, keys);
            dxe(exe, args);
            sprintf(exp, "%s%.8s.out", EXPDIR, name);
            if (update) copyfile(TMPOUT, exp);
            sprintf(what, "UI %.8s: %.20s.txt%s%.60s -Sk %.60s matches %.40s", name, prog, *opts ? " " : "", opts, keys, exp);
            if (!compare(TMPOUT, exp, why)) {
                strcat(what, ": ");
                strcat(what, why);
                check(0, what);
            } else check(1, what);
        }
        fclose(lst);
    }
    remove(TMPOUT);
}

int main(int argc, char **argv)
{
    char line[256], name[80], what[100];
    FILE *lst;
    int i;

    const char *sc = 0;
    int bmp = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) update = 1;
        else if (!strcmp(argv[i], "-fb") && i + 1 < argc) fb2dx = argv[++i];
        else if (!strcmp(argv[i], "-sc") && i + 1 < argc) sc = argv[++i];
        else if (!strcmp(argv[i], "-bmp")) bmp = 1;
        else dxsim = argv[i];
    }
    if (sc) {                               /* only the preview tests */
        printf("dxtest: the preview of %s\n", sc);
        screens(sc, bmp);
        printf("%d passed, %d failed%s\n", nok, nfail, update ? " (expected files rewritten)" : "");
        return nfail ? 1 : 0;
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
    plots();

    lst = fopen("fb.lst", "r");
    if (lst) {
        FILE *exe = fopen(fb2dx, "rb");
        if (!exe) {
            printf("skip  the fb2dx tests: %s not found (build it, or give -fb path)\n", fb2dx);
            fclose(lst);
            lst = 0;
        } else fclose(exe);
    }
    if (lst) {
        while (fgets(name, sizeof name, lst)) {
            char *prog, *opts;
            chop(name);
            if (!name[0] || name[0] == ';') continue;
            prog = strchr(name, ' ');
            if (!prog) continue;
            *prog++ = 0;
            while (*prog == ' ') prog++;
            opts = strchr(prog, ' ');
            if (opts) {
                *opts++ = 0;
                while (*opts == ' ') opts++;
            }
            runfb(name, prog, opts ? opts : "");
        }
        fclose(lst);
        fbhand();
        corpus();
    }

    sprintf(what, "built-in cycle coverage: %d of 628 non-empty blocks executed", ncov);
    check(ncov >= 600, what);
    remove(TMPOUT);

    printf("%d passed, %d failed%s\n", nok, nfail, update ? " (expected files rewritten)" : "");
    return nfail ? 1 : 0;
}
