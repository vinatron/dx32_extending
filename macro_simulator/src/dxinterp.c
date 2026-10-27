/*
 * dxinterp.c - the block interpreter.
 *
 * Modelled on the firmware loop at 0x1ed12: a block is executed one character
 * at a time and every character has a handler (letter table 0x1ee84).  Control
 * transfers requested by a block (calls, returns, jumps) take effect when the
 * block ends, as the state variable 0x3c54 does in the firmware.
 *
 * Canned cycles run the firmware's own cycle text (dxcorpus.c).  While a cycle
 * block is being read ("capture", 0x32e3 bit 5) the X Y Z A B R J F words load
 * cycle variables instead of moving; at the end of the block the driver entry
 * of table A is called (0x1c842).
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "dxsim.h"

static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/* letter indices, filled in by dxinit */
static int LA, LB, LC, LF, LI, LJ, LP, LQ, LR, LU, LV, LW, LX, LY, LZ;

#define V(l, n)  (s->var[l][n])

/* a variable the firmware sets directly (no trace line) */
static void vz(dxsim *s, int l, int n, double v)
{
    s->var[l][n] = v;
    s->setv[l][n] = 1;
}

/* ------------------------------------------------------------------ */
/* errors and warnings                                                 */

/* the program block that called the canned cycle being executed, or -1 */
static long callerblk(const dxsim *s)
{
    long b;
    if (s->sdepth <= 0) return -1;
    b = s->sret[0].off ? s->sret[0].blk : s->sret[0].blk - 1;
    return b >= 0 && b < s->corp0 ? b : -1;
}

/* 1 if the block being executed belongs to the user's program (not the built-in text) */
int dxuser(const dxsim *s)
{
    return s->start.blk < s->corp0;
}

/*
 * Stop with an error, as the control does.  The message uses the control's
 * wording (operator's manual appendix D); dxerrrep() prints it with the line,
 * the block and a ^ under the word in error.
 */
void dxerror(dxsim *s, const char *msg)
{
    long b = s->start.blk, call;
    const char *t = dxbtext(s, b);
    strncpy(s->emsg, msg, sizeof s->emsg - 1);
    s->emsg[sizeof s->emsg - 1] = 0;
    strncpy(s->etext, t, DX_MAXLINE - 1);
    s->etext[DX_MAXLINE - 1] = 0;
    s->ecol = -1;
    if (s->wstart && s->bt == t && s->wstart >= t && s->wstart <= t + strlen(t))
        s->ecol = (int)(s->wstart - t);
    s->eloc[0] = 0;
    if (b >= s->corp0 && b < s->ntotal && s->nblk > 0) {
        /* inside the firmware's cycle text: report the program line that called it */
        call = callerblk(s);
        if (call >= 0) {
            sprintf(s->eloc, "line %ld, in its canned cycle (built-in block @%05lx: %.24s)",
                    dxbline(s, call), dxcorpof[b - s->corp0], t);
            strncpy(s->etext, dxbtext(s, call), DX_MAXLINE - 1);
            s->ecol = -1;
        } else sprintf(s->eloc, "built-in cycle block @%05lx", dxcorpof[b - s->corp0]);
    } else if (b >= 0 && b < s->nblk) {
        sprintf(s->eloc, "line %ld", dxbline(s, b));
    }
    s->stop = 2;
    longjmp(s->err, 1);
}

/*
 * ERROR   line 12: Parser: Illegal G-Code
 *         N60G1X1.G33
 *                 ^
 */
void dxerrrep(const dxsim *s, FILE *fp)
{
    int i;
    if (s->eloc[0]) fprintf(fp, "ERROR   %s: %s\n", s->eloc, s->emsg);
    else fprintf(fp, "ERROR   %s\n", s->emsg);
    if (!s->eloc[0] || !s->etext[0]) return;
    fprintf(fp, "        %s\n", s->etext);
    if (s->ecol >= 0) {
        fprintf(fp, "        ");
        for (i = 0; i < s->ecol; i++) fputc(' ', fp);
        fprintf(fp, "^\n");
    }
}

void dxwarn(dxsim *s, int once, const char *msg)
{
    char buf[160];
    if (s->linting) return;
    if (once > 0 && once < 32) {
        if (s->warned[once]) return;
        s->warned[once] = 1;
    }
    s->nwarn++;
    sprintf(buf, "WARN  %.150s", msg);
    dxtline(s, buf);
}

/* ------------------------------------------------------------------ */
/* setup                                                               */

void dxinit(dxsim *s)
{
    memset(s, 0, sizeof *s);
    s->pppl = 1;
    s->strip = 1;
    s->maxsteps = 500000L;
    s->peckclr = 0.05;
    s->p8 = 1;
    s->dunit = 1.0;
    s->rapid = s->zrapid = 472.0;       /* manual chapter 8 */
    s->smax = 4200.0;                   /* manual table 2-2 */
    LA = dxlet('A'); LB = dxlet('B'); LC = dxlet('C'); LF = dxlet('F');
    LI = dxlet('I'); LJ = dxlet('J'); LP = dxlet('P'); LQ = dxlet('Q');
    LR = dxlet('R'); LU = dxlet('U'); LV = dxlet('V'); LW = dxlet('W');
    LX = dxlet('X'); LY = dxlet('Y'); LZ = dxlet('Z');
}

