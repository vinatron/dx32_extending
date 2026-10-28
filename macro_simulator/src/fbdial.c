/* fbdial.c - the FANUC series table and series detection (FB2DX.md 3.7).
   Every cell is TENTATIVE until checked against that series' manual (FB2DX.md Q6).
   A cell is corrected here and nowhere else. */

#include <stdlib.h>
#include <string.h>
#include "fb2dx.h"

static const struct { const char *key, *ctrl, *macro; } sers[FB_NSER] = {
    { "a",   "Custom Macro A on any series that has it (0, 6, 10/11, 15, early 16/18)", "Macro A (G65 Hm)" },
    { "0",   "Series 0 (0-M, 0-T, 0-MC, 0-TC, 0-MF), Power Mate", "Macro B" },
    { "6",   "Series 6 (6M, 6T)", "Macro B" },
    { "10",  "Series 10, 11, 12", "Macro B" },
    { "15",  "Series 15, 150", "Macro B" },
    { "16",  "Series 16, 18, 20, 21, 160/180/210, i and is models, Power Mate i", "Macro B" },
    { "0i",  "0i-A to 0i-D, 0i Mate", "Macro B" },
    { "30i", "30i, 31i, 32i, 35i (A and B), 0i-F, 0i-F Plus", "Macro B, newer" }
};

/* what exists where: y yes, n no, ? not known, o option, - not this macro type.
   Columns in the order of sers[]. */
static const struct { const char *name, *shrt, *cell; } feats[FB_NFEAT] = {
    { "Macro B statements and expressions (#i=, [ ], IF, WHILE, functions)", "Macro B", "-yyyyyyy" },
    { "IF [c] THEN", "THEN", "-nn??yyy" },
    { "ASIN ACOS", "ASIN/ACOS", "-nn???yy" },
    { "LN EXP", "LN/EXP", "-nn????y" },
    { "POW, ADP, ATAN[a,b], ATAN[x], MOD", "POW/ADP/ATAN[a,b]/ATAN[x]/MOD", "-nnnnn?y" },
    { "AND OR between conditions", "AND/OR of conditions", "-nnnnn?y" },
    { "named system variables #_NAME", "#_NAME", "-nnnnn?y" },
    { "common variables beyond #100-#149, #500-#531", "more common variables", "-oo??ooy" }
};

static const char *const means[FB_NMEAN] = {
    "a number without a decimal point in a dimension word",
    "ATAN/ASIN/ACOS result range",
    "ROUND inside an address",
    "G65 with H: Macro A operation or Macro B argument #11",
    "M98 P with more than 4 digits",
    "a variable in a dimension word under Macro A"
};

/* series where ATAN's range is set by parameter NAT (6004 bit 0): 0i and 30i, TENTATIVE */
#define NATSER  ((1 << FS_0I) | (1 << FS_30I))
/* series where ROUND in an address rounds to the least increment: 16, 0i, 30i; others not known */
#define RNDSER  ((1 << FS_16) | (1 << FS_0I) | (1 << FS_30I))
/* series where M98 P > 4 digits = repetitions (4 digits) and program (4 digits); 30i also needs o8=0 */
#define M98SER  ((1 << FS_0) | (1 << FS_16) | (1 << FS_0I) | (1 << FS_30I))

int fbsfind(const char *key)
{
    int i;
    for (i = 0; i < FB_NSER; i++)
        if (!strcmp(sers[i].key, key)) return i;
    return -1;
}

const char *fbskey(int ser)
{
    return ser >= 0 && ser < FB_NSER ? sers[ser].key : "?";
}

