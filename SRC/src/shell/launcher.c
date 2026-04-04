/* NOS-DOS: NOS-SHELL
 * launcher.c - Application launcher (F9).
 *
 * Config file: C:\NOS\SHELL\LAUNCHER.CFG
 * Format: one entry per line — "Display Name|COMMAND.COM /C PROG.EXE"
 * Lines beginning with '#' and blank lines are ignored.
 * Maximum entries: LAU_MAX (32).
 *
 * Dialog layout (50 wide, centred on 80x25):
 *   ╔═══════════[ Launch ]════════════╗  <- row LAU_ROW
 *   ║                                 ║
 *   ║  > Entry 1                      ║  <- items start row+2
 *   ║    Entry 2                      ║
 *   ║    ... (up to LAU_VROWS = 8)    ║
 *   ║                                 ║
 *   ║  [Enter] Launch  [Esc] Close    ║
 *   ╚═════════════════════════════════╝
 *
 * License: GPL-2.0
 */

#include <dos.h>     /* (needed for nos_scr_init calls via screen.h) */
#include <stdio.h>   /* fopen, fgets, fclose, sprintf */
#include <string.h>  /* strcpy, strlen, strchr, strncpy */
#include <stdlib.h>  /* system */
#include <direct.h>  /* chdir */
#include "screen.h"
#include "input.h"
#include "launcher.h"

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

#define LAU_MAX    32            /* max launcher entries                    */
#define LAU_NAME   36            /* max display name length                 */
#define LAU_CMD    80            /* max command string length               */
#define LAU_DIR    65            /* max working directory length            */
#define LAU_CFG    "C:\\NOS\\SHELL\\LAUNCHER.CFG"

/* LAUNCHER.CFG supports two formats (backward-compatible):
 *   2-field:  Name|Exec           (no working directory override)
 *   3-field:  Name|Dir|Exec       (chdir to Dir before launch)
 *
 * NPKG-managed entries are preceded by a marker line:
 *   #NPKG:<ID>
 *   Name|Dir|Exec
 * The lau_load() parser skips all '#' lines (including markers), so
 * no change is needed here to handle them at load time.            */

#define LAU_W      50            /* dialog width (matches dialog.c)         */
#define LAU_COL    ((80 - LAU_W) / 2)   /* = 15                            */
#define LAU_VROWS   8            /* visible item rows in dialog             */
#define LAU_H      (LAU_VROWS + 5)  /* total dialog height (= 13)          */
#define LAU_ROW    ((25 - LAU_H) / 2)   /* = 6                             */

/* -----------------------------------------------------------------------
 * Colour attributes (matching dialog.c style)
 * ----------------------------------------------------------------------- */

#define LAU_ATTR_BG     NOS_ATTR(NOS_BLACK,  NOS_LGRAY)
#define LAU_ATTR_TITLE  NOS_ATTR(NOS_RED,    NOS_LGRAY)
#define LAU_ATTR_KEY    NOS_ATTR(NOS_BLUE,   NOS_LGRAY)
#define LAU_ATTR_CURSOR NOS_ATTR(NOS_WHITE,  NOS_BLUE)
#define LAU_ATTR_ITEM   NOS_ATTR(NOS_BLACK,  NOS_LGRAY)

/* -----------------------------------------------------------------------
 * Entry storage
 * ----------------------------------------------------------------------- */

typedef struct {
    char name[LAU_NAME];
    char dir[LAU_DIR];   /* working directory; empty = no chdir */
    char cmd[LAU_CMD];
} lau_entry_t;

static lau_entry_t g_entries[LAU_MAX];
static int         g_entry_count;

/* -----------------------------------------------------------------------
 * Config file loader
 * ----------------------------------------------------------------------- */

static int lau_load(void)
{
    FILE *fp;
    char  line[LAU_NAME + LAU_DIR + LAU_CMD + 6]; /* 3-field: name|dir|cmd */
    char *pipe;
    int   len;

    g_entry_count = 0;
    fp = fopen(LAU_CFG, "r");
    if (!fp) return 0;

    while (fgets(line, (int)sizeof(line), fp) && g_entry_count < LAU_MAX) {
        /* Strip trailing newline / CR */
        len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip blank lines and comments */
        if (len == 0 || line[0] == '#') continue;

        /* Split at first '|' — always separates Name from rest */
        pipe = strchr(line, '|');
        if (!pipe) continue;
        *pipe = '\0';

        strncpy(g_entries[g_entry_count].name, line, LAU_NAME - 1);
        g_entries[g_entry_count].name[LAU_NAME - 1] = '\0';
        g_entries[g_entry_count].dir[0]              = '\0';

        {
            char *rest  = pipe + 1;
            char *pipe2 = strchr(rest, '|');

            if (pipe2) {
                /* 3-field: Name|Dir|Exec */
                *pipe2 = '\0';
                strncpy(g_entries[g_entry_count].dir,  rest,    LAU_DIR - 1);
                strncpy(g_entries[g_entry_count].cmd,  pipe2+1, LAU_CMD - 1);
                g_entries[g_entry_count].dir[LAU_DIR - 1] = '\0';
            } else {
                /* 2-field: Name|Exec */
                strncpy(g_entries[g_entry_count].cmd, rest, LAU_CMD - 1);
            }
            g_entries[g_entry_count].cmd[LAU_CMD - 1] = '\0';
        }
        g_entry_count++;
    }
    fclose(fp);
    return g_entry_count;
}

/* -----------------------------------------------------------------------
 * Dialog drawing
 * ----------------------------------------------------------------------- */

