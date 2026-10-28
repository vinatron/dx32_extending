/* fbeval.c - a small FANUC Macro B evaluator (FB2DX.md phase 1).
   Runs the statements the way the FANUC control does: locals per G65 level, vacant
   variables, M98/M99, G65 with arguments, GOTO, WHILE/DO/END.  It prints a trace of
   the NC blocks with their values; the translator (phases 2-6) uses the same rules
   to find what a program really reads, and phase 6 to compare tool paths. */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "fb2dx.h"

#define PI      3.14159265358979323846
#define MAXLVL  6           /* main program + 5 G65 levels */
#define MAXCALL 16
#define NVAC    64

typedef struct val {
    double v;
    int vac;
} val;

typedef struct frame {
    int ret;                /* statement after the call */
    int lvl;                /* local level of the caller */
    int pstart, pend;       /* caller's program */
    int start;              /* the called program's first statement */
    long left;              /* repetitions still to run */
    int g65;
    val args[34];
} frame;

static fbx *X;
static FILE *O;
static val com[1000];
static val loc[MAXLVL][34];
static int lvl, err, units;
static int vacl[NVAC];
static long vacv[NVAC];
static int nvac;
static int curline;
static fbs *cur;                /* the statement being evaluated */

/* a trace line; nothing when the evaluation runs only for the translator */
static void tr(const char *fmt, ...)
{
    va_list ap;
    if (!O) return;
    va_start(ap, fmt);
    vfprintf(O, fmt, ap);
    va_end(ap);
}

static void fail(const char *kind, const char *fmt, const char *a, double v)
{
    char m[200];
    if (err) return;
    sprintf(m, fmt, a, v);
    fbrep(X, kind, curline, "%s", m);
    err = 1;
    X->stop = 1;
}

static void vacread(long n)
{
    int i;
    if (cur) cur->vacbad = 1;
    for (i = 0; i < nvac; i++)
        if (vacl[i] == curline && vacv[i] == n) return;
    if (nvac < NVAC) {
        vacl[nvac] = curline;
        vacv[nvac++] = n;
    }
    fbrep(X, "VACANT", curline, "#%ld is read while vacant (counts as 0 here)", n);
}

static val mkv(double v)
{
    val r;
    r.v = v;
    r.vac = 0;
    return r;
}

static long varno(const fbn *e);
static val ev(const fbn *e, int addr);

static val *vref(long n)
{
    if (n >= 1 && n <= 33) return &loc[lvl][n];
    if (n >= 100 && n <= 999) return &com[n];
    if (n >= 1000) fail("REFUSED", "%s#%.0f: system variables do not translate", "", (double)n);
    else if (n != 0) fail("REFUSED", "%s#%.0f does not exist", "", (double)n);
    return 0;
}

static long varno(const fbn *e)
{
    val i = ev(e->a, 0);
    double r = floor(i.v + 0.5);
    if (fabs(i.v - r) > 1e-9) fail("REFUSED", "%svariable number %g is not a whole number", "", i.v);
    return (long)r;
}

/* read a variable; how a vacant one counts is up to the caller */
static val getvar(const fbn *e)
{
    val r, *p;
    long n;
    r.v = 0;
    r.vac = 1;
    if (e->k == EN_SYS) {
        fail("REFUSED", "#_%s: system variables do not translate", e->name, 0);
        return r;
    }
    n = varno(e);
    if (err || n == 0) return r;
    p = vref(n);
    return p ? *p : r;
}

static double num(const fbn *e, int addr)
{
    val r = ev(e, addr);
    if (r.vac && !err) {
        const fbn *v = e->k == EN_NEG ? e->a : e;
        if (v->k == EN_VAR) vacread(varno(v));
    }
    return r.vac ? 0.0 : r.v;
}

static double deg(double r)
{
    return r * 180.0 / PI;
}

static double rad(double d)
{
    return d * PI / 180.0;
}

static double incr(int letter)
{
    double d = X->par.is == 'c' ? 10.0 : 1.0;
    if (letter == 'A' || letter == 'B' || letter == 'C') return 0.001 / d;
    if (!units && X->par.units) units = X->par.units;
    if (!units) {
        fail("REFUSED", "%sthe unit system is not known: G20/G21 in the program, or -inch/-mm", "", 0);
        return 0;
    }
    return (units == 20 ? 0.0001 : 0.001) / d;
}

