/*
 * dxmap.c - the call map (-map FILE; features/FLOW.md phase 4b).
 *
 * Two sources go into it:
 *   - a scan of the program text (dxmapscan): every macro and M-macro
 *     definition, every call site (=#n, M<n> of a defined M-macro), every
 *     canned-cycle G code, and labels L8/L9; each with the part of the program
 *     it sits in (main program, or the body of a definition);
 *   - the run (dxmapcall): how often each call was actually made.
 * A call in the text that the run never made is drawn dashed / listed as
 * "never".  The output is an indented text tree, or an SVG diagram with the
 * main program on the left and callees in columns by call depth.
 *
 * Node ids are those of dxsrc(): 0 main, n macro #n, 1000+m M-macro,
 * 2000+g canned cycle.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dxsim.h"

#define MNODE   64
#define MEDGE   128
#define MLINE   4

typedef struct mnode { long id; long defl; int col, row; } mnode;
typedef struct medge { long from, to; int nsite; long line[MLINE]; long count; } medge;

static mnode nodes[MNODE];
static medge edges[MEDGE];
static int nnode, nedge, maxdepth, l89, anycyc;

static int nodeix(long id)
{
    int i;
    for (i = 0; i < nnode; i++)
        if (nodes[i].id == id) return i;
    if (nnode >= MNODE) return MNODE - 1;
    nodes[nnode].id = id;
    nodes[nnode].defl = -1;
    nodes[nnode].col = -1;
    return nnode++;
}

static medge *edge(long from, long to)
{
    int i;
    for (i = 0; i < nedge; i++)
        if (edges[i].from == from && edges[i].to == to) return &edges[i];
    if (nedge >= MEDGE) return &edges[MEDGE - 1];
    memset(&edges[nedge], 0, sizeof edges[nedge]);
    edges[nedge].from = from;
    edges[nedge].to = to;
    nodeix(from);
    nodeix(to);
    return &edges[nedge++];
}

static int ismmac(const dxsim *s, long m)
{
    int i;
    for (i = 0; i < s->nmac; i++)
        if (s->macnum[i] == m) return 1;
    return 0;
}

static int iscycle(long g)
{
    return (g >= 77 && g <= 79) || (g >= 81 && g <= 89) || (g >= 170 && g <= 179) ||
           (g >= 181 && g <= 189) || (g >= 191 && g <= 199);
}

/* the program text: definitions, call sites, cycles, L8/L9 */
void dxmapscan(dxsim *s)
{
    long b, ctx = 0, n;
    nnode = nedge = maxdepth = l89 = anycyc = 0;
    nodeix(0);
    nodes[0].defl = 1;
    for (b = 0; b < s->nblk; b++) {
        const char *t = dxbtext(s, b), *p = t, *q;
        int prev = 0, c, first = 1, endctx = 0;
        while ((c = (unsigned char)*p) != 0) {
            if (c == ';') break;
            if (c == '\'' || c == '"' || c == '`') {
                p++;
                while (*p && *p != c) p++;
                if (*p) p++;
                prev = 0;
                continue;
            }
            if ((c == '#' || c == 'M' || c == 'G' || c == 'L') && isdigit((unsigned char)p[1])) {
                n = strtol(p + 1, (char **)&q, 10);
                if (c == '#' && prev != '=') {                  /* definition */
                    ctx = n;
                    nodes[nodeix(n)].defl = dxbline(s, b);
                } else if (c == '#') {                          /* =#n */
                    medge *e = edge(ctx, n);
                    if (e->nsite < MLINE) e->line[e->nsite] = dxbline(s, b);
                    e->nsite++;
                } else if (c == 'M') {
                    const char *r = q;
                    while (*r == ' ' || *r == '\t') r++;
                    if (*r == ':') {
                        ctx = 1000 + n;
                        nodes[nodeix(ctx)].defl = dxbline(s, b);
                        q = r + 1;
                    } else if (ismmac(s, n)) {
                        medge *e = edge(ctx, 1000 + n);
                        if (e->nsite < MLINE) e->line[e->nsite] = dxbline(s, b);
                        e->nsite++;
                    }
                } else if (c == 'G' && iscycle(n)) {
                    medge *e = edge(ctx, 2000 + n);
                    if (e->nsite < MLINE) e->line[e->nsite] = dxbline(s, b);
                    e->nsite++;
                    anycyc = 1;
                } else if (c == 'L' && first && (n == 8 || n == 9)) l89 = 1;
                prev = c;
                p = q;
                first = 0;
                continue;
            }
            if (c == '$' || c == '|') endctx = 1;
            if (c != ' ' && c != '\t' && c != 'N' && !isdigit(c)) first = 0;
            prev = c;
            p++;
        }
        if (endctx) ctx = 0;
    }
}

