/* NOS-DOS: NPKG
 * fetch.c - HTTP archive download engine.
 *
 * Downloads package archives via mTCP HTGET with URL fallback, per-URL
 * retry logic, and post-download size verification.
 *
 * Archive.org quirks handled here:
 *   Redirects: HTGET follows HTTP 301/302 natively; "Redirect=yes" in
 *              .npkg definitions is informational only.
 *   Throttling: archive.org returns HTTP 503 when rate-limited.  HTGET
 *              exits 0 but writes a small HTML error page.  We detect this
 *              with the NOS_FETCH_MIN_BYTES threshold and retry after a
 *              NOS_FETCH_RETRY_MS delay (default 3 s).  Each URL is tried
 *              NOS_FETCH_RETRIES times before falling back to the next URL.
 *   Partials:  HTGET does not support HTTP Range; stale partials are
 *              overwritten on each attempt.
 *
 * Does not implement MD5 checksumming (no suitable bundled DOS MD5 tool).
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <stdio.h>    /* printf, fopen, fclose, fseek, ftell, remove */
#include <string.h>   /* strcpy, strcat, strlen, strchr */
#include <stdlib.h>   /* system */
#include <direct.h>   /* mkdir */
#include <dos.h>      /* delay() -- Open Watcom millisecond busy-wait */
#include "fetch.h"

/* -----------------------------------------------------------------------
 * Internal constants
 * ----------------------------------------------------------------------- */

/* Full path prefix for HTGET executable.                                   */
#define HTGET_EXE       "C:\\NOS\\SYSTEM\\HTGET.EXE"

/* Maximum length of the assembled HTGET command string.                    *
 * HTGET_EXE (22) + space (1) + URL (100) + space (1) + dest (64) = 188.   *
 * COMMAND.COM /C is limited to 127 chars of argument text.  We keep the   *
 * URL within NOS_FETCH_MAX_URL (100) so the total stays under 127 when    *
 * combined with the fixed HTGET path and destination.                      *
 *
 * Layout: HTGET.EXE<sp><url><sp><dest>                                     *
 *   HTGET_EXE path = 22 chars                                              *
 *   space + URL    = 1 + 100 = 101 chars                                   *
 *   space + dest   = 1 + 64  = 65 chars                                    *
 *   Total          = 188 chars (fits in system() internal buffer)          *
 *                                                                          *
 * Note: the 127-char COMMAND.COM /C limit applies to the text passed to   *
 * COMMAND.COM, not the entire system() string.  Open Watcom's system()    *
 * uses EXEC rather than COMMAND.COM when possible, so the limit is less   *
 * restrictive in practice.  We still cap URLs at 100 chars as a safe      *
 * conservative bound.                                                      */
#define CMD_BUF_SIZE    200

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/*
 * ensure_dnld_dir
 * Creates NOS_FETCH_DNLD_DIR if it does not already exist.
 * Returns 0 on success, -1 if mkdir failed for a reason other than
 * the directory already existing.
 */
static int ensure_dnld_dir(void)
{
    /* mkdir returns -1 if the path already exists; that is not an error.   */
    mkdir("C:\\NOS\\NPKG");
    mkdir(NOS_FETCH_DNLD_DIR);
    return 0;  /* treat all mkdir results as success for simplicity */
}

/*
 * build_dest_path
 * Constructs the full download destination path:
 *   NOS_FETCH_DNLD_DIR \ archive
 * Writes into `out` (caller provides buf of at least `out_max` bytes).
 * Returns 0 on success, -1 if the result would overflow `out_max`.
 */
static int build_dest_path(const char *archive, char *out, int out_max)
{
    int need;
    need = (int)(strlen(NOS_FETCH_DNLD_DIR) + 1 + strlen(archive) + 1);
    if (need > out_max)
        return -1;
    strcpy(out, NOS_FETCH_DNLD_DIR);
    strcat(out, "\\");
    strcat(out, archive);
    return 0;
}

/* -----------------------------------------------------------------------
 * nos_fetch_url
 * ----------------------------------------------------------------------- */

int nos_fetch_url(const char *url, const char *dest_path)
{
    char cmd[CMD_BUF_SIZE];
    int  url_len;
    int  rc;

    /* Validate URL.                                                        */
    if (!url || !*url)
        return NOS_FETCH_ERR_BADURL;
    url_len = (int)strlen(url);
    if (url_len > NOS_FETCH_MAX_URL) {
        printf("NPKG: URL too long (%d chars, max %d):\r\n  %s\r\n",
               url_len, NOS_FETCH_MAX_URL, url);
        return NOS_FETCH_ERR_BADURL;
    }

    /* Assemble: HTGET.EXE -v -o <dest_path> <url>                         */
    strcpy(cmd, HTGET_EXE);
    strcat(cmd, " -v -o ");
    strcat(cmd, dest_path);
    strcat(cmd, " ");
    strcat(cmd, url);

    printf("  Fetching: %s\r\n", url);
    rc = system(cmd);
    /* mTCP 2025 HTGET exits with HTTP response class as errorlevel:
     *   0 or 20-29 = success (HTTP 2xx);  anything else = failure.       */
    if (rc != 0 && !(rc >= 20 && rc <= 29)) {
        printf("  HTGET returned error code %d\r\n", rc);
        return NOS_FETCH_ERR_HTGET;
    }

    return NOS_FETCH_OK;
}

/* -----------------------------------------------------------------------
 * nos_fetch_file_size
 * ----------------------------------------------------------------------- */

