/* NOS-DOS: NOS-SHELL
 * shell.c - Main shell loop.
 *
 * Draws two file panels side-by-side, a header bar, and an F-key footer.
 * Dispatches keyboard events to the active panel.
 *
 * Layout (80x25 default):
 *   Row  0   : header bar  "NOS-DOS  [path]  [mem]  [time]"
 *   Rows 1-22: left panel (cols 0-38) | right panel (cols 40-79)
 *   Row 23   : command line (future use)
 *   Row 24   : F-key bar
 *
 * F-key assignments (Norton Commander standard):
 *   F1  Help     F2  Menu      F3  View    F4  Edit
 *   F5  Copy     F6  Move      F7  MkDir   F8  Delete
 *   F9  PullMenu F10 Quit      F12 DOS Shell
 *
 * License: GPL-2.0
 */

#include <dos.h>    /* int86, union REGS */
#define INCL_DOSMEMMGR
#include <stdio.h>  /* sprintf */
#include <string.h> /* strcpy, strlen */
#include <stdlib.h> /* exit */
#include "screen.h"
#include "input.h"
#include "panel.h"

/* -----------------------------------------------------------------------
 * Layout constants
 * ----------------------------------------------------------------------- */

#define HDR_ROW      0
#define PANEL_TOP    1
#define PANEL_BOTTOM 22
#define PANEL_H      (PANEL_BOTTOM - PANEL_TOP + 1)  /* 22 rows */
#define CMD_ROW      23
#define FKEY_ROW     24

#define LEFT_COL     0
#define LEFT_W       39
#define SEP_COL      39
#define RIGHT_COL    40
#define RIGHT_W      40   /* 40 cols to fill to col 79 */

/* -----------------------------------------------------------------------
 * Shell state
 * ----------------------------------------------------------------------- */

static nos_panel_t g_left;
static nos_panel_t g_right;
static int         g_active = 0;  /* 0=left, 1=right */
static int         g_running = 1;

/* -----------------------------------------------------------------------
 * Header and footer
 * ----------------------------------------------------------------------- */

static void draw_header(void)
{
    union REGS r;
    unsigned int conv_kb = 0;
    unsigned char attr   = NOS_ATTR(NOS_BLACK, NOS_CYAN);
    char buf[82];
    unsigned int hours, mins, secs;

    /* Clear header */
    nos_scr_fill(0, HDR_ROW, 80, 1, ' ', attr);

    /* Conventional memory via INT 12h */
    r.h.ah = 0x00;
    int86(0x12, &r, &r);
    conv_kb = (unsigned int)r.x.ax;

    /* Time via INT 1Ah / AH=00h: CH=hours, CL=mins, DH=secs */
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);
    hours = (unsigned int)r.h.ch;
    mins  = (unsigned int)r.h.cl;
    secs  = (unsigned int)r.h.dh;

    sprintf(buf, " NOS-DOS                       %3uKB  %02u:%02u:%02u ",
            conv_kb, hours, mins, secs);
    nos_scr_putn(0, HDR_ROW, buf, 80, attr);

    /* Overwrite centre with active panel path */
    {
        nos_panel_t *ap = (g_active == 0) ? &g_left : &g_right;
        int plen = (int)strlen(ap->path);
        int pcol = (80 - plen) / 2;
        nos_scr_puts(pcol, HDR_ROW, ap->path, attr);
    }
}

static void draw_fkey_bar(void)
{
    static const char *labels[] = {
        "Help", "Menu", "View", "Edit",
        "Copy", "Move", "MkDir","Del ",
        "Pull", "Quit"
    };
    unsigned char num_attr  = NOS_ATTR(NOS_BLACK, NOS_CYAN);
    unsigned char lbl_attr  = NOS_ATTR(NOS_BLACK, NOS_LGRAY);
    int i, col = 0;

    nos_scr_fill(0, FKEY_ROW, 80, 1, ' ', lbl_attr);

    for (i = 0; i < 10; i++) {
        char nbuf[3];
        sprintf(nbuf, "%2d", i + 1);
        nos_scr_puts(col, FKEY_ROW, nbuf, num_attr);
        col += 2;
        nos_scr_puts(col, FKEY_ROW, labels[i], lbl_attr);
        col += 4;
        if (col < 80)
            nos_scr_putchar(col, FKEY_ROW, ' ', lbl_attr);
        col++;
    }
}

static void draw_separator(void)
{
    int r;
    unsigned char attr = NOS_ATTR(NOS_LGRAY, NOS_BLACK);
    for (r = PANEL_TOP; r <= PANEL_BOTTOM; r++)
        nos_scr_putchar(SEP_COL, r, NOS_CH_V, attr);
}

/* -----------------------------------------------------------------------
 * Full screen redraw
 * ----------------------------------------------------------------------- */

