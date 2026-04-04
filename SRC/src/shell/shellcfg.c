/* NOS-DOS: NOS-SHELL
 * shellcfg.c - Shell configuration (F2).
 *
 * Manages C:\NOS\SHELL\SHELL.CFG.  Currently the only configurable
 * setting is panel sort order (Name / Extension / Size / Date).
 *
 * Dialog layout (50 wide, centred on 80x25):
 *   ╔══════════[ Options ]══════════╗  <- CFG_ROW
 *   ║                               ║
 *   ║  Sort panels by:              ║
 *   ║                               ║
 *   ║  > Name                       ║
 *   ║    Extension                  ║
 *   ║    Size                       ║
 *   ║    Date                       ║
 *   ║                               ║
 *   ║  [Enter] Apply  [Esc] Cancel  ║
 *   ╚═══════════════════════════════╝
 *
 * License: GPL-2.0
 */

#include <stdio.h>   /* fopen, fputs, fgets, fclose */
#include <string.h>  /* strcmp, strncmp, strlen */
#include "screen.h"
#include "input.h"
#include "panel.h"   /* NOS_SORT_* constants */
#include "shellcfg.h"

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

#define CFG_PATH  "C:\\NOS\\SHELL\\SHELL.CFG"

#define CFG_W     50
#define CFG_COL   ((80 - CFG_W) / 2)   /* = 15 */
#define CFG_H     11
#define CFG_ROW   ((25 - CFG_H) / 2)   /* = 7  */

/* -----------------------------------------------------------------------
 * Colour attributes (matching dialog.c / launcher.c style)
 * ----------------------------------------------------------------------- */

#define CFG_ATTR_BG     NOS_ATTR(NOS_BLACK, NOS_LGRAY)
#define CFG_ATTR_TITLE  NOS_ATTR(NOS_RED,   NOS_LGRAY)
#define CFG_ATTR_KEY    NOS_ATTR(NOS_BLUE,  NOS_LGRAY)
#define CFG_ATTR_CURSOR NOS_ATTR(NOS_WHITE, NOS_BLUE)
#define CFG_ATTR_ITEM   NOS_ATTR(NOS_BLACK, NOS_LGRAY)

/* -----------------------------------------------------------------------
 * Sort name table
 * ----------------------------------------------------------------------- */

static const char *sort_labels[] = {
    "Name",
    "Extension",
    "Size",
    "Date"
};
#define SORT_COUNT 4

/* sort mode index → config file keyword */
static const char *sort_keys[] = { "name", "ext", "size", "date" };

/* -----------------------------------------------------------------------
 * Config file I/O
 * ----------------------------------------------------------------------- */

int nos_cfg_load(void)
{
    FILE *fp;
    char  line[64];
    int   i;

    fp = fopen(CFG_PATH, "r");
    if (!fp) return NOS_SORT_NAME;

    while (fgets(line, (int)sizeof(line), fp)) {
        if (strncmp(line, "sort=", 5) == 0) {
            /* Strip trailing whitespace */
            int len = (int)strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'
                               || line[len-1] == ' '))
                line[--len] = '\0';

            for (i = 0; i < SORT_COUNT; i++) {
                if (strcmp(line + 5, sort_keys[i]) == 0) {
                    fclose(fp);
                    return i;
                }
            }
        }
    }
    fclose(fp);
    return NOS_SORT_NAME;
}

void nos_cfg_save(int sort_mode)
{
    FILE *fp;

    if (sort_mode < 0 || sort_mode >= SORT_COUNT)
        sort_mode = NOS_SORT_NAME;

    fp = fopen(CFG_PATH, "w");
    if (!fp) return;
    fputs("sort=", fp);
    fputs(sort_keys[sort_mode], fp);
    fputs("\r\n", fp);
    fclose(fp);
}

/* -----------------------------------------------------------------------
 * NOS-HW.CFG reader (network present check)
 * ----------------------------------------------------------------------- */

#define HWCFG_PATH "C:\\NOS\\SYSTEM\\NOS-HW.CFG"

int nos_hwcfg_net_present(void)
{
    FILE *fp;
    char  line[64];
    int   in_net = 0;

    fp = fopen(HWCFG_PATH, "r");
    if (!fp) return 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        /* Strip trailing whitespace */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'
                           || line[len-1] == ' '))
            line[--len] = '\0';

        if (strcmp(line, "[NETWORK]") == 0) {
            in_net = 1;
            continue;
        }
        if (line[0] == '[') {
            in_net = 0;
            continue;
        }
        if (in_net && strcmp(line, "PRESENT=1") == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

/* -----------------------------------------------------------------------
 * Sort selection dialog
 * ----------------------------------------------------------------------- */

int nos_cfg_sort_dialog(int current)
{
    nos_event_t evt;
    int         cursor, i;
    int         item_col, item_w, item_row;
    char        buf[CFG_W];
    unsigned char attr;
    char        tbuf[24];
    int         tlen, tcol;

    if (current < 0 || current >= SORT_COUNT) current = NOS_SORT_NAME;
    cursor = current;

    /* Draw dialog frame */
    nos_scr_fill(CFG_COL, CFG_ROW, CFG_W, CFG_H, ' ', CFG_ATTR_BG);
    nos_scr_dbox(CFG_COL, CFG_ROW, CFG_W, CFG_H, CFG_ATTR_BG);

    /* Title */
    strcpy(tbuf, "[ Options ]");
    tlen = (int)strlen(tbuf);
    tcol = CFG_COL + (CFG_W - tlen) / 2;
    nos_scr_puts(tcol, CFG_ROW, tbuf, CFG_ATTR_TITLE);

    /* Prompt */
    nos_scr_puts(CFG_COL + 2, CFG_ROW + 2, "Sort panels by:", CFG_ATTR_BG);

    /* Key hints */
    {
        const char *hint = "[Enter] Apply   [Esc] Cancel";
        int hlen = (int)strlen(hint);
        int hcol = CFG_COL + (CFG_W - hlen) / 2;
        nos_scr_puts(hcol, CFG_ROW + CFG_H - 2, hint, CFG_ATTR_KEY);
    }

    item_col = CFG_COL + 1;
    item_w   = CFG_W - 2;

    nos_inp_flush();

    for (;;) {
        /* Draw sort options */
        for (i = 0; i < SORT_COUNT; i++) {
            item_row = CFG_ROW + 4 + i;
            attr = (i == cursor) ? CFG_ATTR_CURSOR : CFG_ATTR_ITEM;
            sprintf(buf, "  %s %-*s",
                    (i == cursor) ? ">" : " ",
                    item_w - 4,
                    sort_labels[i]);
            nos_scr_putn(item_col, item_row, buf, item_w, attr);
        }

        nos_inp_wait(&evt);
        if (evt.type != NOS_EVT_KEY) continue;

        switch (evt.key.code) {
        case NOS_KEY_ESC:
            return -1;

        case NOS_KEY_ENTER:
            return cursor;

        case NOS_KEY_UP:
            if (cursor > 0) cursor--;
            break;

        case NOS_KEY_DOWN:
            if (cursor < SORT_COUNT - 1) cursor++;
            break;

        case NOS_KEY_HOME:
            cursor = 0;
            break;

        case NOS_KEY_END:
            cursor = SORT_COUNT - 1;
            break;

        default:
            break;
        }
    }
}