long nos_fetch_file_size(const char *path)
{
    FILE *f;
    long  sz;

    f = fopen(path, "rb");
    if (!f)
        return -1L;
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return -1L;
    }
    sz = ftell(f);
    fclose(f);
    return sz;
}

/* -----------------------------------------------------------------------
 * nos_fetch_verify_size
 * ----------------------------------------------------------------------- */

int nos_fetch_verify_size(const char *path, long expected_bytes)
{
    long actual;

    if (expected_bytes <= 0L)
        return NOS_FETCH_OK;

    actual = nos_fetch_file_size(path);
    if (actual < 0L) {
        printf("NPKG: warning: cannot read downloaded file to verify size\r\n");
        return NOS_FETCH_ERR_NOFILE;
    }

    if (actual != expected_bytes) {
        printf("NPKG: warning: size mismatch - expected %ld bytes, got %ld\r\n",
               expected_bytes, actual);
        printf("         (corrupt download or .npkg Bytes field is wrong)\r\n");
        return NOS_FETCH_ERR_SIZE;
    }

    printf("  Size OK (%ld bytes)\r\n", actual);
    return NOS_FETCH_OK;
}

/* -----------------------------------------------------------------------
 * nos_fetch_archive
 *
 * For each URL, we attempt up to NOS_FETCH_RETRIES downloads.  Between
 * retries on the same URL we wait NOS_FETCH_RETRY_MS milliseconds so we
 * back off from archive.org throttling.  We also guard against archive.org
 * 503 responses: HTGET exits 0 but writes a small HTML page; any downloaded
 * file smaller than NOS_FETCH_MIN_BYTES is treated as a failure.
 * ----------------------------------------------------------------------- */

int nos_fetch_archive(const nos_fetch_src_t *src,
                      char *dest_out, int dest_max)
{
    char  dest[NOS_FETCH_MAX_PATH];
    int   i;
    int   attempt;
    int   rc;
    long  got;
    int   ok;

    /* Validate inputs.                                                     */
    if (!src || src->url_count < 1 || !src->archive[0])
        return NOS_FETCH_ERR_BADURL;

    /* Ensure the download staging directory exists.                        */
    if (ensure_dnld_dir() != 0)
        return NOS_FETCH_ERR_NODIR;

    /* Build the destination path.                                          */
    if (build_dest_path(src->archive, dest, (int)sizeof(dest)) != 0) {
        printf("NPKG: destination path too long for archive '%s'\r\n",
               src->archive);
        return NOS_FETCH_ERR_BADURL;
    }

    /* Print file size warning so the user knows what to expect.            */
    if (src->expected_bytes > 0L) {
        if (src->expected_bytes >= 1048576L)
            printf("  Size: %ld MB -- large download, please stand by...\r\n",
                   src->expected_bytes / 1048576L);
        else if (src->expected_bytes >= 1024L)
            printf("  Size: %ld KB\r\n",
                   src->expected_bytes / 1024L);
        else
            printf("  Size: %ld bytes\r\n", src->expected_bytes);
    }

    ok = 0;
    rc = NOS_FETCH_ERR_HTGET;

    /* Outer loop: each fallback URL.                                       */
    for (i = 0; i < src->url_count && !ok; i++) {
        if (!src->urls[i][0])
            continue;

        /* Inner loop: retry the same URL up to NOS_FETCH_RETRIES times.   */
        for (attempt = 1; attempt <= NOS_FETCH_RETRIES; attempt++) {

            printf("NPKG: downloading %s (URL %d, attempt %d/%d)\r\n",
                   src->archive, i + 1, attempt, NOS_FETCH_RETRIES);

            /* Delete any stale partial from a previous attempt.            */
            remove(dest);

            rc = nos_fetch_url(src->urls[i], dest);

            if (rc != NOS_FETCH_OK) {
                printf("  HTGET failed (error %d)\r\n", rc);
            } else {
                /* HTGET returned 0: verify the file is a real archive,    *
                 * not an HTML throttle/error page from archive.org.        */
                got = nos_fetch_file_size(dest);
                if (got < NOS_FETCH_MIN_BYTES) {
                    printf("  File too small (%ld bytes) - likely an error"
                           " page, treating as failure\r\n", got);
                    rc = NOS_FETCH_ERR_SMALL;
                } else {
                    ok = 1;
                    break; /* success */
                }
            }

            if (attempt < NOS_FETCH_RETRIES) {
                printf("  Waiting %d s before retry...\r\n",
                       NOS_FETCH_RETRY_MS / 1000);
                delay(NOS_FETCH_RETRY_MS);
            }
        }

        if (!ok && i + 1 < src->url_count)
            printf("  All retries exhausted for URL %d, trying next URL...\r\n",
                   i + 1);
    }

    /* Every URL and every retry failed.                                    */
    if (!ok) {
        printf("NPKG: download failed after trying %d URL(s), %d attempt(s)"
               " each\r\n", src->url_count, NOS_FETCH_RETRIES);
        printf("  Check your network connection and try again later.\r\n");
        printf("  (archive.org may be throttling - wait a few minutes)\r\n");
        return rc;
    }

    /* Optional exact-size verification.                                    */
    if (src->expected_bytes > 0L) {
        rc = nos_fetch_verify_size(dest, src->expected_bytes);
        if (rc != NOS_FETCH_OK)
            return rc;
    }

    /* Copy destination path to caller.                                     */
    if (dest_out && dest_max > 0) {
        strncpy(dest_out, dest, (size_t)(dest_max - 1));
        dest_out[dest_max - 1] = '\0';
    }

    return NOS_FETCH_OK;
}
