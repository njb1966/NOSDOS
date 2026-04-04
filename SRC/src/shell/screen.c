/* NOS-DOS: NOS-SHELL
 * screen.c - Direct video memory screen rendering.
 *
 * All drawing goes straight to the CGA text buffer at B800:0000.
 * Each cell is 2 bytes: character (low) + attribute (high).
 * Offset of cell (col, row) = (row * g_scr_cols + col) * 2.
 *
 * INT 10h is used only in nos_scr_init() and nos_scr_cursor() — never
 * inside the hot drawing paths.
 *
 * License: GPL-2.0
 */

#include <dos.h>    /* int86, MK_FP */
#include <string.h> /* strlen */
#include "screen.h"

/* -----------------------------------------------------------------------
 * Video buffer — far pointer to B800:0000
 * ----------------------------------------------------------------------- */

/* Each cell: byte 0 = character, byte 1 = attribute. */
#define VIDEO_SEG  0xB800
#define VIDEO_OFS  0x0000

static unsigned char far *g_video = NULL;

/* -----------------------------------------------------------------------
 * Screen geometry (filled in by nos_scr_init)
 * ----------------------------------------------------------------------- */

int g_scr_cols = 80;
int g_scr_rows = 25;

/* Saved cursor shape so we can restore it on exit. */
static unsigned int g_saved_cursor_shape = 0x0607; /* CGA default underline */

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Return byte offset of cell (col, row) in the video buffer. */
#define CELL(col, row)  (((row) * g_scr_cols + (col)) * 2)

static int scr_clip_col(int col) { return (col < 0) ? 0 : (col >= g_scr_cols) ? g_scr_cols - 1 : col; }
static int scr_clip_row(int row) { return (row < 0) ? 0 : (row >= g_scr_rows) ? g_scr_rows - 1 : row; }

/* -----------------------------------------------------------------------
 * Init / restore
 * ----------------------------------------------------------------------- */

void nos_scr_init(void)
{
    union REGS r;

    /* Set far pointer to CGA text buffer. */
    g_video = (unsigned char far *)MK_FP(VIDEO_SEG, VIDEO_OFS);

    /* Read current video mode (INT 10h / AH=0Fh).
     * Returns: AL=mode, AH=cols, BH=active page. */
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    g_scr_cols = (r.h.ah == 0) ? 80 : (int)(unsigned char)r.h.ah;

    /* Determine rows: INT 10h / AX=1130h returns rows-1 in DL.
     * Fallback to 25 if BIOS doesn't support it (very old hardware). */
    r.x.ax = 0x1130;
    r.h.bh = 0x00;
    int86(0x10, &r, &r);
    g_scr_rows = ((int)(unsigned char)r.h.dl > 0) ? (int)(unsigned char)r.h.dl + 1 : 25;
    /* Clamp to sane values: 25 or 50. */
    if (g_scr_rows != 50) g_scr_rows = 25;

    /* Save cursor shape (INT 10h / AH=03h, BH=page 0).
     * Returns shape in CX: CH=start scan, CL=end scan. */
    r.h.ah = 0x03;
    r.h.bh = 0x00;
    int86(0x10, &r, &r);
    g_saved_cursor_shape = r.x.cx;

    nos_scr_hide_cursor();
}

void nos_scr_restore(void)
{
    union REGS r;

    /* Restore cursor shape (INT 10h / AH=01h). */
    r.h.ah = 0x01;
    r.x.cx = g_saved_cursor_shape;
    int86(0x10, &r, &r);

    /* Move cursor to bottom-left so it's visible on return to DOS. */
    nos_scr_cursor(0, g_scr_rows - 1);
}

/* -----------------------------------------------------------------------
 * Clear
 * ----------------------------------------------------------------------- */

void nos_scr_clear(unsigned char attr)
{
    nos_scr_fill(0, 0, g_scr_cols, g_scr_rows, ' ', attr);
}

/* -----------------------------------------------------------------------
 * Single character / string output
 * ----------------------------------------------------------------------- */

void nos_scr_putchar(int col, int row, unsigned char ch, unsigned char attr)
{
    int off;
    if (col < 0 || col >= g_scr_cols || row < 0 || row >= g_scr_rows)
        return;
    off = CELL(col, row);
    g_video[off]     = ch;
    g_video[off + 1] = attr;
}

void nos_scr_puts(int col, int row, const char *s, unsigned char attr)
{
    int off;
    if (row < 0 || row >= g_scr_rows || col >= g_scr_cols)
        return;
    if (col < 0) { s += -col; col = 0; }
    off = CELL(col, row);
    while (*s && col < g_scr_cols) {
        g_video[off]     = (unsigned char)*s;
        g_video[off + 1] = attr;
        off += 2;
        s++;
        col++;
    }
}

