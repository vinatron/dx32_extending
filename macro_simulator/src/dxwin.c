/*
 * dxwin.c - Win32 GDI version of the graph.lib/conio calls used by the live
 * preview (dxscreen.c), for DXSIMW.EXE on Windows 95/98/NT and later.
 *
 * Built with -DDX_WIN (makefile.wat target win32w).  The preview draws into a
 * 640x480 off-screen bitmap that WM_PAINT copies to a window of the same size,
 * so the picture survives being covered or moved.  Keys come from WM_CHAR;
 * closing the window counts as ESC.  Messages are processed whenever the
 * preview reads the keyboard, waits, or about every 30 ms while drawing.
 * Only Win95-era calls are used.  windows.h is not strict ANSI, so this file is
 * compiled without -za.
 */
#ifdef DX_WIN

#include <windows.h>
#include <string.h>
#include "dxgraph.h"

#define W       640
#define H       480
#define KEYQ    32

static HWND wnd;
static HDC mem;
static HBITMAP bmp, oldbmp;
static HFONT font, oldfont;
static HPEN pens[16][2], oldpen;
static int colour = 15, dash, tcolour = 15, tx, ty;
static int keyq[KEYQ], khead, ktail, closed;
static RECT clip = { 0, 0, W, H }, dirty;
static int isdirty;
static DWORD lastpump;

static const COLORREF vga[16] = {
    RGB(0, 0, 0), RGB(0, 0, 170), RGB(0, 170, 0), RGB(0, 170, 170),
    RGB(170, 0, 0), RGB(170, 0, 170), RGB(170, 85, 0), RGB(170, 170, 170),
    RGB(85, 85, 85), RGB(85, 85, 255), RGB(85, 255, 85), RGB(85, 255, 255),
    RGB(255, 85, 85), RGB(255, 85, 255), RGB(255, 255, 85), RGB(255, 255, 255)
};

static void putkey(int c)
{
    int n = (ktail + 1) % KEYQ;
    if (n != khead) {
        keyq[ktail] = c;
        ktail = n;
    }
}

static LRESULT CALLBACK wproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC dc;
    switch (msg) {
    case WM_PAINT:
        dc = BeginPaint(h, &ps);
        if (mem)
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CHAR:
        putkey((int)(wp & 0xff));
        return 0;
    case WM_CLOSE:                  /* the preview closes the window itself, at the end */
        closed = 1;
        putkey(27);
        return 0;
    default:
        return DefWindowProc(h, msg, wp, lp);
    }
}

static void adddirty(int x0, int y0, int x1, int y1)
{
    RECT r;
    r.left = (x0 < x1 ? x0 : x1) - 1;
    r.top = (y0 < y1 ? y0 : y1) - 1;
    r.right = (x0 > x1 ? x0 : x1) + 2;
    r.bottom = (y0 > y1 ? y0 : y1) + 2;
    if (!isdirty) dirty = r;
    else UnionRect(&dirty, &dirty, &r);
    isdirty = 1;
}

static void pump(void)
{
    MSG msg;
    if (isdirty && wnd) {
        InvalidateRect(wnd, &dirty, FALSE);
        isdirty = 0;
    }
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    lastpump = GetTickCount();
}

static void setpen(void)
{
    HPEN p = pens[colour][dash];
    if (!p) p = pens[colour][dash] = CreatePen(dash ? PS_DASH : PS_SOLID, 1, vga[colour]);
    SelectObject(mem, p);
}

static void freeall(void)
{
    int i, k;
    if (mem) {
        SelectObject(mem, oldpen);
        SelectObject(mem, oldfont);
        SelectObject(mem, oldbmp);
        DeleteDC(mem);
        mem = 0;
    }
    if (bmp) DeleteObject(bmp);
    if (font) DeleteObject(font);
    bmp = 0;
    font = 0;
    for (i = 0; i < 16; i++)
        for (k = 0; k < 2; k++)
            if (pens[i][k]) {
                DeleteObject(pens[i][k]);
                pens[i][k] = 0;
            }
}

short dxwmode(short mode)
{
    static int registered;
    WNDCLASS wc;
    RECT r;
    HDC dc;
    HINSTANCE hi = GetModuleHandle(NULL);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    if (mode == _DEFAULTMODE) {
        if (wnd) DestroyWindow(wnd);
        wnd = 0;
        freeall();
        pump();
        return 1;
    }
    if (!registered) {
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = wproc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = "DXSIMPREVIEW";
        if (!RegisterClass(&wc)) return 0;
        registered = 1;
    }
    r.left = r.top = 0;
    r.right = W;
    r.bottom = H;
    AdjustWindowRect(&r, style, FALSE);
    wnd = CreateWindow("DXSIMPREVIEW", "dxsim preview  (SPACE pause, B block, +/- speed, F pacing, ESC stop)",
                       style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                       NULL, NULL, hi, NULL);
    if (!wnd) return 0;
    dc = GetDC(wnd);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, W, H);
    ReleaseDC(wnd, dc);
    oldbmp = (HBITMAP)SelectObject(mem, bmp);
    PatBlt(mem, 0, 0, W, H, BLACKNESS);
    font = CreateFont(16, 8, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    oldfont = (HFONT)SelectObject(mem, font ? (HGDIOBJ)font : GetStockObject(ANSI_FIXED_FONT));
    oldpen = (HPEN)SelectObject(mem, GetStockObject(WHITE_PEN));
    SetBkColor(mem, RGB(0, 0, 0));
    SetBkMode(mem, OPAQUE);
    closed = 0;
    khead = ktail = 0;
    ShowWindow(wnd, SW_SHOWNORMAL);
    UpdateWindow(wnd);
    SetForegroundWindow(wnd);
    pump();
    return 1;
}