static unsigned long u32(double v)
{
    if (v < 0 || v > 4294967295.0 || v != floor(v)) {
        fail("REFUSED", "%sAND/OR/XOR/MOD/BIN/BCD on %g: only whole numbers >= 0 are translated", "", v);
        return 0;
    }
    return (unsigned long)v;
}

static double round0(double v)
{
    return v < 0 ? -floor(-v + 0.5) : floor(v + 0.5);
}

static double fixv(double v)
{
    return v < 0 ? -floor(-v) : floor(v);
}

static double fn(const fbn *e, int addr)
{
    double a = num(e->a, addr), b = e->b ? num(e->b, addr) : 0, r;
    int nat = X->par.nat == 1;
    switch (e->op) {
    case FN_SIN: return sin(rad(a));
    case FN_COS: return cos(rad(a));
    case FN_TAN: return tan(rad(a));
    case FN_ASIN:
    case FN_ACOS:
        if (a < -1 || a > 1) {
            fail("REFUSED", "%sASIN/ACOS of %g: alarm on the control", "", a);
            return 0;
        }
        if (e->op == FN_ACOS) return deg(acos(a));
        r = deg(asin(a));
        return r < 0 && !nat ? r + 360.0 : r;       /* 270..90 with NAT=0 */
    case FN_ATAN: return deg(atan(a));
    case FN_ATAN2:
        r = deg(atan2(a, b));
        if (!nat && r < 0) r += 360.0;
        return r;
    case FN_SQRT:
        if (a < 0) {
            fail("REFUSED", "%sSQRT of %g: alarm on the control", "", a);
            return 0;
        }
        return sqrt(a);
    case FN_ABS: return fabs(a);
    case FN_BIN: {          /* BCD to binary */
        unsigned long u = u32(a), m = 1, out = 0;
        while (u) {
            out += (u & 15) * m;
            m *= 10;
            u >>= 4;
        }
        return (double)out;
    }
    case FN_BCD: {          /* binary to BCD */
        unsigned long u = u32(a), out = 0;
        int sh = 0;
        while (u && sh < 32) {
            out |= (u % 10) << sh;
            u /= 10;
            sh += 4;
        }
        return (double)out;
    }
    case FN_ROUND:
        if (addr) {         /* in an address: to the least increment (16/0i/30i) */
            double q = incr(addr);
            return q > 0 ? round0(a / q) * q : a;
        }
        return round0(a);
    case FN_FIX: return fixv(a);
    case FN_FUP: return fixv(a) == a ? a : fixv(a) + (a < 0 ? -1.0 : 1.0);
    case FN_LN:
        if (a <= 0) {
            fail("REFUSED", "%sLN of %g: alarm on the control", "", a);
            return 0;
        }
        return log(a);
    case FN_EXP: return exp(a);
    case FN_POW: return pow(a, b);
    case FN_ADP:
        fail("REFUSED", "%sADP depends on the address the argument came from; not translated", "", 0);
        return 0;
    }
    return 0;
}

static int cmp(const fbn *e)
{
    val a = ev(e->a, 0), b = ev(e->b, 0);
    if (e->op == CM_EQ || e->op == CM_NE) {
        int eq = a.vac || b.vac ? a.vac == b.vac : a.v == b.v;
        if ((a.vac || b.vac) && cur) cur->vtest = 1;       /* vacant tested: folded if constant (3.14) */
        return e->op == CM_EQ ? eq : !eq;
    }
    if (a.vac) { a.v = 0; if (e->a->k == EN_VAR) vacread(varno(e->a)); }
    if (b.vac) { b.v = 0; if (e->b->k == EN_VAR) vacread(varno(e->b)); }
    switch (e->op) {
    case CM_GT: return a.v > b.v;
    case CM_GE: return a.v >= b.v;
    case CM_LT: return a.v < b.v;
    default: return a.v <= b.v;
    }
}