static void redraw_all(void)
{
    g_left.active  = (g_active == 0);
    g_right.active = (g_active == 1);

    nos_scr_hide_cursor();
    nos_scr_clear(NOS_ATTR_NORMAL);
    draw_header();
    draw_separator();
    nos_panel_draw(&g_left);
    nos_panel_draw(&g_right);
    draw_fkey_bar();
    /* Command line row — blank */
    nos_scr_fill(0, CMD_ROW, 80, 1, ' ', NOS_ATTR_NORMAL);
}

/* -----------------------------------------------------------------------
 * DOS shell-out (F12)
 * ----------------------------------------------------------------------- */

static void dos_shell(void)
{
    nos_scr_restore();
    /* INT 21h / AH=4Bh exec: spawn COMMAND.COM with /K keeps it alive.
     * For simplicity we use system() — available in Open Watcom libc. */
    {
        union REGS r;
        /* Print message before shelling out */
        r.h.ah = 0x09; /* DOS print string (needs $ terminator) */
        /* Just write via stdout approach; avoid printf to stay small */
    }
    /* Open Watcom provides system() in small model. */
    /* system("COMMAND.COM"); */ /* would run a child shell */
    /* For now: just return. Full exec needs AH=4Bh path. */
    nos_scr_init();
    redraw_all();
}

/* -----------------------------------------------------------------------
 * Action handlers
 * ----------------------------------------------------------------------- */

static void action_enter(void)
{
    nos_panel_t *ap = (g_active == 0) ? &g_left : &g_right;
    char path[128];
    int rc = nos_panel_enter(ap, path);
    if (rc == 0 || rc == -1) {
        /* Directory changed or error — redraw panel */
    }
    /* rc == 1 means a file was selected: future viewer/editor hook */
}

static void action_copy(void)
{
    /* Future: F5 copy */
    (void)0;
}

static void action_move(void)
{
    /* Future: F6 move */
    (void)0;
}

static void action_mkdir(void)
{
    /* Future: F7 mkdir */
    (void)0;
}

static void action_delete(void)
{
    /* Future: F8 delete */
    (void)0;
}

static void action_quit(void)
{
    g_running = 0;
}

/* -----------------------------------------------------------------------
 * Event dispatch
 * ----------------------------------------------------------------------- */

static void dispatch(nos_event_t *evt)
{
    nos_panel_t *ap;

    if (evt->type == NOS_EVT_NONE) return;

    if (evt->type == NOS_EVT_KEY) {
        ap = (g_active == 0) ? &g_left : &g_right;

        switch (evt->key.code) {

        /* Panel navigation */
        case NOS_KEY_UP:    nos_panel_move_cursor(ap, -1); break;
        case NOS_KEY_DOWN:  nos_panel_move_cursor(ap,  1); break;
        case NOS_KEY_PGUP:  nos_panel_page(ap, -1);        break;
        case NOS_KEY_PGDN:  nos_panel_page(ap,  1);        break;
        case NOS_KEY_HOME:  nos_panel_move_cursor(ap, -NOS_PANEL_MAX_FILES); break;
        case NOS_KEY_END:   nos_panel_move_cursor(ap,  NOS_PANEL_MAX_FILES); break;
        case NOS_KEY_ENTER: action_enter(); break;

        /* Switch active panel */
        case NOS_KEY_TAB:
            g_active = 1 - g_active;
            break;

        /* Drive switching (Alt+F1 / Alt+F2) */
        case NOS_KEY_ALT_F1:
            /* Prompt for drive letter — TODO: drive picker dialog */
            break;
        case NOS_KEY_ALT_F2:
            break;

        /* F-key actions */
        case NOS_KEY_F5:  action_copy();   break;
        case NOS_KEY_F6:  action_move();   break;
        case NOS_KEY_F7:  action_mkdir();  break;
        case NOS_KEY_F8:  action_delete(); break;
        case NOS_KEY_F10:
        case NOS_KEY_ALT_F4:
            action_quit(); break;
        case NOS_KEY_F12: dos_shell(); return; /* already redraws */

        /* Refresh (Ctrl+R) */
        case NOS_KEY_CTRL_R:
            nos_panel_read_dir(&g_left);
            nos_panel_read_dir(&g_right);
            break;

        default: break;
        }
    }
    /* Mouse events: future click-to-select */

    redraw_all();
}

/* -----------------------------------------------------------------------
 * Entry point
 * ----------------------------------------------------------------------- */

int main(void)
{
    nos_event_t evt;

    nos_scr_init();
    nos_inp_init();

    /* Initialise panels */
    if (nos_panel_init(&g_left,  LEFT_COL,  PANEL_TOP, LEFT_W,  PANEL_H) != 0 ||
        nos_panel_init(&g_right, RIGHT_COL, PANEL_TOP, RIGHT_W, PANEL_H) != 0) {
        nos_scr_restore();
        return 1;
    }

    g_left.active  = 1;
    g_right.active = 0;

    redraw_all();

    while (g_running) {
        nos_inp_wait(&evt);
        dispatch(&evt);
    }

    nos_panel_free(&g_left);
    nos_panel_free(&g_right);
    nos_scr_restore();
    return 0;
}
