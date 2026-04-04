/* NOS-DOS: NOS-SHELL
 * viewer.c - Quick file viewer (F3).
 *
 * Loads up to VWR_BUF bytes into a static buffer, builds a line index
 * for text mode, and lets the user scroll with arrow keys / PgUp / PgDn.
 * F4 toggles between text and hex view.  Esc / F3 / Enter exits.
 *
 * Memory layout (static, inside the 64 KB data segment):
 *   g_vbuf[VWR_BUF+1]       -- raw file bytes (NUL-sentinel at end)
 *   g_vlines[VWR_MAX_LINES] -- near pointers into g_vbuf (text mode only)
 *
 * Hex layout per row (16 bytes):
 *   XXXX: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX  AAAAAAAAAAAAAAAA
 *   0-4   5-52 (each byte = " XX", 3 chars, cols 5+j*3)  55-70
 *
 * License: GPL-2.0
 */

#include <stdio.h>   /* fopen, fread, fclose, sprintf */
#include <string.h>  /* strlen, strncpy */
#include "screen.h"
#include "input.h"
#include "viewer.h"

/* -----------------------------------------------------------------------
 * Static storage
 * ----------------------------------------------------------------------- */

static char  g_vbuf[VWR_BUF + 1];           /* file content + NUL sentinel */
static char *g_vlines[VWR_MAX_LINES];        /* line start pointers (text)  */
static int   g_vline_count;                  /* number of indexed lines      */
static int   g_vfsize;                       /* bytes actually loaded        */

/* Visible content rows (between header row 0 and status row 24) */
#define VWIN_H  23   /* rows 1 .. 23 */

/* -----------------------------------------------------------------------
 * Colour attributes
 * ----------------------------------------------------------------------- */

#define VWR_ATTR_HDR   NOS_ATTR(NOS_BLACK,  NOS_CYAN)   /* header / status  */
#define VWR_ATTR_BODY  NOS_ATTR(NOS_LGRAY,  NOS_BLACK)  /* text body        */
#define VWR_ATTR_HEX   NOS_ATTR(NOS_LCYAN,  NOS_BLACK)  /* hex byte values  */
#define VWR_ATTR_ASC   NOS_ATTR(NOS_WHITE,  NOS_BLACK)  /* ASCII column     */
#define VWR_ATTR_OFS   NOS_ATTR(NOS_LGRAY,  NOS_BLACK)  /* offset column    */

/* -----------------------------------------------------------------------
 * Internal: text line indexer
 * ----------------------------------------------------------------------- */

static void vwr_load_text(void)
{
    int i;
    char *start;

    g_vline_count = 0;
    start = g_vbuf;

    for (i = 0; i < g_vfsize && g_vline_count < VWR_MAX_LINES; i++) {
        if (g_vbuf[i] == '\n') {
            /* Strip \r if present before the \n */
            if (i > 0 && g_vbuf[i - 1] == '\r')
                g_vbuf[i - 1] = '\0';
            g_vbuf[i] = '\0';
            g_vlines[g_vline_count++] = start;
            start = &g_vbuf[i + 1];
        }
    }
    /* Capture last line that may not end with \n */
    if (start < &g_vbuf[g_vfsize] && g_vline_count < VWR_MAX_LINES) {
        g_vbuf[g_vfsize] = '\0'; /* safe: buffer has VWR_BUF+1 bytes */
        g_vlines[g_vline_count++] = start;
    }
    /* Ensure at least one (possibly empty) line */
    if (g_vline_count == 0) {
        g_vbuf[0] = '\0';
        g_vlines[0] = g_vbuf;
        g_vline_count = 1;
    }
}

/* -----------------------------------------------------------------------
 * Internal: drawing
 * ----------------------------------------------------------------------- */

static void vwr_draw_header(const char *fname, int hex_mode)
{
    char buf[82];
    nos_scr_fill(0, 0, 80, 1, ' ', VWR_ATTR_HDR);
    sprintf(buf, " [ %s ]  F4=%-4s  Esc=close",
            fname, hex_mode ? "Text" : "Hex ");
    nos_scr_putn(0, 0, buf, 80, VWR_ATTR_HDR);
}

static void vwr_draw_status(int top, int total, int hex_mode)
{
    char buf[82];
    nos_scr_fill(0, 24, 80, 1, ' ', VWR_ATTR_HDR);
    if (hex_mode) {
        sprintf(buf, " Hex offset: %04X  Row %d/%d  %d bytes",
                top * 16, top + 1, total, g_vfsize);
    } else {
        sprintf(buf, " Line %d/%d  %d bytes%s",
                top + 1, total, g_vfsize,
                (g_vfsize >= VWR_BUF) ? "  (file truncated)" : "");
    }
    nos_scr_putn(0, 24, buf, 80, VWR_ATTR_HDR);
}