void fbslist(FILE *fp)
{
    int i, j;
    fprintf(fp, "FANUC series (-series KEY); every cell is TENTATIVE (features/FB2DX.md 3.7, Q6)\n\n");
    for (i = 0; i < FB_NSER; i++) fprintf(fp, "  %-4s %-17s %s\n", sers[i].key, sers[i].macro, sers[i].ctrl);
    fprintf(fp, "\nwhat exists where (y yes, n no, ? not known, o option, - other macro type):\n\n  %-31s", "");
    for (i = 0; i < FB_NSER; i++) fprintf(fp, "%-4s", sers[i].key);
    fprintf(fp, "\n");
    for (j = 0; j < FB_NFEAT; j++) {
        fprintf(fp, "  %-31s", feats[j].shrt);
        for (i = 0; i < FB_NSER; i++) fprintf(fp, "%c   ", feats[j].cell[i]);
        fprintf(fp, "\n");
    }
    fprintf(fp, "\nmeaning that changes by series or parameter (-fparam):\n");
    fprintf(fp, "  dpi=0|1   numbers without a point: least increments (0) or whole units (1); parameter DPI\n");
    fprintf(fp, "  is=b|c    increment system for dpi=0: IS-B 0.001 mm / 0.0001 in, IS-C ten times finer\n");
    fprintf(fp, "  nat=0|1   ATAN 0..360 (0) or -180..180 (1) on 0i/30i; parameter NAT\n");
    fprintf(fp, "  o8=0|1    30i: 8-digit program numbers, so M98 P is the program and L the count\n");
    fprintf(fp, "  avar=0|1  Macro A: a variable in an address is in least increments (0) or units (1);\n");
    fprintf(fp, "            not a parameter, not covered by -fparam default: check the Macro A manual\n");
    fprintf(fp, "  d83=N     G83 clearance before each peck (parameter 5115), in program units\n");
    fprintf(fp, "  d73=N     G73 retract after each peck (parameter 5114), in program units\n");
    fprintf(fp, "  default   the factory settings: dpi=0 is=b nat=0 o8=0 (not d83/d73: set per machine)\n");
}

void fbfeat(fbx *x, int f, int line)
{
    if (!x->featl[f]) x->featl[f] = line;
}

void fbmean(fbx *x, int m, int line)
{
    if (!x->meanl[m]) x->meanl[m] = line;
}

int fbfparam(fbpar *p, const char *a)
{
    const char *v = strchr(a, '=');
    if (!strcmp(a, "default")) {
        p->dflt = 1;
        if (p->dpi < 0) p->dpi = 0;
        if (p->is < 0) p->is = 'b';
        if (p->nat < 0) p->nat = 0;
        if (p->o8 < 0) p->o8 = 0;
        return 1;
    }
    if (!v) return 0;
    if (!strncmp(a, "d83=", 4) || !strncmp(a, "d73=", 4)) {
        double d = atof(v + 1);
        if (d < 0 || (!d && v[1] != '0' && v[1] != '.')) return 0;
        if (a[1] == '8') p->d83 = d, p->hd83 = 1;
        else p->d73 = d, p->hd73 = 1;
        return 1;
    }
    if (!strncmp(a, "is=", 3) && (v[1] == 'b' || v[1] == 'c' || v[1] == 'B' || v[1] == 'C') && !v[2]) {
        p->is = v[1] | 0x20;
        return 1;
    }
    if ((v[1] != '0' && v[1] != '1') || v[2]) return 0;
    if (!strncmp(a, "dpi=", 4)) p->dpi = v[1] - '0';
    else if (!strncmp(a, "nat=", 4)) p->nat = v[1] - '0';
    else if (!strncmp(a, "o8=", 3)) p->o8 = v[1] - '0';
    else if (!strncmp(a, "avar=", 5)) p->avar = v[1] - '0';
    else return 0;
    return 1;
}

static int exists(int f, int ser)
{
    char c = feats[f].cell[ser];
    return c == 'y' || c == '?' || c == 'o';
}

void fbsnames(char *buf, int mask)
{
    int i;
    buf[0] = 0;
    for (i = 0; i < FB_NSER; i++)
        if (mask & (1 << i)) {
            if (buf[0]) strcat(buf, " ");
            strcat(buf, sers[i].key);
        }
}

static int hasword(const fbs *s, int l)
{
    int i;
    for (i = 0; i < s->nw; i++)
        if (s->w[i].l == l) return i;
    return -1;
}

