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
 *   F9  Launch   F10 Quit      F12 DOS Shell
 *
 * License: GPL-2.0
 */

#include <dos.h>     /* int86, union REGS, intdosx */
#include <stdio.h>   /* sprintf, fopen, fread, fwrite, fclose, rename, remove */
#include <string.h>  /* strcpy, strlen, strcmp, strcat */
#include <stdlib.h>  /* exit, getenv */
#include <direct.h>  /* mkdir, rmdir */
#include <process.h> /* spawnl, P_WAIT */
#include "screen.h"
#include "input.h"
#include "panel.h"
#include "dialog.h"
#include "viewer.h"
#include "launcher.h"
#include "shellcfg.h"
#include "welcome.h"

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
static int         g_active = 0;      /* 0=left, 1=right */
static int         g_running = 1;
static int         g_net_present    = 0; /* 1 if packet driver detected */
static int         g_bridge_mounted = 0; /* 1 if H:\ is accessible      */

/* Copy I/O staging buffer -- static to avoid stack pressure */
static unsigned char g_copy_buf[1024];

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static nos_panel_t *active_panel(void)
{
    return (g_active == 0) ? &g_left : &g_right;
}

static nos_panel_t *inactive_panel(void)
{
    return (g_active == 0) ? &g_right : &g_left;
}

/* Build the full path of the cursor entry into buf (>= 128 bytes). */
static void cursor_path(nos_panel_t *p, char *buf)
{
    nos_panel_cursor_path(p, buf);
}

/* Copy file src to dst.  Returns 0 on success, -1 on error. */
static int copy_file(const char *src, const char *dst)
{
    FILE *fin, *fout;
    int   n;

    fin = fopen(src, "rb");
    if (!fin) return -1;
    fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); return -1; }

    while ((n = (int)fread(g_copy_buf, 1, sizeof(g_copy_buf), fin)) > 0)
        fwrite(g_copy_buf, 1, (unsigned)n, fout);

    fclose(fin);
    fclose(fout);
    return 0;
}

/* -----------------------------------------------------------------------
 * Header and footer
 * ----------------------------------------------------------------------- */

/* Returns 1 if H:\ (drive 8) is a valid mounted drive. */
static int probe_h_drive(void)
{
    union REGS r;
    r.h.ah = 0x36;
    r.h.dl = 8;         /* INT 21h/36h: drive in DL (A=1 .. H=8) */
    int86(0x21, &r, &r);
    return (r.x.ax != 0xFFFF) ? 1 : 0;
}

static void draw_header(void)
{
    union REGS r;
    unsigned int  conv_kb = 0;
    unsigned long free_kb = 0;
    unsigned char attr    = NOS_ATTR(NOS_BLACK, NOS_CYAN);
    char buf[82];
    unsigned int hours, mins, secs;
    nos_panel_t *ap;

    /* Clear header */
    nos_scr_fill(0, HDR_ROW, 80, 1, ' ', attr);

    /* Conventional memory via INT 12h */
    r.h.ah = 0x00;
    int86(0x12, &r, &r);
    conv_kb = (unsigned int)r.x.ax;

    /* Drive free space via INT 21h / AH=36h
     * Input:  DL = drive (0=default, 1=A, 2=B, 3=C, ...)
     * Output: AX=sectors/cluster (FFFFh=invalid), BX=free clusters,
     *         CX=bytes/sector, DX=total clusters */
    ap = active_panel();
    r.h.ah = 0x36;
    r.h.dl = (unsigned char)(ap->drive - 'A' + 1);
    int86(0x21, &r, &r);
    if (r.x.ax != 0xFFFF)
        free_kb = ((unsigned long)r.x.bx * r.x.ax * r.x.cx) >> 10;

    /* Time via INT 1Ah / AH=00h: CH=hours, CL=mins, DH=secs */
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);
    hours = (unsigned int)r.h.ch;
    mins  = (unsigned int)r.h.cl;
    secs  = (unsigned int)r.h.dh;

    /* Right side: conv KB, drive free, NET, H:\ (bridge), clock */
    if (free_kb >= 1024)
        sprintf(buf, " NOS-DOS         %3uKB  %luMB free%s%s  %02u:%02u:%02u ",
                conv_kb, free_kb >> 10,
                g_net_present    ? "  NET" : "     ",
                g_bridge_mounted ? " H:" : "   ",
                hours, mins, secs);
    else
        sprintf(buf, " NOS-DOS         %3uKB  %luKB free%s%s  %02u:%02u:%02u ",
                conv_kb, free_kb,
                g_net_present    ? "  NET" : "     ",
                g_bridge_mounted ? " H:" : "   ",
                hours, mins, secs);
    nos_scr_putn(0, HDR_ROW, buf, 80, attr);

    /* Overwrite centre with active panel path */
    {
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
        "Run ", "Quit"
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
    /* Command line row -- blank */
    nos_scr_fill(0, CMD_ROW, 80, 1, ' ', NOS_ATTR_NORMAL);
}

