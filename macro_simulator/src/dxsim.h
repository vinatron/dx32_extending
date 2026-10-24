/*
 * dxsim.h - Bridgeport DX32 / BMDC part-program and macro language simulator.
 *
 * Portable ANSI C (C89).  Rules kept on purpose so the same source builds with
 * 16-bit DOS compilers (large model), Win32 console compilers and IBM z/OS C:
 *   - no C99 features (no // comments, no mixed declarations, no snprintf);
 *   - no object larger than 64 KB (the block table is chunked);
 *   - letters are looked up in a string, never by c - 'A' (EBCDIC);
 *   - external names are unique in their first 8 characters, ignoring case.
 *
 * Firmware references are BMDC.BIN 6.89 addresses (see ../LANGUAGE.md).
 */
#ifndef DXSIM_H
#define DXSIM_H

#include <stdio.h>
#include <setjmp.h>

#define DX_VERSION "0.1"

/* ---- sizes ---- */
#define DX_NVAR     51      /* variable index 1..50 (0 unused) */
#define DX_MAXLINE  256     /* longest block kept (manual: 132 incl. CR) */
#define DX_CHUNK    512     /* blocks per chunk of the block table */
#define DX_MAXCHNK  256     /* -> up to 131072 blocks */
#define DX_NSUB     201     /* subprogram/macro slots 1..200 (0x3820 table) */
#define DX_NLABEL   30      /* labels L1..L29 (0x3c74) */
#define DX_GOSUB    8       /* =# / M-macro nesting (0x32fc stack) */
#define DX_SYSSTK   5       /* =% nesting (0x3c1c stack) */
#define DX_LOOPS    4       /* =N loops (manual: 4 levels) */
#define DX_NPARAM   64      /* '*' macro parameters (0x3368 area) */
#define DX_NMAC     120     /* M<n>: definitions (0x3b44 list) */
#define DX_NTOOL    25      /* tools 1..24 */
#define DX_NUIO     151     /* user I/O channels 0..150 (RDI/WRI) */

/* ---- number reader flags (d7 bits of 0x23b2c) ---- */
#define DXN_POINT   1       /* bit 0: decimal point seen */
#define DXN_SHORT   2       /* bit 1: 1-2 digit integer, no point */
#define DXN_NEG     4       /* bit 7: minus sign */
#define DXN_PARAM   8       /* value went to the '*' parameter list */
#define DXN_FROMVAR 16      /* value came from a variable (d7 bit 2) */

/* ---- motion ---- */
#define DXM_RAPID   0
#define DXM_LINE    1
#define DXM_CW      2
#define DXM_CCW     3

/* control transfer after the current block (the 0x3c54 states) */
#define DXS_NEXT    0
#define DXS_CALLSUB 1       /* =#n or M-macro */
#define DXS_RETSUB  2       /* $ or | */
#define DXS_JUMPB   3       /* %n -> table B */
#define DXS_LABEL   4       /* =Ln */
#define DXS_CALLSYS 5       /* =%n, =%@, cycle driver */
#define DXS_RETSYS  6       /* & */
#define DXS_GOTO    7       /* continue at stpos (after a definition body) */

/* plot devices */
#define DXP_NONE    0
#define DXP_SVG     1
#define DXP_HPGL    2
#define DXP_PS      3
#define DXP_GDDM    4

/* projections */
#define DXV_XY      0
#define DXV_XZ      1
#define DXV_YZ      2
#define DXV_ISO     3

typedef struct dxpos { long blk; int off; } dxpos;   /* a place in the text */

/* one geometric element, in base part coordinates (inch) */
typedef struct dxseg {
    int    kind;            /* DXM_RAPID or DXM_LINE for feed moves */
    int    arc;             /* 0 straight, else DXM_CW / DXM_CCW */
    int    plane;           /* 17, 18, 19 */
    double p0[3], p1[3];    /* start and end */
    double c[3];            /* arc centre (helix: centre at start height) */
    double sweep;           /* arc sweep in radians, + = CCW in the plane */
    double feed;            /* programmed feed (program units / min) */
    int    quiet;           /* part of a curve already traced as a whole */
} dxseg;

typedef struct dxplot {
    int    dev;             /* DXP_* */
    int    proj;            /* DXV_* */
    int    pass;            /* 1: measure, 2: draw */
    FILE  *fp;
    double minx, miny, maxx, maxy;
    double tol;             /* chord tolerance for arcs (inch) */
    double sc, ox, oy;      /* device scale and origin (pass 2) */
    double mark;            /* half size of a hole marker (device units) */
    int    pen;             /* current pen: -1 up, 0 rapid, 1 feed */
    double lx, ly;          /* last device point */
    int    rapids;          /* draw rapid moves */
    long   nseg;
} dxplot;

typedef struct dxtool { double tlo, dia; } dxtool;