/* a call made by the run */
void dxmapcall(dxsim *s, long from, long to)
{
    medge *e = edge(from, to);
    e->count++;
    if (s->gdepth + 1 > maxdepth) maxdepth = s->gdepth + 1;
}

/* columns: the shortest call depth from the main program */
static void depths(void)
{
    int changed = 1, i, a, b;
    nodes[nodeix(0)].col = 0;
    while (changed) {
        changed = 0;
        for (i = 0; i < nedge; i++) {
            a = nodeix(edges[i].from);
            b = nodeix(edges[i].to);
            if (nodes[a].col >= 0 && (nodes[b].col < 0 || nodes[b].col > nodes[a].col + 1) &&
                nodes[a].col + 1 < MNODE) {
                nodes[b].col = nodes[a].col + 1;
                changed = 1;
            }
        }
    }
}

static void lines(const medge *e, char *out)
{
    int i;
    char b[16];
    out[0] = 0;
    for (i = 0; i < e->nsite && i < MLINE; i++) {
        sprintf(b, "%s%ld", i ? ", " : "", e->line[i]);
        strcat(out, b);
    }
    if (e->nsite > MLINE) strcat(out, ", ...");
}

/* the longest chain of calls in the text, -1 if it has a cycle (recursion) */
static int chain(long id, int depth, long *path)
{
    int i, k, best = depth, d;
    for (k = 0; k < depth; k++)
        if (path[k] == id) return -1;
    if (depth >= 32) return depth;
    path[depth] = id;
    for (i = 0; i < nedge; i++)
        if (edges[i].from == id && edges[i].to < 2000 && edges[i].nsite > 0) {
            d = chain(edges[i].to, depth + 1, path);
            if (d < 0) return -1;
            if (d > best) best = d;
        }
    return best;
}

static void ttree(dxsim *s, FILE *fp, long id, int depth, long *path)
{
    int i, k;
    char name[100], ls[64], cnt[48];
    for (k = 0; k < depth; k++)
        if (path[k] == id) return;
    path[depth] = id;
    for (i = 0; i < nedge; i++) {
        medge *e = &edges[i];
        if (e->from != id) continue;
        dxsrcname(s, e->to, name);
        lines(e, ls);
        if (e->count) sprintf(cnt, "%ld time%s", e->count, e->count > 1 ? "s" : "");
        else strcpy(cnt, "never reached in this run");
        if (*ls)
            fprintf(fp, "%*s%s  (line%s %s): %s\n", 2 * depth + 2, "", name, e->nsite > 1 ? "s" : "", ls, cnt);
        else fprintf(fp, "%*s%s: %s\n", 2 * depth + 2, "", name, cnt);
        for (k = 0; k < depth; k++)
            if (path[k] == e->to) break;
        if (k < depth || e->to == id) fprintf(fp, "%*s  (recursive call)\n", 2 * depth + 2, "");
        else if (e->to < 2000 && depth < 30) ttree(s, fp, e->to, depth + 1, path);
    }
}

