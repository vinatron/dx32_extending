/* fbout.c - write the DX32 program (FB2DX.md phases 2 and 3).
   Phase 2: assignments, IF..THEN, and NC blocks with G0 G1 G17 G20 G21 G90 G91,
   X Y Z, F, S and the common M codes.  Phase 3: WHILE/DO/END, GOTO and IF..GOTO as
   labels (Ln, =Ln) and ?/! regions; the bracket structure of the output is checked
   before anything is written.  The output is only kept when nothing was refused. */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "fb2dx.h"

static const char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char bank2[] = "ACDFIJKRXYZ";     /* letters with indices 20-50 (LANGUAGE.md 3) */
static const char pool[] = "DCAKJIRF";          /* letters for FANUC variables and temporaries */

static fbx *X;
static FILE *W;
static int ok, curline, units, nassign, ng0z, nfix;
static char used[26][51];
static char map[1000][4];      /* commons #100-#999 */
static char lmap[6][34][4];    /* locals #1-#33 per G65 level (FB2DX.md 3.2) */
static int curlvl;             /* the level of the program being written */
static char temps[64][4];
static int ntemp, maxtemp;

static int lix(int c)
{
    const char *p = strchr(alpha, c);
    return p ? (int)(p - alpha) : 0;
}

static void fail(const char *fmt, ...)
{
    char m[300];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(m, fmt, ap);
    va_end(ap);
    fbrep(X, "REFUSED", curline, "%s", m);
    ok = 0;
}

/* the output is kept in memory until the structure is checked (3.10).  Special lines:
   MARK id   where a skip from region id must resume (not written)
   LAB id    the label of label region id (written Ln once n is known)
   JMP id    a jump to it (written =Ln) */
#define LN_MARK '\001'
#define LN_LAB  '\002'
#define LN_JMP  '\003'

static char **ln;
static int nln, cln;

static void addln(const char *t)
{
    if (nln == cln) {
        int nc = cln ? cln * 2 : 512;
        char **a = (char **)fbmalloc((unsigned)(nc * sizeof *a));
        if (nln) memcpy(a, ln, nln * sizeof *a);
        if (ln) free(ln);
        ln = a;
        cln = nc;
    }
    ln[nln++] = fbstrdup(t);
}

static void out(const char *fmt, ...)
{
    char b[480];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(b, fmt, ap);
    va_end(ap);
    addln(b);
}

static void special(int kind, int id)
{
    char b[16];
    sprintf(b, "%c%d", kind, id);
    addln(b);
}

#define MAXST 256

typedef struct fwd {    /* a ? region that closes just before statement t (FB2DX.md 3.10, 3.11):
                           kind 0: opened by statement i (a forward GOTO, an early M99);
                           kind 1: a guard ?F<EQ>0. opened just before statement i */
    int i, t, id, kind;
    int flag;           /* index in flg[], or -1: the jump leaves enclosing loops or regions */
} fwd;

static char flg[MAXST][4];      /* exit flags of the current program */
static int nflg;

typedef struct lab {    /* a label region: the label before statement t, jumps up to statement e */
    int t, e, loop;
    int line, lastjmp;  /* output lines, filled in while writing */
    int n;              /* the label number */
} lab;

static fwd fw[MAXST];
static lab lb[MAXST];
static int nfw, nlb, nreg;
static int ropen[2 * MAXST], rmark[2 * MAXST], rline[2 * MAXST];
static int stfw[2 * MAXST];     /* region id of the ? opened by each statement, -1 if none */

/* a DX32 number: always with a decimal point, up to 9 decimals */
static void dxnum(char *b, double v)
{
    char *p;
    if (v > -5e-10 && v < 5e-10) v = 0;
    sprintf(b, "%.9f", v);
    p = b + strlen(b) - 1;
    while (*p == '0') *p-- = 0;
    if (!strncmp(b, "0.", 2) && b[2]) memmove(b, b + 1, strlen(b));
    else if (!strncmp(b, "-0.", 3) && b[3]) memmove(b + 1, b + 2, strlen(b + 1));
}

static void take(int l, int i)
{
    used[lix(l)][i] = 1;
}

static int isb2(int l)
{
    return strchr(bank2, l) != 0;
}

/* a free DX32 variable from the pool */
static const char *alloc(char *name)
{
    int i, j;
    for (i = 0; pool[i]; i++)
        for (j = 20; j <= 50; j++)
            if (!used[lix(pool[i])][j]) {
                take(pool[i], j);
                sprintf(name, "%c%d", pool[i], j);
                return name;
            }
    fail("more variables than the DX32 pool holds (%d)", (int)(strlen(pool) * 31));
    strcpy(name, "D50");
    return name;
}

/* the carrier for a word value: the word's own letter (LANGUAGE.md 4, variable rule) */
static void carrier(char *name, int l)
{
    sprintf(name, "%c%d", l, isb2(l) ? 21 : 19);
}

static const char *tmp(void)
{
    if (ntemp == maxtemp) {
        if (maxtemp == 64) {
            fail("an expression needs more than 64 temporaries");
            return temps[63];
        }
        alloc(temps[maxtemp++]);
    }
    return temps[ntemp++];
}

/* ------------------------------------------------------------------ */
/* expressions                                                          */

static void xe(const fbn *e, char *b);

static const char *varname(const fbn *e)
{
    long n;
    if (e->k == EN_SYS) {
        fail("#_%s: system variables do not translate", e->name);
        return "D50";
    }
    if (e->a->k != EN_NUM) {
        fail("#[...]: an indirect variable does not translate (the DX32 has no indirection)");
        return "D50";
    }
    n = (long)e->a->v;
    if (n == 0) {
        fail("#0 (vacant): the DX32 has no vacant value");
        return "D50";
    }
    if (n >= 1000) {
        fail("#%ld: system variables do not translate", n);
        return "D50";
    }
    if (n < 34) {
        if (!lmap[curlvl][n][0]) alloc(lmap[curlvl][n]);
        return lmap[curlvl][n];
    }
    if (n < 100) {
        fail("#%ld does not exist", n);
        return "D50";
    }
    if (!map[n][0]) alloc(map[n]);
    return map[n];
}

static int simple(const fbn *e)
{
    return e->k == EN_NUM || (e->k == EN_VAR && e->a->k == EN_NUM);
}

/* e in a variable: its own name if it is one, else a temporary */
static const char *invar(const fbn *e)
{
    char b[420];
    const char *t;
    if (e->k == EN_VAR && e->a->k == EN_NUM) return varname(e);
    b[0] = 0;
    xe(e, b);
    t = tmp();
    out("%s=%s", t, b);
    return t;
}

/* FIX, FUP, ROUND from LSR, which rounds to nearest (FB2DX.md 3.3, fact 2) */
static const char *fixlike(int fn, const char *x)
{
    const char *a = tmp(), *r = tmp();
    nfix++;
    if (fn == FN_ROUND) out("%s=ABS(%s)+.5", a, x);
    else out("%s=ABS(%s)", a, x);
    out("%s=LSR(0,%s)", r, a);
    out("?%s<GT>%s", r, a);
    out("%s=%s-1.!", r, r);
    if (fn == FN_FUP) {
        out("?%s<LT>%s", r, a);
        out("%s=%s+1.!", r, r);
    }
    out("?%s<LT>0.", x);
    out("%s=-%s!", r, r);
    return r;
}

static void cat(char *b, const char *s)
{
    size_t n = strlen(b), m = strlen(s);
    if (n + m < 400) memcpy(b + n, s, m + 1);
    else fail("an expression too long for one DX32 block");
}

static void sub(const fbn *e, char *b)
{
    if (simple(e) || e->k == EN_FUN) xe(e, b);
    else {
        cat(b, "(");
        xe(e, b);
        cat(b, ")");
    }
}

