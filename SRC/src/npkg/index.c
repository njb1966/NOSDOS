/* NOS-DOS: NPKG
 * index.c - Package index parser and downloader.
 *
 * Implements loading and searching of the packages.idx master index.
 * The index is a tab-delimited flat file downloaded from the NOS-DOS
 * package repository via mTCP HTGET and cached on the local HDD.
 *
 * See packages/README.md for the full index file format specification.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <stdio.h>    /* fopen, fgets, fclose, printf */
#include <string.h>   /* strcpy, strncpy, strlen, strchr */
#include <stdlib.h>   /* atoi, system */
#include <ctype.h>    /* tolower, isspace */
#include <direct.h>   /* mkdir */
#include "index.h"

/* -----------------------------------------------------------------------
 * Internal constants
 * ----------------------------------------------------------------------- */

/* Maximum raw line length to read from packages.idx (spec: 255 chars).    */
#define LINE_BUF_SIZE  260

/* Number of tab-separated fields per data line.                            */
#define FIELD_COUNT    7

/* mTCP tools live here (same constant used in nnet.c).                    */
#define SYS_PATH  "C:\\NOS\\SYSTEM\\"

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/*
 * nos_tolower_c
 * Safe single-char lowercase (avoids signed-char UB with tolower).
 */
static int nos_tolower_c(int c)
{
    return tolower((unsigned char)c);
}

/*
 * nos_stricmp
 * Case-insensitive string comparison.  Returns 0 if equal.
 */
static int nos_stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = nos_tolower_c(*a);
        int cb = nos_tolower_c(*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return nos_tolower_c(*a) - nos_tolower_c(*b);
}

/*
 * nos_stristr
 * Case-insensitive substring search.
 * Returns pointer to first occurrence of `needle` in `hay`, or NULL.
 */
static const char *nos_stristr(const char *hay, const char *needle)
{
    size_t nlen;
    const char *p;

    if (!needle || !*needle)
        return hay;

    nlen = strlen(needle);
    for (p = hay; *p; p++) {
        size_t i;
        int match = 1;
        for (i = 0; i < nlen; i++) {
            if (!p[i] ||
                nos_tolower_c(p[i]) != nos_tolower_c(needle[i])) {
                match = 0;
                break;
            }
        }
        if (match)
            return p;
    }
    return NULL;
}

/*
 * strip_crlf
 * Removes trailing CR and LF characters from a string in place.
 */