static void lau_draw_frame(void)
{
    char tbuf[32];
    int  tlen, tcol;

    nos_scr_fill(LAU_COL, LAU_ROW, LAU_W, LAU_H, ' ', LAU_ATTR_BG);
    nos_scr_dbox(LAU_COL, LAU_ROW, LAU_W, LAU_H, LAU_ATTR_BG);

    /* Title */
    strcpy(tbuf, "[ Launch ]");
    tlen = (int)strlen(tbuf);
    tcol = LAU_COL + (LAU_W - tlen) / 2;
    nos_scr_puts(tcol, LAU_ROW, tbuf, LAU_ATTR_TITLE);

    /* Key hints on last interior row */
    {
        const char *hint = "[Enter] Launch  [Esc] Close";
        int hlen = (int)strlen(hint);
        int hcol = LAU_COL + (LAU_W - hlen) / 2;
        nos_scr_puts(hcol, LAU_ROW + LAU_H - 2, hint, LAU_ATTR_KEY);
    }
}

static void lau_draw_items(int cursor, int scroll)
{
    int i, idx;
    int item_row; /* screen row for item i */
    char  buf[LAU_W];
    unsigned char attr;
    int   visible = LAU_VROWS;
    int   item_col = LAU_COL + 1;  /* inside border */
    int   item_w   = LAU_W - 2;    /* full interior width */

    if (visible > g_entry_count) visible = g_entry_count;

    for (i = 0; i < LAU_VROWS; i++) {
        idx      = scroll + i;
        item_row = LAU_ROW + 2 + i;

        if (idx < g_entry_count) {
            attr = (idx == cursor) ? LAU_ATTR_CURSOR : LAU_ATTR_ITEM;
            /* "  > Name" for selected, "    Name" for others */
            sprintf(buf, "  %s %-*s",
                    (idx == cursor) ? ">" : " ",
                    item_w - 4,
                    g_entries[idx].name);
            nos_scr_putn(item_col, item_row, buf, item_w, attr);
        } else {
            /* Blank row */
            nos_scr_fill(item_col, item_row, item_w, 1, ' ', LAU_ATTR_BG);
        }
    }

    /* Scroll indicators on right border column */
    if (scroll > 0)
        nos_scr_putchar(LAU_COL + LAU_W - 1, LAU_ROW + 2,
                        0x1E, LAU_ATTR_BG); /* ▲ */
    if (scroll + LAU_VROWS < g_entry_count)
        nos_scr_putchar(LAU_COL + LAU_W - 1, LAU_ROW + 2 + LAU_VROWS - 1,
                        0x1F, LAU_ATTR_BG); /* ▼ */
}

/* -----------------------------------------------------------------------
 * Execute a selected entry
 * ----------------------------------------------------------------------- */

static void lau_exec(int idx)
{
    nos_scr_restore();
    if (g_entries[idx].dir[0] != '\0')
        chdir(g_entries[idx].dir);
    system(g_entries[idx].cmd);
    nos_scr_init();
    nos_scr_hide_cursor();
    /* Caller (shell.c dispatch) repaints the shell screen */
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void nos_launcher_show(void)
{
    nos_event_t evt;
    int cursor, scroll;

    lau_load();

    if (g_entry_count == 0) {
        /* No entries -- show a brief message using inline drawing */
        const char *title = "[ Launch ]";
        const char *msg1  = "No applications configured.";
        const char *msg2  = "Use NPKG INSTALL or edit LAUNCHER.CFG";
        const char *hint  = "[ Press any key ]";
        int tcol = LAU_COL + (LAU_W - (int)strlen(title)) / 2;
        int hcol = LAU_COL + (LAU_W - (int)strlen(hint))  / 2;
        int mrow = LAU_ROW + 2;

        nos_scr_fill(LAU_COL, LAU_ROW, LAU_W, 7, ' ', LAU_ATTR_BG);
        nos_scr_dbox(LAU_COL, LAU_ROW, LAU_W, 7, LAU_ATTR_BG);
        nos_scr_puts(tcol, LAU_ROW, title, LAU_ATTR_TITLE);
        nos_scr_puts(LAU_COL + 3, mrow,     msg1, LAU_ATTR_ITEM);
        nos_scr_puts(LAU_COL + 3, mrow + 1, msg2, LAU_ATTR_ITEM);
        nos_scr_puts(hcol, LAU_ROW + 5, hint, LAU_ATTR_KEY);

        nos_inp_flush();
        nos_inp_wait(&evt);
        return;
    }

    cursor = 0;
    scroll = 0;

    lau_draw_frame();
    lau_draw_items(cursor, scroll);
    nos_inp_flush();

    for (;;) {
        nos_inp_wait(&evt);
        if (evt.type != NOS_EVT_KEY) continue;

        switch (evt.key.code) {
        case NOS_KEY_ESC:
            return;

        case NOS_KEY_ENTER:
            lau_exec(cursor);
            return;

        case NOS_KEY_UP:
            if (cursor > 0) {
                cursor--;
                if (cursor < scroll) scroll = cursor;
            }
            break;

        case NOS_KEY_DOWN:
            if (cursor < g_entry_count - 1) {
                cursor++;
                if (cursor >= scroll + LAU_VROWS)
                    scroll = cursor - LAU_VROWS + 1;
            }
            break;

        case NOS_KEY_HOME:
            cursor = 0;
            scroll = 0;
            break;

        case NOS_KEY_END:
            cursor = g_entry_count - 1;
            scroll = cursor - LAU_VROWS + 1;
            if (scroll < 0) scroll = 0;
            break;

        default:
            break;
        }

        lau_draw_items(cursor, scroll);
    }
}