static int truth(const fbn *e)
{
    if (e->k == EN_CMP) return cmp(e);
    if (e->k == EN_LOG) {
        int a = truth(e->a), b = truth(e->b);
        if (e->op == OP_AND) return a && b;
        if (e->op == OP_OR) return a || b;
        return a != b;
    }
    return 0;
}

/* addr: the address letter when e is a word value (ROUND rounds to its increment) */
static val ev(const fbn *e, int addr)
{
    double a, b;
    if (err) return mkv(0);
    switch (e->k) {
    case EN_NUM: return mkv(e->v);
    case EN_VAR:
    case EN_SYS: return getvar(e);
    case EN_NEG: return mkv(-num(e->a, addr));
    case EN_FUN: return mkv(fn(e, addr));
    case EN_CMP:
    case EN_LOG: return mkv(truth(e) ? 1.0 : 0.0);
    }
    a = num(e->a, addr);
    b = num(e->b, addr);
    switch (e->op) {
    case OP_ADD: return mkv(a + b);
    case OP_SUB: return mkv(a - b);
    case OP_MUL: return mkv(a * b);
    case OP_DIV:
        if (b == 0) {
            fail("REFUSED", "%sdivision by 0: alarm on the control", "", 0);
            return mkv(0);
        }
        return mkv(a / b);
    case OP_AND: return mkv((double)(u32(a) & u32(b)));
    case OP_OR: return mkv((double)(u32(a) | u32(b)));
    case OP_XOR: return mkv((double)(u32(a) ^ u32(b)));
    case OP_MOD:
        if (u32(b) == 0) {
            fail("REFUSED", "%sMOD by 0: alarm on the control", "", 0);
            return mkv(0);
        }
        return mkv((double)(u32(a) % u32(b)));
    }
    return mkv(0);
}

static int halted;              /* #3000: the FANUC alarm stopped the program */

static void assign(const fbn *lhs, const fbn *e)
{
    val v, *p;
    long n;
    char b[420];
    if (lhs->k == EN_SYS) {
        fail("REFUSED", "#_%s: system variables do not translate", lhs->name, 0);
        return;
    }
    n = varno(lhs);
    if (err) return;
    if (n == 3000 || n == 3006) {       /* alarm / stop with a message */
        double a = num(e, 0);
        const char *m = cur && cur->cmt ? cur->cmt : "";
        tr("L%-5d %s #%ld=%.0f (%s)\n", curline, n == 3000 ? "ALARM " : "STOP  ", n, a, m);
        if (n == 3000) {
            fbrep(X, "NOTE", curline, "the FANUC alarm %.0f (%s) stops the program here", a, m);
            halted = 1;
        }
        return;
    }
    if (n == 0) {
        fail("REFUSED", "%s#0 cannot be set", "", 0);
        return;
    }
    /* #i = #j keeps a vacant #j vacant; in any other expression it counts as 0 */
    if (e->k == EN_VAR) v = getvar(e);
    else v = mkv(num(e, 0));
    p = vref(n);
    if (err || !p) return;
    *p = v;
    if (n < 1000) X->vset[n] = 1;
    b[0] = 0;
    if (v.vac) strcpy(b, "vacant");
    else fbfmt(b, v.v);
    tr("L%-5d SET    #%ld = %s\n", curline, n, b);
}

/* ------------------------------------------------------------------ */
/* NC blocks                                                            */

/* the value of word i, or 0 if it is omitted (a vacant variable) */
static int wval(const fbs *s, int i, double *out)
{
    const fbn *e = s->w[i].e, *v = e->k == EN_NEG ? e->a : e;
    int l = s->w[i].l;
    if (v->k == EN_VAR || v->k == EN_SYS) {
        val r = getvar(v);
        if (err) return 0;
        if (r.vac) {                        /* X#1 with #1 vacant: the word is ignored */
            if (cur) cur->vacbad = 1;
            return 0;
        }
        *out = e->k == EN_NEG ? -r.v : r.v;
        if (X->macroa && fbisdim(l) && X->par.avar == 0) *out *= incr(l);
        return 1;
    }
    *out = num(e, fbisdim(l) ? l : 0);
    if (fbisdim(l) && v->k == EN_NUM && !v->pt && X->par.dpi == 0) *out *= incr(l);
    return 1;
}