typedef struct dxsim {
    /* ---- program text ---- */
    char  **chunk[DX_MAXCHNK];  /* block text */
    long  *lchunk[DX_MAXCHNK];  /* source line numbers */
    long   nblk;                /* user blocks */
    long   nuser;               /* user blocks of the part program proper */
    long   corp0;               /* index of the first built-in block */
    long   ntotal;
    const char *fname;

    /* ---- execution position ---- */
    dxpos  start;               /* where the current block execution began (aeb2) */
    const char *p;              /* current character (A6) */
    const char *bt;             /* text of the current block */
    long   blk;
    int    skip;                /* 0x32f6 conditional skip counter */
    int    callpend;            /* 0x32e3 bit 0: '=' or M-macro call pending */
    int    state;               /* DXS_* after this block */
    int    starg;               /* argument of the state */
    dxpos  stpos;               /* saved position for DXS_CALLSYS */
    int    stop;                /* 1 = program end, 2 = error */
    long   steps, maxsteps;

    /* ---- variables and user I/O ---- */
    double var[26][DX_NVAR];
    unsigned char setv[26][DX_NVAR]; /* assigned at least once */
    double uin[DX_NUIO], uout[DX_NUIO];
    unsigned char upar[256];

    /* ---- flow control tables ---- */
    dxpos  label[DX_NLABEL];
    int    labset[DX_NLABEL];
    dxpos  sub[DX_NSUB];
    int    subset[DX_NSUB];
    int    macnum[DX_NMAC];
    int    nmac;
    dxpos  gret[DX_GOSUB];      /* =# / M return positions */
    int    gprm0[DX_GOSUB];     /* parameter frame start of each call */
    int    grprm[DX_GOSUB];     /* parameter read pointer of the caller */
    int    gmac[DX_GOSUB];      /* 1 if the call was an M-macro */
    int    newframe;            /* parameter frame of the call in this block */
    int    stmac;               /* the pending call is an M-macro */
    int    gdepth;
    double prm[DX_NPARAM];
    int    nprm;                /* write pointer (0x3368) */
    int    rprm;                /* read pointer (0x336c) */
    int    frame0;              /* current frame start */
    dxpos  sret[DX_SYSSTK];
    int    sdepth;              /* 0x3c40 */
    long   lend[DX_LOOPS];      /* =N loops: end sequence number */
    long   lcnt[DX_LOOPS];
    dxpos  lstart[DX_LOOPS];
    int    ldepth;
    long   seqno;               /* N of the current block, -1 if none */
    int    macdepth;            /* 0xc5c0 */

    /* ---- canned cycles ---- */
    int    cycn;                /* 0x3c44: G8x body 1..9, 0 = none */
    int    cycdrv;              /* 0x3c48: table A entry of the driver */
    int    cap;                 /* 0x32e3 bit 5: capture cycle words */
    int    zcyc;                /* 0x32e3 bit 6: G81-G89 single holes */
    int    intext;              /* 0x4088 bit 2: in a cycle or macro (PPPL free) */
    /* per-block word flags (0x32e5, 0x32e7, 0x32e4 bits) */
    int    nx, ny, nz, np, nf, nq, na, nr;
    int    g4arm;               /* G4 in this block: P/Q/'/' set the dwell */
    int    g73blk;              /* G73 in this block: A rotates, X/Y/Z scale */
    int    g92blk;              /* G92 in this block */
    int    g97blk;              /* G97 in this block */
    int    tblk;                /* T word seen: '/' fields are TLO/DIA */
    int    tnum;
    int    tslash;
    int    nloop;               /* '=N' seen: '/' is the repeat count */
    long   nloopseq;
    long   nloopcnt;
    int    endpend;             /* M2/M30 in this block */
    double clhs;                /* '?' left-hand value (0x3cec) */
    int    clet;                /* '?' left-hand letter */
    char   cop[2];              /* '<' operator letters (0x32f8) */
    /* ---- modal state ---- */
    int    mot;                 /* DXM_* (0x32f0) */
    int    abs;                 /* G90 = 1, G91 = 0 */
    int    plane;               /* 17 18 19 */
    int    metric;              /* G71 */
    int    g75;                 /* multi-quadrant */
    int    g9;                  /* modal decel override */
    int    g99;                 /* one-shot */
    int    ccomp;               /* 40 41 42 */
    int    mirx, miry;          /* G31 / G32 */
    double mirx0, miry0;        /* axis of symmetry */
    int    xform;               /* G73 active */
    double rot;                 /* G73 A, degrees */
    double scl[3];              /* G73 X Y Z */
    double g92[3];              /* G92 offset (inch) */
    double g97[3];              /* G97 fixture offset (inch) */
    double wsh[7][3];           /* G54..G59 work shifts (inch), [0] unused */
    int    work;                /* 54..59 */
    double cur[3];              /* current position, part program coordinates (inch) */
    double tgt[3];              /* target being built by this block */
    int    axset;               /* bits 1,2,4: X Y Z given in this block */
    double ijk[3];              /* arc centre / pole words of this block */
    int    ijkset;              /* bits 1,2,4 */
    double rword;               /* R word value (inch) */
    int    rset;
    double aword;               /* A word (degrees) */
    double araw;                /* A word as written (G12/G13: degrees of travel) */
    int    aset;                /* 1 = A (absolute), 2 = B (incremental) */
    double pole[2];             /* polar pole (part coords, inch) */
    double prad;                /* polar radius (inch) */
    int    poleset;
    double acur;                /* current polar angle (degrees, 0x3ee0) */
    double pre[3];              /* G92 / G97 / G54-59 preset words (program units) */
    int    premask;    int    gmot1;               /* one-shot motion of this block: 12, 13, 22, 23 or 0 */
    double feed;                /* program units per minute */
    double dwell;               /* seconds */
    int    tool, ntool;         /* active tool, tool in spindle */
    dxtool tools[DX_NTOOL];
    double spindle;
    int    sdir;                /* 3, 4, 5 */

    /* ---- options ---- */
    int    pppl;                /* param 285 bit 4 */
    int    compat;              /* BOSS 4-7 compatibility (0xaef9 bit 2) */
    int    strip;               /* strip blanks when loading */
    int    bdel;                /* block delete switch */
    int    opstop;              /* honour M1 */
    double peckclr;             /* param 272 peck clearance (inch) */
    int    vtrace;              /* trace variable assignments */
    int    p8;                  /* machine family flag loaded into P8 by G170-G179 */
    int    metric0;             /* power-on G71 */
    unsigned char preset[26][DX_NVAR]; /* -x presets */
    double prval[26][DX_NVAR];
    int    btrace;              /* echo every block */

    /* ---- output ---- */
    FILE  *trace;
    dxplot *plot;
    double dunit;               /* display units per inch (1 or 25.4) */
    long   nmoves;
    double lrapid, lfeed;
    double bmin[3], bmax[3];
    int    bset;
    int    warned[16];
    unsigned char cov[1024];    /* built-in blocks executed (coverage) */

    /* ---- errors ---- */
    jmp_buf err;
    char   emsg[160];
} dxsim;