static void fun(const fbn *e, char *b)
{
    static const char *const nm[] = { "", "SIN", "COS", "TAN", "ASN", "ACS", "ATN", "", "SQR", "ABS",
        "", "", "", "", "", "LGE", "E2X", "", "" };
    const char *x, *y, *r;
    switch (e->op) {
    case FN_SIN: case FN_COS: case FN_TAN: case FN_ACOS: case FN_ATAN:
    case FN_SQRT: case FN_ABS: case FN_LN: case FN_EXP:
        cat(b, nm[e->op]);
        cat(b, "(");
        xe(e->a, b);
        cat(b, ")");
        return;
    case FN_ASIN: {                     /* FANUC 270..90 with NAT=0, DX32 -90..90 */
        char t[420];
        t[0] = 0;
        xe(e->a, t);
        r = tmp();
        out("%s=ASN(%s)", r, t);
        if (X->par.nat != 1) {
            out("?%s<LT>0.", r);
            out("%s=%s+360.!", r, r);
        }
        cat(b, r);
        return;
    }
    case FN_ATAN2:                      /* ATAN[a]/[b]: 0..360 (NAT=0) or -180..180 */
        y = invar(e->a);
        x = invar(e->b);
        r = tmp();
        out("%s=90.", r);
        out("?%s<LT>0.", y);
        out("%s=270.!", r);
        out("?%s<NE>0.", x);
        out("%s=ATN(%s/%s)", r, y, x);
        out("?%s<LT>0.", x);
        out("%s=%s+180.!", r, r);
        out("?%s<LT>0.", r);
        out("%s=%s+360.!", r, r);
        out("!");
        if (X->par.nat == 1) {
            out("?%s<GT>180.", r);
            out("%s=%s-360.!", r, r);
        }
        cat(b, r);
        return;
    case FN_FIX: case FN_FUP: case FN_ROUND:
        cat(b, fixlike(e->op, invar(e->a)));
        return;
    case FN_POW:
        if (e->b->k == EN_NUM && e->b->v == floor(e->b->v) && e->b->v >= 0 && e->b->v <= 8) {
            int i, n = (int)e->b->v;
            if (!n) {
                cat(b, "1.");
                return;
            }
            x = invar(e->a);
            cat(b, "(");
            for (i = 0; i < n; i++) {
                if (i) cat(b, "*");
                cat(b, x);
            }
            cat(b, ")");
            return;
        }
        fail("POW with an exponent other than a whole constant 0-8 is not translated yet");
        return;
    default:
        fail("%s does not translate", e->op == FN_BIN ? "BIN" : e->op == FN_BCD ? "BCD" : "ADP");
    }
}

static void xe(const fbn *e, char *b)
{
    char t[48];
    const char *x, *y, *f;
    switch (e->k) {
    case EN_NUM:
        dxnum(t, e->v);
        cat(b, t);
        return;
    case EN_VAR:
    case EN_SYS:
        cat(b, varname(e));
        return;
    case EN_NEG:
        cat(b, "-");
        sub(e->a, b);
        return;
    case EN_FUN:
        fun(e, b);
        return;
    case EN_CMP:
    case EN_LOG:
        fail("a condition used as a value");
        return;
    }
    switch (e->op) {
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        sub(e->a, b);
        cat(b, e->op == OP_ADD ? "+" : e->op == OP_SUB ? "-" : e->op == OP_MUL ? "*" : "/");
        sub(e->b, b);
        return;
    case OP_AND: case OP_OR: case OP_XOR:
        cat(b, e->op == OP_AND ? "AND(" : e->op == OP_OR ? "LOR(" : "XOR(");
        xe(e->a, b);
        cat(b, ",");
        xe(e->b, b);
        cat(b, ")");
        return;
    case OP_MOD:                        /* a - b*FIX[a/b] */
        x = invar(e->a);
        y = invar(e->b);
        {
            const char *q = tmp();
            out("%s=%s/%s", q, x, y);
            f = fixlike(FN_FIX, q);
        }
        cat(b, "(");
        cat(b, x);
        cat(b, "-");
        cat(b, y);
        cat(b, "*");
        cat(b, f);
        cat(b, ")");
        return;
    }
}

/* ------------------------------------------------------------------ */
/* conditions: each leaves exactly one '?' open, closed later by one '!' */

static const char *const cmpn[] = { "", "EQ", "NE", "GT", "GE", "LT", "LE" };
static const int mirror[] = { 0, CM_EQ, CM_NE, CM_LT, CM_LE, CM_GT, CM_GE };

static const int negate[] = { 0, CM_NE, CM_EQ, CM_LE, CM_LT, CM_GE, CM_GT };

static void opencmp(const fbn *c, int neg)
{
    const fbn *a = c->a, *bb = c->b;
    int op = neg ? negate[c->op] : c->op;
    char r[48];
    const char *l;
    if (a->k == EN_NUM && bb->k == EN_VAR && bb->a->k == EN_NUM) {     /* 5. LT #1 -> #1 GT 5. */
        const fbn *t = a;
        a = bb;
        bb = t;
        op = mirror[op];
    }
    l = invar(a);
    if (bb->k == EN_NUM) dxnum(r, bb->v);
    else strcpy(r, invar(bb));
    out("?%s<%s>%s", l, cmpn[op], r);
}

static const char *flag(const fbn *c)
{
    const char *f, *fa, *fb;
    if (c->k == EN_CMP) {
        f = tmp();
        out("%s=0.", f);
        opencmp(c, 0);
        out("%s=1.!", f);
        return f;
    }
    fa = flag(c->a);
    fb = flag(c->b);
    f = tmp();
    out("%s=0.", f);
    if (c->op == OP_AND) {
        out("?%s<EQ>1.", fa);
        out("?%s<EQ>1.", fb);
        out("%s=1.!", f);
        out("!");
    } else if (c->op == OP_OR) {
        out("?%s<EQ>1.", fa);
        out("%s=1.!", f);
        out("?%s<EQ>1.", fb);
        out("%s=1.!", f);
    } else {
        out("?%s<NE>%s", fa, fb);
        out("%s=1.!", f);
    }
    return f;
}

/* open a ? region that runs when c holds (neg = 0) or when it does not (neg = 1) */
static void opencond(const fbn *c, int neg)
{
    if (c->k == EN_CMP) opencmp(c, neg);
    else out("?%s<EQ>%s", flag(c), neg ? "0." : "1.");
}

/* ------------------------------------------------------------------ */
/* statements                                                           */

static void assign(const fbn *lhs, const fbn *e, int close)
{
    char b[420];
    const char *n;
    b[0] = 0;
    xe(e, b);
    n = varname(lhs);
    out("%s=%s%s", n, b, close ? "!" : "");
    nassign++;
}

static int whole(const fbn *e, double *v)
{
    if (e->k == EN_NEG && e->a->k == EN_NUM) *v = -e->a->v;
    else if (e->k == EN_NUM) *v = e->v;
    else return 0;
    return 1;
}

/* the least increment of a dimension word, for dpi=0 and ROUND in an address */
static double incr(int l)
{
    double d = X->par.is == 'c' ? 10.0 : 1.0;
    if (l == 'A' || l == 'B' || l == 'C') return 0.001 / d;
    return (units == 20 ? 0.0001 : 0.001) / d;
}

static char pre[FB_MAXW][420];    /* static: the DOS stack is small */

static int hasm(const fbs *s, double m)
{
    int i;
    double v;
    for (i = 0; i < s->nw; i++)
        if (s->w[i].l == 'M' && whole(s->w[i].e, &v) && v == m) return 1;
    return 0;
}

static int needpos, anyarc;     /* a G90 arc somewhere: keep the position in X20 Y20 Z20 */
static int anyhole;
static char cyz[4], cyr[4], cyi[4], cyq[4], cyp[4];    /* drilling: Z, R, initial level, Q, dwell */
static int curlab;              /* the label region of the statement being written (peck loops) */

/* the DX32 text of dimension word i: a number in the unit's format, or the word letter's
   carrier, assigned in pre[] (FB2DX.md 3.6).  0 when it can't be written. */