/* clear everything a run changes; program text and options stay */
void dxreset(dxsim *s)
{
    int i;
    for (i = 0; i < 26; i++) {
        memset(s->var[i], 0, sizeof s->var[i]);
        memset(s->setv[i], 0, sizeof s->setv[i]);
    }
    for (i = 0; i < 26; i++) {
        int k;
        for (k = 0; k < DX_NVAR; k++)
            if (s->preset[i][k]) {
                s->var[i][k] = s->prval[i][k];
                s->setv[i][k] = 1;
            }
    }
    memset(s->uout, 0, sizeof s->uout);
    memset(s->labset, 0, sizeof s->labset);
    memset(s->subset, 0, sizeof s->subset);
    memset(s->warned, 0, sizeof s->warned);
    memset(s->vwarn, 0, sizeof s->vwarn);
    s->nwarn = 0;
    s->nmac = 0;
    s->gdepth = s->sdepth = s->ldepth = 0;
    s->nprm = s->rprm = s->frame0 = 0;
    s->macdepth = 0;
    s->cycn = s->cycdrv = s->cap = s->zcyc = s->intext = 0;
    s->skip = 0;
    s->stop = 0;
    s->steps = 0;
    s->nmoves = 0;
    s->lrapid = s->lfeed = 0.0;
    s->bset = 0;
    s->emsg[0] = 0;
    dxmreset(s);
}

/* ------------------------------------------------------------------ */
/* small helpers                                                       */

static int peekc(dxsim *s)
{
    const char *p = s->p;
    while (*p == ' ' || *p == '\t') p++;
    return (unsigned char)*p;
}

static double unitf(dxsim *s)
{
    return dxunit(s);
}

/* current target of an axis in program units (the 0x3f0c/0x3f30/0x3f54 registers) */
static double tgtp(dxsim *s, int ax)
{
    return s->tgt[ax] / unitf(s);
}

/* the finest increment a word accepts (manual table 2-1), as decimals; 99 = not checked */
static int maxdec(const dxsim *s, int c)
{
    if (strchr("ABE", c)) return 3;
    if (c == 'Q') return 2;
    if (strchr("XYZUVWIJKRDP", c)) return s->metric ? 3 : 4;
    return 99;
}

/* read a word value and apply the variable rule; returns -1 if it was a '*' push */
static int wordval(dxsim *s, int li, double *out)
{
    int fl, n, c = letters[li];
    double v, var;
    char buf[160];
    v = dxval(s, &s->p, &fl);
    if (fl & DXN_PARAM) return -1;
    if (dxvref(s, li, fl, v, &var)) {
        n = (int)v;
        if (dxuser(s) && !s->setv[li][n] && !s->vwarn[li][n]) {
            s->vwarn[li][n] = 1;
            sprintf(buf, "%c%d reads variable %c%d, which was never set (0); write %c%d. for the number %d",
                    c, n, c, n, c, n, n);
            dxwarn(s, 0, buf);
        }
        *out = var;
        return fl | DXN_FROMVAR;
    }
    if (dxuser(s) && (fl & DXN_POINT) && s->nfrac > maxdec(s, c)) {
        sprintf(buf, "%c%g has more decimals than the input increment allows (%s); the manual calls this an input error",
                c, v, maxdec(s, c) == 4 ? ".0001" : maxdec(s, c) == 3 ? ".001" : ".01");
        dxwarn(s, 0, buf);
    }
    if (s->compat && !(fl & DXN_POINT)) v /= s->metric ? 1000.0 : 10000.0;
    *out = v;
    return fl;
}

/* integer value for G, M, N, L, T, #, %, : (no variable lookup) */
static long intval(dxsim *s, int *flags)
{
    int fl;
    double v = dxval(s, &s->p, &fl);
    if (flags) *flags = fl;
    return v < 0 ? -(long)(-v + 0.5) : (long)(v + 0.5);
}

static void setpos(dxpos *p, long blk, int off)
{
    p->blk = blk;
    p->off = off;
}

static dxpos here(dxsim *s)
{
    dxpos p;
    setpos(&p, s->start.blk, (int)(s->p - s->bt));
    return p;
}

/* skip from the current character to the matching '$' or '|' (definition body) */
static void skipdef(dxsim *s)
{
    long b = s->start.blk;
    const char *t = s->bt;
    const char *q = s->p;
    for (;;) {
        while (*q && *q != '$' && *q != '|') q++;
        if (*q) {
            setpos(&s->stpos, b, (int)(q - t) + 1);
            s->state = DXS_GOTO;
            s->p = t + strlen(t);   /* nothing more in this block */
            return;
        }
        b++;
        if (b >= s->nuser) dxerror(s, "Parser: macro definition without '$' or '|'");
        t = dxbtext(s, b);
        q = t;
    }
}

/* ------------------------------------------------------------------ */
/* canned cycle bookkeeping                                            */

static void cycvar(dxsim *s, int li, int n, double v)
{
    dxvset(s, li, n, v);
}

/* 0x213b0: an X/Y/A word in a later block re-arms a modal G81-G89/G181/G191 cycle */
static void rearm(dxsim *s)
{
    if (s->cap || !s->cycn || s->sdepth) return;
    s->cap = 1;
    s->intext = 1;
    if (s->cycdrv > 0x17) return;
    s->cycdrv = 0x14;
    vz(s, LX, 1, 0.0); vz(s, LX, 2, 0.0); vz(s, LY, 1, 0.0);
    vz(s, LY, 2, 0.0); vz(s, LU, 1, 0.0); vz(s, LV, 1, 0.0);
    s->zcyc = 1;
}

/* 0x20b2e */
static void cancelcyc(dxsim *s)
{
    s->cycn = 0;
    s->cycdrv = 0;
    s->cap = 0;
    s->zcyc = 0;
    if (!s->sdepth && !s->macdepth) s->intext = 0;
}

