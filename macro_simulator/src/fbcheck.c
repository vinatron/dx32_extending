/* fbcheck.c - fb2dx -check (FB2DX.md phase 6): run dxsim on the DX32 program that was
   written, first with -lint, then for its trace, and compare the moves with the FANUC
   tool path the evaluator recorded.  dxsim runs as a separate program (system()) and
   only the files it writes are read, so this works under COMMAND.COM too. */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fb2dx.h"

#define CHKTMP "FBCHECK.TMP"
#define TOL    1.5e-4           /* inches: dxsim prints 4 decimals */

static int near(double a, double b)
{
    return fabs(a - b) <= TOL;
}

static void chop(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
}

static void run(const char *dxsim, const char *prog, const char *opt)
{
    char cmd[300];
    remove(CHKTMP);
    sprintf(cmd, "%.120s %.120s %s -silent -o %s", dxsim, prog, opt, CHKTMP);
    if (system(cmd)) {
        /* the exit code means nothing under COMMAND.COM: the file counts */
    }
}

/* the FANUC end points, without repeats and without a first move to where it starts (X0 Y0 Z0) */
static int fanuc(fbx *x, fbmv *f, int max)
{
    int i, n = 0;
    double lx = 0, ly = 0, lz = 0;
    for (i = 0; i < x->nmv && n < max; i++) {
        fbmv *m = &x->mv[i];
        if (near(m->x, lx) && near(m->y, ly) && near(m->z, lz) && !m->hasc) continue;
        f[n++] = *m;
        lx = m->x, ly = m->y, lz = m->z;
    }
    return n;
}

int fbcheck(fbx *x, const char *prog, const char *dxsim)
{
    static fbmv f[16000];
    char line[300], last[300], kind[16], where[32];
    FILE *fp;
    int n, j = 0, nerr = 0, nwarn = 0, dxline = 0, bad = 0;
    double lx = 0, ly = 0, lz = 0;

    if (x->mvunk) {
        fbrep(x, "NOTE", 0, "-check skipped: the unit system of some moves is not known, or there are too many");
        return 1;
    }
    fp = fopen(dxsim, "rb");
    if (!fp) {
        fbrep(x, "STOP", 0, "-check: %s not found (give -dxsim PATH)", dxsim);
        x->stop = 1;
        return 0;
    }
    fclose(fp);

    /* 1. dxsim -lint: the DX32 program must read without an error */
    run(dxsim, prog, "-lint");
    last[0] = 0;
    fp = fopen(CHKTMP, "r");
    if (fp) {
        while (fgets(line, sizeof line, fp)) {
            chop(line);
            if (strstr(line, "ERROR") || strstr(line, "error ")) {
                if (++nerr <= 5) fbrep(x, "REFUSED", 0, "-check: dxsim -lint: %s", line);
            }
            if (line[0]) strcpy(last, line);
        }
        fclose(fp);
    }
    if (nerr || strncmp(last, "no errors", 9)) {
        if (!nerr) fbrep(x, "REFUSED", 0, "-check: dxsim -lint did not finish: %s", last[0] ? last : "(no output)");
        fbrep(x, "STOP", 0, "-check: %s does not read cleanly on the DX32; do not run it", prog);
        x->stop = 1;
        remove(CHKTMP);
        return 0;
    }

    /* 2. dxsim's trace against the FANUC tool path */
    n = fanuc(x, f, 16000);
    run(dxsim, prog, "");
    fp = fopen(CHKTMP, "r");
    if (!fp) {
        fbrep(x, "STOP", 0, "-check: dxsim wrote no trace");
        x->stop = 1;
        return 0;
    }
    while (fgets(line, sizeof line, fp)) {
        double mx, my, mz, cx = 0, cy = 0;
        const char *c;
        int hasc = 0;
        chop(line);
        if (strstr(line, " WARN ")) {
            if (++nwarn <= 10) fbrep(x, "NOTE", 0, "-check: dxsim: %s", line);
            continue;
        }
        if (!strncmp(line, "ERROR", 5) || strstr(line, " ERROR ")) {
            fbrep(x, "REFUSED", 0, "-check: dxsim: %s", line);
            bad = 1;
            break;
        }
        if (sscanf(line, "%31s %15s X%lf Y%lf Z%lf", where, kind, &mx, &my, &mz) != 5) continue;
        if (strcmp(kind, "RAPID") && strcmp(kind, "FEED") && strcmp(kind, "ARC2") && strcmp(kind, "ARC3") &&
            strcmp(kind, "SPIRL")) continue;
        dxline = atoi(where + 1);
        c = strstr(line, "CX");
        if (c && sscanf(c, "CX%lf Y%lf", &cx, &cy) == 2) hasc = 1;
        if (near(mx, lx) && near(my, ly) && near(mz, lz) && !(j < n && f[j].hasc && hasc && near(mx, f[j].x) &&
            near(my, f[j].y) && near(mz, f[j].z)))
            continue;                   /* no movement */
        if (j < n && near(mx, f[j].x) && near(my, f[j].y) && near(mz, f[j].z)) {
            if (f[j].hasc && hasc && !(near(cx, f[j].cx) && near(cy, f[j].cy))) {
                fbrep(x, "REFUSED", f[j].line, "-check: the DX32 arc (line %d of the output) has its centre at "
                      "X%.4f Y%.4f, FANUC's is at X%.4f Y%.4f (inches)", dxline, cx, cy, f[j].cx, f[j].cy);
                bad = 1;
                break;
            }
            j++;
        } else if (j < n && !strcmp(kind, "RAPID") &&
                   ((near(mx, f[j].x) && near(my, f[j].y) && near(mz, lz)) ||
                    (near(mx, lx) && near(my, ly) && near(mz, f[j].z)))) {
            ;                           /* the DX32 G0 moves Z and XY one after the other */
        } else {
            if (j < n)
                fbrep(x, "REFUSED", f[j].line, "-check: the DX32 program goes to X%.4f Y%.4f Z%.4f (line %d of the "
                      "output); FANUC goes to X%.4f Y%.4f Z%.4f (inches)", mx, my, mz, dxline, f[j].x, f[j].y, f[j].z);
            else
                fbrep(x, "REFUSED", 0, "-check: the DX32 program moves on to X%.4f Y%.4f Z%.4f (line %d of the "
                      "output) after the last FANUC move", mx, my, mz, dxline);
            bad = 1;
            break;
        }
        lx = mx, ly = my, lz = mz;
    }
    fclose(fp);
    remove(CHKTMP);
    if (!bad && j < n) {
        fbrep(x, "REFUSED", f[j].line, "-check: the DX32 program stops after %d of the %d FANUC end points; the next "
              "is X%.4f Y%.4f Z%.4f (inches)", j, n, f[j].x, f[j].y, f[j].z);
        bad = 1;
    }
    if (bad) {
        fbrep(x, "STOP", 0, "-check: %s does not follow the FANUC tool path; do not run it", prog);
        x->stop = 1;
        return 0;
    }
    fbrep(x, "CHECK", 0, "dxsim reads %s cleanly and reaches all %d FANUC end points in order%s", prog, n,
          nwarn ? " (dxsim warnings above)" : "");
    return 1;
}
