/* NOS-DOS: NOS-SHELL
 * welcome.c - First-boot welcome screen.
 *
 * Shown once after NOS-DETECT completes and the shell starts for the
 * first time.  A flag file (NOS_WELCOME_FLAG) marks that the screen
 * has been displayed; subsequent boots skip it.
 *
 * Layout: full-screen modal dialog with system summary and key hints.
 * User presses any key to dismiss and enter the shell.
 *
 * License: GPL-2.0
 */

#include <dos.h>    /* int86, union REGS */
#include <stdio.h>  /* fopen, fclose */
#include <string.h> /* strlen */
#include "screen.h"
#include "input.h"
#include "welcome.h"

/* Flag file: presence means welcome has already been shown. */
#define NOS_WELCOME_FLAG  "C:\\NOS\\SYSTEM\\WELCOMED"

/* Dialog geometry */
#define WEL_W    60
#define WEL_H    20
#define WEL_COL  ((80 - WEL_W) / 2)   /* = 10 */
#define WEL_ROW  ((25 - WEL_H) / 2)   /* =  2 */

/* Colour scheme */
#define WEL_BG      NOS_ATTR(NOS_BLACK,  NOS_CYAN)
#define WEL_TITLE   NOS_ATTR(NOS_WHITE,  NOS_CYAN)
#define WEL_BOLD    NOS_ATTR(NOS_WHITE,  NOS_CYAN)
#define WEL_NORMAL  NOS_ATTR(NOS_BLACK,  NOS_CYAN)
#define WEL_KEY     NOS_ATTR(NOS_YELLOW, NOS_CYAN)
#define WEL_HINT    NOS_ATTR(NOS_WHITE,  NOS_BLUE)

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static void wel_centre(int row, const char *text, unsigned char attr)
{
    int len  = (int)strlen(text);
    int col  = WEL_COL + (WEL_W - len) / 2;
    nos_scr_puts(col, row, text, attr);
}

static void wel_left(int col, int row, const char *text, unsigned char attr)
{
    nos_scr_puts(WEL_COL + col, row, text, attr);
}

/* -----------------------------------------------------------------------
 * nos_welcome_show
 * ----------------------------------------------------------------------- */

void nos_welcome_show(void)
{
    union REGS r;
    unsigned int conv_kb;
    nos_event_t evt;
    int base;

    /* Read conventional memory for the summary line. */
    r.h.ah = 0x00;
    int86(0x12, &r, &r);
    conv_kb = (unsigned int)r.x.ax;

    base = WEL_ROW;

    /* Draw dialog box */
    nos_scr_fill(WEL_COL, base, WEL_W, WEL_H, ' ', WEL_BG);
    nos_scr_dbox(WEL_COL, base, WEL_W, WEL_H, WEL_BG);

    /* Title bar */
    wel_centre(base,      "NOS-DOS",                          WEL_TITLE);
    wel_centre(base + 1,  "NostalgicDOS v1.0",                WEL_NORMAL);
    wel_centre(base + 2,  "Boot fast. Work clean.",           WEL_NORMAL);

    /* Separator */
    nos_scr_hline(WEL_COL + 1, base + 3, WEL_W - 2, 0xC4, WEL_BG);

    /* System summary */
    wel_centre(base + 4, "Your system is ready.", WEL_BOLD);

    {
        char buf[48];
        sprintf(buf, "%u KB conventional memory free", conv_kb);
        wel_centre(base + 5, buf, WEL_NORMAL);
    }

    /* Separator */
    nos_scr_hline(WEL_COL + 1, base + 6, WEL_W - 2, 0xC4, WEL_BG);

    /* Key reference */
    wel_left(4, base + 7,  "GETTING STARTED",         WEL_BOLD);
    wel_left(4, base + 8,  "F9",                       WEL_KEY);
    wel_left(8, base + 8,  "Launch an application",    WEL_NORMAL);
    wel_left(4, base + 9,  "F12",                      WEL_KEY);
    wel_left(8, base + 9,  "Open DOS prompt",          WEL_NORMAL);
    wel_left(4, base + 10, "F1",                       WEL_KEY);
    wel_left(8, base + 10, "Help and documentation",   WEL_NORMAL);
    wel_left(4, base + 11, "Tab",                      WEL_KEY);
    wel_left(8, base + 11, "Switch file panel",        WEL_NORMAL);

    wel_left(4, base + 12, "INSTALL SOFTWARE",         WEL_BOLD);
    wel_left(4, base + 13, "F12, then type:",          WEL_NORMAL);
    wel_left(4, base + 14, "  NNET DHCP",              WEL_KEY);
    wel_left(4, base + 15, "  NPKG UPDATE",            WEL_KEY);
    wel_left(4, base + 16, "  NPKG SEARCH [name]",     WEL_KEY);
    wel_left(4, base + 17, "  NPKG INSTALL [id]",      WEL_KEY);

    /* Dismiss hint */
    nos_scr_fill(WEL_COL + 1, base + WEL_H - 2, WEL_W - 2, 1, ' ', WEL_HINT);
    wel_centre(base + WEL_H - 2,
               "[ Press any key to enter NOS-DOS ]", WEL_HINT);

    /* Wait for keypress */
    nos_inp_flush();
    nos_inp_wait(&evt);
}

/* -----------------------------------------------------------------------
 * nos_welcome_needed / nos_welcome_mark_shown
 * ----------------------------------------------------------------------- */

int nos_welcome_needed(void)
{
    FILE *f = fopen(NOS_WELCOME_FLAG, "r");
    if (f) { fclose(f); return 0; }   /* flag exists -- already shown */
    return 1;
}

void nos_welcome_mark_shown(void)
{
    FILE *f = fopen(NOS_WELCOME_FLAG, "w");
    if (f) {
        fputs("1\r\n", f);
        fclose(f);
    }
}