short dxwcolor(short c)
{
    short old = (short)colour;
    colour = c & 15;
    if (mem) setpen();
    return old;
}

unsigned short dxwlstyl(unsigned short style)
{
    unsigned short old = (unsigned short)(dash ? 0xf0f0 : 0xffff);
    dash = style != 0xffff;
    if (mem) setpen();
    return old;
}

void dxwmove(short x, short y)
{
    if (mem) MoveToEx(mem, x, y, NULL);
    tx = x;
    ty = y;
}

short dxwline(short x, short y)
{
    if (!mem) return 0;
    LineTo(mem, x, y);
    SetPixel(mem, x, y, vga[colour]);       /* LineTo leaves out the last point */
    adddirty(tx, ty, x, y);
    tx = x;
    ty = y;
    if (GetTickCount() - lastpump > 30) pump();
    return 1;
}

void dxwclip(short x0, short y0, short x1, short y1)
{
    HRGN rg;
    clip.left = x0;
    clip.top = y0;
    clip.right = x1 + 1;
    clip.bottom = y1 + 1;
    if (!mem) return;
    rg = CreateRectRgn(clip.left, clip.top, clip.right, clip.bottom);
    SelectClipRgn(mem, rg);
    DeleteObject(rg);
}

void dxwclear(short area)
{
    (void)area;
    if (!mem) return;
    SelectClipRgn(mem, NULL);
    PatBlt(mem, 0, 0, W, H, BLACKNESS);
    adddirty(0, 0, W - 1, H - 1);
    pump();
}

short dxwtcol(short c)
{
    short old = (short)tcolour;
    tcolour = c & 15;
    return old;
}

void dxwtpos(short row, short col)
{
    tx = (col - 1) * 8;
    ty = (row - 1) * 16;
}

void dxwtext(char *text)
{
    HRGN rg;
    int n = (int)strlen(text);
    if (!mem) return;
    SelectClipRgn(mem, NULL);               /* status lines lie outside the drawing area */
    SetTextColor(mem, vga[tcolour]);
    TextOut(mem, tx, ty, text, n);
    adddirty(tx, ty, tx + 8 * n, ty + 16);
    tx += 8 * n;
    rg = CreateRectRgn(clip.left, clip.top, clip.right, clip.bottom);
    SelectClipRgn(mem, rg);
    DeleteObject(rg);
    pump();
}

/* the nearest of the 16 VGA colours (smoothed text, or a display with fewer colours, has others) */
static short nearest(COLORREF c)
{
    long d, best = 0x7fffffffL;
    short i, k = 0;
    if (c == CLR_INVALID) return -1;
    for (i = 0; i < 16; i++) {
        long r = (long)GetRValue(c) - GetRValue(vga[i]), g = (long)GetGValue(c) - GetGValue(vga[i]),
             b = (long)GetBValue(c) - GetBValue(vga[i]);
        d = r * r + g * g + b * b;
        if (d < best) { best = d; k = i; }
    }
    return k;
}

/* the colour index of a pixel (-bmp) */
short dxwgetpx(short x, short y)
{
    short k;
    if (!mem) return 0;
    k = nearest(GetPixel(mem, x, y));
    return k < 0 ? 0 : k;
}

/* -Sk: a scripted key, sent through the window like a typed one; 'w' closes the window */
int dxwpost(int c)
{
    if (!wnd) return c == 'w' ? 27 : c;
    if (c == 'w') PostMessage(wnd, WM_CLOSE, 0, 0);
    else PostMessage(wnd, WM_CHAR, (WPARAM)c, 0);
    return dxwgetch();
}

/* -Sk r: cover the window with another one, take it away, and compare what WM_PAINT drew with the
   bitmap (every second pixel of the part on the desktop); the number that differ */
long dxwcheck(void)
{
    HWND top;
    HDC dc;
    POINT o;
    long bad = 0;
    int x, y;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (!wnd || !mem) return 0;
    /* on top, so nothing else (the console it was started from) covers it while it is read back */
    SetWindowPos(wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    o.x = o.y = 0;
    ClientToScreen(wnd, &o);
    top = CreateWindow("STATIC", "", WS_POPUP | WS_VISIBLE | WS_BORDER, o.x + 40, o.y + 40, W - 80, H - 80,
                       wnd, NULL, GetModuleHandle(NULL), NULL);
    pump();
    Sleep(50);
    if (top) DestroyWindow(top);
    pump();
    UpdateWindow(wnd);
    dc = GetDC(wnd);
    for (y = 0; y < H; y += 2)
        for (x = 0; x < W; x += 2) {
            if (o.x + x < 0 || o.x + x >= sw || o.y + y < 0 || o.y + y >= sh) continue;   /* off the desktop */
            if (nearest(GetPixel(dc, x, y)) != nearest(GetPixel(mem, x, y))) bad++;
        }
    ReleaseDC(wnd, dc);
    SetWindowPos(wnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    return bad;
}

int dxwkbhit(void)
{
    pump();
    return khead != ktail;
}

int dxwgetch(void)
{
    MSG msg;
    int c;
    pump();
    while (khead == ktail) {
        if (closed || !wnd) return 27;
        if (GetMessage(&msg, NULL, 0, 0) <= 0) return 27;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    c = keyq[khead];
    khead = (khead + 1) % KEYQ;
    return c;
}

void dxwdelay(unsigned ms)
{
    DWORD end = GetTickCount() + ms;
    long left;
    for (;;) {
        pump();
        left = (long)(end - GetTickCount());
        if (left <= 0 || closed) break;
        Sleep(left > 10 ? 10 : (DWORD)left);
    }
}

#else

/* ISO C forbids an empty file */
int dxwnone;

#endif
