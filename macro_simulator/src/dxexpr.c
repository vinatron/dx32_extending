/*
 * dxexpr.c - numbers, variables and the PPPL assignment evaluator.
 *
 * Firmware: number reader 0x23b2c, word value 0x23aa0 ('*' parameters),
 * variable rule 0x23eb2, assignment 0x24ebc, expression 0x24fc8/0x2500a/0x25042,
 * function table 0x26302 with handlers from 0x253f2.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "dxsim.h"

static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
/* letters with 50 variables (second bank, 0x23f0a); the rest have 19 */
static const char extlet[] = "ACDFIJKRXYZ";

#define PI 3.14159265358979323846

int dxlet(int c)
{
    const char *q;
    if (c == 0) return -1;
    c = toupper(c);
    q = strchr(letters, c);
    return q ? (int)(q - letters) : -1;
}

int dxextlet(int li)
{
    return li >= 0 && strchr(extlet, letters[li]) != 0;
}

static int isdig(int c)
{
    return c >= '0' && c <= '9';
}

void dxvset(dxsim *s, int li, int idx, double v)
{
    char buf[80];
    if (li < 0 || idx < 1 || idx >= DX_NVAR) return;
    s->var[li][idx] = v;
    s->setv[li][idx] = 1;
    if (s->vtrace) {
        sprintf(buf, "SET   %c%d = %.6g", letters[li], idx, v);
        dxtline(s, buf);
    }
}

double dxvget(dxsim *s, int li, int idx)
{
    if (li < 0 || idx < 1 || idx >= DX_NVAR) return 0.0;
    return s->var[li][idx];
}

/*
 * 0x23b2c: [blanks] [-|+] {0} digits [. or , digits].  Leading zeros are skipped
 * and do not count, so "X01" reads as the 1-digit integer 1.  With no digit after
 * the sign the value is 0 and the pointer stays on the character that stopped it.
 */
double dxnum(dxsim *s, const char **pp, int *flags)
{
    const char *p = *pp;
    char buf[48];
    int n = 0, nint = 0, nfrac = 0, fl = 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-') { fl |= DXN_NEG; p++; }
    else if (*p == '+') p++;
    while (*p == '0') p++;
    if (!(isdig(*p) || *p == '.' || *p == ',')) {
        *pp = p;
        *flags = fl;
        return 0.0;
    }
    if (fl & DXN_NEG) buf[n++] = '-';
    while (isdig(*p)) {
        if (nint + nfrac >= 18) dxerror(s, "Parser: Syntax Error: Too Many Digits");
        buf[n++] = *p++;
        nint++;
    }
    if (*p == '.' || *p == ',') {
        fl |= DXN_POINT;
        p++;
        buf[n++] = '.';
        while (isdig(*p)) {
            if (nint + nfrac >= 18) dxerror(s, "Parser: Syntax Error: Too Many Digits");
            buf[n++] = *p++;
            nfrac++;
        }
    } else if (nint == 1 || nint == 2) {
        fl |= DXN_SHORT;
    }
    {                                   /* significant decimals, for the increment check */
        int k = n;
        s->nfrac = nfrac;
        while (s->nfrac > 0 && buf[k - 1] == '0') {
            s->nfrac--;
            k--;
        }
    }
    if (n == 0 || (n == 1 && buf[0] == '-')) buf[n++] = '0';
    buf[n] = 0;
    *pp = p;
    *flags = fl;
    return atof(buf);
}

/*
 * 0x23aa0: a word value.  '*' is a macro parameter: in a calling block (call
 * pending) "X*1.5" appends 1.5 to the parameter list and the word itself does
 * nothing; inside the macro a bare "X*" takes the next value.
 */