static int dimval(const fbs *s, int i, char *txt, int *npre)
{
    int l = s->w[i].l;
    const fbn *e = s->w[i].e;
    double v;
    char t[48];
    if (!units && l != 'F') units = X->par.units;
    if (!units && l != 'F') {
        fail("the unit system is not known: G20/G21 before this block, or -inch/-mm");
        return 0;
    }
    if (whole(e, &v)) {
        const fbn *n = e->k == EN_NEG ? e->a : e;
        int dec = units == 20 ? 4 : 3;
        double r;
        if (l != 'F' && !n->pt && X->par.dpi == 0) v *= incr(l);
        if (l == 'F') dec = 3;
        r = floor(fabs(v) * pow(10.0, dec) + 0.5) / pow(10.0, dec);
        if (v < 0) r = -r;
        if (fabs(r - v) > 1e-9) {
            fail("%c%g: more decimals than the DX32 takes (%d)", l, v, dec);
            return 0;
        }
        dxnum(txt, r);
        return 1;
    }
    {                                   /* a variable or an expression: the word letter's carrier */
        char b[420];
        carrier(txt, l);
        b[0] = 0;
        if (e->k == EN_FUN && e->op == FN_ROUND) {      /* ROUND in an address: to the increment */
            const char *q = tmp();
            char x[420];
            x[0] = 0;
            xe(e->a, x);
            dxnum(t, incr(l));
            out("%s=(%s)/%s", q, x, t);
            sprintf(b, "%s*%s", fixlike(FN_ROUND, q), t);
        } else xe(e, b);
        if (X->macroa && X->par.avar == 0 && l != 'F') {
            dxnum(t, incr(l));
            sprintf(pre[(*npre)++], "%s=(%s)*%s", txt, b, t);
        } else sprintf(pre[(*npre)++], "%s=%s", txt, b);
    }
    return 1;
}

/* a word from dimval()'s text: a number gets the letter, a carrier is the word itself */
static int ntlen, ntsel;        /* G43/G49 left out, T without M6 left out */

static int findg(const fbs *s, double g)
{
    int i;
    double v;
    for (i = 0; i < s->nw; i++)
        if (s->w[i].l == 'G' && whole(s->w[i].e, &v) && v == g) return 1;
    return 0;
}

static void word(char *blk, int l, const char *txt)
{
    if (txt[0] >= 'A' && txt[0] <= 'Z') strcat(blk, txt);
    else sprintf(blk + strlen(blk), "%c%s", l, txt);
}

static int findw(const fbs *s, int l)
{
    int i;
    for (i = 0; i < s->nw; i++)
        if (s->w[i].l == l) return i;
    return -1;
}

/* G4: FANUC G4 P<ms> or G4 X<s>; the DX32 sets the time with G4Q and dwells with a lone G4 */
static void dwell(const fbs *s)
{
    int ip = findw(s, 'P'), ix = findw(s, 'X'), i;
    double v, r;
    char t[48];
    for (i = 0; i < s->nw; i++)
        if (!strchr("GPX", s->w[i].l)) {
            fail("G4 with %c in the same block does not translate", s->w[i].l);
            return;
        }
    if (ip >= 0 && whole(s->w[ip].e, &v)) v /= 1000.0;
    else if (ix >= 0 && whole(s->w[ix].e, &v) && (s->w[ix].e->k == EN_NUM ? s->w[ix].e->pt : 1)) ;
    else {
        fail("G4 needs a constant P (ms) or X with a decimal point (s)");
        return;
    }
    r = floor(v * 100.0 + 0.5) / 100.0;
    if (fabs(r - v) > 1e-9 || r < .01 || r > 327.68) {
        fail("G4 of %g s: the DX32 dwell is .01-327.68 s in steps of .01", v);
        return;
    }
    dxnum(t, r);
    out("G4Q%s", t);
    out("G4");
}

/* a drilling cycle hole, written out as moves (FB2DX.md 3.12): G81 G82 G83 G73, G90 only */
static void hole(const fbs *s)
{
    static const char axes[] = "XYZ";
    char txt[48], blk[420], fw[48];
    int i, npre = 0, hasxy = 0, k;
    long m = s->modes;
    const char *ret;
    fw[0] = 0;
    blk[0] = 0;
    if (m & MO_INC) { fail("a drilling cycle in G91 does not translate yet"); return; }
    if ((m & MO_R98) && (m & MO_R99)) { fail("this hole runs with G98 and with G99"); return; }
    if (((m & MO_C81) != 0) + ((m & MO_C82) != 0) + ((m & MO_C83) != 0) + ((m & MO_C73) != 0) > 1) {
        fail("this block drills with more than one cycle in different passes");
        return;
    }
    if ((m & MO_CST) && (m & MO_UNKZ)) { fail("the cycle starts where Z is not known: no initial level"); return; }
    if ((m & MO_C83) && !X->par.hd83) { fail("G83: give the peck clearance with -fparam d83= (parameter 5115)"); return; }
    if ((m & MO_C73) && !X->par.hd73) { fail("G73: give the peck retract with -fparam d73= (parameter 5114)"); return; }
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        double v;
        if (l == 'G') {
            if (whole(s->w[i].e, &v) && (v == 90 || v == 98 || v == 99 || v == 17 || v == 81 || v == 82 ||
                v == 83 || v == 73 || v == 21 || v == 20)) {
                if (v == 20 || v == 21) units = (int)v;
                continue;
            }
            if (whole(s->w[i].e, &v)) fail("G%g in a drilling block does not translate", v);
            else fail("G with a variable value does not translate");
            return;
        }
        if (l == 'X' || l == 'Y') {
            if (!dimval(s, i, txt, &npre)) return;
            word(blk, l, txt);
            if (needpos) sprintf(pre[npre++], "%c20=%s", l, txt);
            hasxy = 1;
        } else if (l == 'Z' || l == 'R' || l == 'Q') {
            if (!dimval(s, i, txt, &npre)) return;
            sprintf(pre[npre++], "%s=%s", l == 'Z' ? cyz : l == 'R' ? cyr : cyq, txt);
        } else if (l == 'P') {
            if (!whole(s->w[i].e, &v) || v < 0) { fail("P (dwell) in a drilling block must be a constant"); return; }
            dxnum(txt, v / 1000.0);
            sprintf(pre[npre++], "%s=%s", cyp, txt);
        } else if (l == 'F') {
            if (!dimval(s, i, txt, &npre)) return;
            word(fw, 'F', txt);
        } else {
            fail("%c in a drilling block does not translate (K/L repeats among them)", l);
            return;
        }
    }
    /* the cycle variables first (a carrier may be among them), then the moves */
    if (m & MO_CST) out("%s=Z20", cyi);
    {
        int j = 0, n = npre;
        for (j = 0; j < n; j++)
            if (strncmp(pre[j], "X20=", 4) && strncmp(pre[j], "Y20=", 4)) out("%s", pre[j]);
    }
    if (hasxy) out("G0%s", blk);
    out("Z21=%s", cyr);
    out("G0Z21");
    if (m & (MO_C81 | MO_C82)) {
        out("Z21=%s", cyz);
        out("G1Z21%s", fw);
        if (m & MO_C82) {
            out("Q19=%s", cyp);
            out("G4Q19");
            out("G4");
        }
    } else {                            /* the pecks: a loop down from R */
        const char *dp = tmp(), *dn = tmp();
        char d[48];
        dxnum(d, (m & MO_C83) ? X->par.d83 : X->par.d73);
        out("%s=%s", dp, cyr);
        for (k = 0; k < nlb; k++)
            if (lb[k].loop == 2 && lb[k].t == curlab) break;
        lb[k].line = nln;
        special(LN_LAB, k);
        out("?%s<GT>%s", dp, cyz);
        out("%s=%s-%s", dn, dp, cyq);
        out("?%s<LT>%s", dn, cyz);
        out("%s=%s!", dn, cyz);
        if (m & MO_C83) {               /* back down to the last depth, less the clearance */
            out("?%s<LT>%s", dp, cyr);
            out("Z21=%s+%s", dp, d);
            out("G0Z21!");
        }
        out("Z21=%s", dn);
        out("G1Z21%s", fw);
        fw[0] = 0;
        if (m & MO_C83) out("Z21=%s", cyr);
        else out("Z21=%s+%s", dn, d);
        out("G0Z21");
        out("%s=%s", dp, dn);
        lb[k].lastjmp = nln;
        special(LN_JMP, k);
        out("!");
    }
    ret = (m & MO_R99) ? cyr : cyi;
    out("Z21=%s", ret);
    out("G0Z21");
    if (needpos) {
        for (i = 0; i < npre; i++)
            if (!strncmp(pre[i], "X20=", 4) || !strncmp(pre[i], "Y20=", 4)) out("%s", pre[i]);
        out("Z20=Z21");
    }
    (void)axes;
}