/* the value of word l if it is a whole constant, else -1 */
static double wconst(const fbs *s, int l)
{
    int i = hasword(s, l);
    if (i < 0 || s->w[i].e->k != EN_NUM) return -1;
    return s->w[i].e->v;
}

static int isg65h(const fbs *s)
{
    return s->k == ST_NC && wconst(s, 'G') == 65 && hasword(s, 'H') >= 0;
}

static int hasfn(const fbn *e, int fn)
{
    if (!e) return 0;
    if (e->k == EN_FUN && (e->op == fn || (fn == FN_ATAN && (e->op == FN_ATAN2 || e->op == FN_ASIN ||
        e->op == FN_ACOS)))) return 1;
    return hasfn(e->a, fn) || hasfn(e->b, fn);
}

static int hasvar(const fbn *e)
{
    if (!e) return 0;
    if (e->k == EN_VAR || e->k == EN_SYS) return 1;
    return hasvar(e->a) || hasvar(e->b);
}

/* a constant without a point: X10, X-10 */
static int nopoint(const fbn *e)
{
    if (e->k == EN_NEG) e = e->a;
    return e->k == EN_NUM && !e->pt && e->v != 0;   /* 0 is 0 in any unit */
}

static int needpar(fbx *x, int m, const char *what)
{
    fbrep(x, "STOP", x->meanl[m], "%s: give %s (fb2dx -series list)", means[m], what);
    x->stop = 1;
    return 0;
}

