/* NOS-DOS: NOS-SHELL
 * panel.c - File panel implementation.
 *
 * Directory reading: INT 21h FindFirst/FindNext (AH=4Eh/4Fh).
 * All directories are sorted to the top; ".." is always first.
 * Files are sorted below directories by the active sort key.
 *
 * Memory: files[] array is allocated in nos_panel_init and reused across
 * directory changes (reallocated if the new dir has more entries).
 *
 * License: GPL-2.0
 */

#include <dos.h>    /* int86, intdos, union REGS, struct SREGS */
#include <stdlib.h> /* malloc, free, realloc */
#include <string.h> /* strcpy, strcat, strcmp, strlen, memset */
#include <stdio.h>  /* sprintf */
#include "panel.h"
#include "screen.h"

/* -----------------------------------------------------------------------
 * DOS FindFirst / FindNext via intdos
 *
 * We use the DOS Disk Transfer Area (DTA) at 0x80 of the PSP (default).
 * For safety we set our own DTA at a static buffer.
 * ----------------------------------------------------------------------- */

/* DOS attribute bits */
#define DOS_ATTR_RDONLY  0x01
#define DOS_ATTR_HIDDEN  0x02
#define DOS_ATTR_SYSTEM  0x04
#define DOS_ATTR_VOLUME  0x08
#define DOS_ATTR_SUBDIR  0x10
#define DOS_ATTR_ARCH    0x20

/* FindFirst/Next search attributes — include hidden+system+directories */
#define SEARCH_ATTR  (DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM | DOS_ATTR_SUBDIR | DOS_ATTR_ARCH)

/* DOS DTA layout for FindFirst/Next (44 bytes) */
typedef struct {
    unsigned char  reserved[21];
    unsigned char  attrib;
    unsigned int   time;
    unsigned int   date;
    unsigned long  size;
    char           name[13];
} nos_dta_t;

static nos_dta_t g_dta;

static void set_dta(void)
{
    union REGS  r;
    struct SREGS sr;
    /* INT 21h / AH=1Ah: set DTA */
    r.h.ah = 0x1A;
    segread(&sr);
    sr.ds  = FP_SEG(&g_dta);
    r.x.dx = FP_OFF(&g_dta);
    intdosx(&r, &r, &sr);
}

/* Returns 0 on success, non-zero on error (no match). */
static int find_first(const char *path, unsigned int attr)
{
    union REGS  r;
    struct SREGS sr;
    r.h.ah = 0x4E;
    r.x.cx = attr;
    segread(&sr);
    sr.ds  = FP_SEG(path);
    r.x.dx = FP_OFF(path);
    intdosx(&r, &r, &sr);
    return (r.x.cflag) ? -1 : 0;   /* carry flag = error */
}

static int find_next(void)
{
    union REGS r;
    r.h.ah = 0x4F;
    intdos(&r, &r);
    return (r.x.cflag) ? -1 : 0;
}

/* -----------------------------------------------------------------------
 * Sorting helpers
 * ----------------------------------------------------------------------- */

/* Global sort key used by the qsort comparator — not re-entrant, but
 * we only sort one panel at a time on a single-threaded DOS system. */
static int g_sort_key = NOS_SORT_NAME;

static int cmp_entries(const void *a, const void *b)
{
    const nos_fileentry_t *fa = (const nos_fileentry_t *)a;
    const nos_fileentry_t *fb = (const nos_fileentry_t *)b;
    int da, db;
    const char *ea, *eb;

    /* ".." always first */
    if (fa->name[0] == '.' && fa->name[1] == '.') return -1;
    if (fb->name[0] == '.' && fb->name[1] == '.') return  1;

    /* Directories before files */
    da = fa->is_dir;
    db = fb->is_dir;
    if (da != db) return db - da;   /* dirs (1) sort before files (0) */

    switch (g_sort_key) {
    case NOS_SORT_EXT: {
        /* Compare extension first, then name */
        ea = (const char *)0;
        eb = (const char *)0;
        { const char *p = fa->name; while (*p && *p != '.') p++; if (*p == '.') ea = p+1; }
        { const char *p = fb->name; while (*p && *p != '.') p++; if (*p == '.') eb = p+1; }
        if (ea && eb) {
            int c = strcmp(ea, eb);
            if (c) return c;
        } else if (ea) return  1;
        else if (eb) return -1;
        return strcmp(fa->name, fb->name);
    }
    case NOS_SORT_SIZE:
        if (fa->size < fb->size) return -1;
        if (fa->size > fb->size) return  1;
        return strcmp(fa->name, fb->name);

    case NOS_SORT_DATE:
        if (fa->date != fb->date) return (fa->date < fb->date) ? -1 : 1;
        if (fa->time != fb->time) return (fa->time < fb->time) ? -1 : 1;
        return strcmp(fa->name, fb->name);

    default: /* NOS_SORT_NAME */
        return strcmp(fa->name, fb->name);
    }
}