static void startg8x(dxsim *s, int n)
{
    s->cycn = n;
    s->cycdrv = 0x14;
    vz(s, LX, 1, 0.0); vz(s, LX, 2, 0.0); vz(s, LY, 1, 0.0);
    vz(s, LY, 2, 0.0); vz(s, LU, 1, 0.0); vz(s, LV, 1, 0.0);
    vz(s, LC, 9, 0.0);  /* 0x208aa: a Z height for the C9 word, not modelled */
    vz(s, LZ, 6, s->peckclr > 0 ? s->peckclr / unitf(s) : 0.0);  /* param 272 */
    s->zcyc = 1;
    s->cap = 1;
    s->intext = 1;
}

static void startmill(dxsim *s, int drv)
{
    double r = 0.0;
    if (s->tool >= 1 && s->tool < DX_NTOOL) r = s->tools[s->tool].dia / 2.0;
    s->cycdrv = drv;
    vz(s, LP, 8, s->p8 ? 1.0 : 0.0);       /* 0x20920: machine family flag */
    cycvar(s, LR, 1, r / unitf(s));         /* manual: R1 = tool radius */
    vz(s, LR, 2, 0.0);
    vz(s, LZ, 2, 0.0);
    vz(s, LZ, 3, 0.0);
    vz(s, LP, 9, s->abs ? 0.0 : 1.0);       /* restore G91 afterwards (B12) */
    s->cap = 1;
    s->intext = 1;
}

/* ------------------------------------------------------------------ */
/* G codes with flow-control or cycle meaning; the rest go to dxgcode   */

static void gword(dxsim *s)
{
    int fl;
    long g = intval(s, &fl);
    if (fl & DXN_PARAM) return;
    if (g >= 0 && g <= 3) {
        dxgcode(s, (int)g);
        if (!s->sdepth) cancelcyc(s);
        return;
    }
    if (g == 4) {
        s->g4arm = 1;
        return;
    }
    if (g == 80) {
        dxgcode(s, 0);
        if (!s->sdepth) cancelcyc(s);
        return;
    }
    if (g >= 81 && g <= 89) {
        startg8x(s, (int)g - 80);
        return;
    }
    if (g >= 77 && g <= 79) {
        s->cycdrv = 0x1b + (int)g - 77;
        vz(s, LP, 9, s->abs ? 0.0 : 1.0);
        s->cap = 1;
        s->intext = 1;
        return;
    }
    if (g >= 170 && g <= 179) {
        startmill(s, (int)g - 160);
        return;
    }
    if (g >= 180 && g <= 199) {
        int d = (int)(g >= 190 ? g - 190 : g - 180);
        s->cycn = d;
        if (d == 0) {
            cancelcyc(s);
            return;
        }
        s->cycdrv = g >= 190 ? 0x1a : 0x18;
        s->cap = 1;
        s->intext = 1;
        vz(s, LF, 1, s->feed);
        vz(s, LW, 9, s->peckclr > 0 ? s->peckclr / unitf(s) : 0.0);
        return;
    }
    dxgcode(s, (int)g);
}

/* ------------------------------------------------------------------ */
/* M codes                                                             */

static void mcode(dxsim *s, long m)
{
    char buf[64];
    switch (m) {
    case 0:  dxtline(s, "STOP  M0 program stop"); break;
    case 1:  dxtline(s, s->opstop ? "STOP  M1 optional stop" : "NOTE  M1 optional stop (off)"); break;
    case 2: case 30: case 22:
        s->endpend = (int)m + 1;
        break;
    case 3: case 4: case 5:
        s->sdir = (int)m;
        sprintf(buf, "SPIN  M%ld S%.0f", m, s->spindle);
        dxtline(s, buf);
        break;
    case 6: case 26:
        s->ntool = s->tool;
        sprintf(buf, "TOOL  M%ld change to T%d (TLO %.4f DIA %.4f)", m, s->tool,
                s->tool > 0 && s->tool < DX_NTOOL ? s->tools[s->tool].tlo : 0.0,
                s->tool > 0 && s->tool < DX_NTOOL ? s->tools[s->tool].dia : 0.0);
        dxtline(s, buf);
        break;
    case 20: case 21:
        sprintf(buf, "STOP  M%ld goto clear point", m);
        dxtline(s, buf);
        break;
    default:
        if (m > 99 && (m < 300 || m > 663)) {
            sprintf(buf, "M%ld is not defined as a macro", m);
            dxwarn(s, 0, buf);
        }
        sprintf(buf, "MCODE M%ld", m);
        dxtline(s, buf);
        break;
    }
}

static int findmac(dxsim *s, int n)
{
    int i;
    for (i = s->nmac - 1; i >= 0; i--)
        if (s->macnum[i] == n) return i;
    return -1;
}

static int macslot(int i)
{
    return (DX_NSUB - 2) - i;   /* 0x1f63e: slots from 199 downward */
}

static void mword(dxsim *s)
{
    int fl, i;
    long m = intval(s, &fl);
    if (fl & DXN_PARAM) return;
    if (++s->nm == 2 && dxuser(s) && peekc(s) != ':')
        dxwarn(s, 0, "more than one M code in a block (manual chapter 9: only one M code per block)");
    if (s->cstate >= 4 && s->cstate <= 10 && dxuser(s) && peekc(s) != ':')
        dxwarn(s, 15, "M code while cutter compensation is on (manual p.67, factor 8)");
    if (peekc(s) == ':') {      /* a definition met at run time: skip its body */
        skipdef(s);
        return;
    }
    i = findmac(s, (int)m);
    if (i >= 0) {               /* 0x1f666: call a user M-macro */
        s->callpend = 1;
        s->newframe = s->nprm;
        if (s->macdepth == 0) s->intext = 1;
        s->macdepth++;
        s->state = DXS_CALLSUB;
        s->starg = macslot(i);
        s->stmac = 1;
        return;
    }
    mcode(s, m);
}

/* ------------------------------------------------------------------ */
/* letters                                                             */