void nos_scr_putn(int col, int row, const char *s, int n, unsigned char attr)
{
    int off, i, slen;
    if (row < 0 || row >= g_scr_rows || col >= g_scr_cols || n <= 0)
        return;
    if (col < 0) {
        int skip = -col;
        s   += (skip < n) ? skip : n;
        n   -= (skip < n) ? skip : n;
        col  = 0;
    }
    if (n > g_scr_cols - col)
        n = g_scr_cols - col;

    slen = 0;
    while (s[slen] && slen < n) slen++;

    off = CELL(col, row);
    for (i = 0; i < n; i++) {
        g_video[off]     = (i < slen) ? (unsigned char)s[i] : ' ';
        g_video[off + 1] = attr;
        off += 2;
    }
}

/* -----------------------------------------------------------------------
 * Fill / box drawing
 * ----------------------------------------------------------------------- */

void nos_scr_fill(int col, int row, int w, int h,
                  unsigned char ch, unsigned char attr)
{
    int r, c, off, c0, r0, c1, r1;
    /* Clip to screen — all vars declared first (C89). */
    c0 = (col < 0) ? 0 : col;
    r0 = (row < 0) ? 0 : row;
    c1 = col + w; if (c1 > g_scr_cols) c1 = g_scr_cols;
    r1 = row + h; if (r1 > g_scr_rows) r1 = g_scr_rows;
    for (r = r0; r < r1; r++) {
        off = CELL(c0, r);
        for (c = c0; c < c1; c++) {
            g_video[off]     = ch;
            g_video[off + 1] = attr;
            off += 2;
        }
    }
}

void nos_scr_hline(int col, int row, int len,
                   unsigned char ch, unsigned char attr)
{
    nos_scr_fill(col, row, len, 1, ch, attr);
}

void nos_scr_box(int col, int row, int w, int h, unsigned char attr)
{
    int i;
    if (w < 2 || h < 2) return;

    /* Top and bottom edges */
    nos_scr_putchar(col,         row,         NOS_CH_TL, attr);
    nos_scr_putchar(col + w - 1, row,         NOS_CH_TR, attr);
    nos_scr_putchar(col,         row + h - 1, NOS_CH_BL, attr);
    nos_scr_putchar(col + w - 1, row + h - 1, NOS_CH_BR, attr);

    for (i = 1; i < w - 1; i++) {
        nos_scr_putchar(col + i, row,         NOS_CH_H, attr);
        nos_scr_putchar(col + i, row + h - 1, NOS_CH_H, attr);
    }
    for (i = 1; i < h - 1; i++) {
        nos_scr_putchar(col,         row + i, NOS_CH_V, attr);
        nos_scr_putchar(col + w - 1, row + i, NOS_CH_V, attr);
    }
}

void nos_scr_dbox(int col, int row, int w, int h, unsigned char attr)
{
    int i;
    if (w < 2 || h < 2) return;

    nos_scr_putchar(col,         row,         NOS_CH_DTL, attr);
    nos_scr_putchar(col + w - 1, row,         NOS_CH_DTR, attr);
    nos_scr_putchar(col,         row + h - 1, NOS_CH_DBL, attr);
    nos_scr_putchar(col + w - 1, row + h - 1, NOS_CH_DBR, attr);

    for (i = 1; i < w - 1; i++) {
        nos_scr_putchar(col + i, row,         NOS_CH_DH, attr);
        nos_scr_putchar(col + i, row + h - 1, NOS_CH_DH, attr);
    }
    for (i = 1; i < h - 1; i++) {
        nos_scr_putchar(col,         row + i, NOS_CH_DV, attr);
        nos_scr_putchar(col + w - 1, row + i, NOS_CH_DV, attr);
    }
}

/* -----------------------------------------------------------------------
 * Cursor
 * ----------------------------------------------------------------------- */

void nos_scr_cursor(int col, int row)
{
    union REGS r;
    col = scr_clip_col(col);
    row = scr_clip_row(row);
    /* INT 10h / AH=02h: set cursor position. DH=row, DL=col, BH=page 0. */
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = (unsigned char)row;
    r.h.dl = (unsigned char)col;
    int86(0x10, &r, &r);
}

void nos_scr_hide_cursor(void)
{
    /* Move cursor to row 25 in 25-line mode (off-screen). */
    union REGS r;
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = (unsigned char)g_scr_rows;   /* one past last visible row */
    r.h.dl = 0;
    int86(0x10, &r, &r);
}
