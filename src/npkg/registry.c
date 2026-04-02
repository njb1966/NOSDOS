/* NOS-DOS: NPKG
 * registry.c - Installed package database (INSTALLED.DB).
 *
 * INSTALLED.DB is a tab-separated flat file.  All public functions reload
 * the file on entry and (for writes) rewrite it atomically via a temp
 * file: write to INSTALLED.TMP, then rename over INSTALLED.DB.  This
 * prevents a partial DB on power loss during a write.
 *
 * Internal storage: static nos_reg_t g_reg holds up to NOS_REG_MAX_ENTRIES
 * records (~7.5 KB).  It is file-scope only; callers never see it directly.
 *
 * Date format: YYYY-MM-DD via Open Watcom _dos_getdate() from <dos.h>.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <stdio.h>    /* fopen, fgets, fclose, fprintf, rename, remove */
#include <string.h>   /* strcpy, strncpy, strlen, strchr, strcmp */
#include <stdlib.h>   /* atoi, memset */
#include <ctype.h>    /* tolower */
#include <direct.h>   /* mkdir */
#include <dos.h>      /* _dos_getdate, struct dosdate_t */
#include "registry.h"

/* -----------------------------------------------------------------------
 * Internal types
 * ----------------------------------------------------------------------- */

typedef struct {
    nos_reg_entry_t entries[NOS_REG_MAX_ENTRIES];
    int             count;
} nos_reg_t;

/* File-scope registry buffer.  Never put on the stack (~7.5 KB).          */
static nos_reg_t g_reg;

/* Temp file path used during atomic rewrite.                              */
#define DB_TMP  "C:\\NOS\\NPKG\\INSTALLED.TMP"

/* Number of tab-separated fields per data line.                            */
#define REG_FIELD_COUNT  5

/* Line read buffer.                                                        */
#define LINE_BUF  200

/* -----------------------------------------------------------------------
 * Shared string helpers
 * ----------------------------------------------------------------------- */

static int nos_stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void scopy(char *dst, const char *src, int max)
{
    strncpy(dst, src, (size_t)(max - 1));
    dst[max - 1] = '\0';
}

static void strip_crlf(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}

/* -----------------------------------------------------------------------
 * Internal: split a line into tab-separated fields (in-place).
 * Returns number of fields found.
 * ----------------------------------------------------------------------- */

static int split_tabs(char *line, char *fields[], int max)
{
    int   n = 0;
    char *p = line;

    fields[n++] = p;
    while (n < max) {
        p = strchr(p, '\t');
        if (!p) break;
        *p++ = '\0';
        fields[n++] = p;
    }
    return n;
}

/* -----------------------------------------------------------------------
 * Internal: get today's date as "YYYY-MM-DD"
 * ----------------------------------------------------------------------- */

static void get_today(char *buf)
{
    struct dosdate_t d;
    _dos_getdate(&d);
    sprintf(buf, "%04u-%02u-%02u",
            (unsigned)d.year, (unsigned)d.month, (unsigned)d.day);
}

/* -----------------------------------------------------------------------
 * Internal: load INSTALLED.DB into g_reg
 *
 * Returns NOS_REG_OK on success.
 * Returns NOS_REG_OK (with count=0) if the file does not exist yet.
 * ----------------------------------------------------------------------- */

static int reg_load(void)
{
    FILE  *f;
    char   buf[LINE_BUF];
    char  *fields[REG_FIELD_COUNT + 1];
    int    nf;
    nos_reg_entry_t *e;

    memset(&g_reg, 0, sizeof(g_reg));

    f = fopen(NOS_REG_DB_PATH, "r");
    if (!f)
        return NOS_REG_OK;   /* not yet created — empty registry */

    while (fgets(buf, (int)sizeof(buf), f)) {
        strip_crlf(buf);
        if (!buf[0] || buf[0] == '#' || buf[0] == ';')
            continue;
        if (g_reg.count >= NOS_REG_MAX_ENTRIES)
            break;

        nf = split_tabs(buf, fields, REG_FIELD_COUNT + 1);
        if (nf < REG_FIELD_COUNT)
            continue;

        e = &g_reg.entries[g_reg.count++];
        scopy(e->id,          fields[0], NOS_REG_ID_LEN);
        scopy(e->version,     fields[1], NOS_REG_VER_LEN);
        scopy(e->install_dir, fields[2], NOS_REG_DIR_LEN);
        scopy(e->category,    fields[3], NOS_REG_CAT_LEN);
        scopy(e->date,        fields[4], NOS_REG_DATE_LEN);
    }

    fclose(f);
    return NOS_REG_OK;
}

/* -----------------------------------------------------------------------
 * Internal: write g_reg to INSTALLED.TMP, then rename over INSTALLED.DB
 * ----------------------------------------------------------------------- */

