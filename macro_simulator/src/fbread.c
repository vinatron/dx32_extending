/* fbread.c - read FANUC programs into statements (FB2DX.md phase 1).
   A block is cleaned (comments out, upper case, no blanks), then parsed:
   macro statements (#i=, IF, GOTO, WHILE, DO, END) or an NC block of words.
   The constructs that exist on some series only are recorded for detection. */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fb2dx.h"

#define FB_LINE 512

typedef struct ps {
    const char *p;
    fbx *x;
    int line;
    char err[160];
} ps;

static fbn *cexpr(ps *s);
static fbn *unary(ps *s);

static fbn *node(int k)
{
    fbn *n = (fbn *)fbmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->k = k;
    return n;
}

static void perr(ps *s, const char *m)
{
    if (!s->err[0]) sprintf(s->err, "%.80s at \"%.20s\"", m, s->p);
}

static int kw(ps *s, const char *w)
{
    size_t n = strlen(w);
    if (strncmp(s->p, w, n)) return 0;
    s->p += n;
    return 1;
}

static int expect(ps *s, int c)
{
    if (*s->p == c) {
        s->p++;
        return 1;
    }
    {
        char m[40];
        sprintf(m, "'%c' expected", c);
        perr(s, m);
    }
    return 0;
}

static int isbool(const fbn *n)
{
    return n && (n->k == EN_CMP || n->k == EN_LOG);
}

/* letters that take a decimal point (distances, angles): FB2DX.md 3.7, TENTATIVE for E */
int fbisdim(int l)
{
    return l && strchr("XYZUVWABCIJKQRE", l) != 0;
}

static fbn *number(ps *s)
{
    char buf[40];
    int i = 0, pt = 0;
    fbn *n;
    while ((isdigit((unsigned char)*s->p) || *s->p == '.') && i < 38) {
        if (*s->p == '.') pt++;
        buf[i++] = *s->p++;
    }
    buf[i] = 0;
    if (pt > 1) perr(s, "two decimal points");
    n = node(EN_NUM);
    n->v = atof(buf);
    n->pt = pt;
    return n;
}

static int isnum(ps *s)
{
    return isdigit((unsigned char)*s->p) || (*s->p == '.' && isdigit((unsigned char)s->p[1]));
}

/* #n, #[e], #_NAME, #_NAME[e] */
static fbn *var(ps *s)
{
    fbn *n;
    s->p++;
    if (*s->p == '[') {
        s->p++;
        n = node(EN_VAR);
        n->a = cexpr(s);
        expect(s, ']');
        fbfeat(s->x, FF_MACB, s->line);
        return n;
    }
    if (*s->p == '_') {
        char buf[32];
        int i = 0;
        s->p++;
        while ((isalnum((unsigned char)*s->p) || *s->p == '_') && i < 30) buf[i++] = *s->p++;
        buf[i] = 0;
        n = node(EN_SYS);
        n->name = fbstrdup(buf);
        if (*s->p == '[') {
            s->p++;
            n->a = cexpr(s);
            expect(s, ']');
        }
        fbfeat(s->x, FF_NAMED, s->line);
        return n;
    }
    if (!isdigit((unsigned char)*s->p)) {
        perr(s, "variable number expected after #");
        return node(EN_NUM);
    }
    n = node(EN_VAR);
    n->a = number(s);
    if (n->a->pt) perr(s, "a variable number has no decimal point");
    {
        double v = n->a->v;
        if ((v >= 150 && v < 200) || (v >= 532 && v < 1000)) fbfeat(s->x, FF_COMEXT, s->line);
    }
    return n;
}

static const struct { const char *name; int fn, feat; } funcs[] = {
    { "ASIN", FN_ASIN, FF_ASIN }, { "ACOS", FN_ACOS, FF_ASIN }, { "ATAN", FN_ATAN, FF_MACB },
    { "SIN", FN_SIN, FF_MACB }, { "COS", FN_COS, FF_MACB }, { "TAN", FN_TAN, FF_MACB },
    { "SQRT", FN_SQRT, FF_MACB }, { "ABS", FN_ABS, FF_MACB }, { "BIN", FN_BIN, FF_MACB },
    { "BCD", FN_BCD, FF_MACB }, { "ROUND", FN_ROUND, FF_MACB }, { "FIX", FN_FIX, FF_MACB },
    { "FUP", FN_FUP, FF_MACB }, { "LN", FN_LN, FF_LNEXP }, { "EXP", FN_EXP, FF_LNEXP },
    { "POW", FN_POW, FF_NEW30 }, { "ADP", FN_ADP, FF_NEW30 }, { 0, 0, 0 }
};