/* -----------------------------------------------------------------------
 * Alloc helpers
 * ----------------------------------------------------------------------- */

static int ensure_capacity(nos_panel_t *p, int need)
{
    nos_fileentry_t *newbuf;
    if (need <= p->alloc_count) return 0;
    newbuf = (nos_fileentry_t *)realloc(p->files, (size_t)need * sizeof(nos_fileentry_t));
    if (!newbuf) return -1;
    p->files       = newbuf;
    p->alloc_count = need;
    return 0;
}

/* -----------------------------------------------------------------------
 * Init / free
 * ----------------------------------------------------------------------- */

int nos_panel_init(nos_panel_t *p, int col, int row, int width, int height)
{
    union REGS r;
    char path[128];

    memset(p, 0, sizeof(*p));
    p->col    = col;
    p->row    = row;
    p->width  = width;
    p->height = height;
    p->sort   = NOS_SORT_NAME;

    /* Initial allocation */
    p->files = (nos_fileentry_t *)malloc(64 * sizeof(nos_fileentry_t));
    if (!p->files) return -1;
    p->alloc_count = 64;

    /* Get current drive (INT 21h / AH=19h → AL=drive 0=A) */
    r.h.ah = 0x19;
    intdos(&r, &r);
    p->drive = (char)('A' + r.h.al);

    /* Get current directory (INT 21h / AH=47h)
     * DL=drive (0=default), DS:SI → 64-byte buffer (no leading backslash) */
    {
        struct SREGS sr;
        char         dirbuf[65];
        dirbuf[0] = '\0';
        r.h.ah = 0x47;
        r.h.dl = 0; /* current drive */
        segread(&sr);
        sr.ds  = FP_SEG(dirbuf);
        r.x.si = FP_OFF(dirbuf);
        intdosx(&r, &r, &sr);
        if (dirbuf[0])
            sprintf(path, "%c:\\%s", p->drive, dirbuf);
        else
            sprintf(path, "%c:\\", p->drive);
    }
    strcpy(p->path, path);

    set_dta();
    return nos_panel_read_dir(p);
}

void nos_panel_free(nos_panel_t *p)
{
    if (p->files) {
        free(p->files);
        p->files = NULL;
    }
}

/* -----------------------------------------------------------------------
 * Directory reading
 * ----------------------------------------------------------------------- */

int nos_panel_read_dir(nos_panel_t *p)
{
    char   pattern[136]; /* path + "\*.*" */
    int    n = 0;
    int    is_root;
    size_t plen;

    set_dta();
    p->file_count = 0;
    p->cursor     = 0;
    p->scroll     = 0;

    /* Build search pattern: path\*.* */
    strcpy(pattern, p->path);
    plen = strlen(pattern);
    if (plen > 0 && pattern[plen-1] != '\\') {
        pattern[plen]   = '\\';
        pattern[plen+1] = '\0';
        plen++;
    }
    strcat(pattern, "*.*");

    /* Is this the root directory? (no ".." entry from DOS) */
    is_root = (p->path[2] == '\\' && p->path[3] == '\0') ||
              (p->path[2] == '\0');

    /* Add ".." entry unless we're at root */
    if (!is_root) {
        if (ensure_capacity(p, 1) != 0) return -1;
        memset(&p->files[0], 0, sizeof(p->files[0]));
        strcpy(p->files[0].name, "..");
        p->files[0].is_dir = 1;
        n = 1;
    }

    if (find_first(pattern, SEARCH_ATTR) != 0)
        goto done;

    do {
        /* Skip "." and ".." from DOS (we added ".." manually above) */
        if (g_dta.name[0] == '.')
            continue;
        /* Skip volume labels */
        if (g_dta.attrib & DOS_ATTR_VOLUME)
            continue;

        if (n >= NOS_PANEL_MAX_FILES) break;
        if (ensure_capacity(p, n + 1) != 0) break;

        memset(&p->files[n], 0, sizeof(p->files[n]));
        strncpy(p->files[n].name, g_dta.name, NOS_PANEL_NAME_LEN - 1);
        p->files[n].name[NOS_PANEL_NAME_LEN - 1] = '\0';
        p->files[n].size   = g_dta.size;
        p->files[n].date   = g_dta.date;
        p->files[n].time   = g_dta.time;
        p->files[n].attrib = g_dta.attrib;
        p->files[n].is_dir = (g_dta.attrib & DOS_ATTR_SUBDIR) ? 1 : 0;
        n++;
    } while (find_next() == 0);

done:
    p->file_count = n;
    g_sort_key = p->sort;
    if (n > 1)
        qsort(p->files + (is_root ? 0 : 1),
              (size_t)(n - (is_root ? 0 : 1)),
              sizeof(nos_fileentry_t), cmp_entries);
    return 0;
}