/* -----------------------------------------------------------------------
 * DOS shell-out (F12)
 * ----------------------------------------------------------------------- */

static void set_text_mode(void)
{
    union REGS r;
    r.h.ah = 0x00;
    r.h.al = 0x03;  /* 80x25 colour text — clears screen, cursor to (0,0) */
    int86(0x10, &r, &r);
}

static void dos_shell(void)
{
    char *comspec;
    set_text_mode();   /* clear shell UI before dropping to DOS */
    comspec = getenv("COMSPEC");
    if (!comspec || *comspec == '\0') comspec = "COMMAND.COM";
    spawnl(P_WAIT, comspec, comspec, NULL);
    nos_scr_init();
    nos_scr_hide_cursor();
    redraw_all();
}

/* -----------------------------------------------------------------------
 * Action handlers
 * ----------------------------------------------------------------------- */

static void action_view(void)
{
    nos_panel_t *ap = active_panel();
    char path[128];

    if (ap->file_count == 0) return;
    if (ap->files[ap->cursor].is_dir) return;

    cursor_path(ap, path);
    nos_viewer_open(path);
    /* redraw_all() called by dispatch() after we return */
}

static void action_edit(void)
{
    nos_panel_t *ap = active_panel();
    char path[128];

    if (ap->file_count == 0) return;
    if (ap->files[ap->cursor].is_dir) return;

    cursor_path(ap, path);
    set_text_mode();
    spawnl(P_WAIT, "C:\\NOS\\SYSTEM\\EDIT.EXE", "EDIT.EXE", path, NULL);
    nos_scr_init();
    nos_scr_hide_cursor();
    /* redraw_all() called by dispatch() after we return */
}

static void action_enter(void)
{
    nos_panel_t *ap = active_panel();
    char path[128];
    int  rc = nos_panel_enter(ap, path);

    if (rc == 1) {
        /* File selected -- open in viewer */
        nos_viewer_open(path);
    }
}

static void action_copy(void)
{
    nos_panel_t *ap = active_panel();
    nos_panel_t *ip = inactive_panel();
    char src[128], dst[128], dstfull[145];
    char msg[80];
    int  last;

    if (ap->file_count == 0) return;
    if (ap->files[ap->cursor].is_dir) {
        nos_dlg_msg("Copy", "Directory copy not supported.");
        return;
    }

    /* Source: full path of highlighted file */
    cursor_path(ap, src);

    /* Default destination: other panel's path with trailing backslash */
    strcpy(dst, ip->path);
    last = (int)strlen(dst) - 1;
    if (last >= 0 && dst[last] != '\\') {
        dst[last + 1] = '\\';
        dst[last + 2] = '\0';
    }

    if (!nos_dlg_input("Copy", "Copy to:", dst, (int)sizeof(dst)))
        return;

    /* If destination ends with '\', append the source filename */
    strcpy(dstfull, dst);
    last = (int)strlen(dstfull) - 1;
    if (last >= 0 && dstfull[last] == '\\')
        strcat(dstfull, ap->files[ap->cursor].name);

    if (copy_file(src, dstfull) != 0) {
        sprintf(msg, "Cannot copy: %s", ap->files[ap->cursor].name);
        nos_dlg_msg("Error", msg);
    }

    nos_panel_read_dir(&g_left);
    nos_panel_read_dir(&g_right);
}

static void action_move(void)
{
    nos_panel_t *ap = active_panel();
    nos_panel_t *ip = inactive_panel();
    char src[128], dst[128], dstfull[145];
    char msg[80];
    int  last;

    if (ap->file_count == 0) return;
    if (strcmp(ap->files[ap->cursor].name, "..") == 0) return;

    cursor_path(ap, src);

    /* Default destination: other panel's path with trailing backslash */
    strcpy(dst, ip->path);
    last = (int)strlen(dst) - 1;
    if (last >= 0 && dst[last] != '\\') {
        dst[last + 1] = '\\';
        dst[last + 2] = '\0';
    }

    if (!nos_dlg_input("Move / Rename", "Move to:", dst, (int)sizeof(dst)))
        return;

    strcpy(dstfull, dst);
    last = (int)strlen(dstfull) - 1;
    if (last >= 0 && dstfull[last] == '\\')
        strcat(dstfull, ap->files[ap->cursor].name);

    /* Try rename first (works on the same drive for both files and dirs) */
    if (rename(src, dstfull) != 0) {
        /* Cross-drive file move: copy then delete */
        if (ap->files[ap->cursor].is_dir) {
            nos_dlg_msg("Error", "Cross-drive directory move not supported.");
        } else if (copy_file(src, dstfull) == 0) {
            remove(src);
        } else {
            sprintf(msg, "Cannot move: %s", ap->files[ap->cursor].name);
            nos_dlg_msg("Error", msg);
        }
    }

    nos_panel_read_dir(&g_left);
    nos_panel_read_dir(&g_right);
}