static void word(dxsim *s, int li)
{
    double v;
    int fl, c = letters[li], n;
    long k;
    char buf[120];

    switch (c) {
    case 'G': gword(s); return;
    case 'M': mword(s); return;
    case 'O': return;                       /* 0x1f7a2: rts */
    case 'N':
        k = intval(s, &fl);
        if (fl & DXN_PARAM) return;
        if (s->callpend) {                  /* =Nseq/count loop start */
            s->nloop = 1;
            s->nloopseq = k;
            s->nloopcnt = 1;
        } else s->seqno = k;
        return;
    case 'L':
        k = intval(s, &fl);
        if (fl & DXN_PARAM) return;
        if (s->callpend) {
            if (k < 1 || k >= DX_NLABEL) dxerror(s, "Parser: #9 Internal Addressing Error");
            s->state = DXS_LABEL;
            s->starg = (int)k;
        } else {
            if (k < 1 || k >= DX_NLABEL || (k > 9 && !s->macdepth))
                dxerror(s, "Parser: #5 Internal Addressing Error");
            s->label[k] = s->start;
            s->labset[k] = 1;
        }
        return;
    case 'T':
        k = intval(s, &fl);
        if (fl & DXN_PARAM) return;
        if (k < 0 || k >= DX_NTOOL) dxerror(s, "Parser: #15 Internal Addressing Error");
        s->tnum = (int)k;
        s->tblk = 1;
        s->tslash = 0;
        s->tool = (int)k;
        return;
    default:
        break;
    }

    fl = wordval(s, li, &v);
    if (fl < 0) return;

    switch (c) {
    case 'X': case 'U':
        if (s->g73blk || s->g92blk || s->g97blk) break;
        if (c == 'X' && s->nx) {            /* 0x21776: 2nd and 3rd X */
            if (s->nx == 1) {
                cycvar(s, LX, 2, v);
                s->nx = 2;
                if (s->cycdrv == 0x1c) {
                    cycvar(s, LY, 2, v);
                    vz(s, LX, 3, 0.0);
                } else if (s->zcyc) s->cycdrv = 0x17;
            } else cycvar(s, LX, 3, v);
            return;
        }
        rearm(s);
        s->nx = 1;
        if (s->cap) {
            double a = (c == 'U' || !s->abs) ? tgtp(s, 0) + v : v;
            cycvar(s, LX, 1, a);
            cycvar(s, LU, 1, a - tgtp(s, 0));
            return;
        }
        break;
    case 'Y': case 'V':
        if (s->g73blk || s->g92blk || s->g97blk) break;
        if (c == 'Y' && s->ny) {            /* 0x21a7c */
            cycvar(s, LY, 2, v);
            if (s->zcyc) s->cycdrv = 0x17;
            return;
        }
        rearm(s);
        s->ny = 1;
        if (s->cap) {
            double a = (c == 'V' || !s->abs) ? tgtp(s, 1) + v : v;
            cycvar(s, LY, 1, a);
            cycvar(s, LV, 1, a - tgtp(s, 1));
            return;
        }
        break;
    case 'Z': case 'W':
        if (s->g73blk || s->g92blk || s->g97blk) break;
        if (c == 'Z' && s->nz) {            /* 0x21d2a: later Z words */
            if (!s->cap) return;
            if (s->nz == 1) {
                s->nz = 2;
                if (!s->zcyc) cycvar(s, LZ, 2, v);
                cycvar(s, LZ, 3, v);
                cycvar(s, LZ, 4, v);
            } else if (s->zcyc) {
                cycvar(s, LZ, 4, v);
            } else if (s->nz == 2) {
                s->nz = 3;
                cycvar(s, LZ, 3, v);
                cycvar(s, LZ, 4, v);
            } else cycvar(s, LZ, 4, v);
            return;
        }
        if (c == 'Z' && s->zcyc) {          /* 0x21c02: G81-G89 depth */
            s->nz = 1;
            cycvar(s, LZ, 2, v);
            cycvar(s, LZ, 3, v);
            cycvar(s, LZ, 4, v);
            return;
        }
        s->nz = 1;
        if (s->cap) {                       /* clearance plane */
            double a = (c == 'W' || !s->abs) ? tgtp(s, 2) + v : v;
            cycvar(s, LZ, 1, a);
            return;
        }
        break;
    case 'A': case 'B':
        if (s->g73blk || s->g92blk) break;
        if (c == 'A' && s->na) {            /* 0x22300: second A */
            cycvar(s, LA, 2, v);
            s->cycdrv = s->zcyc ? 0x16 : 0x19;
            if (V(LB, 1) >= 0 && v < 0) V(LB, 1) -= 360.0;
            else if (V(LB, 1) < 0 && v >= 0) V(LB, 1) += 360.0;
            return;
        }
        rearm(s);
        s->na = 1;
        if (s->cap) {
            double a = (c == 'B' || !s->abs) ? s->acur + v : v;
            cycvar(s, LA, 1, a);
            cycvar(s, LB, 1, a - s->acur);
            if (s->zcyc) s->cycdrv = 0x15;
            return;
        }
        break;
    case 'R':
        if (s->cap && s->cycn == 0) {       /* 0x1f8f8: mill cycle radius */
            cycvar(s, LR, 2, v);
            return;
        }
        break;
    case 'J':
        if (s->cap && s->cycdrv == 0x1d) {  /* 0x21ec6: G79 */
            cycvar(s, LJ, 1, v);
            return;
        }
        break;
    case 'F':
        if (v < 0) v = 0;
        if (s->nf == 0) {                   /* 0x1f344 */
            s->nf = 1;
            s->feed = v;
            if (dxuser(s) && !s->cap && v > (s->metric ? 7500.0 : 295.0)) {
                sprintf(buf, "F%g is above the maximum feed (%s); the control clamps it",
                        v, s->metric ? "7500 mm/min" : "295 ipm");
                dxwarn(s, 0, buf);
            }
            if (s->cap) {
                cycvar(s, LF, 1, v);
                if (s->cycdrv == 13 || s->cycdrv == 14 || s->cycdrv == 19) {
                    cycvar(s, LF, 2, v / 2.0);
                    cycvar(s, LF, 3, v / 2.0);
                } else {
                    cycvar(s, LF, 2, v);
                    cycvar(s, LF, 3, v / 2.0);
                }
            }
        } else if (s->nf == 1) {
            s->nf = 2;
            cycvar(s, LF, 2, v);
        } else cycvar(s, LF, 3, v);
        return;
    case 'P':
        if (s->g4arm) {                     /* 0x1f7c6: dwell */
            s->dwell = v;
            s->g4arm = 2;
            return;
        }
        n = s->np < 3 ? s->np + 1 : 4;      /* 0x1f81a: P1..P4 */
        cycvar(s, LP, n, v);
        s->np++;
        return;
    case 'Q':
        if (s->g4arm) {
            s->dwell = v;
            s->g4arm = 2;
            return;
        }
        if (!s->nq) {
            cycvar(s, LQ, 1, v);
            cycvar(s, LQ, 2, v);
            s->nq = 1;
        } else cycvar(s, LQ, 2, v);
        return;
    case 'D':
        if (s->tblk && s->tnum > 0) {
            s->tools[s->tnum].dia = v * unitf(s);
            return;
        }
        dxwarn(s, 1, "D word outside a T__D__ block ignored");
        return;
    case 'S':
        s->spindle = v;
        if (dxuser(s) && (v < 0 || v > s->smax)) {
            if (s->mp)
                sprintf(buf, "S%g is above the spindle maximum %.0f rpm (params 73-76); "
                        "the control clamps it", v, s->smax);
            else
                sprintf(buf, "S%g is outside the range 1-4200 (manual table 2-2)", v);
            dxwarn(s, 0, buf);
        }
        return;
    case 'C':
        return;                             /* C axis / C9: not modelled */
    case 'H':
        return;
    case 'E':
        dxwarn(s, 2, "spherical coordinates (E word) are not simulated");
        return;
    default:
        break;
    }
    if (c == 'K' || c == 'I' || c == 'J' || c == 'R' || c == 'A' || c == 'B' ||
        c == 'X' || c == 'Y' || c == 'Z' || c == 'U' || c == 'V' || c == 'W') {
        dxmword(s, li, v, fl);
        return;
    }
    sprintf(buf, "word %c ignored", c);
    dxwarn(s, 0, buf);
}

