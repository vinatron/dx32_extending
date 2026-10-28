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
    int modes;          /* MO_ bits: the modal state the evaluator saw it run in */
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

#endif