double dxval(dxsim *s, const char **pp, int *flags)
{
    const char *p = *pp;
    double v;
    int fl;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '*') {
        *pp = p;
        return dxnum(s, pp, flags);
    }
    p++;
    if (s->callpend) {
        v = dxnum(s, &p, &fl);
        if (s->compat && !(fl & DXN_POINT)) v /= s->metric ? 1000.0 : 10000.0;
        if (s->nprm >= DX_NPARAM)
            dxerror(s, "Parser: too many macro variables");
        s->prm[s->nprm++] = v;
        *pp = p;
        *flags = DXN_PARAM;
        return v;
    }
    if (s->rprm >= s->nprm) s->rprm = s->frame0;    /* wrap like 0x23ac0 */
    if (s->rprm >= s->nprm)
        dxerror(s, "Parser: macro variable '*' without a value");
    v = s->prm[s->rprm++];
    *pp = p;
    *flags = DXN_POINT | (v < 0 ? DXN_NEG : 0);
    return v;
}

/*
 * 0x23eb2: a word whose value was written as a positive 1-2 digit integer
 * without a decimal point names a variable when the index is in range
 * (1-19, or 1-50 for A C D F I J K R X Y Z).  Disabled in BOSS 4-7
 * compatibility mode outside the built-in cycles.
 */