static void nc(const fbs *s)
{
    static const char axes[] = "XYZ", ctr[] = "IJK";
    char blk[420], t[48], txt[3][48], ctxt[3][48];
    int i, npre = 0, nm = 0, hasg0 = 0, hasz = 0, hasxy = 0, m98 = hasm(s, 98), g4 = 0, arc, plane, a;
    int hasax[3] = { 0, 0, 0 }, hasc[3] = { 0, 0, 0 }, hasr = 0;
    double v;
    blk[0] = 0;
    for (i = 0; i < s->nw; i++) {
        if (s->w[i].l != 'G' || !whole(s->w[i].e, &v)) continue;
        if (v == 66 || v == 67) {
            fail("G%.0f: modal macro calls do not translate", v);
            return;
        }
        if (v == 4) g4 = 1;
    }
    if (g4) {
        dwell(s);
        return;
    }
    if (s->modes & MO_HOLE) {
        if (s->modes & (MO_LIN | MO_ARC)) fail("this block drills in one pass and moves in another");
        else hole(s);
        return;
    }
    if (s->modes & MO_NOMOT) {
        fail("axis words with no G0/G1 in force (after G80, or before the first one): FANUC's motion there "
             "depends on the control; give G0 or G1");
        return;
    }
    if (s->modes & MO_HBAD) {
        fail("G43 H other than the tool in the spindle: the DX32 takes the length of the tool itself");
        return;
    }
    arc = (s->modes & MO_ARC) != 0;
    plane = s->modes & MO_P18 ? 18 : s->modes & MO_P19 ? 19 : 17;
    /* G codes first: G20/G21 decide how the dimension words in the block are written */
    for (i = 0; i < s->nw; i++) {
        int g;
        if (s->w[i].l != 'G') continue;
        if (!whole(s->w[i].e, &v) || v != floor(v)) {
            fail("G with a variable value does not translate");
            continue;
        }
        g = (int)v;
        if (g == 20 || g == 21) units = g;
        if (g == 43 || g == 49) {       /* tool length: the DX32 takes it from the tool table */
            ntlen++;
            continue;
        }
        if (g == 98 || g == 99) continue;   /* the return level of the holes (written by hole()) */
        if (!(g <= 3 || (g >= 17 && g <= 21) || (g >= 54 && g <= 59) || g == 80 || g == 90 || g == 91 ||
              g == 94)) {
            fail("G%d does not translate yet", g);
            continue;
        }
        if (g == 0) hasg0 = 1;
        sprintf(t, "G%d", g == 20 ? 70 : g == 21 ? 71 : g);
        cat(blk, t);
    }
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        const fbn *e = s->w[i].e;
        const char *p;
        if (l == 'G' || (m98 && (l == 'P' || l == 'L'))) continue;
        if (l == 'H' && findg(s, 43)) continue;
        if (l == 'T') {                 /* the DX32 takes T with M6 (written there) */
            if (!hasm(s, 6)) ntsel++;
            continue;
        }
        if (l == 'M' && whole(e, &v) && v == 6) {
            if (s->tool < 0) {
                fail(s->tool == -2 ? "M6 changes to different tools in different passes" :
                     "M6 not reached when evaluated: the tool is not known");
                continue;
            }
            if (++nm > 1) { fail("more than one M code in a block (the DX32 takes one)"); continue; }
            sprintf(t, "T%ldM6", s->tool);
            cat(blk, t);
            continue;
        }
        if (l == 'M') {
            if (!whole(e, &v)) { fail("M with a variable value does not translate"); continue; }
            if (v == 98 || v == 99) continue;   /* the call or the return: written by the caller of nc() */
            if (v != 0 && v != 1 && v != 2 && v != 3 && v != 4 && v != 5 && v != 7 && v != 8 && v != 9 &&
                v != 30) {
                fail("M%.0f does not translate yet", v);
                continue;
            }
            if (++nm > 1) { fail("more than one M code in a block (the DX32 takes one)"); continue; }
            sprintf(t, "M%.0f", v);
            cat(blk, t);
            continue;
        }
        if (l == 'S') {
            if (!whole(e, &v) || v != floor(v) || v < 0) { fail("S must be a whole constant here"); continue; }
            sprintf(t, "S%.0f", v);
            cat(blk, t);
            continue;
        }
        if ((p = strchr(axes, l)) != 0) {
            a = (int)(p - axes);
            if (!dimval(s, i, txt[a], &npre)) continue;
            hasax[a] = 1;
            if (l == 'Z') hasz = 1;
            else hasxy = 1;
            continue;
        }
        if ((p = strchr(ctr, l)) != 0 && arc) {
            a = (int)(p - ctr);
            if (dimval(s, i, ctxt[a], &npre)) hasc[a] = 1;
            continue;
        }
        if (l == 'R' && arc) {
            if (s->modes & MO_NEGR) { fail("R below 0 (the long arc): the DX32 R arc is always the short one"); continue; }
            if (dimval(s, i, t, &npre)) {
                hasr = 1;
                word(blk, 'R', t);
            }
            continue;
        }
        if (l == 'F') {
            if (dimval(s, i, t, &npre)) word(blk, 'F', t);
            continue;
        }
        fail("address %c does not translate yet", l);
    }
    if (arc) {                          /* FB2DX.md 3.12 */
        int pa = plane == 18 ? 2 : plane == 19 ? 1 : 0, pb = plane == 18 ? 0 : plane == 19 ? 2 : 1;
        int abs = (s->modes & MO_ABS) != 0;
        if (s->modes & MO_LIN) fail("this block runs as an arc and as a line in different passes");
        else if ((s->modes & MO_ABS) && (s->modes & MO_INC)) fail("this arc runs in G90 and in G91");
        else if ((s->modes & MO_P17 ? 1 : 0) + (s->modes & MO_P18 ? 1 : 0) + (s->modes & MO_P19 ? 1 : 0) > 1)
            fail("this arc runs in more than one plane");
        else if (hasax[3 - pa - pb]) fail("a helical arc (the third axis in an arc) does not translate");
        else if (hasr && (hasc[0] || hasc[1] || hasc[2])) fail("an arc with R and I/J/K does not translate");
        else if (!hasr && abs && (s->modes & MO_UNK))
            fail("a G90 arc where the position is not known yet: the DX32 needs the absolute centre");
        else if (!hasr) {
            int k;
            for (k = 0; k < 2; k++) {
                int c = k ? pb : pa;    /* FANUC I J K: from the start; DX32 in G90: absolute (fact 13) */
                char cn[8];
                carrier(cn, ctr[c]);
                if (abs) {
                    if (hasc[c])
                        sprintf(pre[npre++], "%s=%c20%s%s", cn, axes[c], ctxt[c][0] == '-' ? "" : "+", ctxt[c]);
                    else sprintf(pre[npre++], "%s=%c20", cn, axes[c]);
                    strcpy(ctxt[c], cn);
                } else if (!hasc[c]) strcpy(ctxt[c], "0.");
                word(blk, ctr[c], ctxt[c]);
            }
        }
        for (a = 0; a < 3; a++)
            if (hasc[a] && a != pa && a != pb) fail("%c is not a centre word in G%d", ctr[a], plane);
    }
    /* the axis words after the G codes, before the centre, as the DX32 manual writes them */
    t[0] = 0;
    for (a = 0; a < 3; a++)
        if (hasax[a]) word(t, axes[a], txt[a]);
    {
        char b2[420];
        size_t gl = 0;
        while (blk[gl] == 'G' || (blk[gl] >= '0' && blk[gl] <= '9')) gl++;
        sprintf(b2, "%.*s%s%s", (int)gl, blk, t, blk + gl);
        strcpy(blk, b2);
    }
    if (hasg0 && hasz && hasxy) ng0z++;
    for (i = 0; i < npre; i++) out("%s", pre[i]);
    if (blk[0]) out("%s", blk);
    /* the position, for the absolute arc centres */
    if (needpos && (hasax[0] || hasax[1] || hasax[2])) {
        if (!(s->modes & (MO_ABS | MO_INC))) {
            fail("not reached when evaluated: its G90/G91 state, which the arc centres need, is not known");
            return;
        }
        if ((s->modes & MO_ABS) && (s->modes & MO_INC)) {
            fail("this block moves in G90 and in G91 in different passes; the arc centres need one");
            return;
        }
        for (a = 0; a < 3; a++)
            if (hasax[a]) {
                if (s->modes & MO_ABS) out("%c20=%s", axes[a], txt[a]);
                else out("%c20=%c20%s%s", axes[a], axes[a], txt[a][0] == '-' ? "" : "+", txt[a]);
            }
    }
}

