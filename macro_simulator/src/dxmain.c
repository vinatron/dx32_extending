/*
 * dxmain.c - command line front end of the DX32 macro language simulator.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dxsim.h"

static dxsim sim;       /* static: too big for a 16-bit DOS stack */
static dxplot plot;

static const char *const help[] = {
    "dxsim %s - Bridgeport DX32 (BMDC 6.89) part program and macro simulator",
    "usage: dxsim [options] program.txt",
    "  -o FILE    write the trace to FILE (default: screen); -q no trace",
    "  -p FILE    plot file: .svg, .plt/.hpg (HPGL) or .ps/.eps (PostScript)",
    "  -v VIEW    view: g17 (XY, default), g18 (XZ), g19 (YZ), iso, or all four on one sheet",
    "  -r         do not draw rapid moves",
    "  -pc        colour feed moves by the macro or canned cycle that made them, with a key",
    "  -S / -Sf   live preview, paced per move / at the feed rate (DOS VGA, DXSIMW window)",
    "  -M FILE    load M-macro definitions (M<n>: ... |) from FILE",
    "  -s         trace variable assignments",
    "  -b         echo program blocks;  -B  also built-in cycle blocks",
    "  -d         dump the variables that were set, at the end",
    "  -cov       list the built-in cycle blocks this run executed",
    "  -m         metric power-on mode (G71); -mm show results in mm",
    "  -t T=D[,L] tool T diameter D (and length offset L), e.g. -t 1=.5",
    "  -x V=val   preset a variable, e.g. -x X1=2.5",
    "  -i N=val   start value of entry N of the RDI/GET/WRI/PUT table (0-150)",
    "  -n N       stop after N blocks (default 500000)",
    "  -nopppl    the PPPL option is off (assignments in the program fail)",
    "  -boss      BOSS 4-7 compatibility mode",
    "  -keep      keep blanks in blocks (normally removed)",
    "  -del       block delete switch on ('/' blocks skipped)",
    "  -peck V    peck clearance, param 272 (default .05 in)",
    "  -P FILE    machine parameters: BMDCPRMS.SYS (travel, rapid, spindle), BMDCPRMS.INI",
    "  -model M   apply section [M] of BMDCPRMS.INI, e.g. -model \"VMC-1000/22\" (repeatable)",
    "  -zero X,Y,Z  machine position of part zero (in; mm with -m): check the travel limits",
    "  -p8 0|1    machine family flag loaded into P8 by G170-G179 (default 1)",
    "  -e EXPR    evaluate a PPPL expression and exit",
    "  -corpus    list the built-in cycle entry points and exit",
    "  -check     parse every built-in cycle block and exit",
    "  -selftest  check the cutter-compensation geometry against hand results and exit",
    "  -lint      check every line of the program without running it; list all errors",
    "  -silent    no error or warning summary on the console (the trace has them)",
    "  -flow      program flow only: calls, returns, cycles, loops, jumps, skipped blocks",
    "  -flow+     the same flow lines inside the full trace, every loop pass",
    "  -map FILE  call map: FILE.svg as a diagram, otherwise an indented text tree",
    0
};

static void usage(void)
{
    int i;
    fprintf(stderr, help[0], DX_VERSION);
    fputc('\n', stderr);
    for (i = 1; help[i]; i++) fprintf(stderr, "%s\n", help[i]);
}

static int ext(const char *f, const char *e)
{
    size_t n = strlen(f), m = strlen(e), i;
    if (n < m) return 0;
    for (i = 0; i < m; i++)
        if (tolower((unsigned char)f[n - m + i]) != tolower((unsigned char)e[i])) return 0;
    return 1;
}

/* "X-10.,Y-5.,Z-12." or "-10,-5,-12" -> machine position of part zero */
static int parsezero(const char *a, double *z)
{
    int k = 0;
    while (*a && k < 3) {
        if (*a == 'X' || *a == 'x') k = 0;
        else if (*a == 'Y' || *a == 'y') k = 1;
        else if (*a == 'Z' || *a == 'z') k = 2;
        if (isalpha((unsigned char)*a)) a++;
        z[k++] = atof(a);
        a = strchr(a, ',');
        if (!a) return 0;
        a++;
    }
    return *a != 0;
}