static int find(const fbs *s, int l)
{
    int i;
    for (i = 0; i < s->nw; i++)
        if (s->w[i].l == l) return i;
    return -1;
}

static int progat(long o)
{
    int i;
    for (i = 0; i < X->nst; i++)
        if (X->st[i]->k == ST_PROG && X->st[i]->prog == o) return i;
    return -1;
}

static int progend(int start)
{
    int i;
    for (i = start + 1; i < X->nst; i++)
        if (X->st[i]->k == ST_PROG) return i;
    return X->nst;
}

static void wfmt(char *b, int l, double v)
{
    if (fbisdim(l) || l == 'F' || v != floor(v)) fbfmt(b, v);
    else sprintf(b, "%.0f", v);
}

/* G65 arguments: specification I, or II for repeated I J K (FB2DX.md 3.4) */
static int args(const fbs *s, val *a)
{
    static const char *spec1 = "ABCIJKDEF H M   QRSTUVWXYZ";      /* #1.. in order; blank = none */
    int i, cnt[3] = { 0, 0, 0 }, other = 0, g = 0, last = -1, sp2;
    for (i = 1; i <= 33; i++) {
        a[i].v = 0;
        a[i].vac = 1;
    }
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        if (l == 'I' || l == 'J' || l == 'K') cnt[l - 'I']++;
        else if (strchr("DEFHMQRSTUVWXYZ", l)) other = 1;
    }
    sp2 = cnt[0] > 1 || cnt[1] > 1 || cnt[2] > 1;
    if (sp2 && other) {
        fail("REFUSED", "%sG65 mixes argument specifications I and II", "", 0);
        return 0;
    }
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l, n;
        double v;
        const char *p;
        if (strchr("GLNOP", l)) continue;
        if (sp2 && (l == 'I' || l == 'J' || l == 'K')) {
            int c = l - 'I';
            if (c <= last) g++;
            last = c;
            n = 4 + 3 * g + c;
            if (n > 33) {
                fail("REFUSED", "%sG65: more than 10 I J K groups", "", 0);
                return 0;
            }
        } else {
            p = strchr(spec1, l);
            if (!p) {
                char lt[2];
                lt[0] = (char)l;
                lt[1] = 0;
                fail("REFUSED", "G65: %s is not an argument address", lt, 0);
                return 0;
            }
            n = (int)(p - spec1) + 1;
        }
        if (wval(s, i, &v)) {
            a[n].v = v;
            a[n].vac = 0;
        }
        if (err) return 0;
    }
    return 1;
}

/* the modal state of the axes, as the FANUC control keeps it (for the writer, FB2DX.md 3.12) */
static int gabs, gmot, gpl, known[3], gcyc, gret, tool;
static double pos[3], cz, cr, cq, cinit;   /* the position and the drilling values, program units */
static int cycabs;              /* G90 or G91 when the cycle started */

/* a move of the FANUC tool path, in inches like the dxsim trace (-check) */
static void addmv(int k, double x, double y, double z, int hasc, double cx, double cy)
{
    fbmv *m;
    double f;
    int u = units ? units : X->par.units;
    if (!u) {
        X->mvunk = 1;
        return;
    }
    f = u == 20 ? 1.0 : 1.0 / 25.4;
    if (X->nmv == X->cmv) {
        int nc = X->cmv ? X->cmv * 2 : 256;
        fbmv *a;
        if (nc > 16000) {
            X->mvunk = 1;
            return;
        }
        a = (fbmv *)fbmalloc((unsigned)(nc * sizeof *a));
        if (X->nmv) memcpy(a, X->mv, X->nmv * sizeof *a);
        if (X->mv) free(X->mv);
        X->mv = a;
        X->cmv = nc;
    }
    m = &X->mv[X->nmv++];
    m->k = (char)k;
    m->line = curline;
    m->x = x * f;
    m->y = y * f;
    m->z = z * f;
    m->hasc = hasc;
    m->cx = cx * f;
    m->cy = cy * f;
}

/* the moves of one drilling block, as fbout.c writes them: K (or L) holes; in G91 X Y are steps,
   R is from the initial level and Z from R */