static int reg_save(void)
{
    FILE *f;
    int   i;
    const nos_reg_entry_t *e;

    /* Ensure directory exists.                                             */
    mkdir(NOS_REG_DB_DIR);

    f = fopen(DB_TMP, "w");
    if (!f)
        return NOS_REG_ERR_WRITE;

    fprintf(f, "%s\r\n", NOS_REG_MAGIC);

    for (i = 0; i < g_reg.count; i++) {
        e = &g_reg.entries[i];
        fprintf(f, "%s\t%s\t%s\t%s\t%s\r\n",
                e->id, e->version, e->install_dir, e->category, e->date);
    }

    fclose(f);

    /* Atomic replace: remove old, rename temp over it.                     *
     * DOS rename fails if destination exists, so remove first.             */
    remove(NOS_REG_DB_PATH);
    if (rename(DB_TMP, NOS_REG_DB_PATH) != 0) {
        printf("NPKG: warning: could not rename %s to %s\r\n",
               DB_TMP, NOS_REG_DB_PATH);
        return NOS_REG_ERR_WRITE;
    }

    return NOS_REG_OK;
}

/* -----------------------------------------------------------------------
 * nos_registry_add
 * ----------------------------------------------------------------------- */

int nos_registry_add(const char *id, const char *version,
                     const char *install_dir, const char *category)
{
    nos_reg_entry_t *e;
    char today[NOS_REG_DATE_LEN];
    int  i;
    int  rc;

    rc = reg_load();
    if (rc != NOS_REG_OK)
        return rc;

    get_today(today);

    /* Update in place if ID already exists (upgrade / reinstall).          */
    for (i = 0; i < g_reg.count; i++) {
        if (nos_stricmp(g_reg.entries[i].id, id) == 0) {
            e = &g_reg.entries[i];
            scopy(e->version,     version,     NOS_REG_VER_LEN);
            scopy(e->install_dir, install_dir, NOS_REG_DIR_LEN);
            scopy(e->category,    category,    NOS_REG_CAT_LEN);
            scopy(e->date,        today,        NOS_REG_DATE_LEN);
            return reg_save();
        }
    }

    /* New entry.                                                           */
    if (g_reg.count >= NOS_REG_MAX_ENTRIES) {
        printf("NPKG: registry full (%d entries)\r\n", NOS_REG_MAX_ENTRIES);
        return NOS_REG_ERR_FULL;
    }

    e = &g_reg.entries[g_reg.count++];
    scopy(e->id,          id,          NOS_REG_ID_LEN);
    scopy(e->version,     version,     NOS_REG_VER_LEN);
    scopy(e->install_dir, install_dir, NOS_REG_DIR_LEN);
    scopy(e->category,    category,    NOS_REG_CAT_LEN);
    scopy(e->date,        today,        NOS_REG_DATE_LEN);

    return reg_save();
}

/* -----------------------------------------------------------------------
 * nos_registry_remove
 * ----------------------------------------------------------------------- */

int nos_registry_remove(const char *id)
{
    int i;
    int found = 0;
    int rc;

    rc = reg_load();
    if (rc != NOS_REG_OK)
        return rc;

    for (i = 0; i < g_reg.count; i++) {
        if (nos_stricmp(g_reg.entries[i].id, id) == 0) {
            found = 1;
            /* Shift remaining entries down to fill the gap.                */
            for (; i < g_reg.count - 1; i++)
                g_reg.entries[i] = g_reg.entries[i + 1];
            g_reg.count--;
            break;
        }
    }

    if (!found)
        return NOS_REG_ERR_NOTFOUND;

    return reg_save();
}

/* -----------------------------------------------------------------------
 * nos_registry_find
 * ----------------------------------------------------------------------- */

int nos_registry_find(const char *id, nos_reg_entry_t *out)
{
    int i;
    int rc;

    rc = reg_load();
    if (rc != NOS_REG_OK)
        return rc;

    for (i = 0; i < g_reg.count; i++) {
        if (nos_stricmp(g_reg.entries[i].id, id) == 0) {
            if (out)
                *out = g_reg.entries[i];
            return NOS_REG_OK;
        }
    }
    return NOS_REG_ERR_NOTFOUND;
}

/* -----------------------------------------------------------------------
 * nos_registry_is_installed
 * ----------------------------------------------------------------------- */

int nos_registry_is_installed(const char *id)
{
    return (nos_registry_find(id, NULL) == NOS_REG_OK) ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * nos_registry_list
 * ----------------------------------------------------------------------- */

int nos_registry_list(nos_reg_entry_t *buf, int max_entries)
{
    int i;
    int rc;
    int n;

    rc = reg_load();
    if (rc != NOS_REG_OK)
        return rc;

    n = g_reg.count < max_entries ? g_reg.count : max_entries;
    for (i = 0; i < n; i++)
        buf[i] = g_reg.entries[i];

    return n;
}