/* the machine parameter files, BMDCPRMS.INI next to the first .SYS by default */
static int loadparm(dxsim *s, char **files, int nfiles, char **models, int nmodels)
{
    char err[160], ini[200];
    const char *e;
    int i, k;
    ini[0] = 0;
    for (i = 0; i < nfiles; i++) {
        if (ext(files[i], ".ini")) {
            sprintf(ini, "%.199s", files[i]);
            continue;
        }
        if (!dxpload(files[i], err)) {
            fprintf(stderr, "dxsim: %s\n", err);
            return 1;
        }
        if (!ini[0] && nmodels) {
            e = files[i] + strlen(files[i]);
            while (e > files[i] && e[-1] != '/' && e[-1] != '\\' && e[-1] != ':') e--;
            k = (int)(e - files[i]);
            if (k > 180) k = 180;
            sprintf(ini, "%.*sBMDCPRMS.INI", k, files[i]);
        }
    }
    if (nmodels && !ini[0]) {
        fprintf(stderr, "dxsim: -model needs a parameter file (-P BMDCPRMS.SYS or BMDCPRMS.INI)\n");
        return 1;
    }
    for (i = 0; i < nmodels; i++)
        if (!dxpmodel(ini, models[i], err)) {
            fprintf(stderr, "dxsim: %s\n", err);
            return 1;
        }
    dxpapply(s);
    return 0;
}

static int parsevar(const char *a, int *li, int *idx, double *v)
{
    const char *eq = strchr(a, '=');
    *li = dxlet((unsigned char)a[0]);
    if (*li < 0 || !eq) return -1;
    *idx = atoi(a + 1);
    if (*idx < 1 || *idx >= DX_NVAR) return -1;
    *v = atof(eq + 1);
    return 0;
}

static void corpus(void)
{
    static const char *const names[41] = {
        "G81 drill", "G82 spot face", "G83 deep hole", "G84 tap", "G85 bore",
        "G86 bore, stop", "G87 chip break", "G88 (none)", "G89 bore, dwell",
        "G170 outside frame", "G171 inside frame", "G172 pocket frame", "G173 outside face",
        "G174 inside face", "G175 outside circle", "G176 inside circle", "G177 pocket circle",
        "G178 (none)", "G179 slot", "G81-89 driver", "polar hole (A)", "bolt circle (A A)",
        "row (X X / Y Y)", "G181-189 row", "G181-189 bolt circle", "G191-199 frame",
        "G77 facing", "G78 pocket", "G79 bore mill", "%21", "%22", "%23", "%24", "%25",
        "%26", "%27", "%28", "%29", "%30", "%31", "%32" };
    int i;
    printf("table A (=%%n calls entry n+9; cycle bodies and drivers)\n");
    for (i = 0; i < 41; i++)
        printf("  A%-3d %05lx  %-22s %s\n", i + 1, dxcorpof[dxcorpta[i]], names[i],
               dxcorptx[dxcorpta[i]]);
    printf("table B (%%n jumps)\n");
    for (i = 0; i < 12; i++)
        printf("  B%-3d %05lx  %s\n", i + 1, dxcorpof[dxcorptb[i]], dxcorptx[dxcorptb[i]]);
}

/* -e: print the value, or "error: message" */
static int evalexpr(dxsim *s, FILE *out, const char *expr)
{
    if (setjmp(s->err)) {
        fprintf(out, "error: %s\n", s->emsg);
        return 2;
    }
    fprintf(out, "%.10g\n", dxexprs(s, expr));
    return 0;
}