static void holemoves(const fbs *s, int start, long k)
{
    int i, n;
    long r;
    double v, dx = 0, dy = 0, nx, ny, dp, dn, ret, ar, az;
    int hx = 0, hy = 0;
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        if (!wval(s, i, &v)) continue;
        if (l == 'X') dx = v, hx = 1;
        else if (l == 'Y') dy = v, hy = 1;
        else if (l == 'Z') cz = v;
        else if (l == 'R') cr = v;
        else if (l == 'Q') cq = v;
    }
    if (start) cinit = pos[2];
    ar = gabs == 91 ? cinit + cr : cr;
    az = gabs == 91 ? ar + cz : cz;
    for (r = 0; r < k; r++) {
        nx = gabs == 91 ? pos[0] + dx : hx ? dx : pos[0];
        ny = gabs == 91 ? pos[1] + dy : hy ? dy : pos[1];
        if (nx != pos[0] || ny != pos[1]) addmv('R', nx, ny, pos[2], 0, 0, 0);
        addmv('R', nx, ny, ar, 0, 0, 0);
        ret = gret == 99 ? ar : cinit;
        if (gcyc == 83 || gcyc == 73) {
            if (cq > 0)
                for (dp = ar, n = 0; dp > az && n < 10000; dp = dn, n++) {
                    dn = dp - cq < az ? az : dp - cq;
                    if (gcyc == 83 && dp < ar) addmv('R', nx, ny, dp + X->par.d83, 0, 0, 0);
                    addmv('F', nx, ny, dn, 0, 0, 0);
                    addmv('R', nx, ny, gcyc == 83 ? ar : dn + X->par.d73, 0, 0, 0);
                }
        } else addmv('F', nx, ny, az, 0, 0, 0);
        if (gcyc == 85 || gcyc == 89) addmv('F', nx, ny, ar, 0, 0, 0);     /* feed out to R */
        addmv('R', nx, ny, ret, 0, 0, 0);
        pos[0] = nx;
        pos[1] = ny;
        pos[2] = ret;
    }
}