static fbn *func(ps *s)
{
    int i;
    fbn *n;
    for (i = 0; funcs[i].name; i++) {
        size_t l = strlen(funcs[i].name);
        if (!strncmp(s->p, funcs[i].name, l) && s->p[l] == '[') break;
    }
    if (!funcs[i].name) {
        perr(s, "unknown function or a value expected");
        s->p += strlen(s->p);
        return node(EN_NUM);
    }
    s->p += strlen(funcs[i].name) + 1;
    fbfeat(s->x, FF_MACB, s->line);
    n = node(EN_FUN);
    n->op = funcs[i].fn;
    n->a = cexpr(s);
    if (n->op == FN_ATAN) {
        if (*s->p == ',') {                 /* ATAN[a,b]: 30i */
            s->p++;
            n->b = cexpr(s);
            expect(s, ']');
            n->op = FN_ATAN2;
            fbfeat(s->x, FF_NEW30, s->line);
        } else {
            expect(s, ']');
            if (s->p[0] == '/' && s->p[1] == '[') {     /* ATAN[a]/[b]: every Macro B */
                s->p += 2;
                n->b = cexpr(s);
                expect(s, ']');
                n->op = FN_ATAN2;
            } else fbfeat(s->x, FF_NEW30, s->line);     /* ATAN[x]: 30i */
        }
        return n;
    }
    fbfeat(s->x, funcs[i].feat, s->line);
    if (n->op == FN_POW) {
        expect(s, ',');
        n->b = cexpr(s);
    }
    expect(s, ']');
    return n;
}

static fbn *factor(ps *s)
{
    fbn *n;
    if (*s->p == '[') {
        s->p++;
        fbfeat(s->x, FF_MACB, s->line);
        n = cexpr(s);
        expect(s, ']');
        return n;
    }
    if (*s->p == '#') return var(s);
    if (isnum(s)) return number(s);
    if (isalpha((unsigned char)*s->p)) return func(s);
    perr(s, "a value expected");
    return node(EN_NUM);
}

static fbn *unary(ps *s)
{
    fbn *n;
    if (*s->p == '-') {
        s->p++;
        n = node(EN_NEG);
        n->a = unary(s);
        return n;
    }
    if (*s->p == '+') {
        s->p++;
        return unary(s);
    }
    return factor(s);
}

/* a and b joined by an arithmetic or a logical operator */
static fbn *join(ps *s, int op, fbn *a, fbn *b)
{
    fbn *n;
    if (isbool(a) != isbool(b)) {
        perr(s, "a condition and a value mixed in one expression");
        return a;
    }
    if (isbool(a)) {
        if (op != OP_AND && op != OP_OR && op != OP_XOR) {
            perr(s, "arithmetic on a condition");
            return a;
        }
        fbfeat(s->x, FF_LOGIC, s->line);
        n = node(EN_LOG);
    } else n = node(EN_BIN);
    n->op = op;
    n->a = a;
    n->b = b;
    return n;
}

static fbn *term(ps *s)
{
    fbn *n = unary(s);
    for (;;) {
        int op = 0;
        if (*s->p == '*') { s->p++; op = OP_MUL; }
        else if (*s->p == '/') { s->p++; op = OP_DIV; }
        else if (kw(s, "AND")) op = OP_AND;
        else if (kw(s, "MOD")) { op = OP_MOD; fbfeat(s->x, FF_NEW30, s->line); }
        if (!op) return n;
        n = join(s, op, n, unary(s));
    }
}

static fbn *expr(ps *s)
{
    fbn *n = term(s);
    for (;;) {
        int op = 0;
        if (*s->p == '+') { s->p++; op = OP_ADD; }
        else if (*s->p == '-') { s->p++; op = OP_SUB; }
        else if (kw(s, "XOR")) op = OP_XOR;
        else if (kw(s, "OR")) op = OP_OR;
        if (!op) return n;
        n = join(s, op, n, term(s));
    }
}

/* an expression, or a comparison of two (inside brackets) */
static fbn *cexpr(ps *s)
{
    static const char *const cm[] = { "EQ", "NE", "GT", "GE", "LT", "LE", 0 };
    fbn *a = expr(s), *n;
    int i;
    for (i = 0; cm[i]; i++)
        if (kw(s, cm[i])) break;
    if (!cm[i]) return a;
    n = node(EN_CMP);
    n->op = CM_EQ + i;
    n->a = a;
    n->b = expr(s);
    if (isbool(n->a) || isbool(n->b)) perr(s, "a comparison of conditions");
    return n;
}

