/* NOS-DOS: NOS-INSTALL
 * install.c - Interactive TUI installer.
 *
 * Installs NOS-DOS from the installation CD to drive C:.
 *
 * Flow:
 *   Welcome -> Preflight -> Confirm -> Format -> Copy -> Done
 *
 * Requires (on the CD):
 *   NOSCD.ID      -- CD marker (used to detect the CD drive letter)
 *   FORMAT.COM    -- FreeDOS FORMAT utility
 *   INSTALL\*.*   -- NOS-DOS files to be copied to C:\
 *
 * License: GPL-2.0
 */

#include <dos.h>      /* int86, MK_FP, _dos_findfirst, _dos_findnext */
#include <direct.h>   /* mkdir */
#include <stdlib.h>   /* system */
#include <string.h>   /* strcpy, strcat, strlen */
#include <stdio.h>    /* fopen, fread, fwrite, fclose */
#include "screen.h"

/* -----------------------------------------------------------------------
 * Installer colour scheme
 * ----------------------------------------------------------------------- */

#define INST_BODY    NOS_ATTR(NOS_WHITE,  NOS_BLUE)   /* body text         */
#define INST_TITLE   NOS_ATTR(NOS_YELLOW, NOS_BLUE)   /* title bar         */
#define INST_HILIGHT NOS_ATTR(NOS_LGREEN, NOS_BLUE)   /* success / go      */
#define INST_WARN    NOS_ATTR(NOS_YELLOW, NOS_BLUE)   /* warning text      */
#define INST_ERROR   NOS_ATTR(NOS_LRED,   NOS_BLUE)   /* error text        */
#define INST_DIM     NOS_ATTR(NOS_LGRAY,  NOS_BLUE)   /* secondary text    */
#define INST_STATUS  NOS_ATTR(NOS_BLACK,  NOS_CYAN)   /* bottom hints bar  */
#define INST_CHKYES  NOS_ATTR(NOS_LGREEN, NOS_BLUE)   /* preflight OK      */
#define INST_CHKNO   NOS_ATTR(NOS_LRED,   NOS_BLUE)   /* preflight FAIL    */

/* -----------------------------------------------------------------------
 * Screen layout constants
 * ----------------------------------------------------------------------- */

#define HDR_ROW    0    /* title row                                        */
#define SEP1_ROW   1    /* double-line separator below title                */
#define BODY_TOP   2    /* first usable body row                            */
#define BODY_BOT  22    /* last usable body row                             */
#define SEP2_ROW  23    /* single-line separator above hints                */
#define HINT_ROW  24    /* bottom key-hint row                              */

/* -----------------------------------------------------------------------
 * Installer step constants
 * ----------------------------------------------------------------------- */

#define STEP_WELCOME   0
#define STEP_PREFLIGHT 1
#define STEP_CONFIRM   2
#define STEP_FORMAT    3
#define STEP_COPY      4
#define STEP_DONE      5
#define STEP_ERROR     6

/* -----------------------------------------------------------------------
 * Limits
 * ----------------------------------------------------------------------- */

#define MAX_PATH_LEN  128
#define COPY_BUF_SZ  2048  /* one CD-ROM sector — keeps reads within driver limits */

/* -----------------------------------------------------------------------
 * Globals
 * ----------------------------------------------------------------------- */

static char          g_cd_drive    = 0;
static int           g_files_done  = 0;
static int           g_copy_row    = BODY_TOP + 4;
static char          g_error_msg[80];
static unsigned char g_copy_buf[COPY_BUF_SZ];

/* -----------------------------------------------------------------------
 * Helpers: keyboard
 * ----------------------------------------------------------------------- */

/* Block until a key is pressed; return ASCII code or 0x100|scancode. */
static int inst_wait_key(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
    if (r.h.al == 0)
        return 0x100 | (int)(unsigned char)r.h.ah;
    return (int)(unsigned char)r.h.al;
}

/* Discard any buffered keystrokes. */
static void inst_flush_keys(void)
{
    union REGS r;
    r.h.ah = 0x0C;
    r.h.al = 0x00;
    int86(0x21, &r, &r);
}