/* reads of variables the evaluation never set, in a statement it never reached */
static int unset(const fbn *e)
{
    if (!e) return 0;
    if (e->k == EN_VAR && e->a->k == EN_NUM) {
        long n = (long)e->a->v;
        if (n > 0 && n < 1000 && !X->vset[n]) return (int)n;
    }
    {
        int r = unset(e->a);
        return r ? r : unset(e->b);
    }
}

static int unsetst(const fbs *s)
{
    int i, r = unset(s->e);
    if (!r) r = unset(s->e2);
    for (i = 0; !r && i < s->nw; i++) r = unset(s->w[i].e);
    return r;
}

static void prescan(const fbn *e)
{
    if (!e) return;
    if (e->k == EN_VAR && e->a->k == EN_NUM) {
        long n = (long)e->a->v;
        if (n > 0 && n < 34 && !lmap[curlvl][n][0]) alloc(lmap[curlvl][n]);
        else if (n >= 100 && n < 1000 && !map[n][0]) alloc(map[n]);
        return;
    }
    prescan(e->a);
    prescan(e->b);
}

/* ------------------------------------------------------------------ */
/* phase 3: jumps and loops (FB2DX.md 3.5, 3.10)                        */

static int newreg(int line)
{
    if (nreg == 2 * MAXST) {
        fail("more than %d jumps and loops", 2 * MAXST);
        return 0;
    }
    ropen[nreg] = rmark[nreg] = -1;
    rline[nreg] = line;
    return nreg++;
}

static int newlab(int t, int e, int loop)
{
    if (nlb == MAXST) {
        fail("more than %d labels", MAXST);
        return 0;
    }
    lb[nlb].t = t;
    lb[nlb].e = e;
    lb[nlb].loop = loop;
    lb[nlb].line = lb[nlb].lastjmp = -1;
    lb[nlb].n = 0;
    return nlb++;
}

/* the statement a GOTO in statement i goes to, as the FANUC control searches: forward, then from the top */
static int target(fbx *x, int i, int s0, int end)
{
    const fbn *e = x->st[i]->lhs;
    long n;
    int j;
    if (e->k != EN_NUM) {
        fail("GOTO to a computed sequence number does not translate");
        return -1;
    }
    n = (long)e->v;
    for (j = i + 1; j < end; j++)
        if (x->st[j]->n == n) return j;
    for (j = s0; j <= i; j++)
        if (x->st[j]->n == n) return j;
    fail("GOTO %ld: no such sequence number in this program", n);
    return -1;
}

/* an M99 before the last block of a subprogram: an early return (a jump to its $) */
static int early99(fbx *x, int i, int end, int sub)
{
    return sub && x->st[i]->k == ST_NC && hasm(x->st[i], 99) && i < end - 1;
}

static int addfw(int i, int t, int kind, int flag, int line)
{
    if (nfw == MAXST) {
        fail("more than %d forward jumps", MAXST);
        return -1;
    }
    fw[nfw].i = i;
    fw[nfw].t = t;
    fw[nfw].kind = kind;
    fw[nfw].flag = flag;
    fw[nfw].id = newreg(line);
    return nfw++;
}

