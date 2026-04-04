/* NOS-DOS: NPKG
 * index.h - Package index types and interface.
 *
 * The index is a tab-separated flat file (packages.idx) downloaded from the
 * NOS-DOS package repository.  It is the first file fetched during
 * "NPKG UPDATE" and is cached at NOS_INDEX_CACHE_PATH on the HDD.
 *
 * IMPORTANT: nos_index_t is ~20 KB in size.  Never declare it as a local
 * variable (stack overflow).  Declare globally in the .c file that owns it.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#ifndef NOS_INDEX_H
#define NOS_INDEX_H

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

/* Maximum number of package entries held in memory.                        */
#define NOS_INDEX_MAX_ENTRIES   128

/* On-disk cache location.  Must fit in 8.3 plus full path.                */
#define NOS_INDEX_CACHE_PATH    "C:\\NOS\\NPKG\\CACHE\\PKGS.IDX"
#define NOS_INDEX_CACHE_DIR     "C:\\NOS\\NPKG\\CACHE"

/* Default repository base URL (compiled in).  Override at runtime by        *
 * writing the desired base URL to C:\NOS\NPKG\REPO.URL (one line, no        *
 * trailing slash, plain HTTP — mTCP HTGET does not support HTTPS).           */
#define NOS_INDEX_REPO_URL      "http://nosdos.njb1966.com/packages"

/* Runtime URL path for REPO.URL override file.                              */
#define NOS_REPO_URL_FILE       "C:\\NOS\\NPKG\\REPO.URL"

/* Header magic string expected on the first line of packages.idx.          */
#define NOS_INDEX_MAGIC         "#NPKG-INDEX-1.0"

/* Field sizes match the spec in packages/README.md.                        */
#define NOS_PKG_ID_LEN          9    /* 8 chars + NUL */
#define NOS_PKG_CATEGORY_LEN    17   /* 16 chars + NUL */
#define NOS_PKG_NAME_LEN        41   /* 40 chars + NUL */
#define NOS_PKG_VERSION_LEN     13   /* 12 chars + NUL */
#define NOS_PKG_DESC_LEN        61   /* 60 chars + NUL */
#define NOS_PKG_LICENSE_LEN     13   /* 12 chars + NUL */

/* Return codes for nos_index_* functions.                                  */
#define NOS_INDEX_OK            0
#define NOS_INDEX_ERR_OPEN     -1    /* cannot open file */
#define NOS_INDEX_ERR_MAGIC    -2    /* missing or wrong header magic */
#define NOS_INDEX_ERR_FULL     -3    /* more entries than NOS_INDEX_MAX_ENTRIES */
#define NOS_INDEX_ERR_DOWNLOAD -4    /* HTGET invocation failed */
#define NOS_INDEX_ERR_NODIR    -5    /* cache directory could not be created */

/* -----------------------------------------------------------------------
 * Types
 * ----------------------------------------------------------------------- */

/*
 * One record from packages.idx.
 * Fields correspond 1-to-1 with the tab-separated columns described in
 * packages/README.md § packages.idx.
 */
typedef struct {
    char         id[NOS_PKG_ID_LEN];
    char         category[NOS_PKG_CATEGORY_LEN];
    char         name[NOS_PKG_NAME_LEN];
    char         version[NOS_PKG_VERSION_LEN];
    char         description[NOS_PKG_DESC_LEN];
    unsigned int size_kb;            /* download size, KiB */
    char         license[NOS_PKG_LICENSE_LEN];
} nos_pkginfo_t;

/*
 * The in-memory index.  Declare one instance globally in npkg.c; pass a
 * pointer to all nos_index_* functions.
 */
typedef struct {
    nos_pkginfo_t entries[NOS_INDEX_MAX_ENTRIES];
    int           count;             /* number of valid entries */
} nos_index_t;

/* -----------------------------------------------------------------------
 * Interface
 * ----------------------------------------------------------------------- */

/*
 * nos_repo_url
 *
 * Returns the repository base URL to use for NPKG UPDATE and definition
 * fetches.  On first call, reads C:\NOS\NPKG\REPO.URL; if the file exists
 * and contains a non-empty line it is used as the URL.  Otherwise falls
 * back to NOS_INDEX_REPO_URL.  Result is cached after the first call.
 *
 * Always returns a non-NULL pointer to a static buffer.
 */
const char *nos_repo_url(void);

/*
 * nos_index_download
 *
 * Fetches the package index from `url` and saves it to `dest_path` using
 * mTCP HTGET.EXE (expected in C:\NOS\SYSTEM\).  Creates the destination
 * directory if it does not exist.
 *
 * Returns NOS_INDEX_OK on success, NOS_INDEX_ERR_* on failure.
 */
int nos_index_download(const char *url, const char *dest_path);

/*
 * nos_index_load
 *
 * Parses the packages.idx flat file at `path` into `idx`.  Existing
 * contents of `idx` are replaced.  Lines that fail field-count validation
 * are skipped with a warning printed to stdout.
 *
 * Returns NOS_INDEX_OK on success, NOS_INDEX_ERR_* on failure.
 * On NOS_INDEX_ERR_FULL, entries up to NOS_INDEX_MAX_ENTRIES are loaded
 * and a warning is printed; the function still returns NOS_INDEX_ERR_FULL
 * so the caller can surface the truncation to the user.
 */
int nos_index_load(nos_index_t *idx, const char *path);

/*
 * nos_index_find
 *
 * Searches `idx` for a package whose ID matches `id` (case-insensitive).
 * Returns a pointer into idx->entries[], or NULL if not found.
 */
const nos_pkginfo_t *nos_index_find(const nos_index_t *idx, const char *id);

/*
 * nos_index_search
 *
 * Performs a case-insensitive substring search of `term` across the ID,
 * Name, and Description fields of every entry in `idx`.  Matching entries
 * are stored as pointers into idx->entries[] in `results[]`, up to
 * `max_results` entries.
 *
 * Returns the number of matches found (may exceed max_results if the index
 * has more hits than the results array can hold — the caller sees only the
 * first max_results matches).
 */
int nos_index_search(const nos_index_t *idx, const char *term,
                     const nos_pkginfo_t *results[], int max_results);

/*
 * nos_index_search_cat
 *
 * Like nos_index_search but restricts results to entries whose Category
 * matches `category` (case-insensitive, exact match).  Pass NULL for
 * `category` to search all categories (equivalent to nos_index_search).
 */
int nos_index_search_cat(const nos_index_t *idx, const char *term,
                         const char *category,
                         const nos_pkginfo_t *results[], int max_results);

#endif /* NOS_INDEX_H */
