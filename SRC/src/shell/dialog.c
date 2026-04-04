/* NOS-DOS: NOS-SHELL
 * dialog.c - Modal dialog box implementation.
 *
 * Dialogs are drawn directly onto the live screen (no save/restore).
 * The caller must repaint after the dialog closes -- shell.c's
 * dispatch() calls redraw_all() after every action, so this is free.
 *
 * Layout (all dialogs, 80x25 assumed):
 *   Width  = DLG_W (50 chars), centered at col DLG_COL (= 15)
 *   msg/confirm height = DLG_H_MSG  (6 rows), top at row DLG_ROW_MSG
 *   input   height = DLG_H_INPUT (9 rows), top at row DLG_ROW_INPUT
 *
 * Colours (Norton Commander light-gray style):
 *   Background / border : black text on light-gray
 *   Title               : red on light-gray
 *   Key hints           : blue on light-gray
 *   Input field         : black on white
 *
 * License: GPL-2.0
 */

#include <dos.h>     /* (unused but consistent with other shell units) */
#include <stdio.h>   /* sprintf */
#include <string.h>  /* strlen, strcpy, strncpy */
#include "screen.h"
#include "input.h"
#include "dialog.h"

/* -----------------------------------------------------------------------
 * Layout constants
 * ----------------------------------------------------------------------- */

#define DLG_W           50              /* total dialog width incl. border  */
#define DLG_COL         ((80 - DLG_W) / 2)  /* = 15                        */

#define DLG_H_MSG        6              /* height of message / confirm box  */
#define DLG_H_INPUT      9              /* height of input box              */

/* Vertically centre each dialog height on a 25-row screen */
#define DLG_ROW_MSG     ((25 - DLG_H_MSG)   / 2)  /* = 9  */
#define DLG_ROW_INPUT   ((25 - DLG_H_INPUT) / 2)  /* = 8  */

/* Input field inside the dialog (single-line box) */
#define DLG_FIELD_OFF    3              /* padding from dialog left border  */
#define DLG_FIELD_W     (DLG_W - 6)    /* = 44 chars of usable input area  */

/* -----------------------------------------------------------------------
 * Colour attributes
 * ----------------------------------------------------------------------- */

#define DLG_ATTR_BG     NOS_ATTR(NOS_BLACK, NOS_LGRAY)  /* body + border   */
#define DLG_ATTR_TITLE  NOS_ATTR(NOS_RED,   NOS_LGRAY)  /* title text      */
#define DLG_ATTR_KEY    NOS_ATTR(NOS_BLUE,  NOS_LGRAY)  /* key-hint text   */
#define DLG_ATTR_INPUT  NOS_ATTR(NOS_BLACK, NOS_WHITE)  /* input field     */

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Draw a double-line framed dialog at (DLG_COL, top_row) with height h.
 * Clears the interior and renders the title centred in the top border. */
static void dlg_draw_frame(int top_row, int h, const char *title)
{
    char tbuf[44];
    int tlen, tcol;

    /* Fill interior (including border cells -- dbox overwrites them) */
    nos_scr_fill(DLG_COL, top_row, DLG_W, h, ' ', DLG_ATTR_BG);
    /* Draw double-line outer border */
    nos_scr_dbox(DLG_COL, top_row, DLG_W, h, DLG_ATTR_BG);
    /* Overwrite centre of top border with "[ title ]" */
    if (title && *title) {
        sprintf(tbuf, "[ %s ]", title);
        tlen = (int)strlen(tbuf);
        tcol = DLG_COL + (DLG_W - tlen) / 2;
        nos_scr_puts(tcol, top_row, tbuf, DLG_ATTR_TITLE);
    }
}