static int cmpint(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

/* find the jumps and loops of statements s0..end-1; sub: a called program */
static void structure(fbx *x, int s0, int end, int sub)
{
    static int jt[2 * MAXST];       /* the target of each jump statement, -1 if none */
    int i, j;
    nfw = nlb = nreg = nflg = 0;
    if (end - s0 > 2 * MAXST) {
        curline = x->st[s0]->line;
        fail("a program longer than %d blocks does not translate yet", 2 * MAXST);
        return;
    }
    /* the targets first: a jump's way out depends on the other jumps */
    for (i = s0; i < end; i++) {
        fbs *s = x->st[i];
        int t;
        jt[i - s0] = -1;
        stfw[i - s0] = -1;
        if (s->bdel && x->bdelsw == 1 &&
            (s->k == ST_GOTO || s->k == ST_IFGOTO || s->k == ST_WHILE || s->k == ST_DO || s->k == ST_END)) {
            curline = s->line;
            fail("a deleted (/) block that is part of a jump or a loop does not translate");
            continue;
        }
        if (s->k == ST_WHILE || s->k == ST_DO) newlab(i, s->pair, 1);
        if (s->k == ST_NC && (s->modes & (MO_C83 | MO_C73))) newlab(i, i, 2);  /* the peck loop */
        if (s->k != ST_GOTO && s->k != ST_IFGOTO && !early99(x, i, end, sub)) continue;
        curline = s->line;
        stfw[i - s0] = -3;              /* refused, until it is placed */
        t = s->k == ST_NC ? end : target(x, i, s0, end);
        if (t < 0) continue;
        for (j = s0; j < end; j++) {
            fbs *w = x->st[j];
            if ((w->k == ST_WHILE || w->k == ST_DO) && !(j < i && i < w->pair) && j < t && t <= w->pair) {
                fail("GOTO into a WHILE/DO loop (line %d); FANUC does not allow it either", w->line);
                t = -1;
                break;
            }
        }
        if (t < 0) continue;
        jt[i - s0] = t;
        if (t <= i) {                   /* backward: a label region */
            stfw[i - s0] = -1;
            for (j = 0; j < nlb; j++)
                if (!lb[j].loop && lb[j].t == t) break;
            if (j == nlb) newlab(t, i, 0);
            else if (lb[j].e < i) lb[j].e = i;
        }
    }
    /* forward jumps: where do the structures around the jump, but not around its target, end? */
    for (i = s0; i < end; i++) {
        int t = jt[i - s0], c[3 * MAXST], nc = 0, k, f;
        if (t <= i) continue;
        curline = x->st[i]->line;
        for (j = s0; j < end; j++) {
            int e;
            fbs *w = x->st[j];
            if (w->k == ST_WHILE || w->k == ST_DO) {            /* loops: they end after their END */
                e = w->pair;
                if (j < i && i < e && t > e) c[nc++] = e + 1;
            }
            if (jt[j - s0] > j && j < i && i < jt[j - s0] && jt[j - s0] < t)
                c[nc++] = jt[j - s0];                           /* other forward jumps' regions */
        }
        for (k = 0; k < nlb; k++)                               /* backward jumps: after the jump */
            if (!lb[k].loop && lb[k].t <= i && i < lb[k].e && t > lb[k].e) c[nc++] = lb[k].e + 1;
        for (k = 0; k < nc; k++)
            if (c[k] >= t) c[k--] = c[--nc];
        qsort(c, nc, sizeof c[0], cmpint);
        if (!nc) {                                              /* a plain region to the target */
            stfw[i - s0] = addfw(i, t, 0, -1, x->st[i]->line);
            continue;
        }
        if (nflg == MAXST) {
            fail("too many jumps out of loops");
            continue;
        }
        f = nflg++;
        alloc(flg[f]);
        stfw[i - s0] = addfw(i, c[0], 0, f, x->st[i]->line);
        for (k = 0; k < nc; k++) {
            if (k && c[k] == c[k - 1]) continue;
            addfw(c[k], k + 1 < nc ? c[k + 1] : t, 1, f, x->st[i]->line);
        }
    }
}

/* everything that happens just before statement p: regions that end here (innermost first),
   exit flags set back to 0, guards that open here (outermost first), labels (outermost first) */
static void atpos(int p)
{
    int k, best;
    for (;;) {
        best = -1;
        for (k = 0; k < nfw; k++)
            if (fw[k].t == p && rmark[fw[k].id] < 0 && ropen[fw[k].id] >= 0 &&
                (best < 0 || ropen[fw[k].id] > ropen[fw[best].id])) best = k;
        if (best < 0) break;
        out("!");
        rmark[fw[best].id] = nln;
        special(LN_MARK, fw[best].id);
    }
    for (k = 0; k < nfw; k++)                   /* a jump with a flag arrives: clear it */
        if (fw[k].flag >= 0 && fw[k].t == p && fw[k].kind == 1 &&
            (k + 1 == nfw || fw[k + 1].flag != fw[k].flag || fw[k + 1].kind == 0))
            out("%s=0.", flg[fw[k].flag]);
    for (;;) {
        best = -1;
        for (k = 0; k < nfw; k++)
            if (fw[k].kind == 1 && fw[k].i == p && ropen[fw[k].id] < 0 && (best < 0 || fw[k].t > fw[best].t))
                best = k;
        if (best < 0) break;
        out("?%s<EQ>0.", flg[fw[best].flag]);
        ropen[fw[best].id] = nln - 1;
    }
    for (;;) {
        best = -1;
        for (k = 0; k < nlb; k++)
            if (lb[k].t == p && lb[k].loop != 2 && lb[k].line < 0 && (best < 0 || lb[k].e > lb[best].e)) best = k;
        if (best < 0) break;
        lb[best].line = nln;
        special(LN_LAB, best);
    }
}

/* the ? of a forward jump (or an early M99) in statement s */
static void openjump(const fbs *s, int f)
{
    fwd *w = &fw[f];
    if (w->flag < 0) {
        if (s->k == ST_IFGOTO) opencond(s->e, 1);
        else {
            const char *z = tmp();
            out("%s=0.", z);
            out("?%s<EQ>1.", z);
        }
    } else {                                    /* set the exit flag, then skip while it is set */
        if (s->k == ST_IFGOTO) {
            opencond(s->e, 0);
            out("%s=1.!", flg[w->flag]);
        } else out("%s=1.", flg[w->flag]);
        out("?%s<EQ>0.", flg[w->flag]);
    }
    ropen[w->id] = nln - 1;
}

/* lines that open (?) and close (!) a skip */
static int opens(const char *l)
{
    return l[0] == '?';
}

static int closes(const char *l)
{
    size_t n = strlen(l);
    return n && l[n - 1] == '!';
}

/* check the written structure: every skip resumes where the FANUC jump goes, every
   label is defined before its jumps can run, labels nest; then number the labels */
static void check(void)
{
    int *match = (int *)fbmalloc((unsigned)(nln * sizeof(int)));
    int *stk = (int *)fbmalloc((unsigned)(nln * sizeof(int)));
    int i, j, sp = 0;
    for (i = 0; i < nln; i++) {
        match[i] = -1;
        if (opens(ln[i])) stk[sp++] = i;
        if (closes(ln[i])) {
            if (!sp) {
                fail("internal: a ! without a ?");
                break;
            }
            sp--;
            match[stk[sp]] = i;
            match[i] = stk[sp];
        }
    }
    if (sp) fail("internal: a ? without a !");
    for (j = 0; j < nreg && ok; j++) {
        int o = ropen[j], c = o >= 0 ? match[o] : -1, m = rmark[j], k;
        curline = rline[j];
        if (o < 0 || m < 0 || c < 0) {
            fail("this jump crosses a loop or another jump; it cannot be written with ?/!");
            continue;
        }
        /* the skip resumes after line c, the jump goes to m: only no-op lines (!) may lie between */
        {
            int lo = c + 1 < m ? c + 1 : m, hi = c + 1 < m ? m : c + 1;
            for (k = lo; k < hi; k++)
                if (strcmp(ln[k], "!") && ln[k][0] != LN_MARK) break;
            if (k < hi) k = -1;
        }
        if (k < 0) {
            fail("this jump leaves a loop to a block other than the one right after its END; not translated yet");
            continue;
        }
        /* a skip over a label whose jumps come after the skip: the label would not be set */
        for (k = 0; k < nlb; k++)
            if (lb[k].line > o && lb[k].line < c && lb[k].lastjmp > c) {
                fail("this jump goes into the middle of a loop");
                break;
            }
    }
    /* labels must nest; the number is the nesting depth (L1 outermost) */
    for (i = 0; i < nlb && ok; i++) {
        int d = 1;
        curline = 0;
        for (j = 0; j < nlb; j++) {
            if (j == i) continue;
            if (lb[j].line < lb[i].line && lb[j].lastjmp > lb[i].lastjmp) d++;
            else if ((lb[j].line < lb[i].line && lb[j].lastjmp > lb[i].line && lb[j].lastjmp < lb[i].lastjmp) ||
                     (lb[i].line < lb[j].line && lb[i].lastjmp > lb[j].line && lb[i].lastjmp < lb[j].lastjmp)) {
                curline = 0;
                fail("two loops or backward jumps overlap without one containing the other");
                break;
            }
        }
        if (d > 7) fail("loops and backward jumps nested %d deep: the DX32 has L1-L7 (L8, L9 belong to the cycles)", d);
        lb[i].n = d;
    }
    free(match);
    free(stk);
}

/* ------------------------------------------------------------------ */
/* phase 4: calls (FB2DX.md 3.4, 3.11)                                  */

#define MAXINST 99
#define MAXCALL 512

typedef struct inst {   /* one FANUC program at one local level: one DX32 #n macro */
    int p;              /* its O statement, -1 for the main program */
    int s0, end, lvl;
    int num;            /* the DX32 macro number; 0 for the main program */
    int base;           /* labels in it start above this */
    int depth;          /* call depth */
    int indeg, done;
    char **txt;
    int ntxt;
} inst;

static inst ins[MAXINST + 1];
static int nins, maxl;
static int eto[MAXCALL], efrom[MAXCALL], nedge;
static int cto[MAXCALL], cln2[MAXCALL], ncl;     /* calls written in the current instance */

/* is s a call?  1 = yes (G65 or M98 with a constant P), 0 = no; refusals are reported */
static int callinfo(fbx *x, const fbs *s, int *g65, long *o, long *cnt)
{
    int i, ip = -1, il = -1, g = 0, m = 0;
    double v;
    if (s->k != ST_NC) return 0;
    for (i = 0; i < s->nw; i++) {
        if (s->w[i].l == 'G' && whole(s->w[i].e, &v) && v == 65) g = 1;
        if (s->w[i].l == 'M' && whole(s->w[i].e, &v) && v == 98) m = 1;
        if (s->w[i].l == 'P') ip = i;
        if (s->w[i].l == 'L') il = i;
    }
    if (!g && !m) return 0;
    curline = s->line;
    if (ip < 0 || !whole(s->w[ip].e, &v) || v != floor(v) || v < 0) {
        fail("a call needs a constant program number P");
        return 0;
    }
    *o = (long)v;
    *cnt = 1;
    if (il >= 0) {
        if (!whole(s->w[il].e, &v) || v != floor(v) || v < 0) {
            fail("a call with a computed repeat count L does not translate");
            return 0;
        }
        *cnt = (long)v;
    }
    if (m && x->m98split && *o > 9999) {
        *cnt = *o / 10000;
        *o %= 10000;
    }
    if (*cnt > 99) {
        fail("a call repeated more than 99 times does not translate yet");
        return 0;
    }
    *g65 = g;
    return 1;
}

static int progat(fbx *x, long o)
{
    int i;
    for (i = 0; i < x->nst; i++)
        if (x->st[i]->k == ST_PROG && x->st[i]->prog == o) return i;
    return -1;
}

static int progend(fbx *x, int start)
{
    int i;
    for (i = start + 1; i < x->nst; i++)
        if (x->st[i]->k == ST_PROG) return i;
    return x->nst;
}

static int instof(fbx *x, int p, int lvl)
{
    int i;
    for (i = 1; i < nins; i++)
        if (ins[i].p == p && ins[i].lvl == lvl) return i;
    if (nins > MAXINST) {
        fail("more than %d called programs (one DX32 macro per program and level)", MAXINST);
        return 0;
    }
    ins[nins].p = p;
    ins[nins].s0 = p + 1;
    ins[nins].end = progend(x, p);
    ins[nins].lvl = lvl;
    ins[nins].num = nins;
    ins[nins].base = ins[nins].depth = ins[nins].indeg = ins[nins].done = 0;
    ins[nins].txt = 0;
    ins[nins].ntxt = 0;
    return nins++;
}

/* find every program the main program calls, and at which level */
static void callgraph(fbx *x)
{
    int k, i;
    nedge = 0;
    for (k = 0; k < nins && ok; k++)
        for (i = ins[k].s0; i < ins[k].end; i++) {
            int g65, p, to, lvl;
            long o, cnt;
            if (!callinfo(x, x->st[i], &g65, &o, &cnt)) continue;
            p = progat(x, o);
            if (p < 0) {
                fail("O%ld is not in the input (give its file too)", o);
                continue;
            }
            lvl = ins[k].lvl + (g65 ? 1 : 0);
            if (lvl > 5) {
                fail("G65 nested more than 5 levels deep");
                continue;
            }
            to = instof(x, p, lvl);
            if (!ok) return;
            if (nedge == MAXCALL) {
                fail("more than %d calls", MAXCALL);
                return;
            }
            efrom[nedge] = k;
            eto[nedge++] = to;
            ins[to].indeg++;
        }
}

/* G65 arguments: the local number each word sets, specification I or II (FB2DX.md 3.4) */
static void argnos(const fbs *s, int *an)
{
    static const char spec1[] = "ABCIJKDEF H M   QRSTUVWXYZ";
    int i, cnt[3] = { 0, 0, 0 }, g = 0, last = -1, sp2;
    for (i = 0; i < s->nw; i++)
        if (s->w[i].l == 'I' || s->w[i].l == 'J' || s->w[i].l == 'K') cnt[s->w[i].l - 'I']++;
    sp2 = cnt[0] > 1 || cnt[1] > 1 || cnt[2] > 1;
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        const char *p;
        an[i] = 0;
        if (strchr("GLNOP", l)) continue;
        if (sp2 && (l == 'I' || l == 'J' || l == 'K')) {
            int c = l - 'I';
            if (c <= last) g++;
            last = c;
            an[i] = 4 + 3 * g + c;
        } else if ((p = strchr(spec1, l)) != 0) an[i] = (int)(p - spec1) + 1;
        if (an[i] > 33) an[i] = 0;
    }
}