/* ------------------------------------------------------------------ */
/* punctuation                                                         */

/* 0x1ffc6: '?' letter index -> left-hand side */
static void condlhs(dxsim *s)
{
    int li = -1, fl;
    double v, var;
    while (*s->p && (li = dxlet((unsigned char)*s->p)) < 0) s->p++;
    if (li < 0) dxerror(s, "Parser: Illegal Condition Code");
    s->p++;
    v = dxnum(s, &s->p, &fl);
    if (!dxvref(s, li, fl, v, &var)) dxerror(s, "Parser: #16 Internal Addressing Error");
    if (dxuser(s) && !s->setv[li][(int)v] && !s->vwarn[li][(int)v]) {
        char buf[100];
        s->vwarn[li][(int)v] = 1;
        sprintf(buf, "?%c%d tests variable %c%d, which was never set (0)", letters[li], (int)v,
                letters[li], (int)v);
        dxwarn(s, 0, buf);
    }
    s->clhs = var;
    s->clet = li;
}

/* 0x2001a: '>' right-hand side and the compare (0x2021c) */
static void condrhs(dxsim *s)
{
    static const char ops[] = "EQNELTLEGTGE";
    int c = peekc(s), li, fl, i;
    double v, var, d;
    while (*s->p == ' ' || *s->p == '\t') s->p++;
    if ((c >= '0' && c <= '9') || c == '-' || c == '.' || c == ',') {
        li = s->clet;           /* quirk: the left-hand letter's record is still in A0 */
    } else {
        li = dxlet(c);
        if (li < 0) return;
        s->p++;
    }
    v = dxnum(s, &s->p, &fl);
    if (dxvref(s, li, fl, v, &var)) v = var;
    for (i = 0; i < 6; i++)
        if (s->cop[0] == ops[2 * i] && s->cop[1] == ops[2 * i + 1]) break;
    if (i == 6) dxerror(s, "Parser: Illegal Condition Code");
    d = s->clhs - v;
    switch (i) {
    case 0: c = d == 0; break;
    case 1: c = d != 0; break;
    case 2: c = d < 0; break;
    case 3: c = d <= 0; break;
    case 4: c = d > 0; break;
    default: c = d >= 0; break;
    }
    if (!c) s->skip++;
}

/* 0x200ba: '#n' call (after '=') or definition */
static void hashc(dxsim *s)
{
    int fl;
    long n = intval(s, &fl);
    if (s->callpend) {
        if (n < 1 || n >= DX_NSUB) dxerror(s, "Parser: #12 Internal Addressing Error");
        s->state = DXS_CALLSUB;
        s->starg = (int)n;
        s->stmac = 0;
        s->newframe = s->nprm;
        return;
    }
    if (n == 0) return;
    if (n < 0 || n >= DX_NSUB) dxerror(s, "Parser: #12 Internal Addressing Error");
    s->sub[n] = here(s);
    s->subset[n] = 1;
    skipdef(s);
}