static void vwr_draw_text(int top)
{
    int i, idx;
    nos_scr_fill(0, 1, 80, VWIN_H, ' ', VWR_ATTR_BODY);
    for (i = 0; i < VWIN_H; i++) {
        idx = top + i;
        if (idx >= g_vline_count) break;
        nos_scr_putn(0, 1 + i, g_vlines[idx], 80, VWR_ATTR_BODY);
    }
}

static void vwr_draw_hex(int top)
{
    int i, j, base;
    unsigned char b;
    char hbuf[4];

    nos_scr_fill(0, 1, 80, VWIN_H, ' ', VWR_ATTR_BODY);

    for (i = 0; i < VWIN_H; i++) {
        base = (top + i) * 16;
        if (base >= g_vfsize) break;

        /* Offset: "XXXX:" at col 0 (5 chars) */
        sprintf(hbuf, "%04X:", base);
        nos_scr_puts(0, 1 + i, hbuf, VWR_ATTR_OFS);

        /* 16 hex bytes starting at col 5 (" XX" per byte, 3 chars each) */
        for (j = 0; j < 16; j++) {
            if (base + j < g_vfsize) {
                b = (unsigned char)g_vbuf[base + j];
                sprintf(hbuf, " %02X", (int)b);
            } else {
                hbuf[0] = ' '; hbuf[1] = ' '; hbuf[2] = ' '; hbuf[3] = '\0';
            }
            nos_scr_puts(5 + j * 3, 1 + i, hbuf, VWR_ATTR_HEX);
        }

        /* ASCII column at col 55 (16 chars, 1 per byte) */
        for (j = 0; j < 16; j++) {
            if (base + j < g_vfsize) {
                b = (unsigned char)g_vbuf[base + j];
                hbuf[0] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
            } else {
                hbuf[0] = ' ';
            }
            hbuf[1] = '\0';
            nos_scr_puts(55 + j, 1 + i, hbuf, VWR_ATTR_ASC);
        }
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void nos_viewer_open(const char *path)
{
    FILE *fp;
    int   n, top, hex_mode, running, total;
    int   max_top;
    nos_event_t evt;
    const char *fname, *p;
    char  errbuf[80];

    fp = fopen(path, "rb");
    if (!fp) {
        sprintf(errbuf, "Cannot open: %s", path);
        /* Simple inline error -- no dialog dependency here */
        nos_scr_fill(0, 12, 80, 1, ' ', NOS_ATTR_ERROR);
        nos_scr_puts(2, 12, errbuf, NOS_ATTR_ERROR);
        nos_inp_flush();
        nos_inp_wait(&evt);
        return;
    }

    n = (int)fread(g_vbuf, 1, VWR_BUF, fp);
    fclose(fp);
    g_vfsize = n;
    g_vbuf[n] = '\0';

    /* Extract filename from path (last component after the last backslash) */
    fname = path;
    for (p = path; *p; p++) {
        if (*p == '\\') fname = p + 1;
    }

    vwr_load_text();

    top      = 0;
    hex_mode = 0;
    running  = 1;

    nos_scr_hide_cursor();
    nos_scr_clear(VWR_ATTR_BODY);

    while (running) {
        if (hex_mode) {
            total   = (g_vfsize + 15) / 16;
        } else {
            total   = g_vline_count;
        }
        max_top = total - VWIN_H;
        if (max_top < 0) max_top = 0;

        vwr_draw_header(fname, hex_mode);
        if (hex_mode)
            vwr_draw_hex(top);
        else
            vwr_draw_text(top);
        vwr_draw_status(top, total, hex_mode);

        nos_inp_wait(&evt);
        if (evt.type != NOS_EVT_KEY) continue;

        switch (evt.key.code) {
        case NOS_KEY_ESC:
        case NOS_KEY_F3:
        case NOS_KEY_ENTER:
            running = 0;
            break;

        case NOS_KEY_F4:
            hex_mode = !hex_mode;
            top = 0;
            break;

        case NOS_KEY_UP:
            if (top > 0) top--;
            break;

        case NOS_KEY_DOWN:
            if (top < max_top) top++;
            break;

        case NOS_KEY_PGUP:
            top -= VWIN_H;
            if (top < 0) top = 0;
            break;

        case NOS_KEY_PGDN:
            top += VWIN_H;
            if (top > max_top) top = max_top;
            break;

        case NOS_KEY_HOME:
            top = 0;
            break;

        case NOS_KEY_END:
            top = max_top;
            break;

        default:
            break;
        }
    }
    /* Caller (dispatch in shell.c) repaints the shell screen. */
}
