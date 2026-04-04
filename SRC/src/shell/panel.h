/* NOS-DOS: NOS-SHELL
 * panel.h - Dual-pane file panel interface.
 *
 * Each panel is an independent nos_panel_t.  The shell owns two of them
 * (left and right) and draws them side-by-side.
 *
 * File list is populated by nos_panel_read_dir().  Sorting, scrolling,
 * and selection are all managed inside the panel.
 *
 * License: GPL-2.0
 */

#ifndef NOS_PANEL_H
#define NOS_PANEL_H

/* -----------------------------------------------------------------------
 * Limits
 * ----------------------------------------------------------------------- */

#define NOS_PANEL_MAX_FILES  512   /* max entries per directory */
#define NOS_PANEL_NAME_LEN    13   /* 8.3 name + NUL */

/* -----------------------------------------------------------------------
 * Sort order
 * ----------------------------------------------------------------------- */

#define NOS_SORT_NAME   0
#define NOS_SORT_EXT    1
#define NOS_SORT_SIZE   2
#define NOS_SORT_DATE   3
#define NOS_SORT_UNSORTED 4

/* -----------------------------------------------------------------------
 * File entry
 * ----------------------------------------------------------------------- */

typedef struct {
    char          name[NOS_PANEL_NAME_LEN]; /* 8.3 name, NUL-terminated */
    unsigned long size;                     /* 0 for directories */
    unsigned int  date;                     /* DOS packed date */
    unsigned int  time;                     /* DOS packed time */
    unsigned char attrib;                   /* DOS attribute byte */
    int           is_dir;                   /* non-zero for directories */
} nos_fileentry_t;

/* -----------------------------------------------------------------------
 * Panel state
 * ----------------------------------------------------------------------- */

typedef struct {
    /* Screen position — set by the shell before calling nos_panel_draw */
    int col;               /* left column (0-based) */
    int row;               /* top row (0-based, includes border) */
    int width;             /* total width including borders */
    int height;            /* total height including borders */

    /* Current location */
    char path[128];        /* current directory, e.g. "C:\NOS" */
    char drive;            /* current drive letter, e.g. 'C' */

    /* File list */
    nos_fileentry_t *files;         /* heap-allocated array */
    int              file_count;    /* entries in files[] */
    int              alloc_count;   /* allocated slots */

    /* Selection state */
    int cursor;            /* index of highlighted entry */
    int scroll;            /* index of topmost visible entry */
    int sort;              /* NOS_SORT_* */
    int active;            /* non-zero when this panel has keyboard focus */
} nos_panel_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/*
 * nos_panel_init — initialise a panel at (col,row) with given dimensions.
 * Sets initial drive/path to the current DOS directory.
 * Returns 0 on success, -1 on memory allocation failure.
 */
int nos_panel_init(nos_panel_t *p, int col, int row, int width, int height);

/*
 * nos_panel_free — release all heap memory owned by the panel.
 */
void nos_panel_free(nos_panel_t *p);

/*
 * nos_panel_read_dir — read p->path into p->files[], sort, reset scroll.
 * Returns 0 on success, -1 on error (path not found, etc.).
 */
int nos_panel_read_dir(nos_panel_t *p);

/*
 * nos_panel_draw — paint the panel onto the screen.
 */
void nos_panel_draw(nos_panel_t *p);

/*
 * nos_panel_move_cursor — move the highlight bar by delta (-1 or +1, etc.)
 * adjusting scroll as needed.  Does NOT redraw.
 */
void nos_panel_move_cursor(nos_panel_t *p, int delta);

/*
 * nos_panel_page — move by a full page (-1=up, +1=down).
 */
void nos_panel_page(nos_panel_t *p, int dir);

/*
 * nos_panel_enter — descend into a directory or return the path of a file.
 * If p->files[p->cursor] is a directory: changes p->path, rereads.
 * Otherwise: copies full path into out_path (caller-allocated, >= 128 bytes)
 * and returns 1.  Returns 0 on directory change, -1 on error.
 */
int nos_panel_enter(nos_panel_t *p, char *out_path);

/*
 * nos_panel_set_drive — change the panel's drive letter and re-read root.
 * drive: 'A'..'Z'  Returns 0 on success, -1 if drive not available.
 */
int nos_panel_set_drive(nos_panel_t *p, char drive);

/*
 * nos_panel_cursor_path — fill buf with the full path of the highlighted
 * entry.  buf must be at least 128 bytes.
 */
void nos_panel_cursor_path(const nos_panel_t *p, char *buf);

#endif /* NOS_PANEL_H */
