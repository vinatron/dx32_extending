/* fbmain.c - fb2dx: FANUC custom macro (Macro A / Macro B) to DX32 translator.
   Read the program, decide the FANUC series, evaluate it (phase 1), and write the DX32
   program (-w; phase 2 covers straight-line programs, FB2DX.md). */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "fb2dx.h"

static const char *const usage[] = {
    "fb2dx - FANUC custom macro (Macro A / Macro B) to DX32 translator (phase 1)",
    "usage: fb2dx [options] main.nc [sub.nc ...]",
    "  -series KEY      the FANUC series: a 0 6 10 15 16 0i 30i; otherwise detected",
    "  -series list     the series table, what exists where, and the -fparam settings",
    "  -fparam N=V      dpi=0|1 is=b|c nat=0|1 o8=0|1 avar=0|1, or default; repeatable",
    "  -inch | -mm      the unit system when the program has no G20/G21",
    "  -bdel on|off     the block delete switch, for programs with / blocks",
    "  -parse           list the statements as read",
    "  -eval            evaluate the program and trace its NC blocks",
    "  -w FILE          write the DX32 program to FILE (only when nothing is refused)",
    "  -limit N         stop the evaluation after N blocks (default 100000)",
    "  -o FILE          write the report and the trace to FILE",
    "  -silent          nothing on the console",
    0
};

static fbx gx;
static int silent;

void *fbmalloc(unsigned n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "fb2dx: out of memory\n");
        exit(3);
    }
    return p;
}

char *fbstrdup(const char *s)
{
    char *d = (char *)fbmalloc((unsigned)strlen(s) + 1);
    strcpy(d, s);
    return d;
}

/* a value with a decimal point and no trailing zeros: 1.5, 2., -.25 */
void fbfmt(char *buf, double v)
{
    char *p;
    if (v > -5e-7 && v < 5e-7) v = 0;
    sprintf(buf, "%.6f", v);
    p = buf + strlen(buf) - 1;
    while (*p == '0') *p-- = 0;
    if (!strncmp(buf, "0.", 2) && buf[2]) memmove(buf, buf + 1, strlen(buf));
    else if (!strncmp(buf, "-0.", 3) && buf[3]) memmove(buf + 1, buf + 2, strlen(buf + 1));
}

void fbrep(fbx *x, const char *kind, int line, const char *fmt, ...)
{
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(msg, fmt, ap);
    va_end(ap);
    if (line > 0) fprintf(x->rep, "%-8s line %d: %s\n", kind, line, msg);
    else fprintf(x->rep, "%-8s %s\n", kind, msg);
    if (!strcmp(kind, "REFUSED") || !strcmp(kind, "STOP")) x->stop = 1;
    if (x->rep != stdout && !silent && kind[0] != 'V') {
        if (line > 0) printf("%-8s line %d: %s\n", kind, line, msg);
        else printf("%-8s %s\n", kind, msg);
    }
}

int main(int argc, char **argv)
{
    fbx *x = &gx;
    const char *outf = 0;
    int i, parse = 0, eval = 0, line = 0, ok = 1;
    const char *wout = 0;
    long limit = 100000;

    x->ser = -1;
    x->bdelsw = -1;
    x->par.dpi = x->par.nat = x->par.is = x->par.o8 = x->par.avar = -1;
    x->rep = stdout;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-series") && i + 1 < argc) {
            a = argv[++i];
            if (!strcmp(a, "list")) {
                fbslist(stdout);
                return 0;
            }
            x->ser = fbsfind(a);
            if (x->ser < 0) {
                fprintf(stderr, "fb2dx: unknown series %s (fb2dx -series list)\n", a);
                return 1;
            }
        } else if (!strcmp(a, "-fparam") && i + 1 < argc) {
            if (!fbfparam(&x->par, argv[++i])) {
                fprintf(stderr, "fb2dx: bad -fparam %s (fb2dx -series list)\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(a, "-inch")) x->par.units = 20;
        else if (!strcmp(a, "-mm")) x->par.units = 21;
        else if (!strcmp(a, "-bdel") && i + 1 < argc) {
            a = argv[++i];
            x->bdelsw = !strcmp(a, "on") ? 1 : !strcmp(a, "off") ? 0 : -2;
            if (x->bdelsw == -2) {
                fprintf(stderr, "fb2dx: -bdel on or off\n");
                return 1;
            }
        } else if (!strcmp(a, "-parse")) parse = 1;
        else if (!strcmp(a, "-eval")) eval = 1;
        else if (!strcmp(a, "-w") && i + 1 < argc) wout = argv[++i];
        else if (!strcmp(a, "-limit") && i + 1 < argc) limit = atol(argv[++i]);
        else if (!strcmp(a, "-o") && i + 1 < argc) outf = argv[++i];
        else if (!strcmp(a, "-silent")) silent = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "fb2dx: unknown option %s\n", a);
            return 1;
        } else if (x->nfile < 8) x->fname[x->nfile++] = argv[i];
    }
    if (!x->nfile) {
        for (i = 0; usage[i]; i++) printf("%s\n", usage[i]);
        return 1;
    }
    if (outf) {
        x->rep = fopen(outf, "w");
        if (!x->rep) {
            fprintf(stderr, "fb2dx: cannot write %s\n", outf);
            return 1;
        }
    }
    for (i = 0; i < x->nfile; i++) {
        FILE *fp = fopen(x->fname[i], "r");
        if (!fp) {
            fbrep(x, "STOP", 0, "cannot open %s", x->fname[i]);
            ok = 0;
            break;
        }
        fprintf(x->rep, "FILE     %s (from line %d)\n", x->fname[i], line + 1);
        if (!fbread(x, fp, i, &line)) ok = 0;
        fclose(fp);
    }
    if (ok) ok = fbpair(x);
    if (ok) ok = fbdetect(x);
    if (ok) ok = fbmacroa(x);
    if (ok && x->bdelsw < 0)
        for (i = 0; i < x->nst; i++)
            if (x->st[i]->bdel) {
                fbrep(x, "STOP", x->st[i]->line, "a / block: give -bdel on or off (the block delete switch)");
                ok = 0;
                break;
            }
    if (ok && parse)
        for (i = 0; i < x->nst; i++) {
            fprintf(x->rep, "L%-5d ", x->st[i]->line);
            if (x->st[i]->n >= 0) fprintf(x->rep, "N%-5ld ", x->st[i]->n);
            else fprintf(x->rep, "       ");
            fbpstmt(x->rep, x->st[i]);
        }
    /* the translator needs the evaluation too: what the program really reads (FB2DX.md 3.2) */
    if (ok && (eval || wout)) ok = fbeval(x, eval ? x->rep : 0, limit);
    if (ok && wout) ok = fbwrite(x, wout);
    else if (ok && !x->stop) fbrep(x, "NOTE", 0, "-w FILE writes the DX32 program (features/FB2DX.md)");
    fprintf(x->rep, "RESULT   %s\n", x->stop ? "stopped: see REFUSED/STOP above" : "ok");
    if (outf) fclose(x->rep);
    return x->stop ? 2 : 0;
}