static void modal(fbs *s)
{
    static const char ax[] = "XYZ";
    int i, a, moved = 0, cw = 0, g4 = 0, pa, pb, newcyc = 0, start = 0, g43 = 0, hole = 0, g28 = 0;
    double v, h = -1, d = -1;
    int g41 = 0;
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        if (l == 'T' && wval(s, i, &v)) tool = (int)v;
        if (l == 'H' && wval(s, i, &v)) h = v;
        if (l == 'D' && wval(s, i, &v)) d = v;
        if (l == 'M' && wval(s, i, &v) && v == 6) {
            if (s->tool == -1) s->tool = tool;
            else if (s->tool != tool) s->tool = -2;
        }
        if (l != 'G' || !wval(s, i, &v)) continue;
        if (v == 0 || v == 1 || v == 2 || v == 3) gmot = (int)v, gcyc = 0;
        else if (v == 90 || v == 91) gabs = (int)v;
        else if (v == 17 || v == 18 || v == 19) gpl = (int)v;
        else if (v == 4) g4 = 1;
        else if (v == 80) gcyc = 0, gmot = 80;
        else if (v == 81 || v == 82 || v == 83 || v == 73 || v == 85 || v == 86 || v == 89) newcyc = (int)v;
        else if (v == 98 || v == 99) gret = (int)v;
        else if (v == 43) g43 = 1;
        else if (v == 41 || v == 42) g41 = 1;
        else if (v == 28) g28 = 1;
    }
    if (g28) {                          /* to the reference point: where that is, is not known */
        for (i = 0; i < s->nw; i++) {
            const char *p = strchr(ax, s->w[i].l);
            if (p) known[p - ax] = 0;
        }
        return;
    }
    if (g43 && h != tool) s->modes |= MO_HBAD;
    if (g41 && d >= 0 && d != tool) s->modes |= MO_DBAD;
    if (g4) return;
    if (newcyc) {
        if (!gcyc) start = 1, cycabs = gabs;
        gcyc = newcyc;
    }
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        const char *p = strchr(ax, l);
        if (l == 'I' || l == 'J' || l == 'K' || l == 'R') cw = 1;
        if (l == 'R' && wval(s, i, &v) && v < 0) s->modes |= MO_NEGR;
        if (p && wval(s, i, &v)) moved = 1;
    }
    s->modes |= gabs == 91 ? MO_INC : MO_ABS;
    if (gcyc)                           /* a hole in a block with X Y Z or R (TENTATIVE: FB2DX.md 3.12) */
        for (i = 0; i < s->nw; i++)
            if (strchr("XYZR", s->w[i].l)) hole = 1;
    if (hole) {
        long k = 1;
        s->modes |= MO_HOLE | (gcyc == 81 ? MO_C81 : gcyc == 82 ? MO_C82 : gcyc == 83 ? MO_C83 :
                               gcyc == 73 ? MO_C73 : gcyc == 85 ? MO_C85 : gcyc == 86 ? MO_C86 : MO_C89);
        if (gabs != cycabs) s->modes |= MO_MIXC;
        if (gabs == 91 && (!known[0] || !known[1])) s->modes |= MO_UNK;
        for (i = 0; i < s->nw; i++)
            if (s->w[i].l == 'K' || s->w[i].l == 'L') {     /* a count: never scaled */
                v = num(s->w[i].e, 0);
                s->modes |= s->w[i].l == 'K' ? MO_KK : MO_KL;
                k = (long)v;
            }
        if (s->krep == -1) s->krep = k;
        else if (s->krep != k) s->krep = -2;
        s->modes |= gret == 99 ? MO_R99 : MO_R98;
        if (start) s->modes |= MO_CST;
        if (start && !known[2]) s->modes |= MO_UNKZ;
        for (i = 0; i < s->nw; i++) {
            const char *p = strchr(ax, s->w[i].l);
            if (s->w[i].l == 'Q' && (gcyc == 83 || gcyc == 73) && wval(s, i, &v) && v <= 0)
                fail("REFUSED", "%sG83/G73 with Q <= 0: alarm on the control", "", 0);
            if (!p || s->w[i].l == 'Z' || !wval(s, i, &v)) continue;
            if (gabs == 90) known[p - ax] = 1;
        }
        holemoves(s, start, s->krep >= 0 ? s->krep : 1);
        return;
    }
    s->modes |= gpl == 18 ? MO_P18 : gpl == 19 ? MO_P19 : MO_P17;
    if ((gmot == 2 || gmot == 3) && (moved || cw)) {
        s->modes |= MO_ARC;
        pa = gpl == 18 ? 2 : gpl == 19 ? 1 : 0;
        pb = gpl == 18 ? 0 : gpl == 19 ? 2 : 1;
        if (!known[pa] || !known[pb]) s->modes |= MO_UNK;
    } else if (moved && (gmot == 80 || gmot < 0)) s->modes |= MO_NOMOT;
    else if (moved) s->modes |= MO_LIN;
    if ((s->modes & (MO_ARC | MO_LIN)) && !(s->modes & MO_NOMOT)) {   /* the tool path */
        double np[3], c[3];
        int hasc = 0, hasr = 0;
        np[0] = pos[0], np[1] = pos[1], np[2] = pos[2];
        c[0] = pos[0], c[1] = pos[1], c[2] = pos[2];
        for (i = 0; i < s->nw; i++) {
            static const char ijk[] = "IJK";
            const char *p = strchr(ax, s->w[i].l), *q = strchr(ijk, s->w[i].l);
            if (s->w[i].l == 'R') hasr = 1;
            if (!wval(s, i, &v)) continue;
            if (p) np[p - ax] = gabs == 91 ? pos[p - ax] + v : v;
            if (q) c[q - ijk] += v;
        }
        if (gmot == 2 || gmot == 3) hasc = !hasr && gpl == 17;
        addmv(gmot == 0 ? 'R' : gmot == 1 ? 'F' : gmot == 2 ? '2' : '3', np[0], np[1], np[2], hasc, c[0], c[1]);
        pos[0] = np[0], pos[1] = np[1], pos[2] = np[2];
    }
    for (i = 0; i < s->nw; i++) {
        const char *p = strchr(ax, s->w[i].l);
        if (!p || !wval(s, i, &v)) continue;
        a = (int)(p - ax);
        if (gabs == 90) known[a] = 1;
    }
}