static void action_mkdir(void)
{
    nos_panel_t *ap = active_panel();
    char name[13];
    char full[145];
    char msg[80];
    int  len;

    name[0] = '\0';
    if (!nos_dlg_input("MkDir", "New directory name:", name, (int)sizeof(name)))
        return;
    if (name[0] == '\0') return;

    strcpy(full, ap->path);
    len = (int)strlen(full);
    if (len > 0 && full[len - 1] != '\\')
        full[len++] = '\\';
    strcpy(full + len, name);

    if (mkdir(full) != 0) {
        sprintf(msg, "Cannot create: %s", name);
        nos_dlg_msg("Error", msg);
    }

    nos_panel_read_dir(&g_left);
    nos_panel_read_dir(&g_right);
}

static void action_delete(void)
{
    nos_panel_t *ap = active_panel();
    char path[128];
    char msg[80];

    if (ap->file_count == 0) return;
    if (strcmp(ap->files[ap->cursor].name, "..") == 0) return;

    cursor_path(ap, path);
    sprintf(msg, "Delete %s?", ap->files[ap->cursor].name);

    if (!nos_dlg_confirm("Delete", msg))
        return;

    if (ap->files[ap->cursor].is_dir) {
        if (rmdir(path) != 0)
            nos_dlg_msg("Error", "Cannot remove: directory not empty.");
    } else {
        _dos_setfileattr(path, _A_NORMAL);
        if (remove(path) != 0) {
            sprintf(msg, "Cannot delete: %s", ap->files[ap->cursor].name);
            nos_dlg_msg("Error", msg);
        }
    }

    nos_panel_read_dir(&g_left);
    nos_panel_read_dir(&g_right);
}

static void action_launch(void)
{
    nos_launcher_show();
}

static void action_config(void)
{
    int new_sort = nos_cfg_sort_dialog(g_left.sort);
    if (new_sort >= 0) {
        g_left.sort  = new_sort;
        g_right.sort = new_sort;
        nos_panel_read_dir(&g_left);
        nos_panel_read_dir(&g_right);
        nos_cfg_save(new_sort);
    }
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
        ap = active_panel();

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

        /* Drive switching (Alt+F1 / Alt+F2) -- TODO: drive picker dialog */
        case NOS_KEY_ALT_F1:
        case NOS_KEY_ALT_F2:
            break;

        /* F-key actions */
        case NOS_KEY_F2:  action_config(); break;
        case NOS_KEY_F3:  action_view();   break;
        case NOS_KEY_F4:  action_edit();   break;
        case NOS_KEY_F5:  action_copy();   break;
        case NOS_KEY_F6:  action_move();   break;
        case NOS_KEY_F7:  action_mkdir();  break;
        case NOS_KEY_F8:  action_delete(); break;
        case NOS_KEY_F9:  action_launch(); break;
        case NOS_KEY_F10:
        case NOS_KEY_ALT_F4:
            action_quit(); break;
        case NOS_KEY_F12: dos_shell(); return; /* dos_shell redraws itself */

        /* Refresh (Ctrl+R) */
        case NOS_KEY_CTRL_R:
            g_net_present    = nos_hwcfg_net_present();
            g_bridge_mounted = probe_h_drive();
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

    /* First-boot welcome screen */
    if (nos_welcome_needed()) {
        nos_welcome_show();
        nos_welcome_mark_shown();
    }

    /* Check network status from NOS-HW.CFG */
    g_net_present    = nos_hwcfg_net_present();
    /* Probe H:\ for host bridge */
    g_bridge_mounted = probe_h_drive();

    /* Initialise panels */
    if (nos_panel_init(&g_left,  LEFT_COL,  PANEL_TOP, LEFT_W,  PANEL_H) != 0 ||
        nos_panel_init(&g_right, RIGHT_COL, PANEL_TOP, RIGHT_W, PANEL_H) != 0) {
        nos_scr_restore();
        return 1;
    }

    /* Apply saved sort order (overrides panel_init default of NOS_SORT_NAME) */
    {
        int saved_sort = nos_cfg_load();
        if (saved_sort != NOS_SORT_NAME) {
            g_left.sort  = saved_sort;
            g_right.sort = saved_sort;
            nos_panel_read_dir(&g_left);
            nos_panel_read_dir(&g_right);
        }
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
    set_text_mode();   /* clear shell UI so COMMAND.COM prompt is visible */
    return 0;
}