int dxvref(dxsim *s, int li, int flags, double v, double *out)
{
    int n;
    if (s->compat && !s->intext) return 0;
    if (!(flags & DXN_SHORT) || (flags & (DXN_POINT | DXN_NEG))) return 0;
    n = (int)v;
    if (n >= 1 && n <= 19) {
        *out = s->var[li][n];
        return 1;
    }
    if (n >= 20 && n <= 50 && dxextlet(li)) {
        *out = s->var[li][n];
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* expression evaluator (0x24fc8)                                      */

typedef struct ev {
    dxsim *s;
    const char *begin;  /* start of the text */
    const char *p;      /* next character */
    const char *end;    /* terminator ('!', ';', CR or NUL) */
} ev;

static double eexpr(ev *e);

/* an error inside an expression: point at the character the evaluator stopped on */
static void eerr(ev *e, const char *msg)
{
    e->s->wstart = e->p > e->begin ? e->p - 1 : e->p;
    dxerror(e->s, msg);
}

/* 0x260c4: next character, blanks skipped, lower case folded */
static int enext(ev *e)
{
    int c;
    do {
        if (e->p >= e->end) return 0;
        c = (unsigned char)*e->p++;
    } while (c == ' ' || c == '\t');
    return toupper(c);
}

static int epeek(ev *e)
{
    const char *p = e->p;
    while (p < e->end && (*p == ' ' || *p == '\t')) p++;
    return p < e->end ? toupper((unsigned char)*p) : 0;
}

/* The 6.89 integer functions (NOT 0x25aa6, LSR 0x25d66, ...) convert with
   fmove.l fp,dn under the 68882's default FPCR, i.e. round to nearest with
   ties to even, not truncation (TENTATIVE until bench/BFIX.TXT is run). */
static double rneven(double v)
{
    double f = floor(v), d = v - f;
    if (d > 0.5 || (d == 0.5 && fmod(f, 2.0) != 0.0)) f += 1.0;
    return f;
}

static unsigned long tou32(double v)
{
    v = rneven(v);
    if (v < 0) {
        v = fmod(-v, 4294967296.0);
        return (0UL - (unsigned long)v) & 0xffffffffUL;
    }
    if (v >= 4294967296.0) v = fmod(v, 4294967296.0);
    return (unsigned long)v & 0xffffffffUL;
}

static double fromu32(unsigned long u)
{
    return (double)(u & 0xffffffffUL);
}

/* RDI GET WRI PUT(n): entry n of the firmware's table of 151 variables at 0x263a2
   (LANGUAGE.md section 4, "The variable table").
   n is rounded; below 0 or above 150 it is 150 (an unsigned compare, 0x255bc). */
static int uioent(double x)
{
    x = rneven(x);
    if (x < 0.0 || x > 150.0) return 150;
    return (int)x;
}

/* the entries WRI/PUT write (type 1 in the table); the others are read-only */
static int uiowr(int n)
{
    static const unsigned char wr[] = { 64, 65, 66, 68, 69, 71, 72, 73, 74, 75, 76, 77, 82, 83, 84, 87,
        92, 93, 94, 96, 97, 98, 99, 100, 131, 132, 133, 134, 136, 137, 141, 147, 148, 149, 0 };
    int i;
    if ((n >= 10 && n <= 19) || (n >= 102 && n <= 130)) return 1;
    for (i = 0; wr[i]; i++)
        if (wr[i] == n) return 1;
    return 0;
}

/* what entry n holds: the value written in this run, else the -i value, else 0 */
static double uioval(dxsim *s, int n)
{
    return s->uwr[n] ? s->uout[n] : s->uin[n];
}

static unsigned long rotr(unsigned long v, unsigned n)
{
    n &= 31;
    if (n == 0) return v & 0xffffffffUL;
    return ((v >> n) | (v << (32 - n))) & 0xffffffffUL;
}

static double func1(ev *e, const char *name, double x)
{
    dxsim *s = e->s;
    int n;
    double r = PI / 180.0;
    if (!strcmp(name, "ABS")) return fabs(x);
    if (!strcmp(name, "SQR")) {
        if (x < 0) eerr(e, "PPPL: SQR of a negative number");
        return sqrt(x);
    }
    if (!strcmp(name, "SIN")) return sin(x * r);
    if (!strcmp(name, "COS")) return cos(x * r);
    if (!strcmp(name, "TAN")) return tan(x * r);
    if (!strcmp(name, "ATN")) return atan(x) / r;
    if (!strcmp(name, "ASN")) return asin(x) / r;
    if (!strcmp(name, "ACS")) return acos(x) / r;
    if (!strcmp(name, "LOG")) return log10(x);
    if (!strcmp(name, "ALG")) return pow(10.0, x);
    if (!strcmp(name, "LGE")) return log(x);
    if (!strcmp(name, "E2X")) return exp(x);
    if (!strcmp(name, "HSN")) return sinh(x * r);
    if (!strcmp(name, "HCS")) return cosh(x * r);
    if (!strcmp(name, "HTN")) return tanh(x * r);
    if (!strcmp(name, "HAT")) return 0.5 * log((1.0 + x) / (1.0 - x)) / r;
    if (!strcmp(name, "NOT")) return fromu32(~tou32(x));
    if (!strcmp(name, "FFB")) {
        unsigned long u = tou32(x);
        for (n = 31; n >= 0; n--)
            if (u & (1UL << n)) return (double)n;
        return 31.0;    /* BFFFO finds nothing: offset 32, ~32 & 31 */
    }
    if (!strcmp(name, "RDI") || !strcmp(name, "GET")) {
        unsigned long u;
        n = uioent(x);
        if (!strcmp(name, "RDI") && (n == 55 || n == 56))
            return uioval(s, n);        /* 0xae2e / 0xae3a are read as extended reals (0x25622) */
        u = tou32(uioval(s, n));
        if (name[0] == 'G' || u < 0x80000000UL) return fromu32(u);
        return -(double)((0xffffffffUL - u) + 1UL);  /* RDI: signed (0x2561a) */
    }
    if (!strcmp(name, "RDP")) {
        n = (int)x - 1;
        if (n < 0 || n > 255) return 0.0;
        return (double)(signed char)s->upar[n];
    }
    eerr(e, "PPPL: Improper Function call");
    return 0.0;
}

/* two-argument functions: a is the first argument (FP7), b the second (FP6) */
static double func2(ev *e, const char *name, double a, double b)
{
    dxsim *s = e->s;
    char buf[64];
    unsigned long ua = tou32(a), ub = tou32(b);
    int n;
    if (!strcmp(name, "DTF")) return sqrt(a * a + b * b);
    if (!strcmp(name, "XOR")) return fromu32(ua ^ ub);
    if (!strcmp(name, "AND")) return fromu32(ua & ub);
    if (!strcmp(name, "LOR")) return fromu32(ua | ub);
    /* shifts and bit ops take the count/bit number first: LSR(n,v) = v >> n */
    if (!strcmp(name, "LSR")) return (ua & 63) > 31 ? 0.0 : fromu32(ub >> (ua & 31));
    if (!strcmp(name, "LSL")) return fromu32((ub << (ua & 31)) & 0xffffffffUL);
    if (!strcmp(name, "ROR")) return fromu32(rotr(ub, (unsigned)ua));
    if (!strcmp(name, "ROL")) return fromu32(rotr(ub, 32 - ((unsigned)ua & 31)));
    if (!strcmp(name, "SET")) return fromu32(ub | (1UL << (ua & 31)));
    if (!strcmp(name, "CLR")) return fromu32(ub & ~(1UL << (ua & 31)));
    if (!strcmp(name, "TST")) return (ub >> (ua & 31)) & 1UL ? 1.0 : 0.0;
    if (!strcmp(name, "MRG")) {                 /* 0x25b5c: the mask is entry 64 (0x764c) */
        unsigned long m = tou32(uioval(s, 64));
        return fromu32((ua & m) | (ub & ~m));
    }
    if (!strcmp(name, "WRI") || !strcmp(name, "PUT")) {
        n = uioent(a);
        if (uiowr(n)) {
            s->uout[n] = fromu32(ub);
            s->uwr[n] = 1;
            sprintf(buf, "IO    %s(%d) <- %.6g", name, n, b);
        } else
            sprintf(buf, "IO    %s(%d) <- %.6g (read-only entry: not written)", name, n, b);
        dxtline(s, buf);
        if (name[0] == 'P' || ub < 0x80000000UL) return fromu32(ub);
        return -(double)((0xffffffffUL - ub) + 1UL);
    }
    if (!strcmp(name, "WRP")) {
        n = (int)a - 1;
        if (n >= 0 && n <= 255) s->upar[n] = (unsigned char)(long)b;
        return b;
    }
    eerr(e, "PPPL: Improper Function call");
    return 0.0;
}

static int isfunc2(const char *name)
{
    static const char *const f2[] = { "DTF", "WRI", "WRP", "PUT", "XOR", "AND", "LOR",
        "LSR", "LSL", "ROR", "ROL", "SET", "CLR", "TST", "MRG", 0 };
    int i;
    for (i = 0; f2[i]; i++)
        if (!strcmp(f2[i], name)) return 1;
    return 0;
}

static int isfunc1(const char *name)
{
    static const char *const f1[] = { "ABS", "SIN", "COS", "SQR", "TAN", "ATN", "LOG",
        "ALG", "ASN", "ACS", "LGE", "E2X", "HSN", "HCS", "HTN", "HAT", "NOT", "FFB",
        "RDI", "RDP", "GET", 0 };
    int i;
    for (i = 0; f1[i]; i++)
        if (!strcmp(f1[i], name)) return 1;
    return 0;
}

/* variable index after the letter: digit 1-9, optional second digit (0x251ca) */
static int evindex(ev *e, int li)
{
    int c = enext(e), n;
    if (c < '1' || c > '9') eerr(e, "PPPL: Bad destination variable index");
    n = c - '0';
    if (isdig(epeek(e))) {
        n = n * 10 + (enext(e) - '0');
        if (n > 50 || (n > 19 && !dxextlet(li)))
            eerr(e, "Parser: #16 Internal Addressing Error");
    }
    return n;
}

/* find the end of a parenthesised argument; e->p is just after '(' */
static const char *eclose(ev *e, int stopcomma)
{
    const char *q = e->p;
    int depth = 1;
    while (q < e->end) {
        if (*q == '(') depth++;
        else if (*q == ')') {
            if (--depth == 0) return q;
        } else if (*q == ',' && depth == 1 && stopcomma) return q;
        q++;
    }
    eerr(e, "PPPL: Syntax Error-Parsed past End of Line");
    return q;
}

static double esub(ev *e, const char *end)
{
    ev sub;
    double v;
    sub.s = e->s;
    sub.begin = e->begin;
    sub.p = e->p;
    sub.end = end;
    v = eexpr(&sub);
    if (epeek(&sub) != 0) eerr(e, "PPPL: Expected Operator");
    e->p = end + 1;
    return v;
}

static double efactor(ev *e)
{
    int c = enext(e), li;
    char name[4], numbuf[40];
    double a, b;
    const char *q;
    int n;

    if (c == 0) eerr(e, "PPPL: Syntax Error: Missing Value");
    if (c == '(') {
        q = eclose(e, 0);
        return esub(e, q);
    }
    if (c == '-') return -efactor(e);
    if (c == '+') return efactor(e);
    if (c == '#') {
        if (enext(e) != 'P' || enext(e) != 'I') eerr(e, "PPPL: Bad PI");
        return PI;
    }
    if (isdig(c) || c == '.' || c == ',') {
        n = 0;
        numbuf[n++] = (char)(c == ',' ? '.' : c);
        while (e->p < e->end && (isdig(*e->p) || *e->p == '.' || *e->p == ',')) {
            if (n >= 20) eerr(e, "PPPL: Syntax Error: Too Many Digits");
            numbuf[n++] = (char)(*e->p == ',' ? '.' : *e->p);
            e->p++;
        }
        numbuf[n] = 0;
        return atof(numbuf);
    }
    li = dxlet(c);
    if (li < 0) eerr(e, "PPPL: Improper Function call");
    if (isdig(epeek(e))) {
        n = evindex(e, li);
        if (dxuser(e->s) && !e->s->setv[li][n] && !e->s->vwarn[li][n]) {
            char buf[100];
            e->s->vwarn[li][n] = 1;
            sprintf(buf, "%c%d is used in an expression but was never set (0)", c, n);
            dxwarn(e->s, 0, buf);
        }
        return e->s->var[li][n];
    }
    name[0] = (char)c;
    name[1] = (char)enext(e);
    name[2] = (char)enext(e);
    name[3] = 0;
    if (isfunc2(name)) {
        if (enext(e) != '(') eerr(e, "PPPL: 2-Argument Functions Require Parentheses");
        q = eclose(e, 1);
        if (*q != ',') eerr(e, "PPPL: 2-Argument Functions Require Parentheses");
        a = esub(e, q);
        q = eclose(e, 0);
        b = esub(e, q);
        return func2(e, name, a, b);
    }
    if (isfunc1(name)) {
        a = efactor(e);    /* a one-argument function applies to the next factor */
        return func1(e, name, a);
    }
    eerr(e, "PPPL: Improper Function call");
    return 0.0;
}

static double eterm(ev *e)
{
    double v = efactor(e), r;
    int c;
    for (;;) {
        c = epeek(e);
        if (c == '*') {
            enext(e);
            v *= efactor(e);
        } else if (c == '/') {
            enext(e);
            r = efactor(e);
            if (r == 0.0) eerr(e, "PPPL: division by zero");
            v /= r;
        } else return v;
    }
}

static double eexpr(ev *e)
{
    double v = eterm(e);
    int c;
    for (;;) {
        c = epeek(e);
        if (c == '+') {
            enext(e);
            v += eterm(e);
        } else if (c == '-') {
            enext(e);
            v -= eterm(e);
        } else if (c == 0 || c == ')' || c == ',') {
            return v;
        } else {
            eerr(e, "PPPL: Expected Operator");
        }
    }
}

/* 0x260a0: the assignment ends at CR, '!', ';' or NUL */
static const char *aend(const char *p)
{
    while (*p && *p != '\r' && *p != '!' && *p != ';') p++;
    return p;
}

/* 0x24ebc: "Ln=expr" at s->p; leaves s->p on the terminator */
void dxassgn(dxsim *s)
{
    ev e;
    int c, li, n;
    double v;

    e.s = s;
    e.begin = s->p;
    e.p = s->p;
    e.end = aend(s->p);
    c = enext(&e);
    li = dxlet(c);
    if (li < 0) eerr(&e, "PPPL: Bad destination variable letter");
    n = evindex(&e, li);
    if (enext(&e) != '=') eerr(&e, "PPPL: Cannot find equals sign");
    v = eexpr(&e);
    if (epeek(&e) != 0) eerr(&e, "PPPL: Expected Operator");
    dxvset(s, li, n, v);
    s->p = e.end;
}

/* evaluate a stand-alone expression (used by tests and the command line) */
double dxexprs(dxsim *s, const char *text)
{
    ev e;
    double v;
    e.s = s;
    e.begin = text;
    e.p = text;
    e.end = text + strlen(text);
    v = eexpr(&e);
    if (epeek(&e) != 0) eerr(&e, "PPPL: Expected Operator");
    return v;
}