/* 0x203c8: '%n' / '%@' */
static void pctc(dxsim *s)
{
    long n;
    int at = 0, fl;
    if (s->compat && !s->intext) return;
    if (*s->p == '@') {
        s->p++;
        at = 1;
        n = s->cycn;
    } else n = intval(s, &fl);
    if (s->callpend) {
        long idx = at ? n - 1 : n + 8;
        if (idx < 0 || idx > 40) dxerror(s, "Parser: #6 Internal Addressing Error");
        s->state = DXS_CALLSYS;
        s->starg = (int)idx;
        s->stpos = here(s);
    } else {
        if (n < 1 || n > 12) dxerror(s, "Parser: #4 Internal Addressing Error");
        s->state = DXS_JUMPB;
        s->starg = (int)n - 1;
    }
}

static void slashc(dxsim *s)
{
    int fl;
    double v;
    if (s->g4arm) {
        v = dxnum(s, &s->p, &fl);
        s->dwell = v;
        s->g4arm = 2;
        return;
    }
    if (s->tblk && s->tnum > 0) {           /* T n / tlo / dia */
        if (*s->p == '/' || *s->p == 0) {
            s->tslash++;
            return;
        }
        v = dxnum(s, &s->p, &fl);
        if (s->tslash == 0) s->tools[s->tnum].tlo = v * unitf(s);
        else s->tools[s->tnum].dia = v * unitf(s);
        s->tslash++;
        return;
    }
    if (s->nloop) {
        s->nloopcnt = (long)(dxnum(s, &s->p, &fl) + 0.5);
        return;
    }
}

static void quote(dxsim *s, int q)
{
    char buf[DX_MAXLINE + 8];
    int n = 0;
    strcpy(buf, "MSG   ");
    n = 6;
    while (*s->p && *s->p != q) buf[n++] = *s->p++;
    if (*s->p) s->p++;
    buf[n] = 0;
    dxtline(s, buf);
}

static void dochar(dxsim *s, int c)
{
    int li = dxlet(c);
    if (li >= 0) {
        word(s, li);
        return;
    }
    switch (c) {
    case '?': condlhs(s); break;
    case '!': if (s->skip) s->skip--; break;
    case '<':
        s->cop[0] = (char)(*s->p ? toupper((unsigned char)*s->p++) : 0);
        s->cop[1] = (char)(*s->p ? toupper((unsigned char)*s->p++) : 0);
        break;
    case '>': condrhs(s); break;
    case '=':
        s->callpend = 1;
        if (dxuser(s) && ((*s->p >= '0' && *s->p <= '9') || *s->p == '.' || *s->p == '-')) {
            char buf[120];
            sprintf(buf, "'=%.12s' is ignored: an assignment must start its block (no N number or other word before it)",
                    s->p);
            dxwarn(s, 0, buf);
        }
        break;
    case '#': hashc(s); break;
    case '$': s->state = DXS_RETSUB; s->starg = 0; break;
    case '|': s->state = DXS_RETSUB; s->starg = 1; break;
    case '%': pctc(s); break;
    case '&': s->state = DXS_RETSYS; break;
    case ':':                               /* 0x202b6: program number, rest ignored */
    case ';':
        s->p += strlen(s->p);
        break;
    case '"': case '\'': case '`': quote(s, c); break;
    case '/': slashc(s); break;
    default: break;                         /* digits, + - . * @ ... */
    }
}

/* ------------------------------------------------------------------ */
/* one block                                                           */

static void exblock(dxsim *s)
{
    const char *t = dxbtext(s, s->start.blk);
    int first = 1, c;
    char buf[DX_MAXLINE + 24];

    if (s->blkhook) s->blkhook(s);
    s->bt = t;
    s->p = t + s->start.off;
    if (s->start.blk >= s->corp0 && s->start.blk - s->corp0 < 1024)
        s->cov[s->start.blk - s->corp0] = 1;
    s->state = DXS_NEXT;
    s->callpend = 0;
    s->seqno = -1;
    s->nx = s->ny = s->nz = s->np = s->nf = s->nq = s->na = s->nr = 0;
    s->g4arm = 0;
    s->tblk = 0;
    s->nloop = 0;
    s->endpend = 0;
    s->nm = 0;
    s->wstart = 0;
    dxmblock(s);

    if (s->btrace == 2 || (s->btrace == 1 && s->start.blk < s->corp0)) {
        sprintf(buf, "BLK   %s%s", s->skip ? "(skip) " : "", s->p);
        dxtline(s, buf);
    }
    if (s->start.off == 0 && t[0] == '/' && s->bdel && !s->skip) return;

    while ((c = (unsigned char)*s->p) != 0) {
        if (s->skip) {                      /* 0x1ed44 */
            s->p++;
            if (c == '?') s->skip++;
            else if (c == '!') s->skip--;
            first = 0;
            continue;
        }
        if (first) {                        /* 0x1ed64: assignment block? */
            const char *p = s->p;
            first = 0;
            if (c != '$' && c != ';' && p[1] && p[2] && (p[2] == '=' || p[3] == '=')) {
                s->wstart = s->p;
                if (!s->intext && !s->pppl)
                    dxerror(s, "PPPL option (param 285 bit 4) not enabled");
                dxassgn(s);
                continue;
            }
        }
        s->wstart = s->p;
        s->p++;
        dochar(s, c);
    }
    s->wstart = 0;
}

static void gosub(dxsim *s, dxpos ret, int slot)
{
    if (slot < 1 || slot >= DX_NSUB || !s->subset[slot])
        dxerror(s, "Parser: Call Of Undefined Loop Or Macro");
    if (s->gdepth >= DX_GOSUB) dxerror(s, "Parser: #1 Internal Addressing Error");
    s->gret[s->gdepth] = ret;
    s->gprm0[s->gdepth] = s->frame0;
    s->grprm[s->gdepth] = s->rprm;
    s->gmac[s->gdepth] = s->stmac;
    s->gdepth++;
    s->frame0 = s->newframe;
    s->rprm = s->frame0;
    s->start = s->sub[slot];
}