/* -----------------------------------------------------------------------
 * Drawing
 * ----------------------------------------------------------------------- */

/* Format a file size for the panel column (5 chars, right-aligned).
 * Directories show "<DIR>". */
static void fmt_size(const nos_fileentry_t *f, char *buf)
{
    unsigned long sz;
    if (f->is_dir) { strcpy(buf, "<DIR>"); return; }
    sz = f->size;
    if      (sz < 1000000UL)       sprintf(buf, "%5lu",  sz);
    else if (sz < 1000000000UL)    sprintf(buf, "%4luK", sz / 1024UL);
    else                           sprintf(buf, "%4luM", sz / (1024UL*1024UL));
}

/* Format DOS packed date as "MM-DD-YY" (8 chars). */
static void fmt_date(unsigned int d, char *buf)
{
    unsigned int year  = ((d >> 9) & 0x7F) + 1980;
    unsigned int month = (d >> 5) & 0x0F;
    unsigned int day   = d & 0x1F;
    sprintf(buf, "%02u-%02u-%02u", month, day, year % 100);
}

void nos_panel_draw(nos_panel_t *p)
{
    unsigned char border_attr, title_attr, normal_attr, dir_attr, sel_attr;
    int inner_w, inner_h, visible, i, idx, entry_row;
    char line[128];
    char size_buf[8];
    char date_buf[12];

    border_attr = p->active ? NOS_ATTR(NOS_CYAN,   NOS_BLACK)
                            : NOS_ATTR(NOS_LGRAY,  NOS_BLACK);
    title_attr  = p->active ? NOS_ATTR(NOS_BLACK,  NOS_CYAN)
                            : NOS_ATTR(NOS_BLACK,  NOS_LGRAY);
    normal_attr = NOS_ATTR(NOS_LGRAY, NOS_BLACK);
    dir_attr    = NOS_ATTR(NOS_WHITE, NOS_BLACK);
    sel_attr    = p->active ? NOS_ATTR(NOS_BLACK,  NOS_CYAN)
                            : NOS_ATTR(NOS_BLACK,  NOS_LGRAY);

    /* Draw border */
    nos_scr_box(p->col, p->row, p->width, p->height, border_attr);

    /* Title bar: path centred inside the top border */
    inner_w = p->width - 2;
    {
        char title[80];
        int tlen, tpad, tcol;
        sprintf(title, " %s ", p->path);
        tlen = (int)strlen(title);
        if (tlen > inner_w) tlen = inner_w;
        tpad = (inner_w - tlen) / 2;
        tcol = p->col + 1 + tpad;
        nos_scr_putn(tcol, p->row, title, tlen, title_attr);
    }

    /* File list */
    inner_h = p->height - 2;
    visible = inner_h;
    if (visible > p->file_count) visible = p->file_count;

    /* Clear interior first */
    nos_scr_fill(p->col + 1, p->row + 1, inner_w, inner_h, ' ', normal_attr);

    for (i = 0; i < visible; i++) {
        idx       = p->scroll + i;
        entry_row = p->row + 1 + i;

        if (idx >= p->file_count) break;
        {
            nos_fileentry_t *f = &p->files[idx];
            unsigned char attr;

            if (idx == p->cursor)
                attr = sel_attr;
            else if (f->is_dir)
                attr = dir_attr;
            else
                attr = normal_attr;

            fmt_size(f, size_buf);
            fmt_date(f->date, date_buf);

            /* Layout: name (left-padded to 12) + size (5) + space + date (8) */
            /* Total used: 12 + 1 + 5 + 1 + 8 = 27.  Width varies by panel. */
            if (inner_w >= 27) {
                sprintf(line, "%-12s %5s %8s", f->name, size_buf, date_buf);
            } else {
                sprintf(line, "%-*s", inner_w, f->name);
            }
            nos_scr_putn(p->col + 1, entry_row, line, inner_w, attr);
        }
    }

    /* Scroll indicator (right border column) */
    if (p->file_count > inner_h) {
        int bar_row;
        bar_row = p->row + 1 + (p->scroll * inner_h) / p->file_count;
        if (bar_row > p->row + p->height - 2) bar_row = p->row + p->height - 2;
        nos_scr_putchar(p->col + p->width - 1, bar_row, 0xDB, border_attr);
    }
}