static void callout(fbx *x, const fbs *s, int k)
{
    int g65, to, an[FB_MAXW], i, r;
    long o, cnt;
    double v;
    char b[420], t[48];
    if (!callinfo(x, s, &g65, &o, &cnt)) return;
    to = instof(x, progat(x, o), ins[k].lvl + (g65 ? 1 : 0));
    if (!g65) nc(s);                    /* the other words of an M98 block */
    argnos(s, an);
    for (r = 0; r < cnt; r++) {
        if (g65)                        /* the caller sets the arguments: * can't (FB2DX.md fact 1) */
            for (i = 0; i < s->nw; i++) {
                const char *nm;
                int l = s->w[i].l;
                if (!an[i]) continue;
                nm = lmap[curlvl + 1][an[i]];
                if (!nm[0]) continue;   /* the called program never reads it */
                if (whole(s->w[i].e, &v)) {
                    const fbn *n = s->w[i].e->k == EN_NEG ? s->w[i].e->a : s->w[i].e;
                    if (!n->pt && X->par.dpi == 0 && fbisdim(l)) v *= incr(l);
                    dxnum(t, v);
                    out("%s=%s", nm, t);
                } else {
                    b[0] = 0;
                    xe(s->w[i].e, b);
                    out("%s=%s", nm, b);
                }
            }
        if (ncl < MAXCALL) {
            cto[ncl] = to;
            cln2[ncl++] = nln;
        }
        out("=#%d", ins[to].num);
    }
}

/* write instance k into memory, check it, and number its labels */
static void emit(fbx *x, int k)
{
    inst *in = &ins[k];
    int i, j, sub = k > 0;
    curlvl = in->lvl;
    nln = 0;
    ncl = 0;
    structure(x, in->s0, in->end, sub);
    if (!sub && anyarc) out("G75");
    for (i = 0; i < nflg; i++) out("%s=0.", flg[i]);
    for (i = in->s0; i < in->end; i++) {
        fbs *s = x->st[i];
        int n, kk;
        curline = s->line;
        ntemp = 0;
        atpos(i);
        if (s->bdel && x->bdelsw == 1) continue;
        if (s->vacbad) {
            fail("uses a vacant variable where the DX32 would use 0 (it has no vacant values)");
            continue;
        }
        if (!s->nexec) {
            n = unsetst(s);
            if (n) {
                fail("not reached when evaluated, and reads #%d, which the run never set", n);
                continue;
            }
            fbrep(x, "NOTE", curline, "not reached when evaluated; translated as written");
        }
        switch (s->k) {
        case ST_ASSIGN:
            assign(s->lhs, s->e, 0);
            break;
        case ST_IFTHEN:
            opencond(s->e, 0);
            assign(s->lhs, s->e2, 1);
            break;
        case ST_NC: {
            int g65;
            long o, cnt;
            curlab = i;
            if (callinfo(x, s, &g65, &o, &cnt)) {
                callout(x, s, k);
                break;
            }
            if (sub && (hasm(s, 30) || hasm(s, 2))) {
                fail("M30/M2 in a called program does not translate");
                break;
            }
            if (hasm(s, 99) && !sub) {
                fail("M99 in the main program makes FANUC repeat it for ever; not translated");
                break;
            }
            nc(s);
            if (early99(x, i, in->end, sub) && stfw[i - in->s0] >= 0)
                openjump(s, stfw[i - in->s0]);     /* return early: skip to the $ */
            break;
        }
        case ST_WHILE:
            opencond(s->e, 0);
            j = newreg(s->line);
            ropen[j] = nln - 1;
            stfw[i - in->s0] = -2 - j;
            break;
        case ST_DO:
            break;
        case ST_END:
            for (kk = 0; kk < nlb; kk++)
                if (lb[kk].loop && lb[kk].t == s->pair) break;
            lb[kk].lastjmp = nln;
            special(LN_JMP, kk);
            if (x->st[s->pair]->k == ST_WHILE && stfw[s->pair - in->s0] <= -2) {
                j = -2 - stfw[s->pair - in->s0];
                out("!");
                rmark[j] = nln;
                special(LN_MARK, j);
            }
            break;
        case ST_GOTO:
        case ST_IFGOTO:
            if (stfw[i - in->s0] == -3) break;
            if (stfw[i - in->s0] >= 0) openjump(s, stfw[i - in->s0]);
            else {
                int tg = target(x, i, in->s0, in->end);
                for (kk = 0; kk < nlb; kk++)
                    if (!lb[kk].loop && lb[kk].t == tg) break;
                if (kk == nlb) { fail("internal: no label for this GOTO"); break; }
                if (s->k == ST_IFGOTO) opencond(s->e, 0);
                lb[kk].lastjmp = nln;
                special(LN_JMP, kk);
                if (s->k == ST_IFGOTO) out("!");
            }
            break;
        case ST_UNSUP:
            fail("%s does not translate", s->text);
            break;
        default:
            break;
        }
    }
    curline = 0;
    atpos(in->end);                         /* early M99s land on the $ */
    if (sub) out("$");
    if (!ok) return;
    check();
    if (!ok) return;
    /* labels of the callees start above the ones open here at the call */
    for (i = 0; i < ncl; i++) {
        int enc = 0;
        for (j = 0; j < nlb; j++)
            if (lb[j].line < cln2[i] && lb[j].lastjmp > cln2[i]) enc++;
        if (ins[cto[i]].base < in->base + enc) ins[cto[i]].base = in->base + enc;
        if (ins[cto[i]].depth < in->depth + 1) ins[cto[i]].depth = in->depth + 1;
    }
    for (j = 0; j < nlb; j++) {
        int n = in->base + lb[j].n;
        if (n > 7) {
            curline = x->st[lb[j].t]->line;
            fail("labels nested %d deep over the calls: the DX32 has L1-L7 (L8, L9 belong to the cycles)", n);
            return;
        }
        if (n > maxl) maxl = n;
    }
    in->txt = (char **)fbmalloc((unsigned)((nln + 1) * sizeof(char *)));
    in->ntxt = 0;
    for (i = 0; i < nln; i++) {
        char b[24];
        if (ln[i][0] == LN_MARK) continue;
        if (ln[i][0] == LN_LAB || ln[i][0] == LN_JMP) {
            sprintf(b, "%sL%d", ln[i][0] == LN_JMP ? "=" : "", in->base + lb[atoi(ln[i] + 1)].n);
            in->txt[in->ntxt++] = fbstrdup(b);
        } else in->txt[in->ntxt++] = fbstrdup(ln[i]);
    }
}

