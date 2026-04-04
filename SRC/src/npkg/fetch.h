/* NOS-DOS: NPKG
 * fetch.h - HTTP archive download engine interface.
 *
 * Wraps mTCP HTGET to download package archives from the repository.
 * Supports multiple fallback URLs (URL1/URL2/URL3 from [SOURCE] section),
 * post-download size verification, and a clean retry loop.
 *
 * DOS command-line constraint: COMMAND.COM /C argument is limited to 127
 * characters.  URLs longer than NOS_FETCH_MAX_URL will be rejected before
 * invoking HTGET to avoid silent truncation.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#ifndef NOS_FETCH_H
#define NOS_FETCH_H

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

/* Maximum number of fallback URLs per package source.                      */
#define NOS_FETCH_MAX_URLS      3

/* Maximum URL length.  HTGET command is "C:\NOS\SYSTEM\HTGET.EXE <url>    *
 * <dest>" — keeping URLs at or below this leaves room for the executable   *
 * path and destination within the 127-char COMMAND.COM /C limit.           */
#define NOS_FETCH_MAX_URL       100

/* Download staging directory.  Archives land here before extraction.       */
#define NOS_FETCH_DNLD_DIR      "C:\\NOS\\NPKG\\DNLD"

/* Maximum path length for a downloaded archive (dir + backslash + 8.3).   */
#define NOS_FETCH_MAX_PATH      64

/* Retry policy for transient failures (e.g. archive.org 503 throttling).  *
 * Per URL, we try NOS_FETCH_RETRIES times with a NOS_FETCH_RETRY_MS delay *
 * between attempts before moving to the next fallback URL.                 */
#define NOS_FETCH_RETRIES       3
#define NOS_FETCH_RETRY_MS      3000  /* 3 s between retries                */

/* archive.org returns a small HTML error page when throttling; a real DOS  *
 * archive is never this small.  Files below this threshold are treated as  *
 * failed downloads even when HTGET exits 0.                                */
#define NOS_FETCH_MIN_BYTES     1024L

/* Return codes.                                                            */
#define NOS_FETCH_OK            0
#define NOS_FETCH_ERR_BADURL   -1   /* URL too long or empty               */
#define NOS_FETCH_ERR_NODIR    -2   /* could not create download directory  */
#define NOS_FETCH_ERR_HTGET    -3   /* HTGET returned non-zero on all URLs  */
#define NOS_FETCH_ERR_SIZE     -4   /* downloaded size does not match Bytes */
#define NOS_FETCH_ERR_NOFILE   -5   /* destination file missing after HTGET */
#define NOS_FETCH_ERR_SMALL    -6   /* file too small — likely error page   */

/* -----------------------------------------------------------------------
 * Types
 * ----------------------------------------------------------------------- */

/*
 * nos_fetch_src_t
 *
 * Mirrors the [SOURCE] section of a .npkg definition file.
 * Populated by the .npkg parser in install.c before calling nos_fetch_archive.
 */
typedef struct {
    char urls[NOS_FETCH_MAX_URLS][NOS_FETCH_MAX_URL + 1]; /* URL1..URL3      */
    int  url_count;                  /* number of valid URLs (1-3)           */
    char archive[13];                /* 8.3 filename, e.g. "DOOM19S.ZIP"     */
    long expected_bytes;             /* 0 = unknown; >0 = verify after fetch */
} nos_fetch_src_t;

/* -----------------------------------------------------------------------
 * Interface
 * ----------------------------------------------------------------------- */

/*
 * nos_fetch_url
 *
 * Downloads `url` to `dest_path` using C:\NOS\SYSTEM\HTGET.EXE.
 * Prints the URL and HTGET's own progress output to stdout.
 *
 * Returns NOS_FETCH_OK on success, NOS_FETCH_ERR_* on failure.
 * On NOS_FETCH_ERR_BADURL the download is not attempted.
 */
int nos_fetch_url(const char *url, const char *dest_path);

/*
 * nos_fetch_archive
 *
 * High-level download function.  Tries each URL in `src->urls[]` in order
 * until one succeeds.  The archive is saved to NOS_FETCH_DNLD_DIR\<archive>.
 *
 * On success, `dest_out` receives the full path to the downloaded file
 * (caller must provide a buffer of at least NOS_FETCH_MAX_PATH bytes).
 *
 * If `src->expected_bytes > 0`, the downloaded file size is compared to
 * the expected value; a mismatch causes NOS_FETCH_ERR_SIZE.
 *
 * Returns NOS_FETCH_OK on success, NOS_FETCH_ERR_* on failure.
 */
int nos_fetch_archive(const nos_fetch_src_t *src,
                      char *dest_out, int dest_max);

/*
 * nos_fetch_file_size
 *
 * Returns the size of the file at `path` in bytes, or -1L on error
 * (file not found, or seek failed).
 */
long nos_fetch_file_size(const char *path);

/*
 * nos_fetch_verify_size
 *
 * Checks that the file at `path` is exactly `expected_bytes` long.
 * Returns NOS_FETCH_OK if the size matches or expected_bytes <= 0.
 * Returns NOS_FETCH_ERR_SIZE and prints a warning if there is a mismatch.
 */
int nos_fetch_verify_size(const char *path, long expected_bytes);

#endif /* NOS_FETCH_H */