static void notes(dxsim *s, FILE *fp, const char *pre)
{
    int i, j, called;
    long path[40];
    char name[100];
    for (i = 0; i < nnode; i++) {
        if (nodes[i].id == 0 || nodes[i].id >= 2000) continue;
        called = 0;
        for (j = 0; j < nedge; j++)
            if (edges[j].to == nodes[i].id && edges[j].nsite) called = 1;
        dxsrcname(s, nodes[i].id, name);
        if (!called && nodes[i].defl >= 0)
            fprintf(fp, "%sWARN  %s (defined at line %ld) is never called\n", pre, name, nodes[i].defl);
        if (called && nodes[i].defl < 0)
            fprintf(fp, "%sWARN  %s is called but never defined (Parser: Call Of Undefined Loop Or Macro)\n",
                    pre, name);
    }
    j = chain(0, 0, path);
    if (j < 0) fprintf(fp, "%sWARN  a macro calls itself, directly or through others (recursion)\n", pre);
    else if (j > 8)
        fprintf(fp, "%sWARN  the text nests calls %d deep: the 6.89 limit is 8 (the manuals say 4)\n", pre, j);
    if (l89 && anycyc)
        fprintf(fp, "%sWARN  the program uses label L8 or L9 and canned cycles: the cycles use L8 and L9 "
                "too (manual ch.10)\n", pre);
}