static void subret(dxsim *s, int bar)
{
    if (s->gdepth == 0) dxerror(s, "Parser: return ('$' or '|') without a macro call");
    s->gdepth--;
    s->nprm = s->frame0;
    s->frame0 = s->gprm0[s->gdepth];
    s->rprm = s->grprm[s->gdepth];
    s->start = s->gret[s->gdepth];
    if (s->gmac[s->gdepth] || bar) {        /* 0x20176 */
        if (s->macdepth > 0) s->macdepth--;
        if (s->macdepth == 0 && !s->sdepth) s->intext = 0;
    }
}

static void syscall(dxsim *s, dxpos ret, int idx)
{
    if (idx < 0 || idx > 40) dxerror(s, "Parser: #6 Internal Addressing Error");
    if (s->sdepth >= DX_SYSSTK) dxerror(s, "Parser: #13 Internal Addressing Error");
    s->sret[s->sdepth++] = ret;
    setpos(&s->start, s->corp0 + dxcorpta[idx], 0);
}

static void sysret(dxsim *s)
{
    if (s->sdepth == 0) dxerror(s, "Parser: '&' without a cycle call");
    s->start = s->sret[--s->sdepth];
    if (s->sdepth == 0 && !s->macdepth) s->intext = 0;   /* 0x1cafc */
}

/* the block after this one, with =N loops applied (manual chapter 10) */
static dxpos nextpos(dxsim *s)
{
    dxpos next;
    setpos(&next, s->start.blk + 1, 0);
    if (s->nloop) {                         /* =Nseq/count: the loop starts after this block */
        if (s->ldepth >= DX_LOOPS) dxerror(s, "Parser: #8 Internal Addressing Error");
        s->lend[s->ldepth] = s->nloopseq;
        s->lcnt[s->ldepth] = s->nloopcnt < 1 ? 1 : s->nloopcnt;
        s->lstart[s->ldepth] = next;
        s->ldepth++;
        s->nloop = 0;
    }
    while (s->seqno >= 0 && s->ldepth > 0 && s->lend[s->ldepth - 1] == s->seqno) {
        if (--s->lcnt[s->ldepth - 1] > 0) return s->lstart[s->ldepth - 1];
        s->ldepth--;                        /* finished; an outer loop may end here too */
    }
    return next;
}

static void endblock(dxsim *s)
{
    char buf[64];
    int st = s->state;

    if (s->g4arm == 1) {
        sprintf(buf, "DWELL %.2f s", s->dwell);
        dxtline(s, buf);
    }
    if (s->cap) {                           /* 0x1c7f0: run the cycle driver */
        int go = s->cycn == 0 || s->nx || s->ny || s->na;
        s->cap = 0;
        s->zcyc = 0;
        if (go) {
            dxmpole(s);
            syscall(s, nextpos(s), s->cycdrv - 1);
            return;
        }
    } else {
        dxmend(s);
        dxcblk(s);
    }
    s->zcyc = 0;
    if (s->endpend) {
        dxcfin(s);
        sprintf(buf, "END   M%d", s->endpend - 1);
        dxtline(s, buf);
        s->stop = 1;
        return;
    }
    switch (st) {
    case DXS_NEXT:    s->start = nextpos(s); break;
    case DXS_CALLSUB: gosub(s, nextpos(s), s->starg); break;
    case DXS_RETSUB:  subret(s, s->starg); break;
    case DXS_JUMPB:   setpos(&s->start, s->corp0 + dxcorptb[s->starg], 0); break;
    case DXS_LABEL:
        if (!s->labset[s->starg]) dxerror(s, "Parser: Call Of Undefined Loop Or Macro");
        s->start = s->label[s->starg];
        break;
    case DXS_CALLSYS: syscall(s, s->stpos, s->starg); break;
    case DXS_RETSYS:  sysret(s); break;
    case DXS_GOTO:    s->start = s->stpos; break;
    default:          s->start = nextpos(s); break;
    }
}

/* ------------------------------------------------------------------ */
/* pre-scan: register #n and M<n>: definitions (load pass, 0xaefb bit 5) */

static void prescan(dxsim *s)
{
    long b;
    for (b = 0; b < s->nblk; b++) {
        const char *t = dxbtext(s, b), *p = t;
        int prev = 0, c;
        while ((c = (unsigned char)*p) != 0) {
            if (c == ';') break;
            if (c == '\'' || c == '"' || c == '`') {
                p++;
                while (*p && *p != c) p++;
                if (*p) p++;
                prev = 0;
                continue;
            }
            if ((c == '#' || c == 'M') && prev != '=') {
                const char *q = p + 1;
                long n = 0;
                int nd = 0;
                while (*q >= '0' && *q <= '9') {
                    n = n * 10 + (*q - '0');
                    q++;
                    nd++;
                }
                if (nd && c == '#' && n > 0 && n < DX_NSUB) {
                    setpos(&s->sub[n], b, (int)(q - t));
                    s->subset[n] = 1;
                } else if (nd && c == 'M') {
                    const char *r = q;
                    while (*r == ' ' || *r == '\t') r++;
                    if (*r == ':') {
                        int i = findmac(s, (int)n);
                        if (i < 0) {
                            if (s->nmac >= DX_NMAC) dxerror(s, "Parser: #2 Internal Addressing Error");
                            i = s->nmac++;
                            s->macnum[i] = (int)n;
                        }
                        setpos(&s->sub[macslot(i)], b, (int)(r - t));
                        s->subset[macslot(i)] = 1;
                    }
                }
                prev = c;
                p = q;
                continue;
            }
            prev = c;
            p++;
        }
    }
}

