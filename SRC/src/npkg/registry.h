/* NOS-DOS: NPKG
 * registry.h - Installed package database interface.
 *
 * INSTALLED.DB is a tab-separated flat file at NOS_REG_DB_PATH.
 * One record per installed package; written by nos_registry_add() and
 * updated by nos_registry_remove().
 *
 * Format (packages/README.md § registry):
 *   #NPKG-REGISTRY-1.0\r\n
 *   ID\tVersion\tInstallDir\tCategory\tDate\r\n
 *   ...
 *
 * All public functions load the file fresh on each call and rewrite it
 * after any modification.  The registry is small (tens of entries) so
 * the overhead is negligible on DOS.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#ifndef NOS_REGISTRY_H
#define NOS_REGISTRY_H

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

#define NOS_REG_DB_PATH         "C:\\NOS\\NPKG\\INSTALLED.DB"
#define NOS_REG_DB_DIR          "C:\\NOS\\NPKG"
#define NOS_REG_MAGIC           "#NPKG-REGISTRY-1.0"

/* Maximum installed packages tracked simultaneously.                       */
#define NOS_REG_MAX_ENTRIES     64

/* Field widths (chars + NUL).                                              */
#define NOS_REG_ID_LEN          9
#define NOS_REG_VER_LEN         13
#define NOS_REG_DIR_LEN         65
#define NOS_REG_CAT_LEN         17
#define NOS_REG_DATE_LEN        11   /* YYYY-MM-DD */

/* Return codes.                                                            */
#define NOS_REG_OK              0
#define NOS_REG_ERR_OPEN       -1    /* cannot open INSTALLED.DB            */
#define NOS_REG_ERR_WRITE      -2    /* cannot write INSTALLED.DB           */
#define NOS_REG_ERR_FULL       -3    /* registry at NOS_REG_MAX_ENTRIES     */
#define NOS_REG_ERR_NOTFOUND   -4    /* package ID not in registry          */

/* -----------------------------------------------------------------------
 * Types
 * ----------------------------------------------------------------------- */

/*
 * One installed-package record.
 */
typedef struct {
    char id[NOS_REG_ID_LEN];
    char version[NOS_REG_VER_LEN];
    char install_dir[NOS_REG_DIR_LEN];
    char category[NOS_REG_CAT_LEN];
    char date[NOS_REG_DATE_LEN];     /* install date: YYYY-MM-DD */
} nos_reg_entry_t;

/* -----------------------------------------------------------------------
 * Interface
 * ----------------------------------------------------------------------- */

/*
 * nos_registry_add
 *
 * Adds or replaces the registry entry for pkg->id.  If an entry with the
 * same ID already exists it is overwritten (upgrade detection).  The
 * install date is set to today's date from DOS INT 21h.
 *
 * Returns NOS_REG_OK on success, NOS_REG_ERR_* on failure.
 */
int nos_registry_add(const char *id, const char *version,
                     const char *install_dir, const char *category);

/*
 * nos_registry_remove
 *
 * Removes the entry for `id` from INSTALLED.DB.  Does NOT run [REMOVE]
 * batch lines or modify AUTOEXEC.BAT — those are the caller's (npkg.c)
 * responsibility before calling this function.
 *
 * Returns NOS_REG_OK on success, NOS_REG_ERR_NOTFOUND if not installed.
 */
int nos_registry_remove(const char *id);

/*
 * nos_registry_find
 *
 * Finds the entry for `id` (case-insensitive) and copies it into `out`.
 * Returns NOS_REG_OK if found, NOS_REG_ERR_NOTFOUND if not installed.
 */
int nos_registry_find(const char *id, nos_reg_entry_t *out);

/*
 * nos_registry_is_installed
 *
 * Returns 1 if `id` is in the registry, 0 if not.
 */
int nos_registry_is_installed(const char *id);

/*
 * nos_registry_list
 *
 * Copies up to `max_entries` records from INSTALLED.DB into `buf`.
 * Returns the number of entries copied, or NOS_REG_ERR_* on failure.
 * If INSTALLED.DB does not exist, returns 0 (empty registry is not an
 * error — it just means nothing has been installed yet).
 */
int nos_registry_list(nos_reg_entry_t *buf, int max_entries);

#endif /* NOS_REGISTRY_H */