static void svgout(dxsim *s, FILE *fp)
{
    int i, ncol = 0, rows[MNODE], maxrow = 0, a, b;
    double bw = 230, bh = 34, gx = 90, gy = 20, x0, y0, x1, y1;
    char name[100], ls[64];
    depths();
    memset(rows, 0, sizeof rows);
    for (i = 0; i < nnode; i++) {
        if (nodes[i].col < 0) nodes[i].col = 0;         /* never called: in the first column */
        if (nodes[i].col >= MNODE) nodes[i].col = MNODE - 1;
        nodes[i].row = rows[nodes[i].col]++;
        if (nodes[i].col + 1 > ncol) ncol = nodes[i].col + 1;
        if (nodes[i].row + 1 > maxrow) maxrow = nodes[i].row + 1;
    }
    fprintf(fp, "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" "
            "height=\"%.0f\" font-family=\"sans-serif\" font-size=\"12\">\n",
            ncol * (bw + gx) + 40 < 820 ? 820 : ncol * (bw + gx) + 40, 40 + maxrow * (bh + gy) + 16 * 6);
    fprintf(fp, "<defs><marker id=\"a\" markerWidth=\"8\" markerHeight=\"8\" refX=\"8\" refY=\"4\" "
            "orient=\"auto\"><path d=\"M0,0 L8,4 L0,8 z\" fill=\"#555\"/></marker></defs>\n");
    for (i = 0; i < nedge; i++) {
        a = nodeix(edges[i].from);
        b = nodeix(edges[i].to);
        x0 = 10 + nodes[a].col * (bw + gx) + bw;
        y0 = 30 + nodes[a].row * (bh + gy) + bh / 2;
        x1 = 10 + nodes[b].col * (bw + gx);
        y1 = 30 + nodes[b].row * (bh + gy) + bh / 2;
        if (a == b) {                                   /* a macro calling itself: a loop on the right */
            fprintf(fp, "<path d=\"M%.0f,%.0f C%.0f,%.0f %.0f,%.0f %.0f,%.0f\" stroke=\"#555\" fill=\"none\" "
                    "marker-end=\"url(#a)\"%s/>\n", x0, y0 - 8, x0 + 45, y0 - 30, x0 + 45, y0 + 30, x0,
                    y0 + 8, edges[i].count ? "" : " stroke-dasharray=\"5,4\"");
            fprintf(fp, "<text x=\"%.0f\" y=\"%.0f\" fill=\"#555\" font-size=\"10\">%ld&#215; (itself)</text>\n",
                    x0 + 40, y0 + 4, edges[i].count);
            continue;
        }
        if (nodes[b].col <= nodes[a].col) {             /* back or sideways: from the bottom edge */
            x0 -= bw / 2;
            y0 += bh / 2;
            x1 += bw / 2;
            y1 += bh / 2 * (y1 > y0 ? -1 : 1);
        }
        fprintf(fp, "<path d=\"M%.0f,%.0f L%.0f,%.0f\" stroke=\"#555\" fill=\"none\" "
                "marker-end=\"url(#a)\"%s/>\n", x0, y0, x1, y1,
                edges[i].count ? "" : " stroke-dasharray=\"5,4\"");
        lines(&edges[i], ls);
        fprintf(fp, "<text x=\"%.0f\" y=\"%.0f\" fill=\"#555\" font-size=\"10\">%ld&#215;%s%s</text>\n",
                (x0 + x1) / 2, (y0 + y1) / 2 - 4, edges[i].count, *ls ? "  line " : "", ls);
    }
    for (i = 0; i < nnode; i++) {
        int j, called = nodes[i].id == 0 || nodes[i].id >= 2000;
        const char *stroke = "stroke=\"#1f4fbf\"";
        for (j = 0; j < nedge; j++)
            if (edges[j].to == nodes[i].id && edges[j].nsite) called = 1;
        if (nodes[i].id > 0 && nodes[i].id < 2000 && nodes[i].defl < 0)
            stroke = "stroke=\"#c62828\" stroke-width=\"2\"";           /* called, never defined */
        else if (!called) stroke = "stroke=\"#999\" stroke-dasharray=\"5,4\"";  /* never called */
        x0 = 10 + nodes[i].col * (bw + gx);
        y0 = 30 + nodes[i].row * (bh + gy);
        dxsrcname(s, nodes[i].id, name);
        fprintf(fp, "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" rx=\"6\" fill=\"%s\" "
                "%s/>\n", x0, y0, bw, bh, nodes[i].id >= 2000 ? "#eef3ee" : "#eef2fb", stroke);
        fprintf(fp, "<text x=\"%.0f\" y=\"%.0f\">%.34s</text>\n", x0 + 8, y0 + 21, name);
    }
    fprintf(fp, "<text x=\"10\" y=\"18\" fill=\"#555\">call map: solid arrow = called in this run (count, "
            "calling lines); dashed = never reached; red box = not defined; grey dashed box = never called</text>\n");
    {
        char note[200];
        FILE *tmp;
        int k = 0;
        /* the warnings, one text line each under the boxes */
        tmp = tmpfile();
        if (tmp) {
            notes(s, tmp, "");
            rewind(tmp);
            while (fgets(note, sizeof note, tmp)) {
                note[strcspn(note, "\n")] = 0;
                fprintf(fp, "<text x=\"10\" y=\"%.0f\" fill=\"#c62828\">%s</text>\n",
                        40 + maxrow * (bh + gy) + 16.0 * k++, note);
            }
            fclose(tmp);
        }
    }
    fprintf(fp, "</svg>\n");
}

/* write the map; SVG when the name ends in .svg, text otherwise */
int dxmapout(dxsim *s, const char *file)
{
    FILE *fp = fopen(file, "w");
    long path[40];
    size_t n = strlen(file);
    if (!fp) return 1;
    if (n > 4 && (!strcmp(file + n - 4, ".svg") || !strcmp(file + n - 4, ".SVG"))) svgout(s, fp);
    else {
        fprintf(fp, "call map: calls in the program text, with how often this run made them\n");
        fprintf(fp, "main program\n");
        ttree(s, fp, 0, 0, path);
        fprintf(fp, "deepest nesting in this run: %d level%s (6.89 allows 8)\n", maxdepth,
                maxdepth == 1 ? "" : "s");
        notes(s, fp, "");
    }
    fclose(fp);
    return 0;
}