static void ncout(const fbs *s, const char *skip)
{
    char b[420], t[48];
    int i, n = 0;
    b[0] = 0;
    for (i = 0; i < s->nw; i++) {
        double v;
        int l = s->w[i].l;
        if (strchr(skip, l)) continue;
        if (!wval(s, i, &v)) {
            if (err) return;
            continue;
        }
        if (l == 'G' && (v == 20 || v == 21)) units = (int)v;
        if (strlen(b) > 360) break;
        sprintf(t, "%s%c", n++ ? " " : "", l);
        strcat(b, t);
        wfmt(t, l, v);
        strcat(b, t);
    }
    if (n) tr("L%-5d NC     %s\n", curline, b);
}

/* ------------------------------------------------------------------ */

static frame cs[MAXCALL];      /* static: the DOS stack is small */

int fbeval(fbx *x, FILE *out, long limit)
{
    int ncs = 0, pc, pstart = 0, pend, i;
    long steps = 0;

    X = x;
    O = out;
    err = 0;
    lvl = 0;
    gabs = 90;
    gmot = -1;
    gpl = 17;
    halted = 0;
    gcyc = 0;
    gret = 98;
    tool = -1;
    pos[0] = pos[1] = pos[2] = cz = cr = cq = cinit = 0;
    x->nmv = 0;
    x->mvunk = 0;
    known[0] = known[1] = known[2] = 0;
    nvac = 0;
    units = 0;
    for (i = 0; i < 1000; i++) com[i].vac = 1, com[i].v = 0;
    for (i = 0; i < MAXLVL * 34; i++) loc[i / 34][i % 34].vac = 1, loc[i / 34][i % 34].v = 0;

    pc = 0;
    pend = x->nst && x->st[0]->k == ST_PROG ? progend(0) : progend(-1);
    if (x->nst && x->st[0]->k == ST_PROG) pc = 1;

    while (!err) {
        fbs *s;
        if (pc >= pend) {
            if (ncs) {
                fbrep(x, "REFUSED", x->st[pend - 1]->line, "the subprogram ends without M99");
                x->stop = 1;
            } else tr("END    the program ends without M30\n");
            break;
        }
        if (++steps > limit) {
            fbrep(x, "STOP", x->st[pc]->line, "more than %ld blocks evaluated: an endless loop? (-limit)", limit);
            x->stop = 1;
            break;
        }
        s = x->st[pc];
        curline = s->line;
        cur = s;
        s->nexec++;
        if (s->bdel && x->bdelsw == 1) { pc++; continue; }
        switch (s->k) {
        case ST_ASSIGN:
            assign(s->lhs, s->e);
            pc++;
            if (halted) {
                tr("END    the alarm\n");
                return !x->stop;
            }
            break;
        case ST_IFTHEN:
            if (truth(s->e)) {
                s->ctrue++;
                assign(s->lhs, s->e2);
            } else s->cfalse++;
            pc++;
            break;
        case ST_IFGOTO:
        case ST_GOTO:
            if (s->k == ST_IFGOTO) {
                if (!truth(s->e)) {
                    s->cfalse++;
                    pc++;
                    break;
                }
                s->ctrue++;
            }
            {
                double t = num(s->lhs, 0);
                long n = (long)floor(t + 0.5);
                int j, to = -1;
                if (err) break;
                for (j = pc + 1; j < pend && to < 0; j++)
                    if (x->st[j]->n == n) to = j;
                for (j = pstart; j <= pc && to < 0; j++)
                    if (x->st[j]->n == n) to = j;
                if (to < 0) {
                    fail("REFUSED", "%sGOTO %.0f: no such sequence number in this program", "", (double)n);
                    break;
                }
                tr("L%-5d JUMP   GOTO %ld -> line %d\n", curline, n, x->st[to]->line);
                pc = to;
            }
            break;
        case ST_WHILE:
            if (truth(s->e)) s->ctrue++, pc++;
            else s->cfalse++, pc = s->pair + 1;
            break;
        case ST_DO:
            pc++;
            break;
        case ST_END:
            pc = s->pair;
            break;
        case ST_UNSUP:
            fbrep(x, "NOTE", s->line, "%s: output statement, not translated", s->text);
            pc++;
            break;
        case ST_PROG:
            pc++;
            break;
        case ST_NC: {
            double g = -1, m = -1, v;
            int j;
            for (j = 0; j < s->nw; j++) {
                if (s->w[j].l == 'G' && wval(s, j, &v) && (v == 65 || v == 66 || v == 67)) g = v;
                if (s->w[j].l == 'M' && wval(s, j, &v) && (v == 98 || v == 99 || v == 30 || v == 2)) m = v;
            }
            if (err) break;
            if (g == 66 || g == 67) {
                fail("REFUSED", "%sG66/G67 modal macro calls are not translated", "", 0);
                break;
            }
            if (g == 65 || m == 98) {
                double p = 0, l = 1;
                long o, cnt;
                int ip = find(s, 'P'), il = find(s, 'L'), at;
                frame *f;
                if (g == 65) ncout(s, "GPLABCDEFHIJKMQRSTUVWXYZ");
                else ncout(s, "MPL");
                if (ip < 0 || !wval(s, ip, &p)) {
                    fail("REFUSED", "%sa call without P", "", 0);
                    break;
                }
                if (il >= 0 && !wval(s, il, &l)) l = 1;
                o = (long)p;
                cnt = (long)l;
                if (m == 98 && x->m98split && o > 9999) {
                    cnt = o / 10000;
                    o %= 10000;
                }
                at = progat(o);
                if (at < 0) {
                    fail("REFUSED", "%sO%.0f is not in the input (give its file too)", "", (double)o);
                    break;
                }
                if (ncs == MAXCALL || (g == 65 && lvl + 1 >= MAXLVL)) {
                    fail("REFUSED", "%scalls nested too deep", "", 0);
                    break;
                }
                if (cnt < 1) { pc++; break; }
                f = &cs[ncs];
                f->ret = pc + 1;
                f->lvl = lvl;
                f->pstart = pstart;
                f->pend = pend;
                f->start = at + 1;
                f->left = cnt - 1;
                f->g65 = g == 65;
                if (f->g65) {
                    if (!args(s, f->args)) break;
                    lvl++;
                    memcpy(loc[lvl], f->args, sizeof f->args);
                    for (j = 1; j <= 33; j++)
                        if (!f->args[j].vac) x->vset[j] = 1;
                }
                ncs++;
                {
                    char b[300], t[48];
                    sprintf(b, "%s O%ld", f->g65 ? "G65" : "M98", o);
                    if (cnt > 1) { sprintf(t, " x%ld", cnt); strcat(b, t); }
                    if (f->g65)
                        for (j = 1; j <= 33; j++)
                            if (!f->args[j].vac && strlen(b) < 250) {
                                sprintf(t, " #%d=", j);
                                strcat(b, t);
                                fbfmt(t, f->args[j].v);
                                strcat(b, t);
                            }
                    tr("L%-5d CALL   %s\n", curline, b);
                }
                pstart = at;
                pend = progend(at);
                pc = f->start;
                break;
            }
            if (m == 99) {
                frame *f;
                ncout(s, "M");
                if (find(s, 'P') >= 0) {
                    fail("REFUSED", "%sM99 P (return to a sequence number) is not translated", "", 0);
                    break;
                }
                if (!ncs) {
                    fbrep(x, "NOTE", curline, "M99 in the main program repeats it on the control; evaluated once");
                    tr("END    M99 in the main program\n");
                    return !x->stop;
                }
                f = &cs[ncs - 1];
                if (f->left > 0) {
                    f->left--;
                    if (f->g65) memcpy(loc[lvl], f->args, sizeof f->args);
                    tr("L%-5d AGAIN  the call repeats\n", curline);
                    pc = f->start;
                    break;
                }
                tr("L%-5d RET\n", curline);
                if (f->g65) lvl--;
                pstart = f->pstart;
                pend = f->pend;
                pc = f->ret;
                ncs--;
                break;
            }
            ncout(s, "");
            modal(s);
            if (m == 30 || m == 2) {
                tr("END    M%.0f\n", m);
                return !x->stop;
            }
            pc++;
            break;
        }
        }
    }
    return !x->stop;
}