static void strip_crlf(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

/*
 * safe_copy
 * Copies at most (max_len - 1) chars from src into dst and null-terminates.
 * Silently truncates if src is longer than the field allows.
 */
static void safe_copy(char *dst, const char *src, int max_len)
{
    strncpy(dst, src, (size_t)(max_len - 1));
    dst[max_len - 1] = '\0';
}

/*
 * split_tabs
 * Splits `line` in place by tab characters and records pointers to each
 * field in `fields[]`.  Returns the number of fields found.
 *
 * Each tab is replaced with '\0', so fields[] point directly into `line`.
 * An empty field (two consecutive tabs) is represented as an empty string.
 */
static int split_tabs(char *line, char *fields[], int max_fields)
{
    int   n = 0;
    char *p = line;

    fields[n++] = p;
    while (n < max_fields) {
        p = strchr(p, '\t');
        if (!p)
            break;
        *p++ = '\0';
        fields[n++] = p;
    }
    return n;
}

/*
 * parse_entry
 * Fills `out` from a 7-element fields[] array produced by split_tabs.
 * Fields order: ID, Category, Name, Version, Description, SizeKB, License.
 */
static void parse_entry(nos_pkginfo_t *out, char *fields[])
{
    safe_copy(out->id,          fields[0], NOS_PKG_ID_LEN);
    safe_copy(out->category,    fields[1], NOS_PKG_CATEGORY_LEN);
    safe_copy(out->name,        fields[2], NOS_PKG_NAME_LEN);
    safe_copy(out->version,     fields[3], NOS_PKG_VERSION_LEN);
    safe_copy(out->description, fields[4], NOS_PKG_DESC_LEN);
    out->size_kb = (unsigned int)atoi(fields[5]);
    safe_copy(out->license,     fields[6], NOS_PKG_LICENSE_LEN);
}

/* -----------------------------------------------------------------------
 * nos_repo_url
 * ----------------------------------------------------------------------- */

const char *nos_repo_url(void)
{
    static char s_url[160];
    static int  s_loaded = 0;
    FILE       *f;
    char        line[160];
    int         len;

    if (s_loaded)
        return s_url;

    f = fopen(NOS_REPO_URL_FILE, "r");
    if (f) {
        if (fgets(line, (int)sizeof(line), f)) {
            /* Strip trailing CR/LF/spaces. */
            len = (int)strlen(line);
            while (len > 0 && (line[len - 1] == '\r' ||
                               line[len - 1] == '\n' ||
                               line[len - 1] == ' '))
                line[--len] = '\0';
            if (len > 0 && len < (int)sizeof(s_url)) {
                strcpy(s_url, line);
                fclose(f);
                s_loaded = 1;
                return s_url;
            }
        }
        fclose(f);
    }

    strcpy(s_url, NOS_INDEX_REPO_URL);
    s_loaded = 1;
    return s_url;
}

/* -----------------------------------------------------------------------
 * nos_index_download
 * ----------------------------------------------------------------------- */

int nos_index_download(const char *url, const char *dest_path)
{
    char cmd[320];
    int  rc;

    /* Create cache directory (mkdir returns -1 if it already exists;       *
     * that is fine — ignore the return value).                             */
    mkdir(NOS_INDEX_CACHE_DIR);

    /* Build: C:\NOS\SYSTEM\HTGET.EXE -o <dest_path> <url>                 *
     * mTCP 2025 HTGET syntax: HTGET [options] <URL>  (-o writes to file)  */
    strcpy(cmd, SYS_PATH);
    strcat(cmd, "HTGET.EXE -o ");
    strcat(cmd, dest_path);
    strcat(cmd, " ");
    strcat(cmd, url);

    rc = system(cmd);
    if (rc != 0)
        return NOS_INDEX_ERR_DOWNLOAD;

    return NOS_INDEX_OK;
}

/* -----------------------------------------------------------------------
 * nos_index_load
 * ----------------------------------------------------------------------- */

int nos_index_load(nos_index_t *idx, const char *path)
{
    FILE  *f;
    char   line[LINE_BUF_SIZE];
    char  *fields[FIELD_COUNT + 1];  /* +1 to detect extra fields */
    int    lineno = 0;
    int    nfields;
    int    truncated = 0;

    idx->count = 0;

    f = fopen(path, "r");
    if (!f)
        return NOS_INDEX_ERR_OPEN;

    while (fgets(line, (int)sizeof(line), f)) {
        lineno++;
        strip_crlf(line);

        /* Skip blank lines and comments.                                   */
        if (!line[0] || line[0] == '#' || line[0] == ';')
            continue;

        /* Split by tab.                                                    */
        nfields = split_tabs(line, fields, FIELD_COUNT + 1);
        if (nfields < FIELD_COUNT) {
            printf("NPKG: index line %d: expected %d fields, got %d — skipped\r\n",
                   lineno, FIELD_COUNT, nfields);
            continue;
        }

        /* Check capacity.                                                  */
        if (idx->count >= NOS_INDEX_MAX_ENTRIES) {
            if (!truncated) {
                printf("NPKG: index truncated at %d entries (increase NOS_INDEX_MAX_ENTRIES)\r\n",
                       NOS_INDEX_MAX_ENTRIES);
                truncated = 1;
            }
            continue;
        }

        parse_entry(&idx->entries[idx->count], fields);
        idx->count++;
    }

    fclose(f);
    return truncated ? NOS_INDEX_ERR_FULL : NOS_INDEX_OK;
}

/* -----------------------------------------------------------------------
 * nos_index_find
 * ----------------------------------------------------------------------- */

const nos_pkginfo_t *nos_index_find(const nos_index_t *idx, const char *id)
{
    int i;
    for (i = 0; i < idx->count; i++) {
        if (nos_stricmp(idx->entries[i].id, id) == 0)
            return &idx->entries[i];
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * nos_index_search_cat  (core implementation)
 * ----------------------------------------------------------------------- */

int nos_index_search_cat(const nos_index_t *idx, const char *term,
                         const char *category,
                         const nos_pkginfo_t *results[], int max_results)
{
    int i;
    int found = 0;

    for (i = 0; i < idx->count; i++) {
        const nos_pkginfo_t *e = &idx->entries[i];

        /* Category filter: skip if category given and doesn't match.       */
        if (category && *category) {
            if (nos_stricmp(e->category, category) != 0)
                continue;
        }

        /* Term match: empty term matches everything.                       */
        if (term && *term) {
            if (!nos_stristr(e->id,          term) &&
                !nos_stristr(e->name,        term) &&
                !nos_stristr(e->description, term)) {
                continue;
            }
        }

        /* Record match.                                                    */
        if (found < max_results)
            results[found] = e;
        found++;
    }

    return found;
}

/* -----------------------------------------------------------------------
 * nos_index_search  (all-category wrapper)
 * ----------------------------------------------------------------------- */

int nos_index_search(const nos_index_t *idx, const char *term,
                     const nos_pkginfo_t *results[], int max_results)
{
    return nos_index_search_cat(idx, term, NULL, results, max_results);
}
