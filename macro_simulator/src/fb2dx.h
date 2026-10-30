/* fb2dx.h - FANUC custom macro (Macro A / Macro B) to DX32 translator.
   Design: ../features/FB2DX.md.  ANSI C89; external names unique in 8 characters. */

#ifndef FB2DX_H
#define FB2DX_H

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* FANUC series (FB2DX.md 3.7)                                          */

enum { FS_A, FS_0, FS_6, FS_10, FS_15, FS_16, FS_0I, FS_30I, FB_NSER };
#define FS_ALL   ((1 << FB_NSER) - 1)
#define FS_MACB  (FS_ALL & ~(1 << FS_A))

/* constructs that exist on some series only: they decide detection */
enum {
    FF_MACB,        /* #i=, [ ], IF, WHILE, G65 P with arguments: any Macro B */
    FF_THEN,        /* IF [c] THEN statement */
    FF_ASIN,        /* ASIN ACOS */
    FF_LNEXP,       /* LN EXP */
    FF_NEW30,       /* POW ADP, ATAN[a,b], ATAN[x], MOD */
    FF_LOGIC,       /* AND OR between conditions */
    FF_NAMED,       /* #_NAME system variables */
    FF_COMEXT,      /* common variables beyond #100-#149, #500-#531 */
    FB_NFEAT
};

/* constructs whose meaning differs by series or parameter */
enum {
    FM_DPI,         /* a number without a decimal point in a dimension word */
    FM_ATAN,        /* ATAN ASIN ACOS: result range (parameter NAT) */
    FM_RNDADR,      /* ROUND inside an address */
    FM_G65H,        /* G65 H..: Macro A operation or Macro B argument */
    FM_M98P,        /* M98 P with more than 4 digits */
    FM_AVAR,        /* Macro A: a variable in a dimension word */
    FB_NMEAN
};

/* parameter settings (-fparam); -1 = not given */
typedef struct fbpar {
    int dpi;        /* 0 least increments, 1 whole units */
    int nat;        /* ATAN range: 0 = 0..360, 1 = -180..180 */
    int is;         /* increment system: 'b' or 'c' */
    int o8;         /* 30i: 8-digit program numbers (M98 P is the program, L the count) */
    int avar;       /* Macro A variables in addresses: 0 least increments, 1 whole units */
    int units;      /* 20 inch, 21 metric, 0 unknown (-inch/-mm) */
    int dflt;       /* -fparam default given */
    int g28;        /* -g28: 0 not given, 1 m25 (quill home), 2 drop */
    int comp;       /* -comp dx32: the DX32's own G41/G42 lead-in is accepted for start-up type B */
    int sup;        /* FANUC start-up type of G41/G42 (parameter 5003 bit 0 SUP): 0 type A, 1 type B */
    double d83, d73;    /* G83 clearance, G73 retract (parameters 5115, 5114 on 16/0i/30i), program units */
    int hd83, hd73;     /* given */
} fbpar;

/* ------------------------------------------------------------------ */
/* expressions                                                          */

enum { EN_NUM, EN_VAR, EN_SYS, EN_NEG, EN_BIN, EN_FUN, EN_CMP, EN_LOG };

/* EN_BIN / EN_LOG operators */
enum { OP_ADD = 1, OP_SUB, OP_MUL, OP_DIV, OP_AND, OP_OR, OP_XOR, OP_MOD };
/* EN_CMP */
enum { CM_EQ = 1, CM_NE, CM_GT, CM_GE, CM_LT, CM_LE };
/* EN_FUN */
enum {
    FN_SIN = 1, FN_COS, FN_TAN, FN_ASIN, FN_ACOS, FN_ATAN, FN_ATAN2, FN_SQRT, FN_ABS,
    FN_BIN, FN_BCD, FN_ROUND, FN_FIX, FN_FUP, FN_LN, FN_EXP, FN_POW, FN_ADP
};

typedef struct fbn {
    int k;              /* EN_ */
    int op;             /* operator, comparison or function */
    double v;           /* EN_NUM */
    int pt;             /* EN_NUM: written with a decimal point */
    struct fbn *a, *b;  /* operands; EN_VAR: a = the index */
    char *name;         /* EN_SYS: the name after #_ */
} fbn;

/* ------------------------------------------------------------------ */
/* statements                                                           */

enum {
    ST_PROG,        /* O number: start of a program */
    ST_ASSIGN,      /* #i = e */
    ST_IFGOTO,      /* IF [c] GOTO n */
    ST_GOTO,        /* GOTO n */
    ST_IFTHEN,      /* IF [c] THEN #i = e */
    ST_WHILE,       /* WHILE [c] DOm */
    ST_DO,          /* DOm without WHILE */
    ST_END,         /* ENDm */
    ST_NC,          /* an NC block: words */
    ST_UNSUP        /* SETVN, DPRNT, ...: read but not translated */
};

#define FB_MAXW 32

typedef struct fbw {
    char l;             /* address letter */
    fbn *e;             /* value */
} fbw;

