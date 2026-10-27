/*
 * dxparam.c - machine parameters (BMDCPRMS.SYS, BMDCPRMS.INI) and travel limits.
 *
 * Only the parameters that change what a part program does on paper are used
 * (ROADMAP.md item 1):
 *
 *   7, 8      rapid rate X/Y and Z, units/tick (169.333 = 1 ipm: 10 nm units,
 *             4 ms tick, ../MOTION_PLANNER.md)            -> live preview pacing
 *   17-22     X, Y, Z travel limits, machine coordinates, 2,540,000 units/inch
 *   73-76     spindle motor maximum, range 1-3 maximum (rpm)
 *
 * The limits are checked like the firmware's block check FUN_000231f0 (called
 * from the move finalizer at 0x231d2): the end point of every block, built-in
 * cycle blocks included, against params 17-22, with the messages at 0x331fc
 * "Parser: Commanded X Axis Move Too Far Minus" ...  A Z limit of -2147483648
 * switches the Z check off (0x23280).  Z is checked at all only when the top
 * nibble of param 98 (0xaf58, the machine family) is not 0 or param 62 (Z
 * velocity gain, 0x2988) is not 0 (0x23260); with the nibble 0, a move above
 * the Z positive limit is refused only if it also goes higher than Z already
 * is (0x232ae, 254 units of tolerance).  A block with G54-G59 sets 0x4089
 * bit 3 (0x20dc0) and is not checked at all (0x231ca).  Points between the end points (arcs) are caught by the
 * interpolator at run time on the machine ("dpCom: ..." messages, operator's
 * manual appendix D); dxsim reports those with the dpCom wording.
 *
 * Machine coordinates need the machine position of part zero (-zero); without
 * it only the size of the part is compared with the travel (dxspan).
 *
 * BMDCPRMS.SYS: comment lines start with ';', a parameter line is
 * "<value> ; <number> [; comment]".  BMDCPRMS.INI: "[section]" lines, then
 * "<number> <value>" lines (../BMDCPRMS_INI.md).
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "dxsim.h"

#define NPRM    300
#define UPI     2540000.0           /* firmware units per inch */
#define UPTIPM  (UPI / 15000.0)     /* units/tick for 1 ipm: 60 s * 250 ticks */
#define LNONE   2147483647.0

static double pv[NPRM];
static unsigned char pset[NPRM];
static char pfiles[160];

static void addname(const char *f)
{
    if (strlen(pfiles) + strlen(f) + 3 >= sizeof pfiles) return;
    if (pfiles[0]) strcat(pfiles, ", ");
    strcat(pfiles, f);
}

/* the number after the first ';' of a BMDCPRMS.SYS line, or -1 */
static int sysnum(const char *l)
{
    const char *c = strchr(l, ';');
    if (!c) return -1;
    c++;
    while (*c == ' ' || *c == '\t') c++;
    if (!isdigit((unsigned char)*c)) return -1;
    return atoi(c);
}

int dxpload(const char *file, char *err)
{
    FILE *fp = fopen(file, "r");
    char l[256];
    int n, cnt = 0;
    if (!fp) {
        sprintf(err, "cannot read %.100s", file);
        return 0;
    }
    while (fgets(l, sizeof l, fp)) {
        const char *c = l;
        while (*c == ' ' || *c == '\t') c++;
        if (*c == ';' || *c == '\r' || *c == '\n' || !*c) continue;
        n = sysnum(c);
        if (n < 0 || n >= NPRM) continue;
        if (n == 0) continue;               /* "000000003.2": file version */
        pv[n] = atof(c);
        pset[n] = 1;
        cnt++;
    }
    fclose(fp);
    if (!cnt) {
        sprintf(err, "%.100s holds no parameters", file);
        return 0;
    }
    addname(file);
    return 1;
}

/* apply one [section] of BMDCPRMS.INI */
int dxpmodel(const char *file, const char *section, char *err)
{
    FILE *fp = fopen(file, "r");
    char l[256];
    int in = 0, found = 0, n;
    size_t k;
    if (!fp) {
        sprintf(err, "cannot read %.100s", file);
        return 0;
    }
    while (fgets(l, sizeof l, fp)) {
        k = strlen(l);
        while (k && (l[k - 1] == '\n' || l[k - 1] == '\r' || l[k - 1] == ' ')) l[--k] = 0;
        if (l[0] == '[') {
            char *e = strchr(l, ']');
            if (e) *e = 0;
            in = !strcmp(l + 1, section);
            if (in) found = 1;
            continue;
        }
        if (!in || !isdigit((unsigned char)l[0])) continue;
        n = atoi(l);
        if (n <= 0 || n >= NPRM) continue;
        {
            const char *v = l;
            while (isdigit((unsigned char)*v)) v++;
            pv[n] = atof(v);
            pset[n] = 1;
        }
    }
    fclose(fp);
    if (!found) {
        sprintf(err, "no section [%.40s] in %.80s", section, file);
        return 0;
    }
    if (strlen(pfiles) + strlen(section) + 4 < sizeof pfiles) {
        strcat(pfiles, " [");
        strcat(pfiles, section);
        strcat(pfiles, "]");
    }
    return 1;
}

static double lim(int n, double none)
{
    if (!pset[n] || fabs(pv[n]) >= LNONE) return none;
    return pv[n] / UPI;
}

