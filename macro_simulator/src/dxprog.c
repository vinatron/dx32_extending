/*
 * dxprog.c - part program text: loading, normalising, block storage.
 *
 * The control keeps the program as NUL-terminated blocks.  A part program file
 * has one block per line (CR or CR/LF; the manual writes the end of block as
 * ';').  The block table is split into chunks so that no single object exceeds
 * 64 KB on 16-bit DOS compilers.  The built-in cycle text (dxcorpus.c) is
 * appended after the user blocks so one index addresses both.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dxsim.h"

static char *dupstr(dxsim *s, const char *t)
{
    char *p = (char *)malloc(strlen(t) + 1);
    if (!p) dxerror(s, "out of memory");
    strcpy(p, t);
    return p;
}

static void addblk(dxsim *s, const char *text, long line)
{
    long n = s->nblk;
    int c = (int)(n / DX_CHUNK), i = (int)(n % DX_CHUNK);
    if (c >= DX_MAXCHNK) dxerror(s, "program too long");
    if (!s->chunk[c]) {
        s->chunk[c] = (char **)malloc(DX_CHUNK * sizeof(char *));
        s->lchunk[c] = (long *)malloc(DX_CHUNK * sizeof(long));
        if (!s->chunk[c] || !s->lchunk[c]) dxerror(s, "out of memory");
    }
    s->chunk[c][i] = dupstr(s, text);
    s->lchunk[c][i] = line;
    s->nblk++;
}

const char *dxbtext(const dxsim *s, long blk)
{
    if (blk >= s->corp0 && blk < s->ntotal) return dxcorptx[blk - s->corp0];
    if (blk < 0 || blk >= s->nblk) return "";
    return s->chunk[blk / DX_CHUNK][blk % DX_CHUNK];
}

long dxbline(const dxsim *s, long blk)
{
    if (blk < 0 || blk >= s->nblk) return 0;
    return s->lchunk[blk / DX_CHUNK][blk % DX_CHUNK];
}

/* "L12" for a user line, "@26b9a" for a built-in block (firmware file offset) */
void dxbwhere(const dxsim *s, long blk, char *buf)
{
    if (blk >= s->corp0 && blk < s->ntotal)
        sprintf(buf, "@%05lx", dxcorpof[blk - s->corp0]);
    else
        sprintf(buf, "L%ld", dxbline(s, blk));
}

/*
 * Normalise one line: drop the line end, fold letters to upper case and (with
 * the strip option) remove blanks outside comments and messages.  The control
 * tests "letter digit =" at fixed offsets, so a blank before '=' would hide an
 * assignment; the manual's examples are typeset with blanks.
 */
static void normal(dxsim *s, char *dst, const char *src)
{
    int n = 0, inq = 0, incom = 0;
    int c;
    while ((c = (unsigned char)*src++) != 0 && c != '\n' && c != '\r') {
        if (n >= DX_MAXLINE - 1) break;
        if (incom || inq) {
            if (inq && c == inq) inq = 0;
            dst[n++] = (char)c;
            continue;
        }
        if (c == ';') incom = 1;
        else if (c == '\'' || c == '"' || c == '`') inq = c;
        else if ((c == ' ' || c == '\t') && s->strip) continue;
        else c = toupper(c);
        dst[n++] = (char)c;
    }
    dst[n] = 0;
    /* a lone '%' is a tape leader, not the '%' cycle-call character */
    if (dst[0] == '%' && dst[1] == 0) dst[0] = 0;
}

static void addline(dxsim *s, const char *line, long lno)
{
    char buf[DX_MAXLINE];
    normal(s, buf, line);
    addblk(s, buf, lno);
}

/* load a part program (ismacro = 0) or an M-macro file (1, loaded after the program) */
int dxload(dxsim *s, const char *fname, int ismacro)
{
    FILE *fp;
    char line[1024];
    long lno = 0;
    fp = fopen(fname, "r");
    if (!fp) return -1;
    if (!s->fname) s->fname = fname;
    while (fgets(line, sizeof line, fp)) {
        lno++;
        addline(s, line, lno);
    }
    fclose(fp);
    if (!ismacro) s->nuser = s->nblk;
    return 0;
}

/* load program text from a string (lines separated by '\n') */
int dxldtxt(dxsim *s, const char *text)
{
    char line[1024];
    long lno = 0;
    int n;
    while (*text) {
        n = 0;
        while (*text && *text != '\n') {
            if (n < (int)sizeof line - 1) line[n++] = *text;
            text++;
        }
        if (*text == '\n') text++;
        line[n] = 0;
        addline(s, line, ++lno);
    }
    s->nuser = s->nblk;
    return 0;
}

/* all user text is in: place the built-in blocks after it */
void dxlddone(dxsim *s)
{
    s->corp0 = s->nblk;
    s->ntotal = s->nblk + dxcorpn;
}

void dxbfree(dxsim *s)
{
    long i;
    for (i = 0; i < s->nblk; i++) free(s->chunk[i / DX_CHUNK][i % DX_CHUNK]);
    for (i = 0; i < DX_MAXCHNK; i++) {
        if (s->chunk[i]) free(s->chunk[i]);
        if (s->lchunk[i]) free(s->lchunk[i]);
        s->chunk[i] = 0;
        s->lchunk[i] = 0;
    }
    s->nblk = s->nuser = s->corp0 = s->ntotal = 0;
}