/* ---- dxcorpus.c (generated) ---- */
extern const int dxcorpn;
extern const char *const dxcorptx[];
extern const long dxcorpof[];
extern const int dxcorpta[41];
extern const int dxcorptb[12];

/* ---- dxprog.c ---- */
int   dxload(dxsim *s, const char *fname, int ismacro);
int   dxldtxt(dxsim *s, const char *text);
void  dxlddone(dxsim *s);
const char *dxbtext(const dxsim *s, long blk);
long  dxbline(const dxsim *s, long blk);
void  dxbfree(dxsim *s);
void  dxbwhere(const dxsim *s, long blk, char *buf);

/* ---- dxexpr.c ---- */
int    dxlet(int c);
int    dxextlet(int li);
double dxnum(dxsim *s, const char **pp, int *flags);
double dxval(dxsim *s, const char **pp, int *flags);
int    dxvref(dxsim *s, int li, int flags, double v, double *out);
void   dxassgn(dxsim *s);
double dxexprs(dxsim *s, const char *text);
void   dxvset(dxsim *s, int li, int idx, double v);
double dxvget(dxsim *s, int li, int idx);

/* ---- dxinterp.c ---- */
void  dxinit(dxsim *s);
void  dxreset(dxsim *s);
int   dxrun(dxsim *s);
int   dxcheck(dxsim *s, FILE *out);
void  dxerror(dxsim *s, const char *msg);
void  dxwarn(dxsim *s, int once, const char *msg);

/* ---- dxmotion.c ---- */
void  dxmreset(dxsim *s);
void  dxmblock(dxsim *s);
void  dxmword(dxsim *s, int li, double v, int flags);
void  dxgcode(dxsim *s, int g);
void  dxmend(dxsim *s);
void  dxmpole(dxsim *s);
void  dxtopos(dxsim *s, const double *pp, double *base);
double dxunit(const dxsim *s);

/* ---- dxout.c ---- */
void  dxtline(dxsim *s, const char *text);
void  dxemit(dxsim *s, const dxseg *g);
void  dxplbeg(dxsim *s);
void  dxplend(dxsim *s);
void  dxsumm(dxsim *s);

#ifdef DX_GDDM
/* ---- dxgddm.c (z/OS, not built by default) ---- */
void  dxgdbeg(dxplot *p);
void  dxgdlin(dxplot *p, int pen, double x0, double y0, double x1, double y1);
void  dxgdend(dxplot *p);
#endif

#endif
