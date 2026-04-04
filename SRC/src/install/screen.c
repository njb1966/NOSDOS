/* NOS-DOS: NOS-INSTALL
 * screen.c - Direct video memory screen rendering.
 *
 * All drawing goes straight to the CGA text buffer at B800:0000.
 * Each cell is 2 bytes: character (low) + attribute (high).
 *
 * License: GPL-2.0
 */

#include <dos.h>
#include <string.h>
#include "screen.h"

#define VIDEO_SEG  0xB800
#define VIDEO_OFS  0x0000

static unsigned char far *g_video = NULL;

int g_scr_cols = 80;
int g_scr_rows = 25;

static unsigned int g_saved_cursor_shape = 0x0607;

#define CELL(col, row)  (((row) * g_scr_cols + (col)) * 2)

static int scr_clip_col(int col) { return (col < 0) ? 0 : (col >= g_scr_cols) ? g_scr_cols - 1 : col; }
static int scr_clip_row(int row) { return (row < 0) ? 0 : (row >= g_scr_rows) ? g_scr_rows - 1 : row; }

void nos_scr_init(void)
{
    union REGS r;

    g_video = (unsigned char far *)MK_FP(VIDEO_SEG, VIDEO_OFS);

    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    g_scr_cols = (r.h.ah == 0) ? 80 : (int)(unsigned char)r.h.ah;

    r.x.ax = 0x1130;
    r.h.bh = 0x00;
    int86(0x10, &r, &r);
    g_scr_rows = ((int)(unsigned char)r.h.dl > 0) ? (int)(unsigned char)r.h.dl + 1 : 25;
    if (g_scr_rows != 50) g_scr_rows = 25;

    r.h.ah = 0x03;
    r.h.bh = 0x00;
    int86(0x10, &r, &r);
    g_saved_cursor_shape = r.x.cx;

    nos_scr_hide_cursor();
}

void nos_scr_restore(void)
{
    union REGS r;
    r.h.ah = 0x01;
    r.x.cx = g_saved_cursor_shape;
    int86(0x10, &r, &r);
    nos_scr_cursor(0, g_scr_rows - 1);
}

void nos_scr_clear(unsigned char attr)
{
    nos_scr_fill(0, 0, g_scr_cols, g_scr_rows, ' ', attr);
}

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

void nos_scr_fill(int col, int row, int w, int h,
                  unsigned char ch, unsigned char attr)
{
    int r, c, off, c0, r0, c1, r1;
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

void nos_scr_cursor(int col, int row)
{
    union REGS r;
    col = scr_clip_col(col);
    row = scr_clip_row(row);
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = (unsigned char)row;
    r.h.dl = (unsigned char)col;
    int86(0x10, &r, &r);
}

void nos_scr_hide_cursor(void)
{
    union REGS r;
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = (unsigned char)g_scr_rows;
    r.h.dl = 0;
    int86(0x10, &r, &r);
}