typedef struct fbs {
    int k;              /* ST_ */
    int line;           /* source line (counted over all input files) */
    int file;           /* input file number */
    long n;             /* N number, -1 if none */
    int bdel;           /* starts with / (block delete) */
    fbn *lhs, *e;       /* ASSIGN, IFTHEN: target and value; IF/WHILE: condition; GOTO: target */
    fbn *e2;            /* IFTHEN: the value assigned */
    int m;              /* DO/END/WHILE number */
    int pair;           /* WHILE/DO: index of its END; END: index of its WHILE/DO */
    long prog;          /* ST_PROG: the O number */
    int nw;
    fbw *w;             /* ST_NC words */
    char *text;         /* the block as written, comments removed */
    long nexec;         /* times the evaluator ran it */
    int vacbad;         /* the evaluator saw it use a vacant variable in a way the DX32 can't copy */
    long modes;         /* MO_ bits: the modal state the evaluator saw it run in */
    long tool;          /* M6: the T it changed to; -1 none, -2 more than one */
    long krep;          /* a hole block: its repeat count (K/L); -1 not run, -2 more than one */
    char *cmt;          /* the first comment in the block, or 0 (alarm messages) */
    int vtest;          /* its condition tests a vacant variable (#0, argument checks) */
    long ctrue, cfalse; /* how often its condition held / did not (IF, WHILE) */
} fbs;

/* fbs.modes */
#define MO_ABS    1     /* ran in G90 */
#define MO_INC    2     /* ran in G91 */
#define MO_LIN    4     /* moved the axes with G0/G1 */
#define MO_ARC    8     /* moved them with G2/G3 */
#define MO_UNK   16     /* an arc while the position in its plane was not known (G90 needs it) */
#define MO_NEGR  32     /* an R below 0 (the long arc) */
#define MO_P17   64     /* plane G17 / G18 / G19 */
#define MO_P18  128
#define MO_P19  256
#define MO_HOLE  512L   /* a drilling cycle hole (G81 G82 G83 G73) */
#define MO_C81  1024L
#define MO_C82  2048L
#define MO_C83  4096L
#define MO_C73  8192L
#define MO_R98 16384L   /* back to the initial level / to R */
#define MO_R99 32768L
#define MO_CST 65536L   /* the cycle started here (the initial level is taken) */
#define MO_NOMOT 131072L    /* axis words with no G0-G3 in force (after G80, or at the start) */
#define MO_HBAD 262144L     /* G43 H other than the tool in the spindle */
#define MO_UNKZ 524288L     /* a cycle started while Z was not known */
#define MO_DBAD 1048576L    /* G41/G42 D other than the tool in the spindle */
#define MO_C85  2097152L    /* boring: G85 feed out, G86 spindle stop and rapid out, G89 dwell and feed out */
#define MO_C86  4194304L
#define MO_C89  8388608L
#define MO_MIXC 16777216L   /* a hole in G90 in a cycle started in G91, or the other way round */
#define MO_KK   33554432L   /* the hole block repeats with K / with L */
#define MO_KL   67108864L
#define MO_ROT  134217728L  /* ran with a G68 rotation in force (the DX32 G73) */
#define MO_XYUNK 268435456L /* ran before X and Y were known */
#define MO_CYC  (MO_C81 | MO_C82 | MO_C83 | MO_C73 | MO_C85 | MO_C86 | MO_C89)

#define FB_MVN  512     /* moves per chunk */
#define FB_MVCH 32      /* chunks: 16384 moves at most */

/* a move of the FANUC tool path (evaluator), in inches, for -check (FB2DX.md 3.13) */
typedef struct fbmv {
    char k;             /* R rapid, F feed, 2 / 3 arc */
    int line;
    double x, y, z;
    int hasc;           /* a G17 arc with I J: its centre */
    double cx, cy;
} fbmv;

/* ------------------------------------------------------------------ */
/* shared state                                                         */

typedef struct fbx {
    fbs **st;           /* all statements of all input files */
    int nst, cap;
    int featl[FB_NFEAT];    /* first line that uses each construct, 0 if none */
    int meanl[FB_NMEAN];    /* first line of each meaning item, 0 if none */
    int ser;            /* -series given: FS_ index, or -1 */
    int cand;           /* candidate series mask after detection */
    int macroa;         /* the program is read as Macro A */
    int m98split;       /* M98 P > 4 digits = count * 10000 + program */
    fbpar par;
    int stop;           /* a REFUSED/STOP was reported */
    FILE *rep;          /* report */
    char *fname[8];
    int nfile;
    int bdelsw;         /* -bdel: -1 not given, 0 off, 1 on */
    char vset[1000];    /* the evaluator set #n at some point (any level) */
    fbmv *mvc[FB_MVCH]; /* the FANUC tool path, in chunks of FB_MVN (each under 64 KB for 16-bit DOS) */
    int nmv;
    int mvunk;          /* moves in an unknown unit system: no -check */
} fbx;

/* fbmain.c */
void fbrep(fbx *x, const char *kind, int line, const char *fmt, ...);
char *fbstrdup(const char *s);
void *fbmalloc(unsigned n);
void fbfmt(char *buf, double v);

/* fbread.c */
int fbread(fbx *x, FILE *fp, int file, int *line);
void fbpexpr(char *buf, const fbn *e);
void fbpstmt(FILE *fp, const fbs *s);
int fbisdim(int letter);
int fbpair(fbx *x);

/* fbdial.c */
int fbsfind(const char *key);
const char *fbskey(int ser);
void fbsnames(char *buf, int mask);
void fbslist(FILE *fp);
int fbdetect(fbx *x);
int fbmacroa(fbx *x);
int fbfparam(fbpar *p, const char *arg);
void fbfeat(fbx *x, int f, int line);
void fbmean(fbx *x, int m, int line);

/* fbeval.c */
int fbeval(fbx *x, FILE *out, long limit);

/* fbout.c */
int fbwrite(fbx *x, const char *path);

/* fbcheck.c */
fbmv *fbmvat(fbx *x, int i);
int fbcheck(fbx *x, const char *prog, const char *dxsim);

#endif