int main(int argc, char **argv)
{
    dxsim *s = &sim;
    const char *prog = 0, *trace = 0, *pfile = 0, *expr = 0;
    const char *macf[8];
    char *pfiles[4], *models[4];
    const char *mapf = 0;
    int npf = 0, nmodel = 0;
    int nmacf = 0, quiet = 0, dump = 0, mm = 0, cov = 0, check = 0, self = 0, lint = 0, screen = 0, silent = 0;
    int i, rc, li, idx;
    double v;
    FILE *tf = stdout, *pf = 0;

    dxinit(s);
    memset(&plot, 0, sizeof plot);
    plot.dev = DXP_NONE;
    plot.proj = DXV_XY;
    plot.tol = 0.0005;
    plot.rapids = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *nx = i + 1 < argc ? argv[i + 1] : 0;
        if (a[0] != '-' || a[1] == 0) {
            prog = a;
            continue;
        }
        if (!strcmp(a, "-o") && nx) { trace = nx; i++; }
        else if (!strcmp(a, "-q")) quiet = 1;
        else if (!strcmp(a, "-p") && nx) { pfile = nx; i++; }
        else if (!strcmp(a, "-v") && nx) {
            plot.proj = dxview(nx);
            if (plot.proj < 0) {
                fprintf(stderr, "dxsim: unknown view %s (use g17 g18 g19 iso all)\n", nx);
                return 1;
            }
            i++;
        }
        else if (!strcmp(a, "-r")) plot.rapids = 0;
        else if (!strcmp(a, "-pc")) plot.bysrc = 1;
        else if (!strcmp(a, "-S")) screen = 1;
        else if (!strcmp(a, "-Sf")) screen = 2;
        else if (!strcmp(a, "-M") && nx) { if (nmacf < 8) macf[nmacf++] = nx; i++; }
        else if (!strcmp(a, "-s")) s->vtrace = 1;
        else if (!strcmp(a, "-b")) s->btrace = 1;
        else if (!strcmp(a, "-B")) s->btrace = 2;
        else if (!strcmp(a, "-d")) dump = 1;
        else if (!strcmp(a, "-cov")) cov = 1;
        else if (!strcmp(a, "-m")) s->metric0 = 1;
        else if (!strcmp(a, "-mm")) mm = 1;
        else if (!strcmp(a, "-t") && nx) {
            int t = atoi(nx);
            const char *eq = strchr(nx, '='), *cm;
            if (t < 1 || t >= DX_NTOOL || !eq) { usage(); return 1; }
            s->tools[t].dia = atof(eq + 1);
            cm = strchr(eq, ',');
            if (cm) s->tools[t].tlo = atof(cm + 1);
            i++;
        }
        else if (!strcmp(a, "-x") && nx) {
            if (parsevar(nx, &li, &idx, &v)) { usage(); return 1; }
            s->preset[li][idx] = 1;
            s->prval[li][idx] = v;
            i++;
        }
        else if (!strcmp(a, "-i") && nx) {
            int n = atoi(nx);
            const char *eq = strchr(nx, '=');
            if (n < 0 || n >= DX_NUIO || !eq) { usage(); return 1; }
            s->uin[n] = atof(eq + 1);
            i++;
        }
        else if (!strcmp(a, "-n") && nx) { s->maxsteps = atol(nx); i++; }
        else if (!strcmp(a, "-nopppl")) s->pppl = 0;
        else if (!strcmp(a, "-boss")) s->compat = 1;
        else if (!strcmp(a, "-keep")) s->strip = 0;
        else if (!strcmp(a, "-del")) s->bdel = 1;
        else if (!strcmp(a, "-peck") && nx) { s->peckclr = atof(nx); i++; }
        else if (!strcmp(a, "-P") && nx) { if (npf < 4) pfiles[npf++] = argv[i + 1]; i++; }
        else if (!strcmp(a, "-model") && nx) { if (nmodel < 4) models[nmodel++] = argv[i + 1]; i++; }
        else if (!strcmp(a, "-zero") && nx) {
            if (parsezero(nx, s->zero)) {
                fprintf(stderr, "dxsim: -zero X,Y,Z: machine position of part zero, e.g. -zero 10.,5.,-12.\n");
                return 1;
            }
            s->zset = 1;
            i++;
        }
        else if (!strcmp(a, "-p8") && nx) { s->p8 = atoi(nx); i++; }
        else if (!strcmp(a, "-e") && nx) { expr = nx; i++; }
        else if (!strcmp(a, "-corpus")) { corpus(); return 0; }
        else if (!strcmp(a, "-check")) check = 1;
        else if (!strcmp(a, "-selftest")) self = 1;
        else if (!strcmp(a, "-lint")) lint = 1;
        else if (!strcmp(a, "-silent")) silent = 1;
        else if (!strcmp(a, "-flow")) s->flow = 1;
        else if (!strcmp(a, "-flow+")) s->flow = 2;
        else if (!strcmp(a, "-map") && nx) { mapf = nx; s->mapon = 1; i++; }
        else { usage(); return 1; }
    }

    if (check || expr || self) {                /* both honour -o, for test scripts */
        if (trace && !(tf = fopen(trace, "w"))) {
            fprintf(stderr, "dxsim: cannot write %s\n", trace);
            return 1;
        }
        dxlddone(s);
        dxreset(s);
        rc = self ? (dxcself(tf) ? 2 : 0) : check ? (dxcheck(s, tf) ? 2 : 0) : evalexpr(s, tf, expr);
        if (tf != stdout) fclose(tf);
        return rc;
    }
    if (!prog) { usage(); return 1; }
    if (dxload(s, prog, 0)) {
        fprintf(stderr, "dxsim: cannot read %s\n", prog);
        return 1;
    }
    for (i = 0; i < nmacf; i++)
        if (dxload(s, macf[i], 1)) {
            fprintf(stderr, "dxsim: cannot read %s\n", macf[i]);
            return 1;
        }
    dxlddone(s);
    s->dunit = (mm || s->metric0) ? 25.4 : 1.0;
    if (s->zset && !npf && !nmodel) {
        fprintf(stderr, "dxsim: -zero needs the machine parameters (-P BMDCPRMS.SYS)\n");
        return 1;
    }
    if ((npf || nmodel) && loadparm(s, pfiles, npf, models, nmodel)) return 1;
    if (s->zset && s->metric0) {
        s->zero[0] /= 25.4;
        s->zero[1] /= 25.4;
        s->zero[2] /= 25.4;
    }
    if (lint) {
        if (trace && !(tf = fopen(trace, "w"))) {
            fprintf(stderr, "dxsim: cannot write %s\n", trace);
            return 1;
        }
        dxreset(s);
        rc = dxlint(s, tf) ? 2 : 0;
        if (tf != stdout) fclose(tf);
        return rc;
    }