/* -----------------------------------------------------------------------
 * Helpers: set 80x25 text mode
 * ----------------------------------------------------------------------- */

static void set_text_80x25(void)
{
    union REGS r;
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);
}

/* -----------------------------------------------------------------------
 * Helpers: drive detection
 * ----------------------------------------------------------------------- */

/* Return non-zero if drive (e.g. 'C') is accessible (partition + format). */
static int drive_ok(char drv)
{
    union REGS r;
    r.h.ah = 0x36;
    r.h.dl = (unsigned char)(drv - 'A' + 1);
    int86(0x21, &r, &r);
    return (r.x.ax != 0xFFFF);
}

/* Find the installation CD by looking for the NOSCD.ID marker file.
 * Returns drive letter ('D', 'E', ...) or 0 if not found.
 */
static char find_cd(void)
{
    char drv;
    char path[16];
    FILE *fp;

    for (drv = 'C'; drv <= 'Z'; drv++) {
        path[0]  = drv; path[1]  = ':'; path[2]  = '\\';
        path[3]  = 'N'; path[4]  = 'O'; path[5]  = 'S';
        path[6]  = 'C'; path[7]  = 'D'; path[8]  = '.';
        path[9]  = 'I'; path[10] = 'D'; path[11] = '\0';
        fp = fopen(path, "r");
        if (fp != NULL) {
            fclose(fp);
            return drv;
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Helpers: itoa (C89 — no stdlib itoa in Open Watcom small model)
 * ----------------------------------------------------------------------- */

static void inst_itoa(int n, char *buf)
{
    char tmp[12];
    int  ti = 0;
    int  i  = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    if (n < 0)  { buf[i++] = '-'; n = -n; }
    while (n > 0) { tmp[ti++] = (char)('0' + n % 10); n /= 10; }
    while (ti > 0) buf[i++] = tmp[--ti];
    buf[i] = '\0';
}

/* -----------------------------------------------------------------------
 * Chrome: title bar, separators, hints bar
 * ----------------------------------------------------------------------- */

static void draw_chrome(const char *step_label)
{
    int i;
    int llen;

    /* Full blue background */
    nos_scr_fill(0, 0, 80, 25, ' ', INST_BODY);

    /* Title row */
    nos_scr_fill(0, HDR_ROW, 80, 1, ' ', INST_TITLE);
    nos_scr_puts(2, HDR_ROW,
                 "NOS-DOS Setup v1.0  --  NostalgicDOS", INST_TITLE);
    if (step_label && step_label[0]) {
        llen = (int)strlen(step_label);
        nos_scr_puts(78 - llen, HDR_ROW, step_label, INST_TITLE);
    }

    /* Double separator below title */
    for (i = 0; i < 80; i++)
        nos_scr_putchar(i, SEP1_ROW, NOS_CH_DH,
                        NOS_ATTR(NOS_WHITE, NOS_BLUE));

    /* Single separator above hints */
    for (i = 0; i < 80; i++)
        nos_scr_putchar(i, SEP2_ROW, NOS_CH_H, INST_DIM);

    /* Hints bar */
    nos_scr_fill(0, HINT_ROW, 80, 1, ' ', INST_STATUS);
}

static void draw_hints(const char *hints)
{
    nos_scr_fill(0, HINT_ROW, 80, 1, ' ', INST_STATUS);
    nos_scr_puts(1, HINT_ROW, hints, INST_STATUS);
}

/* -----------------------------------------------------------------------
 * Step: WELCOME
 * ----------------------------------------------------------------------- */

static void draw_welcome(void)
{
    draw_chrome("1/5 Welcome");

    /*
     * Centred banner — "NOS-DOS" in large block letters using CP437 shading.
     * 55 chars wide; starts at column 12 to centre in 80.
     */
    nos_scr_puts(11, BODY_TOP + 1,
        "\xDB\xDB\xDB\xDB\xDB \xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB\xDB   "
        "\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB ",
        INST_TITLE);
    nos_scr_puts(11, BODY_TOP + 2,
        "\xDB\xDB  \xDB\xDB \xDB\xDB  \xDB\xDB        "
        "\xDB\xDB  \xDB\xDB \xDB\xDB     \xDB\xDB  \xDB\xDB",
        INST_TITLE);
    nos_scr_puts(11, BODY_TOP + 3,
        "\xDB\xDB  \xDB\xDB \xDB\xDB  \xDB\xDB\xDB\xDB\xDB   "
        "\xDB\xDB  \xDB\xDB \xDB\xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB\xDB ",
        INST_TITLE);
    nos_scr_puts(11, BODY_TOP + 4,
        "\xDB\xDB  \xDB\xDB \xDB\xDB  \xDB\xDB        "
        "\xDB\xDB  \xDB\xDB \xDB\xDB     \xDB\xDB  \xDB\xDB",
        INST_TITLE);
    nos_scr_puts(11, BODY_TOP + 5,
        "\xDB\xDB\xDB\xDB\xDB  \xDB\xDB  \xDB\xDB\xDB\xDB\xDB\xDB\xDB   "
        "\xDB\xDB\xDB\xDB\xDB  \xDB\xDB     \xDB\xDB  \xDB\xDB",
        INST_TITLE);

    nos_scr_puts(22, BODY_TOP + 7,
                 "Boot fast. Work clean. Remember when", INST_DIM);
    nos_scr_puts(30, BODY_TOP + 8, "computing just worked?", INST_DIM);

    nos_scr_hline(4, BODY_TOP + 10, 72, NOS_CH_H, INST_DIM);

    nos_scr_puts(4, BODY_TOP + 12,
                 "This program installs NOS-DOS on your hard disk.", INST_BODY);
    nos_scr_puts(4, BODY_TOP + 13,
                 "Drive C: must already be partitioned (run FDISK if not).", INST_BODY);
    nos_scr_puts(4, BODY_TOP + 15,
                 "\xFE WARNING: All data on C: will be erased.", INST_WARN);

    draw_hints(" [Enter] Begin    [Esc] Quit");
}

/* -----------------------------------------------------------------------
 * Step: PREFLIGHT
 * ----------------------------------------------------------------------- */

static void draw_preflight(int c_ok, char cd_drv)
{
    char cd_msg[16];
    unsigned char chk_ok  = INST_CHKYES;
    unsigned char chk_no  = INST_CHKNO;

    draw_chrome("2/5 System Check");

    nos_scr_puts(4, BODY_TOP + 1, "Checking system...", INST_BODY);

    nos_scr_hline(4, BODY_TOP + 2, 72, NOS_CH_H, INST_DIM);

    /* Hard disk */
    nos_scr_puts(6, BODY_TOP + 4, "Hard disk C:        ", INST_BODY);
    if (c_ok)
        nos_scr_puts(28, BODY_TOP + 4, "[  OK  ]", chk_ok);
    else
        nos_scr_puts(28, BODY_TOP + 4, "[ FAIL ]", chk_no);

    /* CD-ROM */
    nos_scr_puts(6, BODY_TOP + 5, "Installation disc   ", INST_BODY);
    if (cd_drv != 0) {
        cd_msg[0] = '['; cd_msg[1] = ' '; cd_msg[2] = 'O'; cd_msg[3] = 'K';
        cd_msg[4] = ' '; cd_msg[5] = '('; cd_msg[6] = cd_drv;
        cd_msg[7] = ':'; cd_msg[8] = ')'; cd_msg[9] = ']'; cd_msg[10] = '\0';
        nos_scr_puts(28, BODY_TOP + 5, cd_msg, chk_ok);
    } else {
        nos_scr_puts(28, BODY_TOP + 5, "[ FAIL ]", chk_no);
    }

    nos_scr_hline(4, BODY_TOP + 7, 72, NOS_CH_H, INST_DIM);

    if (!c_ok) {
        nos_scr_puts(4, BODY_TOP + 9,
                     "Drive C: is not accessible.", INST_WARN);
        nos_scr_puts(4, BODY_TOP + 10,
                     "You must create a partition on C: before installing.", INST_BODY);
        nos_scr_puts(4, BODY_TOP + 11,
                     "Exit, run FDISK, create a primary DOS partition,", INST_BODY);
        nos_scr_puts(4, BODY_TOP + 12,
                     "reboot, then run INSTALL.EXE again.", INST_BODY);
        draw_hints(" [Esc] Exit to DOS");
    } else if (cd_drv == 0) {
        nos_scr_puts(4, BODY_TOP + 9,
                     "Installation disc not detected.", INST_WARN);
        nos_scr_puts(4, BODY_TOP + 10,
                     "Check that the CD-ROM driver is loaded in CONFIG.SYS", INST_BODY);
        nos_scr_puts(4, BODY_TOP + 11,
                     "and that the installer disc is inserted.", INST_BODY);
        draw_hints(" [Esc] Exit to DOS");
    } else {
        nos_scr_puts(4, BODY_TOP + 9,
                     "All checks passed. Ready to install.", INST_HILIGHT);
        draw_hints(" [Enter] Continue    [Esc] Quit");
    }
}

/* -----------------------------------------------------------------------
 * Step: CONFIRM
 * ----------------------------------------------------------------------- */

static void draw_confirm(void)
{
    draw_chrome("3/5 Confirm");

    nos_scr_fill(2, BODY_TOP + 1, 76, 1, ' ', INST_WARN);
    nos_scr_puts(3, BODY_TOP + 1,
                 "WARNING: The following steps will ERASE ALL DATA on C:", INST_WARN);

    nos_scr_hline(4, BODY_TOP + 3, 72, NOS_CH_H, INST_DIM);

    nos_scr_puts(4, BODY_TOP + 5,
                 "Step 1 of 2:  Format C: and transfer the system kernel.", INST_BODY);
    nos_scr_puts(4, BODY_TOP + 6,
                 "              FORMAT will ask you to confirm -- press Y.", INST_DIM);
    nos_scr_puts(4, BODY_TOP + 8,
                 "Step 2 of 2:  Copy all NOS-DOS files from the disc to C:", INST_BODY);
    nos_scr_puts(4, BODY_TOP + 9,
                 "              Progress is shown on screen.", INST_DIM);

    nos_scr_hline(4, BODY_TOP + 11, 72, NOS_CH_H, INST_DIM);

    nos_scr_puts(4, BODY_TOP + 13,
                 "Press Enter to begin, or Esc to cancel.", INST_HILIGHT);

    draw_hints(" [Enter] Install Now    [Esc] Cancel");
}

/* -----------------------------------------------------------------------
 * Step: FORMAT (render only — action in main)
 * ----------------------------------------------------------------------- */

static void draw_format(void)
{
    draw_chrome("4/5 Format C:");

    nos_scr_puts(4, BODY_TOP + 2,
                 "Formatting drive C: and transferring the system kernel.", INST_BODY);
    nos_scr_puts(4, BODY_TOP + 3,
                 "The FreeDOS FORMAT utility will now run.", INST_DIM);

    nos_scr_hline(4, BODY_TOP + 5, 72, NOS_CH_H, INST_DIM);

    nos_scr_puts(4, BODY_TOP + 7,
                 "When FORMAT asks \"Are you sure (Y/N)?\" press Y then Enter.", INST_WARN);
    nos_scr_puts(4, BODY_TOP + 8,
                 "FORMAT will also ask for a volume label -- just press Enter", INST_BODY);
    nos_scr_puts(4, BODY_TOP + 9,
                 "to accept the default (NOS-DOS), or type a label.", INST_BODY);

    nos_scr_hline(4, BODY_TOP + 11, 72, NOS_CH_H, INST_DIM);

    nos_scr_puts(4, BODY_TOP + 13,
                 "Press any key to launch FORMAT.", INST_HILIGHT);

    draw_hints(" [Any key] Launch FORMAT    [Esc] Abort");
}

/* -----------------------------------------------------------------------
 * Step: COPY (header only — progress updated inline)
 * ----------------------------------------------------------------------- */

static void draw_copy_header(char cd_drv)
{
    char src_label[40];
    int i;

    draw_chrome("5/5 Copying Files");

    nos_scr_puts(4, BODY_TOP, "Copying NOS-DOS files...", INST_BODY);

    /* Build source path label: "X:\INSTALL  -->  C:\" */
    i = 0;
    src_label[i++] = cd_drv; src_label[i++] = ':';
    src_label[i++] = '\\';
    src_label[i++] = 'I'; src_label[i++] = 'N'; src_label[i++] = 'S';
    src_label[i++] = 'T'; src_label[i++] = 'A'; src_label[i++] = 'L';
    src_label[i++] = 'L'; src_label[i++] = ' '; src_label[i++] = ' ';
    src_label[i++] = '-'; src_label[i++] = '-'; src_label[i++] = '>';
    src_label[i++] = ' '; src_label[i++] = ' ';
    src_label[i++] = 'C'; src_label[i++] = ':'; src_label[i++] = '\\';
    src_label[i] = '\0';
    nos_scr_puts(4, BODY_TOP + 1, src_label, INST_DIM);

    nos_scr_hline(0, BODY_TOP + 2, 80, NOS_CH_H, INST_DIM);

    g_copy_row = BODY_TOP + 3;
    draw_hints(" Copying, please wait...");
}

/* -----------------------------------------------------------------------
 * Step: DONE
 * ----------------------------------------------------------------------- */

static void draw_done(void)
{
    char count_buf[48];
    char num_buf[12];

    draw_chrome("Done");

    nos_scr_fill(2, BODY_TOP + 2, 76, 1, ' ', INST_HILIGHT);
    nos_scr_puts(3, BODY_TOP + 2,
                 "NOS-DOS installed successfully!", INST_HILIGHT);

    inst_itoa(g_files_done, num_buf);
    strcpy(count_buf, "Installed ");
    strcat(count_buf, num_buf);
    strcat(count_buf, " files to C:\\");
    nos_scr_puts(4, BODY_TOP + 4, count_buf, INST_BODY);

    nos_scr_hline(4, BODY_TOP + 6, 72, NOS_CH_H, INST_DIM);

    nos_scr_puts(4, BODY_TOP + 8, "To complete setup:", INST_BODY);
    nos_scr_puts(6, BODY_TOP + 9,
                 "1. Remove the NOS-DOS installation disc.", INST_BODY);
    nos_scr_puts(6, BODY_TOP + 10,
                 "2. Press Enter to reboot.", INST_BODY);
    nos_scr_puts(6, BODY_TOP + 11,
                 "3. NOS-DOS will boot from C: and finish first-boot setup.", INST_BODY);

    draw_hints(" [Enter] Reboot    [Esc] Exit to DOS");
}

/* -----------------------------------------------------------------------
 * Step: ERROR
 * ----------------------------------------------------------------------- */

static void draw_error(void)
{
    draw_chrome("Error");

    nos_scr_fill(2, BODY_TOP + 2, 76, 1, ' ', INST_ERROR);
    nos_scr_puts(3, BODY_TOP + 2,
                 "Installation failed.", INST_ERROR);

    if (g_error_msg[0])
        nos_scr_puts(4, BODY_TOP + 4, g_error_msg, INST_WARN);

    nos_scr_puts(4, BODY_TOP + 6,
                 "Press Esc to exit to DOS.", INST_BODY);
    draw_hints(" [Esc] Exit to DOS");
}

/* -----------------------------------------------------------------------
 * File copy engine
 *
 * DOS has a single global DTA (Data Transfer Area).  _dos_findfirst sets
 * the DTA to point at the caller's find_t, so a recursive call would
 * clobber the parent's find state.  We avoid this by completing each
 * find loop fully and collecting subdirectory names BEFORE recursing.
 * ----------------------------------------------------------------------- */

#define MAX_SUBDIRS   32   /* max subdirs per directory level */
#define MAX_DNAME     13   /* 8.3 filename + NUL */

/* Update the per-file progress display. */
static void copy_update_display(const char *filename)
{
    char numstr[12];
    char countline[32];

    /* Scroll body when we reach the bottom. */
    if (g_copy_row > BODY_BOT) {
        nos_scr_fill(0, BODY_TOP + 3, 80, BODY_BOT - (BODY_TOP + 3) + 1,
                     ' ', INST_BODY);
        g_copy_row = BODY_TOP + 3;
    }

    nos_scr_fill(0, g_copy_row, 80, 1, ' ', INST_BODY);
    nos_scr_putn(4, g_copy_row, filename, 72, INST_DIM);
    g_copy_row++;

    /* Update running file count in top-right of body area. */
    inst_itoa(g_files_done, numstr);
    strcpy(countline, "Files: ");
    strcat(countline, numstr);
    nos_scr_fill(56, BODY_TOP, 23, 1, ' ', INST_BODY);
    nos_scr_puts(56, BODY_TOP, countline, INST_HILIGHT);
}

/* Update the hint bar with a short phase label for copy diagnostics.
 * Shows which I/O operation is in progress so a hang can be pinpointed.
 */
static void copy_phase(const char *phase)
{
    nos_scr_fill(0, HINT_ROW, 80, 1, ' ', INST_STATUS);
    nos_scr_puts(1, HINT_ROW, phase, INST_STATUS);
}

/* Copy src -> dst, returning 1 on success. */
static int copy_file(const char *src, const char *dst)
{
    FILE   *fsrc;
    FILE   *fdst;
    size_t  nr;
    size_t  nw;

    copy_phase(" Opening source...");
    fsrc = fopen(src, "rb");
    if (!fsrc) return 0;

    copy_phase(" Opening dest...");
    fdst = fopen(dst, "wb");
    if (!fdst) { fclose(fsrc); return 0; }

    copy_phase(" Copying...");
    for (;;) {
        nr = fread(g_copy_buf, 1, COPY_BUF_SZ, fsrc);
        if (nr == 0) break;
        nw = fwrite(g_copy_buf, 1, nr, fdst);
        if (nw != nr) { fclose(fsrc); fclose(fdst); return 0; }
    }

    fclose(fsrc);
    fclose(fdst);
    return 1;
}

/* Append a backslash to path if not already present. */
static void ensure_trailing_slash(char *path)
{
    int len = (int)strlen(path);
    if (len > 0 && path[len - 1] != '\\') {
        path[len]     = '\\';
        path[len + 1] = '\0';
    }
}

/*
 * Recursively copy all files from src_dir into dst_dir.
 * Creates dst_dir and all subdirectories as needed.
 * Skips '.' and '..' entries.
 */
static void copy_tree(const char *src_dir, const char *dst_dir)
{
    struct find_t fi;
    char          pat[MAX_PATH_LEN];
    char          src_path[MAX_PATH_LEN];
    char          dst_path[MAX_PATH_LEN];
    /* Subdirectory names collected before recursion to avoid DTA collision */
    char          subdirs[MAX_SUBDIRS][MAX_DNAME];
    int           nsub = 0;
    int           i;
    unsigned      rc;

    mkdir(dst_dir);

    /* Build search pattern: src_dir\*.* */
    strcpy(pat, src_dir);
    ensure_trailing_slash(pat);
    strcat(pat, "*.*");

    /* Single pass: copy files and collect subdirectory names.
     * The find loop is completed in full before any recursion so the DTA
     * (which _dos_findfirst sets to &fi) is never overwritten by a child call.
     */
    rc = _dos_findfirst(pat, 0x3F, &fi);
    while (rc == 0) {
        if (fi.name[0] != '.') {
            if (fi.attrib & _A_SUBDIR) {
                /* Collect name for later recursion */
                if (nsub < MAX_SUBDIRS) {
                    strcpy(subdirs[nsub], fi.name);
                    nsub++;
                }
            } else {
                /* Copy the file now */
                strcpy(src_path, src_dir);
                ensure_trailing_slash(src_path);
                strcat(src_path, fi.name);

                strcpy(dst_path, dst_dir);
                ensure_trailing_slash(dst_path);
                strcat(dst_path, fi.name);

                copy_update_display(fi.name);
                if (copy_file(src_path, dst_path))
                    g_files_done++;
            }
        }
        rc = _dos_findnext(&fi);
    }

    /* Recurse into collected subdirectories (find loop is done, DTA is free) */
    for (i = 0; i < nsub; i++) {
        strcpy(src_path, src_dir);
        ensure_trailing_slash(src_path);
        strcat(src_path, subdirs[i]);

        strcpy(dst_path, dst_dir);
        ensure_trailing_slash(dst_path);
        strcat(dst_path, subdirs[i]);

        copy_tree(src_path, dst_path);
    }
}

/* -----------------------------------------------------------------------
 * Reboot via INT 19h (bootstrap loader)
 * ----------------------------------------------------------------------- */

static void do_reboot(void)
{
    union REGS r;
    nos_scr_restore();
    int86(0x19, &r, &r);
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void)
{
    int  step;
    int  key;
    int  c_ok;
    char cd_drv;
    char fmt_cmd[64];
    char src_dir[MAX_PATH_LEN];

    nos_scr_init();
    nos_scr_hide_cursor();

    step = STEP_WELCOME;
    g_cd_drive   = 0;
    g_files_done = 0;
    g_error_msg[0] = '\0';

    for (;;) {
        switch (step) {

        /* ----------------------------------------------------------------
         * Welcome
         * ---------------------------------------------------------------- */
        case STEP_WELCOME:
            draw_welcome();
            key = inst_wait_key();
            if (key == 0x1B) { nos_scr_restore(); return 0; }
            if (key == 0x0D) { step = STEP_PREFLIGHT; }
            break;

        /* ----------------------------------------------------------------
         * Pre-flight: check C: and CD
         * ---------------------------------------------------------------- */
        case STEP_PREFLIGHT:
            c_ok   = drive_ok('C');
            cd_drv = find_cd();
            if (cd_drv) g_cd_drive = cd_drv;

            draw_preflight(c_ok, cd_drv);
            key = inst_wait_key();

            if (key == 0x1B) { nos_scr_restore(); return 0; }
            if (key == 0x0D && c_ok && cd_drv != 0)
                step = STEP_CONFIRM;
            /* Otherwise stay on this screen. */
            break;

        /* ----------------------------------------------------------------
         * Confirm: last chance to back out
         * ---------------------------------------------------------------- */
        case STEP_CONFIRM:
            draw_confirm();
            key = inst_wait_key();
            if (key == 0x1B) { nos_scr_restore(); return 0; }
            if (key == 0x0D) { step = STEP_FORMAT; }
            break;

        /* ----------------------------------------------------------------
         * Format: shell out to FORMAT.COM on the CD
         * ---------------------------------------------------------------- */
        case STEP_FORMAT:
            draw_format();
            key = inst_wait_key();
            if (key == 0x1B) { nos_scr_restore(); return 0; }

            /* Leave TUI, run FORMAT */
            nos_scr_restore();
            set_text_80x25();

            fmt_cmd[0] = g_cd_drive;
            fmt_cmd[1] = ':';
            fmt_cmd[2] = '\\';
            strcpy(fmt_cmd + 3, "FORMAT.EXE C: /S /V:NOS-DOS");
            system(fmt_cmd);

            /* Re-enter TUI for copy step */
            set_text_80x25();
            nos_scr_init();
            nos_scr_hide_cursor();
            inst_flush_keys();
            step = STEP_COPY;
            break;

        /* ----------------------------------------------------------------
         * Copy: recursive file copy with per-file progress
         * ---------------------------------------------------------------- */
        case STEP_COPY:
            draw_copy_header(g_cd_drive);

            /* Build source path: X:\INSTALL */
            src_dir[0] = g_cd_drive;
            src_dir[1] = ':';
            src_dir[2] = '\\';
            strcpy(src_dir + 3, "INSTALL");

            copy_tree(src_dir, "C:\\");

            if (g_files_done == 0) {
                strcpy(g_error_msg,
                       "No files were copied. Check the installation disc.");
                step = STEP_ERROR;
                break;
            }

            /* Remove any WELCOMED flag that survived from a prior install
             * (FORMAT can fail to wipe C: cleanly; the flag is not in the
             * INSTALL\ tree so copy_tree does not overwrite it). */
            remove("C:\\NOS\\SYSTEM\\WELCOMED");

            /*
             * Finalize C: so it boots reliably:
             *
             * 1. A:\SYS C:
             *    Writes the FreeDOS partition boot record (PBR) and re-stamps
             *    KERNEL.SYS on C: with hidden+system attributes.  Needed
             *    whether or not FORMAT /S successfully transferred the system.
             *
             * 2. D:\FDISK.EXE /ACTIVATE:1
             *    Marks partition 1 on the first hard disk as the active/boot
             *    partition.  Harmless if already set; critical if the user ran
             *    FDISK but forgot this step.
             *
             * 3. D:\FDISK.EXE /MBR
             *    Writes the FreeDOS Master Boot Record loader to sector 0 of
             *    the disk.  The MBR is what SeaBIOS/BIOS actually executes
             *    first — without a valid MBR with 0x55AA, the BIOS reports
             *    "No bootable medium" even when the PBR and files are correct.
             */
            /*
             * Explicitly write C:\CONFIG.SYS and C:\AUTOEXEC.BAT with known-good
             * bootstrapping content.  copy_tree may silently fail to overwrite
             * these files if a previous install left them read-only or with system
             * attributes.  Writing them directly here guarantees the correct content
             * regardless of copy_tree's result.
             */
            {
                FILE *bf;

                bf = fopen("C:\\CONFIG.SYS", "w");
                if (bf) {
                    fprintf(bf, "REM NOS-DOS Configuration\r\n");
                    fprintf(bf, "REM Written by NOS-INSTALL. DETECT.EXE updates on first boot.\r\n");
                    fprintf(bf, "DOS=HIGH,UMB\r\n");
                    fprintf(bf, "DEVICE=C:\\NOS\\SYSTEM\\JEMMEX.EXE NOEMS X=TEST\r\n");
                    fprintf(bf, "FILES=40\r\n");
                    fprintf(bf, "BUFFERS=20\r\n");
                    fprintf(bf, "STACKS=9,256\r\n");
                    fprintf(bf, "SHELL=C:\\COMMAND.COM C:\\ /P /E:512\r\n");
                    fclose(bf);
                }

                bf = fopen("C:\\AUTOEXEC.BAT", "w");
                if (bf) {
                    fprintf(bf, "@ECHO OFF\r\n");
                    fprintf(bf, "REM NOS-DOS Startup\r\n");
                    fprintf(bf, "REM DETECT.EXE will replace this file on first boot.\r\n");
                    fprintf(bf, "SET PROMPT=$P$G\r\n");
                    fprintf(bf, "SET PATH=C:\\;C:\\NOS\\SYSTEM;C:\\NOS\\SHELL;C:\\APPS\r\n");
                    fprintf(bf, "C:\\NOS\\SYSTEM\\DETECT.EXE\r\n");
                    fprintf(bf, "IF EXIST C:\\NOS\\SHELL\\SHELL.EXE C:\\NOS\\SHELL\\SHELL.EXE\r\n");
                    fclose(bf);
                }
            }

            nos_scr_fill(0, g_copy_row, 80, 1, ' ', INST_BODY);
            nos_scr_puts(4, g_copy_row,
                         "Finalizing boot sector...", INST_HILIGHT);
            draw_hints(" Finalizing, please wait...");

            nos_scr_restore();
            set_text_80x25();

            system("A:\\SYS C:");

            {
                char fdisk_cmd[64];
                fdisk_cmd[0] = g_cd_drive;
                fdisk_cmd[1] = ':'; fdisk_cmd[2] = '\\';
                strcpy(fdisk_cmd + 3, "FDISK.EXE /ACTIVATE:1");
                system(fdisk_cmd);

                fdisk_cmd[0] = g_cd_drive;
                fdisk_cmd[1] = ':'; fdisk_cmd[2] = '\\';
                strcpy(fdisk_cmd + 3, "FDISK.EXE /MBR");
                system(fdisk_cmd);
            }

            set_text_80x25();
            nos_scr_init();
            nos_scr_hide_cursor();

            draw_hints(" Copying complete.");
            step = STEP_DONE;
            break;

        /* ----------------------------------------------------------------
         * Done
         * ---------------------------------------------------------------- */
        case STEP_DONE:
            draw_done();
            key = inst_wait_key();
            if (key == 0x0D) { do_reboot(); }
            nos_scr_restore();
            return 0;

        /* ----------------------------------------------------------------
         * Error
         * ---------------------------------------------------------------- */
        case STEP_ERROR:
            draw_error();
            inst_wait_key();
            nos_scr_restore();
            return 1;

        default:
            nos_scr_restore();
            return 1;
        }
    }
}