/* -----------------------------------------------------------------------
 * Cursor movement
 * ----------------------------------------------------------------------- */

void nos_panel_move_cursor(nos_panel_t *p, int delta)
{
    int inner_h = p->height - 2;

    p->cursor += delta;
    if (p->cursor < 0)               p->cursor = 0;
    if (p->cursor >= p->file_count)  p->cursor = p->file_count - 1;

    /* Adjust scroll to keep cursor visible */
    if (p->cursor < p->scroll)
        p->scroll = p->cursor;
    if (p->cursor >= p->scroll + inner_h)
        p->scroll = p->cursor - inner_h + 1;
}

void nos_panel_page(nos_panel_t *p, int dir)
{
    nos_panel_move_cursor(p, dir * (p->height - 2));
}

/* -----------------------------------------------------------------------
 * Navigation
 * ----------------------------------------------------------------------- */

void nos_panel_cursor_path(const nos_panel_t *p, char *buf)
{
    size_t plen;
    if (p->file_count == 0) { strcpy(buf, p->path); return; }
    strcpy(buf, p->path);
    plen = strlen(buf);
    if (buf[plen-1] != '\\') { buf[plen] = '\\'; buf[plen+1] = '\0'; plen++; }
    strcat(buf, p->files[p->cursor].name);
}

int nos_panel_enter(nos_panel_t *p, char *out_path)
{
    nos_fileentry_t *f;
    size_t plen;

    if (p->file_count == 0) return -1;
    f = &p->files[p->cursor];

    if (!f->is_dir) {
        nos_panel_cursor_path(p, out_path);
        return 1; /* caller handles the file */
    }

    /* Navigate into directory */
    if (f->name[0] == '.' && f->name[1] == '.') {
        /* Go up: strip last path component */
        char *slash;
        plen = strlen(p->path);
        /* Remove trailing backslash if any (except root) */
        if (plen > 3 && p->path[plen-1] == '\\') { p->path[plen-1] = '\0'; plen--; }
        slash = p->path + 2; /* skip "C:" */
        {
            char *last = NULL;
            char *s = slash;
            while (*s) { if (*s == '\\') last = s; s++; }
            if (last && last > slash)
                *last = '\0';
            else {
                /* Already at e.g. C:\subdir — go to C:\ */
                p->path[2] = '\\';
                p->path[3] = '\0';
            }
        }
    } else {
        /* Descend: append \ + name */
        plen = strlen(p->path);
        if (p->path[plen-1] != '\\') { p->path[plen] = '\\'; p->path[plen+1] = '\0'; plen++; }
        strcat(p->path, f->name);
    }

    return (nos_panel_read_dir(p) == 0) ? 0 : -1;
}

int nos_panel_set_drive(nos_panel_t *p, char drive)
{
    union REGS r;
    unsigned int total;

    drive = (char)((drive >= 'a') ? drive - 32 : drive); /* uppercase */

    /* INT 21h / AH=0Eh: select drive, returns number of available drives */
    r.h.ah = 0x0E;
    r.h.dl = (unsigned char)(drive - 'A');
    intdos(&r, &r);
    total = (unsigned int)r.h.al;

    /* Verify drive is valid by getting its current directory */
    {
        struct SREGS sr;
        char         dirbuf[65];
        dirbuf[0] = '\0';
        r.h.ah = 0x47;
        r.h.dl = (unsigned char)(drive - 'A' + 1);
        segread(&sr);
        sr.ds  = FP_SEG(dirbuf);
        r.x.si = FP_OFF(dirbuf);
        intdosx(&r, &r, &sr);
        if (r.x.cflag) return -1; /* carry = invalid drive */

        p->drive = drive;
        if (dirbuf[0])
            sprintf(p->path, "%c:\\%s", drive, dirbuf);
        else
            sprintf(p->path, "%c:\\", drive);
    }

    (void)total;
    return nos_panel_read_dir(p);
}