/* the value of a word or of GOTO: [sign] number, variable or [expression] */
static fbn *wordval(ps *s)
{
    fbn *n;
    if (*s->p == '-') {
        s->p++;
        n = node(EN_NEG);
        n->a = wordval(s);
        return n;
    }
    if (*s->p == '+') s->p++;
    if (*s->p == '[') {
        s->p++;
        fbfeat(s->x, FF_MACB, s->line);
        n = cexpr(s);
        expect(s, ']');
        if (isbool(n)) perr(s, "a condition as a word value");
        return n;
    }
    if (*s->p == '#') return var(s);
    if (isnum(s)) return number(s);
    perr(s, "a value expected");
    return node(EN_NUM);
}

static fbn *cond(ps *s)
{
    fbn *n;
    if (!expect(s, '[')) return node(EN_NUM);
    n = cexpr(s);
    expect(s, ']');
    if (!isbool(n)) perr(s, "a condition (EQ NE GT GE LT LE) expected");
    return n;
}

static void assign(ps *s, fbn **lhs, fbn **e)
{
    *lhs = var(s);
    if (!expect(s, '=')) return;
    *e = cexpr(s);
    if (isbool(*e)) perr(s, "a condition assigned to a variable");
}

static int domn(ps *s)
{
    if (*s->p >= '1' && *s->p <= '9') return *s->p++ - '0';
    perr(s, "DO/END number expected");
    return 0;
}