static void mapline(FILE *w, const char *head, char m[][4], int lo, int hi)
{
    char line[200], t[48];
    int j;
    line[0] = 0;
    for (j = lo; j <= hi; j++)
        if (m[j][0]) {
            sprintf(t, "%s#%d=%s", line[0] ? " " : "", j, m[j]);
            if (strlen(line) + strlen(t) > 66) {
                fprintf(w, "; %s%s\r\n", head, line);
                line[0] = 0;
                sprintf(t, "#%d=%s", j, m[j]);
            }
            strcat(line, t);
        }
    if (line[0]) fprintf(w, "; %s%s\r\n", head, line);
}

int fbwrite(fbx *x, const char *path)
{
    int i, j, k, g20 = 0, g21 = 0, nwritten = 0, mdepth = 0;
    char line[200];
    double v;

    X = x;
    ok = 1;
    units = nassign = ng0z = nfix = 0;
    ntemp = maxtemp = 0;
    nln = 0;
    maxl = 0;
    memset(used, 0, sizeof used);
    memset(map, 0, sizeof map);
    memset(lmap, 0, sizeof lmap);
    /* reserved: X20 Y20 Z20 (current position, phase 5) and the word carriers */
    for (i = 0; alpha[i]; i++) take(alpha[i], isb2(alpha[i]) ? 21 : 19);
    take('X', 20);
    take('Y', 20);
    take('Z', 20);

    /* the main program, then everything it calls */
    ins[0].p = -1;
    ins[0].s0 = x->nst && x->st[0]->k == ST_PROG ? 1 : 0;
    ins[0].end = progend(x, ins[0].s0 - 1);
    ins[0].lvl = ins[0].num = ins[0].base = ins[0].depth = ins[0].indeg = ins[0].done = 0;
    ins[0].txt = 0;
    ins[0].ntxt = 0;
    nins = 1;
    callgraph(x);

    /* one unit system for the whole program, unless it changes */
    for (i = 0; i < x->nst; i++)
        for (j = 0; j < x->st[i]->nw; j++)
            if (x->st[i]->w[j].l == 'G' && whole(x->st[i]->w[j].e, &v)) {
                if (v == 20) g20 = 1;
                if (v == 21) g21 = 1;
            }
    if (g20 != g21) x->par.units = g20 ? 20 : 21;
    else if (g20) fbrep(x, "NOTE", 0, "the program switches between G20 and G21");

    /* arcs: G75 (multi-quadrant) at the top; G90 arcs need the position kept in X20 Y20 Z20 */
    needpos = anyarc = anyhole = ntlen = ntsel = 0;
    for (k = 0; k < nins; k++)
        for (i = ins[k].s0; i < ins[k].end; i++) {
            if (x->st[i]->modes & MO_ARC) anyarc = 1;
            if ((x->st[i]->modes & MO_ARC) && (x->st[i]->modes & MO_ABS) && findw(x->st[i], 'R') < 0) needpos = 1;
            if (x->st[i]->modes & MO_HOLE) anyhole = 1;
            if ((x->st[i]->modes & MO_HOLE) && (x->st[i]->modes & MO_R98)) needpos = 1;
        }

    if (anyhole) {
        alloc(cyz);
        alloc(cyr);
        alloc(cyi);
        alloc(cyq);
        alloc(cyp);
    }

    /* the variables of each program at its level */
    for (k = 0; k < nins && ok; k++) {
        curlvl = ins[k].lvl;
        for (i = ins[k].s0; i < ins[k].end; i++) {
            fbs *s = x->st[i];
            prescan(s->lhs);
            prescan(s->e);
            prescan(s->e2);
            for (j = 0; j < s->nw; j++) prescan(s->w[j].e);
        }
    }

    /* callers before callees, so that each knows which labels are open at its calls */
    for (;;) {
        for (k = 0; k < nins; k++)
            if (!ins[k].done && !ins[k].indeg) break;
        if (k == nins || !ok) break;
        units = x->par.units;
        emit(x, k);
        ins[k].done = 1;
        nwritten++;
        if (ins[k].depth > mdepth) mdepth = ins[k].depth;
        for (i = 0; i < nedge; i++)
            if (efrom[i] == k) ins[eto[i]].indeg--;
    }
    if (ok && nwritten < nins) {
        curline = 0;
        fail("the programs call each other in a circle (recursion)");
    }
    if (ok && mdepth > 8) fail("calls nested %d deep: the DX32 (6.89) allows 8", mdepth);
    for (i = 0; i < nln; i++) free(ln[i]);
    nln = 0;

    if (ok) {
        W = fopen(path, "wb");
        if (!W) {
            fbrep(x, "STOP", 0, "cannot write %s", path);
            ok = 0;
        } else {
            fbsnames(line, x->cand);
            fprintf(W, "; fb2dx: %s, FANUC series %s, for the DX32 (BMDC 6.89)\r\n", x->fname[0], line);
            mapline(W, "", lmap[0], 1, 33);
            mapline(W, "", map, 100, 999);
            for (k = 1; k <= 5; k++) {
                sprintf(line, "level %d: ", k);
                mapline(W, line, lmap[k], 1, 33);
            }
            for (k = 1; k < nins; k++) {        /* the called programs, as macros, first */
                fprintf(W, "#%d; O%ld\r\n", ins[k].num, x->st[ins[k].p]->prog);
                for (i = 0; i < ins[k].ntxt; i++) fprintf(W, "%s\r\n", ins[k].txt[i]);
            }
            for (i = 0; i < ins[0].ntxt; i++) fprintf(W, "%s\r\n", ins[0].txt[i]);
            fclose(W);
        }
    }
    for (k = 0; k < nins; k++) {
        for (i = 0; i < ins[k].ntxt; i++) free(ins[k].txt[i]);
        if (ins[k].txt) free(ins[k].txt);
        ins[k].txt = 0;
        ins[k].ntxt = 0;
    }
    if (!ok) {
        remove(path);
        fbrep(x, "STOP", 0, "%s not written: see REFUSED above", path);
        x->stop = 1;
        return 0;
    }
    if (nassign) fbrep(x, "NOTE", 0, "the output assigns variables: the DX32 needs the PPPL option (param 285 bit 4)");
    if (nfix) fbrep(x, "NOTE", 0, "FIX/FUP/ROUND are built on LSR: exact below 4294967296 (TENTATIVE, bench/BFIX.TXT)");
    if (ng0z) fbrep(x, "NOTE", 0, "G0 with Z and X/Y: the DX32 moves Z first up or last down, FANUC all at once");
    if (anyhole) fbrep(x, "NOTE", 0, "drilling cycles are written out as moves; cycle values in %s %s %s %s %s",
                       cyz, cyr, cyi, cyq, cyp);
    if (ntlen) fbrep(x, "NOTE", 0, "G43/G49 left out: the DX32 applies the tool length of the tool in the spindle "
                     "from its table (TENTATIVE)");
    if (ntsel) fbrep(x, "NOTE", 0, "T without M6 left out: the DX32 takes T in the M6 block");
    if (maxl) fbrep(x, "NOTE", 0, "loops and jumps use labels L1-L%d; do not call this from a loop that uses them", maxl);
    if (nins > 1) {
        fbrep(x, "NOTE", 0, "%d called program(s) written as DX32 macros #1-#%d, nested %d deep", nins - 1, nins - 1,
              mdepth);
        if (mdepth > 4) fbrep(x, "NOTE", 0, "more than the 4 macro levels the manuals promise (6.89 allows 8)");
    }
    fbrep(x, "NOTE", 0, "the output needs BOSS 4-7 compatibility mode off");
    fbrep(x, "WROTE", 0, "%s", path);
    return 1;
}