int dxrun(dxsim *s)
{
    if (setjmp(s->err)) return 2;
    prescan(s);
    setpos(&s->start, 0, 0);
    while (!s->stop) {
        if (s->start.blk >= s->nuser && s->start.blk < s->corp0) {
            if (s->gdepth || s->sdepth)
                dxerror(s, "end of program inside a macro or cycle");
            dxcfin(s);
            dxtline(s, "END   end of program text (no M2/M30)");
            s->stop = 1;
            break;
        }
        if (s->start.blk >= s->ntotal) dxerror(s, "ran past the end of the built-in cycle text");
        if (++s->steps > s->maxsteps) dxerror(s, "step limit reached (endless loop?)");
        exblock(s);
        endblock(s);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* -check: parse every built-in block without executing it              */

/* G codes the control accepts (manual table 3-1, firmware G table 0x205e2 and 0x208f8) */
static int gvalid(long g)
{
    static const short ok[] = { 0, 1, 2, 3, 4, 8, 9, 12, 13, 17, 18, 19, 22, 23, 30, 31, 32, 40, 41,
        42, 44, 45, 46, 47, 48, 49, 54, 55, 56, 57, 58, 59, 70, 71, 72, 73, 74, 75, 77, 78, 79, 90,
        91, 92, 94, 95, 96, 97, 99, -1 };
    int i;
    if ((g >= 80 && g <= 89) || (g >= 170 && g <= 199)) return 1;
    for (i = 0; ok[i] >= 0; i++)
        if (ok[i] == g) return 1;
    return 0;
}

static void chkblock(dxsim *s, const char *t)
{
    int c, li, fl, first = 1, eq = 0;
    double v;
    s->bt = t;
    s->p = t;
    while ((c = (unsigned char)*s->p) != 0) {
        s->wstart = s->p;
        if (first) {
            const char *p = s->p;
            first = 0;
            if (c != '$' && c != ';' && p[1] && p[2] && (p[2] == '=' || p[3] == '=')) {
                dxassgn(s);
                continue;
            }
        }
        s->p++;
        li = dxlet(c);
        if (li >= 0) {
            if (c == 'O') continue;
            if (*s->p == '*') {                 /* macro parameter: nothing to read here */
                s->p++;
                (void)dxnum(s, &s->p, &fl);
                eq = 0;
                continue;
            }
            v = dxval(s, &s->p, &fl);
            if (c == 'G' && !(fl & DXN_PARAM) && !gvalid((long)(v + 0.5)))
                dxerror(s, "Parser: Illegal G-Code");
            if (c == 'T' && (v < 0 || v >= DX_NTOOL)) dxerror(s, "Parser: #15 Internal Addressing Error");
            if (c == 'L' && (v < 1 || v > 9))
                dxerror(s, eq ? "Parser: #9 Internal Addressing Error" : "Parser: #5 Internal Addressing Error");
            eq = 0;
            continue;
        }
        eq = c == '=';
        switch (c) {
        case '?': condlhs(s); break;
        case '<':
            s->cop[0] = (char)(*s->p ? *s->p++ : 0);
            s->cop[1] = (char)(*s->p ? *s->p++ : 0);
            break;
        case '>': condrhs(s); s->skip = 0; break;
        case '#': case '%':
            if (*s->p == '@') s->p++;
            else (void)dxnum(s, &s->p, &fl);
            break;
        case ':': case ';': s->p += strlen(s->p); break;
        case '"': case '\'': case '`':
            while (*s->p && *s->p != c) s->p++;
            if (*s->p) s->p++;
            break;
        case '=': case '!': case '$': case '|': case '&': case '/': case '.':
        case '+': case '-': case '*': case '@':
            break;
        default:
            if (c < '0' || c > '9') dxerror(s, "unexpected character (the control ignores it)");
            break;
        }
    }
}

int dxcheck(dxsim *s, FILE *out)
{
    static volatile int i, bad;
    int k;
    bad = 0;
    for (i = 0; i < dxcorpn; i++) {
        for (k = 0; k < 26; k++) {          /* every variable 1: no division by zero */
            int n;
            for (n = 1; n < DX_NVAR; n++) s->var[k][n] = 1.0;
        }
        setpos(&s->start, s->corp0 + i, 0);
        s->vtrace = 0;
        if (setjmp(s->err)) {
            fprintf(out, "FAIL  %05lx  %-30s %s\n", dxcorpof[i], dxcorptx[i], s->emsg);
            bad++;
            continue;
        }
        chkblock(s, dxcorptx[i]);
    }
    fprintf(out, "checked %d built-in blocks, %d failed\n", dxcorpn, bad);
    return bad;
}

/* -lint: parse every block of the program without running it; report every error */
int dxlint(dxsim *s, FILE *out)
{
    static volatile long b;
    static volatile int bad;
    int k, n;
    bad = 0;
    s->linting = 1;
    for (b = 0; b < s->nblk; b++) {
        for (k = 0; k < 26; k++)            /* every variable 1: no division by zero */
            for (n = 1; n < DX_NVAR; n++) s->var[k][n] = 1.0;
        setpos(&s->start, b, 0);
        s->wstart = 0;
        if (setjmp(s->err)) {
            dxerrrep(s, out);
            bad++;
            continue;
        }
        chkblock(s, dxbtext(s, b));
    }
    s->linting = 0;
    if (bad) fprintf(out, "%d error%s in %ld lines\n", bad, bad == 1 ? "" : "s", s->nblk);
    else fprintf(out, "no errors in %ld lines\n", s->nblk);
    return bad;
}

