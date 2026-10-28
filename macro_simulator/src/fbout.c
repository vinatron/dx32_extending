/* fbout.c - write the DX32 program (FB2DX.md phase 2: variables and expressions).
   Phase 2 writes straight-line programs: assignments, IF..THEN, and NC blocks with
   G0 G1 G17 G20 G21 G90 G91, X Y Z, F, S and the common M codes.  Anything else
   is refused with the phase that will translate it.  The output is only kept when
   nothing was refused. */

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
static char map[1000][4];
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

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(W, fmt, ap);
    va_end(ap);
    fputs("\r\n", W);
}

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

static void opencmp(const fbn *c)
{
    const fbn *a = c->a, *bb = c->b;
    int op = c->op;
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
        opencmp(c);
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

static void opencond(const fbn *c)
{
    if (c->k == EN_CMP) opencmp(c);
    else out("?%s<EQ>1.", flag(c));
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

static void nc(const fbs *s)
{
    char blk[420], t[48];
    int i, npre = 0, nm = 0, hasg0 = 0, hasz = 0, hasxy = 0;
    double v;
    blk[0] = 0;
    for (i = 0; i < s->nw; i++)                 /* calls: one refusal for the whole block */
        if (whole(s->w[i].e, &v) && ((s->w[i].l == 'G' && v >= 65 && v <= 67) ||
                                     (s->w[i].l == 'M' && (v == 98 || v == 99)))) {
            fail("%c%.0f: calls are translated in phase 4", s->w[i].l, v);
            return;
        }
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
        if (g == 65 || g == 66 || g == 67) { fail("G%d: calls are translated in phase 4", g); continue; }
        if (g != 0 && g != 1 && g != 17 && g != 20 && g != 21 && g != 90 && g != 91) {
            fail("G%d is translated in phase 5 (motion)", g);
            continue;
        }
        if (g == 0) hasg0 = 1;
        sprintf(t, "G%d", g == 20 ? 70 : g == 21 ? 71 : g);
        cat(blk, t);
    }
    for (i = 0; i < s->nw; i++) {
        int l = s->w[i].l;
        const fbn *e = s->w[i].e;
        if (l == 'G') continue;
        if (l == 'M') {
            if (!whole(e, &v)) { fail("M with a variable value does not translate"); continue; }
            if (v == 98 || v == 99) { fail("M%.0f: calls are translated in phase 4", v); continue; }
            if (v != 0 && v != 1 && v != 2 && v != 3 && v != 4 && v != 5 && v != 7 && v != 8 && v != 9 &&
                v != 30) {
                fail("M%.0f is translated in phase 5", v);
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
        if (l != 'X' && l != 'Y' && l != 'Z' && l != 'F') {
            fail("address %c is translated in phase 5 (motion)", l);
            continue;
        }
        if (l == 'Z') hasz = 1;
        else if (l != 'F') hasxy = 1;
        if (!units && l != 'F') units = X->par.units;
        if (!units && l != 'F') {
            fail("the unit system is not known: G20/G21 before this block, or -inch/-mm");
            continue;
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
                continue;
            }
            dxnum(t, r);
            sprintf(blk + strlen(blk), "%c%s", l, t);
            continue;
        }
        /* a variable or an expression: through a carrier of the word's own letter */
        {
            char cn[8], b[420];
            const fbn *ee = e;
            b[0] = 0;
            carrier(cn, l);
            if (e->k == EN_FUN && e->op == FN_ROUND) {      /* ROUND in an address: to the increment */
                const char *q = tmp();
                char x[420];
                x[0] = 0;
                xe(e->a, x);
                dxnum(t, incr(l));
                out("%s=(%s)/%s", q, x, t);
                sprintf(b, "%s*%s", fixlike(FN_ROUND, q), t);
            } else xe(ee, b);
            if (X->macroa && X->par.avar == 0 && l != 'F') {
                dxnum(t, incr(l));
                sprintf(pre[npre++], "%s=(%s)*%s", cn, b, t);
            } else sprintf(pre[npre++], "%s=%s", cn, b);
            sprintf(blk + strlen(blk), "%s", cn);
        }
    }
    if (hasg0 && hasz && hasxy) ng0z++;
    for (i = 0; i < npre; i++) out("%s", pre[i]);
    if (blk[0]) out("%s", blk);
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
        if (n > 0 && n < 1000 && !map[n][0]) alloc(map[n]);
        return;
    }
    prescan(e->a);
    prescan(e->b);
}

int fbwrite(fbx *x, const char *path)
{
    int i, j, end, nprog = 0;
    char line[200], t[48];

    X = x;
    ok = 1;
    units = nassign = ng0z = nfix = 0;
    ntemp = maxtemp = 0;
    memset(used, 0, sizeof used);
    memset(map, 0, sizeof map);
    /* reserved: X20 Y20 Z20 (current position, phase 5) and the word carriers */
    for (i = 0; alpha[i]; i++) take(alpha[i], isb2(alpha[i]) ? 21 : 19);
    take('X', 20);
    take('Y', 20);
    take('Z', 20);

    W = fopen(path, "wb");
    if (!W) {
        fbrep(x, "STOP", 0, "cannot write %s", path);
        x->stop = 1;
        return 0;
    }
    /* phase 2: the first program only */
    i = x->nst && x->st[0]->k == ST_PROG ? 1 : 0;
    for (end = i; end < x->nst && x->st[end]->k != ST_PROG; end++) {
        fbs *s = x->st[end];
        prescan(s->lhs);
        prescan(s->e);
        prescan(s->e2);
        for (j = 0; j < s->nw; j++) prescan(s->w[j].e);
    }
    for (j = end; j < x->nst; j++)
        if (x->st[j]->k == ST_PROG) nprog++;

    fbsnames(line, x->cand);
    out("; fb2dx: %s, FANUC series %s, for the DX32 (BMDC 6.89)", x->fname[0], line);
    line[0] = 0;
    for (j = 1; j < 1000; j++)
        if (map[j][0]) {
            sprintf(t, "%s#%d=%s", line[0] ? " " : "", j, map[j]);
            if (strlen(line) + strlen(t) > 70) {
                out("; %s", line);
                line[0] = 0;
                sprintf(t, "#%d=%s", j, map[j]);
            }
            strcat(line, t);
        }
    if (line[0]) out("; %s", line);

    for (; i < end; i++) {
        fbs *s = x->st[i];
        int n;
        curline = s->line;
        ntemp = 0;
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
            opencond(s->e);
            assign(s->lhs, s->e2, 1);
            break;
        case ST_NC:
            nc(s);
            break;
        case ST_UNSUP:
            fail("%s does not translate", s->text);
            break;
        default:
            fail("IF..GOTO, GOTO and WHILE/DO/END are translated in phase 3");
            break;
        }
    }
    fclose(W);
    curline = 0;
    if (nprog) fbrep(x, "NOTE", 0, "%d more program(s) after the first: called programs come with phase 4", nprog);
    if (!ok) {
        remove(path);
        fbrep(x, "STOP", 0, "%s not written: see REFUSED above", path);
        x->stop = 1;
        return 0;
    }
    if (nassign) fbrep(x, "NOTE", 0, "the output assigns variables: the DX32 needs the PPPL option (param 285 bit 4)");
    if (nfix) fbrep(x, "NOTE", 0, "FIX/FUP/ROUND are built on LSR: exact below 4294967296 (TENTATIVE, bench/BFIX.TXT)");
    if (ng0z) fbrep(x, "NOTE", 0, "G0 with Z and X/Y: the DX32 moves Z first up or last down, FANUC all at once");
    fbrep(x, "NOTE", 0, "the output needs BOSS 4-7 compatibility mode off");
    fbrep(x, "WROTE", 0, "%s", path);
    return 1;
}