/* clean a line: comments out, blanks out, upper case; returns 0 for a blank line */
static int clean(const char *in, char *out, int *bdel)
{
    int depth = 0;
    char *o = out;
    *bdel = 0;
    for (; *in; in++) {
        if (*in == '(') depth++;
        else if (*in == ')') { if (depth) depth--; }
        else if (depth) continue;
        else if (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') continue;
        else *o++ = (char)toupper((unsigned char)*in);
    }
    *o = 0;
    while (o > out && o[-1] == ';') *--o = 0;           /* EOB written as ; */
    if (out[0] == '/') {
        *bdel = 1;
        memmove(out, out + 1, strlen(out));
        if (isdigit((unsigned char)out[0])) memmove(out, out + 1, strlen(out));
    }
    return out[0] != 0 && strcmp(out, "%");
}

static fbs *addst(fbx *x)
{
    fbs *st = (fbs *)fbmalloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->n = -1;
    st->pair = -1;
    st->tool = -1;
    if (x->nst == x->cap) {
        int nc = x->cap ? x->cap * 2 : 256;
        fbs **a = (fbs **)fbmalloc((unsigned)(nc * sizeof *a));
        if (x->nst) memcpy(a, x->st, x->nst * sizeof *a);
        if (x->st) free(x->st);
        x->st = a;
        x->cap = nc;
    }
    x->st[x->nst++] = st;
    return st;
}

static void block(ps *s, fbs *st)
{
    fbx *x = s->x;
    if (*s->p == 'O' || *s->p == ':') {
        s->p++;
        if (!isdigit((unsigned char)*s->p)) { perr(s, "program number expected"); return; }
        st->k = ST_PROG;
        st->prog = atol(s->p);
        while (isdigit((unsigned char)*s->p)) s->p++;
        return;
    }
    if (*s->p == 'N' && isdigit((unsigned char)s->p[1])) {
        s->p++;
        st->n = atol(s->p);
        while (isdigit((unsigned char)*s->p)) s->p++;
    }
    if (*s->p == '#') {
        st->k = ST_ASSIGN;
        fbfeat(x, FF_MACB, s->line);
        assign(s, &st->lhs, &st->e);
    } else if (kw(s, "IF")) {
        fbfeat(x, FF_MACB, s->line);
        st->e = cond(s);
        if (kw(s, "GOTO")) {
            st->k = ST_IFGOTO;
            st->lhs = wordval(s);
        } else if (kw(s, "THEN")) {
            st->k = ST_IFTHEN;
            fbfeat(x, FF_THEN, s->line);
            if (*s->p == '#') assign(s, &st->lhs, &st->e2);
            else perr(s, "only an assignment after THEN is read");
        } else perr(s, "GOTO or THEN expected");
    } else if (kw(s, "GOTO")) {
        fbfeat(x, FF_MACB, s->line);
        st->k = ST_GOTO;
        st->lhs = wordval(s);
    } else if (kw(s, "WHILE")) {
        fbfeat(x, FF_MACB, s->line);
        st->k = ST_WHILE;
        st->e = cond(s);
        if (kw(s, "DO")) st->m = domn(s);
        else perr(s, "DO expected after WHILE");
    } else if (s->p[0] == 'D' && s->p[1] == 'O' && isdigit((unsigned char)s->p[2])) {
        fbfeat(x, FF_MACB, s->line);
        s->p += 2;
        st->k = ST_DO;
        st->m = domn(s);
    } else if (s->p[0] == 'E' && s->p[1] == 'N' && s->p[2] == 'D' && isdigit((unsigned char)s->p[3])) {
        fbfeat(x, FF_MACB, s->line);
        s->p += 3;
        st->k = ST_END;
        st->m = domn(s);
    } else if (kw(s, "SETVN") || kw(s, "BPRNT") || kw(s, "DPRNT") || kw(s, "POPEN") || kw(s, "PCLOS")) {
        fbfeat(x, FF_MACB, s->line);
        st->k = ST_UNSUP;
        s->p += strlen(s->p);
    } else {
        fbw w[FB_MAXW];
        int n = 0;
        st->k = ST_NC;
        while (*s->p && !s->err[0]) {
            if (!isalpha((unsigned char)*s->p)) { perr(s, "address letter expected"); break; }
            if (n == FB_MAXW) { perr(s, "too many words in one block"); break; }
            w[n].l = *s->p++;
            w[n].e = wordval(s);
            n++;
        }
        st->nw = n;
        if (n) {
            st->w = (fbw *)fbmalloc((unsigned)(n * sizeof *st->w));
            memcpy(st->w, w, n * sizeof *st->w);
        }
    }
    if (!s->err[0] && *s->p) perr(s, "unexpected text after the statement");
}

/* read one file; *line counts lines over all files.  Returns 0 after a read error. */
int fbread(fbx *x, FILE *fp, int file, int *line)
{
    char raw[FB_LINE], buf[FB_LINE];
    int bdel, ok = 1;
    while (fgets(raw, sizeof raw, fp)) {
        ps s;
        fbs *st;
        ++*line;
        if (!clean(raw, buf, &bdel)) continue;
        st = addst(x);
        st->line = *line;
        st->file = file;
        st->bdel = bdel;
        st->text = fbstrdup(buf);
        s.p = buf;
        s.x = x;
        s.line = *line;
        s.err[0] = 0;
        block(&s, st);
        if (s.err[0]) {
            fbrep(x, "REFUSED", *line, "cannot read the block %s: %s", st->text, s.err);
            ok = 0;
        }
    }
    return ok;
}

/* match WHILE/DO with END in each program; at most 3 levels (DO1-DO3) */
int fbpair(fbx *x)
{
    int stk[8], depth = 0, i, ok = 1;
    for (i = 0; i < x->nst; i++) {
        fbs *s = x->st[i];
        if (s->k == ST_PROG) {
            if (depth) {
                fbrep(x, "REFUSED", x->st[stk[depth - 1]]->line, "DO%d has no END%d", x->st[stk[depth - 1]]->m,
                      x->st[stk[depth - 1]]->m);
                ok = 0;
            }
            depth = 0;
        } else if (s->k == ST_WHILE || s->k == ST_DO) {
            if (depth == 3) {
                fbrep(x, "REFUSED", s->line, "DO loops nested more than 3 deep (DO1-DO3)");
                return 0;
            }
            stk[depth++] = i;
        } else if (s->k == ST_END) {
            if (!depth || x->st[stk[depth - 1]]->m != s->m) {
                fbrep(x, "REFUSED", s->line, "END%d does not match an open DO%d", s->m, s->m);
                return 0;
            }
            depth--;
            s->pair = stk[depth];
            x->st[stk[depth]]->pair = i;
        }
    }
    if (depth) {
        fbrep(x, "REFUSED", x->st[stk[depth - 1]]->line, "DO%d has no END%d", x->st[stk[depth - 1]]->m,
              x->st[stk[depth - 1]]->m);
        ok = 0;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* printing                                                             */

static void cat(char *buf, const char *s)
{
    size_t n = strlen(buf), m = strlen(s);
    if (n + m < 400) memcpy(buf + n, s, m + 1);
}

void fbpexpr(char *buf, const fbn *e)
{
    static const char *const bop[] = { "", "+", "-", "*", "/", " AND ", " OR ", " XOR ", " MOD " };
    static const char *const cop[] = { "", " EQ ", " NE ", " GT ", " GE ", " LT ", " LE " };
    static const char *const fn[] = { "", "SIN", "COS", "TAN", "ASIN", "ACOS", "ATAN", "ATAN", "SQRT", "ABS",
        "BIN", "BCD", "ROUND", "FIX", "FUP", "LN", "EXP", "POW", "ADP" };
    char t[48];
    if (!e) return;
    switch (e->k) {
    case EN_NUM:
        if (e->pt) fbfmt(t, e->v);
        else sprintf(t, "%.0f", e->v);
        cat(buf, t);
        break;
    case EN_VAR:
        cat(buf, "#");
        if (e->a->k == EN_NUM) fbpexpr(buf, e->a);
        else { cat(buf, "["); fbpexpr(buf, e->a); cat(buf, "]"); }
        break;
    case EN_SYS:
        cat(buf, "#_");
        cat(buf, e->name);
        if (e->a) { cat(buf, "["); fbpexpr(buf, e->a); cat(buf, "]"); }
        break;
    case EN_NEG:
        cat(buf, "-");
        if (e->a->k == EN_BIN) { cat(buf, "["); fbpexpr(buf, e->a); cat(buf, "]"); }
        else fbpexpr(buf, e->a);
        break;
    case EN_BIN:
    case EN_LOG:
    case EN_CMP:
        if (e->a->k == EN_BIN || e->a->k == EN_CMP || e->a->k == EN_LOG) {
            cat(buf, "["); fbpexpr(buf, e->a); cat(buf, "]");
        } else fbpexpr(buf, e->a);
        cat(buf, e->k == EN_CMP ? cop[e->op] : bop[e->op]);
        if (e->b->k == EN_BIN || e->b->k == EN_CMP || e->b->k == EN_LOG) {
            cat(buf, "["); fbpexpr(buf, e->b); cat(buf, "]");
        } else fbpexpr(buf, e->b);
        break;
    case EN_FUN:
        cat(buf, fn[e->op]);
        cat(buf, "[");
        fbpexpr(buf, e->a);
        if (e->op == FN_ATAN2 || e->op == FN_POW) {
            cat(buf, ",");
            fbpexpr(buf, e->b);
        }
        cat(buf, "]");
        break;
    }
}

void fbpstmt(FILE *fp, const fbs *s)
{
    char b[420];
    int i;
    b[0] = 0;
    switch (s->k) {
    case ST_PROG: fprintf(fp, "PROG     O%ld\n", s->prog); return;
    case ST_ASSIGN:
        fbpexpr(b, s->lhs); cat(b, " = "); fbpexpr(b, s->e);
        fprintf(fp, "ASSIGN   %s\n", b);
        return;
    case ST_IFGOTO:
    case ST_GOTO:
        if (s->k == ST_IFGOTO) { cat(b, "IF ["); fbpexpr(b, s->e); cat(b, "] "); }
        cat(b, "GOTO ");
        fbpexpr(b, s->lhs);
        fprintf(fp, "%s %s\n", s->k == ST_IFGOTO ? "IFGOTO  " : "GOTO    ", b);
        return;
    case ST_IFTHEN:
        cat(b, "IF ["); fbpexpr(b, s->e); cat(b, "] THEN ");
        fbpexpr(b, s->lhs); cat(b, " = "); fbpexpr(b, s->e2);
        fprintf(fp, "IFTHEN   %s\n", b);
        return;
    case ST_WHILE:
        fbpexpr(b, s->e);
        fprintf(fp, "WHILE    [%s] DO%d\n", b, s->m);
        return;
    case ST_DO: fprintf(fp, "DO       DO%d\n", s->m); return;
    case ST_END: fprintf(fp, "END      END%d\n", s->m); return;
    case ST_UNSUP: fprintf(fp, "UNSUP    %s\n", s->text); return;
    case ST_NC:
        for (i = 0; i < s->nw; i++) {
            char t[4];
            const fbn *e = s->w[i].e;
            int br = e->k == EN_BIN || e->k == EN_FUN || (e->k == EN_NEG && e->a->k != EN_NUM && e->a->k != EN_VAR);
            if (i) cat(b, " ");
            t[0] = s->w[i].l;
            t[1] = 0;
            cat(b, t);
            if (br) cat(b, "[");
            fbpexpr(b, e);
            if (br) cat(b, "]");
        }
        fprintf(fp, "NC       %s\n", b);
        return;
    }
}