#ifndef DX_SCREEN
    if (screen) {
        fprintf(stderr, "dxsim: -S needs a build with the preview (makefile.wat dos, dos32 or win32w)\n");
        return 1;
    }
#else
    if (screen) {
        dxscinit(screen == 2);
        if (!trace) quiet = 1;          /* text on the graphics screen would spoil it */
    }
#endif
    if (pfile || screen) {
        if (screen) plot.dev = DXP_SCREEN;
        else if (ext(pfile, ".svg")) plot.dev = DXP_SVG;
        else if (ext(pfile, ".plt") || ext(pfile, ".hpg") || ext(pfile, ".hgl")) plot.dev = DXP_HPGL;
        else if (ext(pfile, ".ps") || ext(pfile, ".eps")) plot.dev = DXP_PS;
        else {
            fprintf(stderr, "dxsim: plot type unknown (use .svg .plt .ps): %s\n", pfile);
            return 1;
        }
        s->plot = &plot;
        /* pass 1: measure the drawing */
        plot.pass = 1;
        s->trace = 0;
        dxreset(s);
        dxrun(s);
    }

    if (trace) {
        tf = fopen(trace, "w");
        if (!tf) {
            fprintf(stderr, "dxsim: cannot write %s\n", trace);
            return 1;
        }
    }
    s->trace = quiet ? 0 : tf;
    dxpdesc(s, s->trace);
    if (screen) {
        plot.pass = 2;
        dxplbeg(s);
    } else if (pfile) {
        pf = fopen(pfile, "w");
        if (!pf) {
            fprintf(stderr, "dxsim: cannot write %s\n", pfile);
            return 1;
        }
        plot.fp = pf;
        plot.pass = 2;
        dxplbeg(s);
    }
    dxreset(s);
    rc = dxrun(s);
    dxflowend(s);
    if (mapf && dxmapout(s, mapf)) fprintf(stderr, "dxsim: cannot write %s\n", mapf);
    if (pfile || screen) dxplend(s);
    if (pf) fclose(pf);
    if (rc) {
        if (s->trace) dxerrrep(s, s->trace);
        if (s->trace != stdout && !silent) dxerrrep(s, stderr);
    }
    if (s->nwarn && s->trace != stdout && !silent)
        fprintf(stderr, "dxsim: %ld warning%s, see the trace\n", s->nwarn, s->nwarn == 1 ? "" : "s");
    dxsumm(s);
    if (dump && s->trace) {
        int k;
        fprintf(s->trace, "------- variables\n");
        for (li = 0; li < 26; li++)
            for (k = 1; k < DX_NVAR; k++)
                if (s->setv[li][k])
                    fprintf(s->trace, "%c%-2d = %.6g\n", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[li], k,
                            s->var[li][k]);
    }
    if (cov) {
        int k, n = 0;
        FILE *cf = tf;              /* the -o file even with -q */
        for (k = 0; k < dxcorpn; k++) if (s->cov[k]) n++;
        fprintf(cf, "------- built-in blocks executed: %d of %d\n", n, dxcorpn);
        for (k = 0; k < dxcorpn; k++)
            if (s->cov[k]) fprintf(cf, "COV %05lx\n", dxcorpof[k]);
    }
    if (tf != stdout) fclose(tf);
    dxbfree(s);
    return rc ? 2 : 0;
}