/* decide the series; record the meaning items and settle them.  0 = stop. */
int fbdetect(fbx *x)
{
    int f, i, mask = FS_ALL, ok = 1;
    char nm[64], why[240];

    /* G65 with H: decided first, because it decides what the other words are */
    for (i = 0; i < x->nst; i++)
        if (isg65h(x->st[i])) fbmean(x, FM_G65H, x->st[i]->line);

    why[0] = 0;
    for (f = 0; f < FB_NFEAT; f++) {
        int m = 0, s;
        if (!x->featl[f]) continue;
        for (s = 0; s < FB_NSER; s++)
            if (exists(f, s)) m |= 1 << s;
        if (x->ser >= 0 && !(m & (1 << x->ser))) {
            fbrep(x, "REFUSED", x->featl[f], "%s: not on series %s", feats[f].name, sers[x->ser].key);
            ok = 0;
        }
        if (!(mask & m) && x->ser < 0) {
            fbsnames(nm, mask);
            fbrep(x, "REFUSED", x->featl[f], "%s does not exist on the series the lines before need (%s)",
                  feats[f].name, nm);
            ok = 0;
        }
        mask &= m;
        if (m != FS_ALL && strlen(why) < 140) {
            char t[80];
            sprintf(t, "%s%s at line %d", why[0] ? ", " : "", feats[f].shrt, x->featl[f]);
            strcat(why, t);
        }
    }
    if (!ok) {
        x->stop = 1;
        return 0;
    }
    x->cand = x->ser >= 0 ? 1 << x->ser : mask;
    fbsnames(nm, x->cand);
    if (x->ser >= 0) fbrep(x, "DIALECT", 0, "series %s (given)", nm);
    else fbrep(x, "DIALECT", 0, "series %s (%s)", nm, why[0] ? why : "nothing series-specific used");

    /* Macro A or Macro B */
    x->macroa = x->cand == (1 << FS_A);
    if (x->meanl[FM_G65H] && (x->cand & (1 << FS_A)) && (x->cand & FS_MACB))
        return needpar(x, FM_G65H, "-series a for Macro A, or the Macro B series");
    if (x->macroa && !x->meanl[FM_G65H] && x->ser < 0) x->macroa = 0;

    /* the other meaning items */
    for (i = 0; i < x->nst; i++) {
        fbs *s = x->st[i];
        int j, ma = x->macroa && isg65h(s);
        if (s->k == ST_ASSIGN || s->k == ST_IFGOTO || s->k == ST_IFTHEN || s->k == ST_WHILE) {
            if (hasfn(s->e, FN_ATAN) || hasfn(s->e2, FN_ATAN)) fbmean(x, FM_ATAN, s->line);
        }
        if (s->k != ST_NC) continue;
        if (ma) {
            double h = wconst(s, 'H');
            if (h >= 31 && h <= 34) fbmean(x, FM_ATAN, s->line);
            continue;
        }
        for (j = 0; j < s->nw; j++) {
            fbn *e = s->w[j].e;
            if (hasfn(e, FN_ATAN)) fbmean(x, FM_ATAN, s->line);
            if (hasfn(e, FN_ROUND)) fbmean(x, FM_RNDADR, s->line);
            if (fbisdim(s->w[j].l) && nopoint(e)) fbmean(x, FM_DPI, s->line);
            if (x->macroa && fbisdim(s->w[j].l) && hasvar(e)) fbmean(x, FM_AVAR, s->line);
        }
        if (wconst(s, 'M') == 98 && wconst(s, 'P') > 9999) fbmean(x, FM_M98P, s->line);
    }

    if (x->meanl[FM_DPI]) {
        if (x->par.dpi < 0) return needpar(x, FM_DPI, "-fparam dpi=0|1, or -fparam default");
        if (x->par.dpi == 0 && x->par.is < 0) return needpar(x, FM_DPI, "-fparam is=b|c");
    }
    if (x->meanl[FM_ATAN] && (x->cand & NATSER)) {
        if (x->par.nat < 0) return needpar(x, FM_ATAN, "-fparam nat=0|1 (parameter NAT on 0i/30i)");
        if (x->par.nat == 1 && (x->cand & ~NATSER))
            return needpar(x, FM_ATAN, "-series: nat=1 applies on 0i/30i only");
    }
    if (x->meanl[FM_RNDADR]) {
        if (x->cand & ~RNDSER) return needpar(x, FM_RNDADR, "-series 16, 0i or 30i (not known for the others)");
        if (x->par.is < 0) return needpar(x, FM_RNDADR, "-fparam is=b|c");
    }
    if (x->meanl[FM_AVAR] && x->par.avar < 0)
        return needpar(x, FM_AVAR, "-fparam avar=0|1 from the Macro A manual of the control");
    if (x->meanl[FM_AVAR] && x->par.avar == 0 && x->par.is < 0)
        return needpar(x, FM_AVAR, "-fparam is=b|c");
    x->m98split = 0;
    if (x->meanl[FM_M98P]) {
        if (x->cand & ~M98SER) return needpar(x, FM_M98P, "-series 0, 16, 0i or 30i (not known for the others)");
        if (x->cand & (1 << FS_30I)) {
            if (x->par.o8 < 0) return needpar(x, FM_M98P, "-fparam o8=0|1 (30i program number length)");
            if (x->par.o8 == 1 && x->cand != (1 << FS_30I)) return needpar(x, FM_M98P, "-series 30i");
            x->m98split = x->par.o8 == 0;
        } else x->m98split = 1;
    }
    for (i = 0; i < FB_NMEAN; i++)
        if (x->meanl[i] && x->par.dflt) {
            fbrep(x, "NOTE", 0, "-fparam default: the factory settings are assumed");
            break;
        }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Macro A: G65 Hm P#i Q#j R#k becomes the same statements as Macro B   */

static fbn *mk(int k, int op, fbn *a, fbn *b)
{
    fbn *n = (fbn *)fbmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->k = k;
    n->op = op;
    n->a = a;
    n->b = b;
    return n;
}

static fbn *num(double v)
{
    fbn *n = mk(EN_NUM, 0, 0, 0);
    n->v = v;
    n->pt = 1;
    return n;
}

int fbmacroa(fbx *x)
{
    int i, ok = 1;
    if (!x->macroa) return 1;
    for (i = 0; i < x->nst; i++) {
        fbs *s = x->st[i];
        int h, ip, iq, ir;
        fbn *p, *q, *r;
        if (!isg65h(s)) continue;
        h = (int)wconst(s, 'H');
        ip = hasword(s, 'P');
        iq = hasword(s, 'Q');
        ir = hasword(s, 'R');
        p = ip >= 0 ? s->w[ip].e : 0;
        q = iq >= 0 ? s->w[iq].e : num(0);
        r = ir >= 0 ? s->w[ir].e : num(0);
        if (h >= 31 && h <= 34 && r->k == EN_NUM) {
            fbrep(x, "REFUSED", s->line, "Macro A: a constant angle; its scale is not known (FB2DX.md Q6)");
            ok = 0;
            continue;
        }
        if (h == 80 || (h >= 81 && h <= 86)) {
            if (!p) { fbrep(x, "REFUSED", s->line, "Macro A: H%d without P", h); ok = 0; continue; }
            s->lhs = p;
            if (h == 80) s->k = ST_GOTO;
            else {
                static const int cm[] = { CM_EQ, CM_NE, CM_GT, CM_LT, CM_GE, CM_LE };
                s->k = ST_IFGOTO;
                s->e = mk(EN_CMP, cm[h - 81], q, r);
            }
            continue;
        }
        if (!p || p->k != EN_VAR) {
            fbrep(x, "REFUSED", s->line, "Macro A: H%d needs P#i, the variable to set", h);
            ok = 0;
            continue;
        }
        s->lhs = p;
        switch (h) {
        case 1: s->e = q; break;
        case 2: s->e = mk(EN_BIN, OP_ADD, q, r); break;
        case 3: s->e = mk(EN_BIN, OP_SUB, q, r); break;
        case 4: s->e = mk(EN_BIN, OP_MUL, q, r); break;
        case 5: s->e = mk(EN_BIN, OP_DIV, q, r); break;
        case 11: s->e = mk(EN_BIN, OP_OR, q, r); break;
        case 12: s->e = mk(EN_BIN, OP_AND, q, r); break;
        case 13: s->e = mk(EN_BIN, OP_XOR, q, r); break;
        case 21: s->e = mk(EN_FUN, FN_SQRT, q, 0); break;
        case 22: s->e = mk(EN_FUN, FN_ABS, q, 0); break;
        case 23:        /* remainder: q - FIX[q/r]*r, TENTATIVE */
            s->e = mk(EN_BIN, OP_SUB, q, mk(EN_BIN, OP_MUL, mk(EN_FUN, FN_FIX, mk(EN_BIN, OP_DIV, q, r), 0), r));
            break;
        case 24: s->e = mk(EN_FUN, FN_BCD, q, 0); break;
        case 25: s->e = mk(EN_FUN, FN_BIN, q, 0); break;
        case 26: s->e = mk(EN_BIN, OP_DIV, mk(EN_BIN, OP_MUL, p, q), r); break;
        case 27:
            s->e = mk(EN_FUN, FN_SQRT, mk(EN_BIN, OP_ADD, mk(EN_BIN, OP_MUL, q, q), mk(EN_BIN, OP_MUL, r, r)), 0);
            break;
        case 28:
            s->e = mk(EN_FUN, FN_SQRT, mk(EN_BIN, OP_SUB, mk(EN_BIN, OP_MUL, q, q), mk(EN_BIN, OP_MUL, r, r)), 0);
            break;
        case 31: s->e = mk(EN_BIN, OP_MUL, q, mk(EN_FUN, FN_SIN, r, 0)); break;
        case 32: s->e = mk(EN_BIN, OP_MUL, q, mk(EN_FUN, FN_COS, r, 0)); break;
        case 33: s->e = mk(EN_BIN, OP_MUL, q, mk(EN_FUN, FN_TAN, r, 0)); break;
        case 34: s->e = mk(EN_FUN, FN_ATAN2, q, r); break;
        default:
            fbrep(x, "REFUSED", s->line, "Macro A: H%02d is %s", h, h == 99 ? "an alarm" : "not an operation");
            ok = 0;
            continue;
        }
        s->k = ST_ASSIGN;
    }
    if (!ok) x->stop = 1;
    return ok;
}