void dxpapply(dxsim *s)
{
    int k;
    s->mp = 1;
    for (k = 0; k < 3; k++) {
        s->tmin[k] = lim(17 + 2 * k, -1e30);
        s->tmax[k] = lim(18 + 2 * k, 1e30);
    }
    {                                               /* 0x23260 */
        double v = pset[98] ? pv[98] : 1.0 * 0x20000000L;
        int nib;
        if (v < 0.0) v += 4294967296.0;
        nib = (int)(fmod(floor(v / 268435456.0), 16.0));
        s->zchk = nib != 0 || !pset[62] || pv[62] != 0.0;
        s->zrule = nib == 0;
    }
    if (pset[21] && pv[21] <= -LNONE - 0.5) {       /* 0x23280: no Z check */
        s->tmin[2] = -1e30;
        s->tmax[2] = 1e30;
    }
    if (pset[7] && pv[7] > 0) s->rapid = pv[7] / UPTIPM;
    if (pset[8] && pv[8] > 0) s->zrapid = pv[8] / UPTIPM;
    s->smax = 0;
    for (k = 74; k <= 76; k++)
        if (pset[k] && pv[k] > s->smax) s->smax = pv[k];
    if (s->smax <= 0 && pset[73]) s->smax = pv[73];
    if (s->smax <= 0) s->smax = 4200.0;
}

static void fmtlim(const dxsim *s, char *b, int k)
{
    char lo[24], hi[24];
    double u = s->dunit;
    if (s->tmin[k] < -1e29) strcpy(lo, "none");
    else sprintf(lo, s->dunit > 1.5 ? "%.3f" : "%.4f", s->tmin[k] * u);
    if (s->tmax[k] > 1e29) strcpy(hi, "none");
    else sprintf(hi, s->dunit > 1.5 ? "%.3f" : "%.4f", s->tmax[k] * u);
    sprintf(b, "%c %s .. %s", "XYZ"[k], lo, hi);
}

/* trace header: what was loaded */
void dxpdesc(const dxsim *s, FILE *fp)
{
    char a[3][60];
    int k;
    if (!fp || !s->mp) return;
    for (k = 0; k < 3; k++) fmtlim(s, a[k], k);
    fprintf(fp, "------- machine parameters: %s\n", pfiles);
    fprintf(fp, "travel  %s  %s  %s %s (params 17-22, machine coordinates)\n", a[0], a[1], a[2],
            s->dunit > 1.5 ? "mm" : "in");
    if (s->zset)
        fprintf(fp, "zero    part zero at machine X%.4f Y%.4f Z%.4f %s\n", s->zero[0] * s->dunit,
                s->zero[1] * s->dunit, s->zero[2] * s->dunit, s->dunit > 1.5 ? "mm" : "in");
    else
        fprintf(fp, "zero    not given (-zero): only the size of the part is checked\n");
    fprintf(fp, "rapid   X/Y %.1f ipm  Z %.1f ipm (params 7, 8); spindle max %.0f rpm (params 73-76)\n",
            s->rapid, s->zrapid, s->smax);
    fprintf(fp, "zcheck  %s (params 98, 62)\n", !s->zchk ? "Z limits not checked" :
            s->zrule ? "Z checked; above the + limit only if going higher" : "Z checked");
    fprintf(fp, "------- program\n");
}

/*
 * A point the tool reaches, base part coordinates (inch).  zfrom is Z where
 * the move starts.  runtime: a point between block end points (dpCom).
 */
void dxtravel(dxsim *s, const double *p, double zfrom, int runtime)
{
    static const char *const ax[3] = { "X", "Y", "Z" };
    char msg[80];
    double m;
    int k, plus;
    if (!s->mp || !s->zset || s->g5xblk) return;
    for (k = 0; k < 3; k++) {
        if (k == 2 && !s->zchk) break;
        m = p[k] + s->zero[k];
        plus = 0;
        if (m < s->tmin[k] - 5e-6) plus = -1;
        else if (m > s->tmax[k] + 5e-6) {
            plus = 1;
            if (k == 2 && s->zrule && m <= zfrom + s->zero[2] + 254.0 / UPI) plus = 0;   /* 0x232ae */
        }
        if (!plus) continue;
        sprintf(msg, "%s: Commanded %s Axis Move Too Far %s", runtime ? "dpCom" : "Parser", ax[k],
                plus > 0 ? "Positive" : "Minus");
        /* a caret under the axis word if the block has one */
        s->wstart = 0;
        if (s->bt && dxuser(s)) {
            const char *c = s->bt, *w = 0;
            for (; *c && *c != ';'; c++)
                if ((*c == "XYZ"[k] || *c == "UVW"[k]) &&
                    (isdigit((unsigned char)c[1]) || c[1] == '-' || c[1] == '+' || c[1] == '.')) {
                    w = c;
                    break;
                }
            s->wstart = w;
        }
        dxerror(s, msg);
    }
}

/* without -zero: is the part bigger than the travel? (end of the run) */
void dxspan(dxsim *s, FILE *fp)
{
    int k;
    double need, have;
    if (!s->mp || s->zset || !s->bset) return;
    for (k = 0; k < 3; k++) {
        if (s->tmin[k] < -1e29 || s->tmax[k] > 1e29) continue;
        need = s->bmax[k] - s->bmin[k];
        have = s->tmax[k] - s->tmin[k];
        if (need <= have + 5e-6) continue;
        s->nwarn++;
        if (fp)
            fprintf(fp, "WARN    the program moves %c over %.4f %s; the machine has %.4f of travel "
                    "(params %d-%d)\n", "XYZ"[k], need * s->dunit, s->dunit > 1.5 ? "mm" : "in",
                    have * s->dunit, 17 + 2 * k, 18 + 2 * k);
    }
}