/* Write s centred within the dialog row. */
static void dlg_puts_centre(int row, const char *s, unsigned char attr)
{
    int slen = (int)strlen(s);
    int col  = DLG_COL + (DLG_W - slen) / 2;
    nos_scr_puts(col, row, s, attr);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void nos_dlg_msg(const char *title, const char *msg)
{
    nos_event_t evt;

    dlg_draw_frame(DLG_ROW_MSG, DLG_H_MSG, title);
    /* Row layout (top_row = 9):
     *   9  ╔═══[ title ]════╗
     *  10  ║                ║
     *  11  ║  msg           ║
     *  12  ║                ║
     *  13  ║ [Press any key]║
     *  14  ╚════════════════╝
     */
    nos_scr_puts(DLG_COL + 2, DLG_ROW_MSG + 2, msg, DLG_ATTR_BG);
    dlg_puts_centre(DLG_ROW_MSG + 4, "[ Press any key ]", DLG_ATTR_KEY);

    nos_inp_flush();
    nos_inp_wait(&evt);
}

int nos_dlg_confirm(const char *title, const char *msg)
{
    nos_event_t evt;
    int result;

    dlg_draw_frame(DLG_ROW_MSG, DLG_H_MSG, title);
    /* Row layout (top_row = 9):
     *   9  ╔═══[ title ]════╗
     *  10  ║                ║
     *  11  ║  msg           ║
     *  12  ║                ║
     *  13  ║ [ Y ] Yes  [ N ] No ║
     *  14  ╚════════════════╝
     */
    nos_scr_puts(DLG_COL + 2, DLG_ROW_MSG + 2, msg, DLG_ATTR_BG);
    dlg_puts_centre(DLG_ROW_MSG + 4,
                    "[ Y ] Yes        [ N ] No", DLG_ATTR_KEY);

    nos_inp_flush();
    result = 0;
    for (;;) {
        nos_inp_wait(&evt);
        if (evt.type == NOS_EVT_KEY) {
            if (evt.key.ch == 'Y' || evt.key.ch == 'y') { result = 1; break; }
            if (evt.key.ch == 'N' || evt.key.ch == 'n') { result = 0; break; }
            if (evt.key.code == NOS_KEY_ESC)             { result = 0; break; }
        }
    }
    return result;
}

int nos_dlg_input(const char *title, const char *prompt,
                  char *buf, int maxlen)
{
    nos_event_t evt;
    char tmp[128];     /* working copy -- 128 covers any DOS path */
    int  len;
    int  field_col;    /* screen col of the input text area */
    int  field_row;    /* screen row of the input text area */
    int  field_max;    /* effective max chars (min of maxlen-1, DLG_FIELD_W) */
    int  result;

    /* Clamp effective input length to visible field width */
    field_max = (maxlen - 1 < DLG_FIELD_W) ? maxlen - 1 : DLG_FIELD_W;

    /* Initialise working copy from caller's buffer */
    strncpy(tmp, buf, (unsigned)field_max);
    tmp[field_max] = '\0';
    len = (int)strlen(tmp);

    dlg_draw_frame(DLG_ROW_INPUT, DLG_H_INPUT, title);
    /* Row layout (top_row = 8):
     *   8  ╔═══[ title ]════╗
     *   9  ║                ║
     *  10  ║  prompt        ║
     *  11  ║  ┌──────────┐  ║
     *  12  ║  │ text     │  ║
     *  13  ║  └──────────┘  ║
     *  14  ║                ║
     *  15  ║ [Enter] [Esc]  ║
     *  16  ╚════════════════╝
     */
    nos_scr_puts(DLG_COL + 2, DLG_ROW_INPUT + 2, prompt, DLG_ATTR_BG);

    /* Single-line input box: box outer is at (DLG_COL+2, ROW+3), 46 wide, 3 high */
    nos_scr_box(DLG_COL + DLG_FIELD_OFF - 1,
                DLG_ROW_INPUT + 3,
                DLG_FIELD_W + 2,
                3,
                DLG_ATTR_BG);

    dlg_puts_centre(DLG_ROW_INPUT + 7,
                    "[Enter] OK   [Esc] Cancel", DLG_ATTR_KEY);

    field_col = DLG_COL + DLG_FIELD_OFF;
    field_row = DLG_ROW_INPUT + 4;

    nos_inp_flush();
    result = 0;

    for (;;) {
        /* Repaint input line */
        nos_scr_fill(field_col, field_row, DLG_FIELD_W, 1, ' ', DLG_ATTR_INPUT);
        nos_scr_putn(field_col, field_row, tmp, DLG_FIELD_W, DLG_ATTR_INPUT);
        nos_scr_cursor(field_col + len, field_row);

        nos_inp_wait(&evt);
        if (evt.type != NOS_EVT_KEY) continue;

        switch (evt.key.code) {
        case NOS_KEY_ENTER:
            strcpy(buf, tmp);
            result = 1;
            goto done;

        case NOS_KEY_ESC:
            result = 0;
            goto done;

        case NOS_KEY_BACKSPACE:
            if (len > 0) tmp[--len] = '\0';
            break;

        default:
            /* Printable ASCII */
            if (evt.key.ch >= 0x20 && evt.key.ch <= 0x7E) {
                if (len < field_max) {
                    tmp[len++] = (char)evt.key.ch;
                    tmp[len]   = '\0';
                }
            }
            break;
        }
    }

done:
    nos_scr_hide_cursor();
    return result;
}
