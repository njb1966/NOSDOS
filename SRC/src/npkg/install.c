/* NOS-DOS: NPKG
 * install.c - Package installer: .npkg parser + installation workflow.
 *
 * Workflow driven by nos_install_run():
 *   1. nos_fetch_archive()   -- download from URL1/URL2/URL3
 *   2. nos_mkdirs()          -- create InstallDir and parents
 *   3. nos_do_extract()      -- run UNZIP/UNARJ/LHA/PKUNZIP/COPY
 *   4. nos_run_post()        -- execute [POST-INSTALL] batch lines
 *   5. nos_update_autoexec() -- append PATH and SET lines if requested
 *
 * AUTOEXEC.BAT is modified by append only.  A comment block marks each
 * NPKG-managed addition so they can be identified during removal.
 *
 * StripDir (remove top-level archive directory) is not implemented in
 * this release.  UNZIP's -j flag achieves the same effect manually when
 * needed.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <stdio.h>    /* fopen, fgets, fclose, fprintf, printf, remove */
#include <string.h>   /* strcpy, strcat, strlen, strcmp, strncpy, strchr */
#include <stdlib.h>   /* system, atoi, atol, memset */
#include <ctype.h>    /* tolower, isspace */
#include <direct.h>   /* mkdir */
#include "install.h"
#include "fetch.h"
#include "index.h"    /* NOS_INDEX_REPO_URL */

/* -----------------------------------------------------------------------
 * Internal constants
 * ----------------------------------------------------------------------- */

#define LINE_BUF        256   /* read buffer for .npkg lines               */
#define SYS_PATH        "C:\\NOS\\SYSTEM\\"

/* Section identifiers used by the parser.                                  */
#define SECT_NONE           0
#define SECT_PACKAGE        1
#define SECT_HARDWARE       2
#define SECT_SOURCE         3
#define SECT_INSTALL        4
#define SECT_POST_INSTALL   5
#define SECT_LAUNCHER       6
#define SECT_GAME           7
#define SECT_REMOVE         8

/* -----------------------------------------------------------------------
 * Shared string helpers (static — not exported)
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

/* Trim leading whitespace; return pointer into s.                          */
static char *ltrim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

/* Trim trailing whitespace in place.                                       */
static void rtrim(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

static void strip_crlf(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}

/* Safe strncpy that always null-terminates.                               */
static void scopy(char *dst, const char *src, int max)
{
    strncpy(dst, src, (size_t)(max - 1));
    dst[max - 1] = '\0';
}

/* Parse boolean value: yes/true/1 → 1, no/false/0 → 0, else 0.           */
static char parse_bool(const char *v)
{
    return (nos_stricmp(v, "yes")  == 0 ||
            nos_stricmp(v, "true") == 0 ||
            strcmp(v, "1") == 0) ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * .npkg parser helpers
 * ----------------------------------------------------------------------- */

static int parse_section(const char *line)
{
    /* line looks like "[SECTION-NAME]" (leading/trailing whitespace already
     * stripped).  Compare the inner name.                                  */
    char name[32];
    int  i = 0;
    const char *p = line + 1;  /* skip '[' */

    while (*p && *p != ']' && i < 31)
        name[i++] = *p++;
    name[i] = '\0';

    if (nos_stricmp(name, "PACKAGE")      == 0) return SECT_PACKAGE;
    if (nos_stricmp(name, "HARDWARE")     == 0) return SECT_HARDWARE;
    if (nos_stricmp(name, "SOURCE")       == 0) return SECT_SOURCE;
    if (nos_stricmp(name, "INSTALL")      == 0) return SECT_INSTALL;
    if (nos_stricmp(name, "POST-INSTALL") == 0) return SECT_POST_INSTALL;
    if (nos_stricmp(name, "LAUNCHER")     == 0) return SECT_LAUNCHER;
    if (nos_stricmp(name, "GAME")         == 0) return SECT_GAME;
    if (nos_stricmp(name, "REMOVE")       == 0) return SECT_REMOVE;
    return SECT_NONE;
}

static void apply_package(nos_pkg_t *pkg, const char *key, const char *val)
{
    if (nos_stricmp(key, "ID")          == 0) scopy(pkg->id,          val, 9);
    else if (nos_stricmp(key, "Name")   == 0) scopy(pkg->name,        val, 41);
    else if (nos_stricmp(key, "Version")== 0) scopy(pkg->version,     val, 13);
    else if (nos_stricmp(key, "Category")==0) scopy(pkg->category,    val, 17);
    else if (nos_stricmp(key, "Description")==0) scopy(pkg->description, val, 61);
    else if (nos_stricmp(key, "Author") == 0) scopy(pkg->author,      val, 41);
    else if (nos_stricmp(key, "Year")   == 0) scopy(pkg->year,        val, 5);
    else if (nos_stricmp(key, "License")== 0) scopy(pkg->license,     val, 13);
    else if (nos_stricmp(key, "Requires")==0) scopy(pkg->requires,    val, 81);
    else if (nos_stricmp(key, "Tags")   == 0) scopy(pkg->tags,        val, 81);
}

static void apply_hardware(nos_pkg_t *pkg, const char *key, const char *val)
{
    if      (nos_stricmp(key, "Memory")   == 0) pkg->memory_kb = (unsigned)atoi(val);
    else if (nos_stricmp(key, "EMS")      == 0) pkg->ems_kb    = (unsigned)atoi(val);
    else if (nos_stricmp(key, "XMS")      == 0) pkg->xms_kb    = (unsigned)atoi(val);
    else if (nos_stricmp(key, "DiskKB")   == 0) pkg->disk_kb   = (unsigned)atoi(val);
    else if (nos_stricmp(key, "Sound")    == 0) scopy(pkg->sound,     val, 8);
    else if (nos_stricmp(key, "CPUSpeed") == 0) scopy(pkg->cpu_speed, val, 8);
    else if (nos_stricmp(key, "Mouse")    == 0) pkg->mouse = parse_bool(val);
    else if (nos_stricmp(key, "VGA")      == 0) pkg->vga   = parse_bool(val);
}

static void apply_source(nos_pkg_t *pkg, const char *key, const char *val)
{
    if      (nos_stricmp(key, "URL1")     == 0) scopy(pkg->url1,    val, NOS_PKG_URL_LEN);
    else if (nos_stricmp(key, "URL2")     == 0) scopy(pkg->url2,    val, NOS_PKG_URL_LEN);
    else if (nos_stricmp(key, "URL3")     == 0) scopy(pkg->url3,    val, NOS_PKG_URL_LEN);
    else if (nos_stricmp(key, "Archive")  == 0) scopy(pkg->archive, val, 13);
    else if (nos_stricmp(key, "Bytes")    == 0) pkg->expected_bytes = atol(val);
    else if (nos_stricmp(key, "MD5")      == 0) scopy(pkg->md5,     val, 33);
    else if (nos_stricmp(key, "Redirect") == 0) pkg->redirect = parse_bool(val);
}

static void apply_install(nos_pkg_t *pkg, const char *key, const char *val)
{
    if (nos_stricmp(key, "InstallDir") == 0) {
        scopy(pkg->install_dir, val, 65);
    } else if (nos_stricmp(key, "Extractor") == 0) {
        scopy(pkg->extractor, val, 9);
    } else if (nos_stricmp(key, "Extract") == 0) {
        pkg->extract = parse_bool(val);
    } else if (nos_stricmp(key, "StripDir") == 0) {
        pkg->strip_dir = parse_bool(val);
    } else if (nos_stricmp(key, "SetPath") == 0) {
        pkg->set_path = parse_bool(val);
    } else if (nos_stricmp(key, "SetVar") == 0) {
        if (pkg->set_var_count < NOS_PKG_MAX_SETVARS)
            scopy(pkg->set_vars[pkg->set_var_count++], val, NOS_PKG_SETVAR_LEN);
    }
}

static void apply_launcher(nos_pkg_t *pkg, const char *key, const char *val)
{
    pkg->has_launcher = 1;
    if      (nos_stricmp(key, "Label") == 0) scopy(pkg->launch_label, val, 21);
    else if (nos_stricmp(key, "Exec")  == 0) scopy(pkg->launch_exec,  val, 65);
    else if (nos_stricmp(key, "Dir")   == 0) scopy(pkg->launch_dir,   val, 65);
    else if (nos_stricmp(key, "Icon")  == 0) pkg->launch_icon = (unsigned)atoi(val);
}

static void apply_game(nos_pkg_t *pkg, const char *key, const char *val)
{
    pkg->has_game = 1;
    if      (nos_stricmp(key, "CPUPreset")  == 0) scopy(pkg->game_cpu_preset,  val, 11);
    else if (nos_stricmp(key, "MemProfile") == 0) scopy(pkg->game_mem_profile, val, 5);
    else if (nos_stricmp(key, "SoundEnv")   == 0) scopy(pkg->game_sound_env,   val, 81);
    else if (nos_stricmp(key, "MusicExt")   == 0) scopy(pkg->game_music_ext,   val, 5);
    else if (nos_stricmp(key, "Notes")      == 0) scopy(pkg->game_notes,       val, 201);
    else if (nos_stricmp(key, "SaveDir")    == 0) scopy(pkg->game_save_dir,    val, 65);
}

/* -----------------------------------------------------------------------
 * nos_npkg_parse
 * ----------------------------------------------------------------------- */

int nos_npkg_parse(const char *path, nos_pkg_t *pkg)
{
    FILE *f;
    char  buf[LINE_BUF];
    char *line;
    char *eq;
    char *key;
    char *val;
    int   sect = SECT_NONE;

    memset(pkg, 0, sizeof(*pkg));

    /* Defaults */
    pkg->extract = 1;
    scopy(pkg->extractor, "pkzip", 9);

    f = fopen(path, "r");
    if (!f)
        return NOS_INSTALL_ERR_PARSE;

    while (fgets(buf, (int)sizeof(buf), f)) {
        strip_crlf(buf);
        line = ltrim(buf);
        if (!*line || *line == ';' || *line == '#')
            continue;

        /* Section header */
        if (*line == '[') {
            sect = parse_section(line);
            continue;
        }

        /* POST-INSTALL and REMOVE: each line is a raw batch command.       */
        if (sect == SECT_POST_INSTALL) {
            if (pkg->post_count < NOS_PKG_MAX_POST_LINES) {
                rtrim(line);
                scopy(pkg->post_lines[pkg->post_count++], line, NOS_PKG_MAX_LINE);
            }
            continue;
        }
        if (sect == SECT_REMOVE) {
            if (pkg->remove_count < NOS_PKG_MAX_REMOVE_LINES) {
                rtrim(line);
                scopy(pkg->remove_lines[pkg->remove_count++], line, NOS_PKG_MAX_LINE);
            }
            continue;
        }

        /* KEY=VALUE sections */
        eq = strchr(line, '=');
        if (!eq)
            continue;   /* line has no '='; skip */

        *eq = '\0';
        key = line;
        val = eq + 1;

        rtrim(key);
        key = ltrim(key);
        val = ltrim(val);
        rtrim(val);

        switch (sect) {
        case SECT_PACKAGE:  apply_package(pkg,  key, val); break;
        case SECT_HARDWARE: apply_hardware(pkg, key, val); break;
        case SECT_SOURCE:   apply_source(pkg,   key, val); break;
        case SECT_INSTALL:  apply_install(pkg,  key, val); break;
        case SECT_LAUNCHER: apply_launcher(pkg, key, val); break;
        case SECT_GAME:     apply_game(pkg,     key, val); break;
        default: break;  /* unknown section — ignore */
        }
    }

    fclose(f);
    return NOS_INSTALL_OK;
}

/* -----------------------------------------------------------------------
 * nos_install_fetch_def
 * ----------------------------------------------------------------------- */

int nos_install_fetch_def(const char *id, const char *category,
                          char *dest, int dest_max)
{
    char local[64];
    char url[160];
    FILE *probe;
    char cmd[200];
    int  rc;

    /* Build the local cache path: C:\NOS\NPKG\DEFS\<ID>.NPK              */
    strcpy(local, NOS_INSTALL_DEFS_DIR);
    strcat(local, "\\");
    strcat(local, id);
    strcat(local, ".NPK");

    /* Return immediately if already cached.                                */
    probe = fopen(local, "r");
    if (probe) {
        fclose(probe);
        if (dest && dest_max > 0) scopy(dest, local, dest_max);
        return NOS_INSTALL_OK;
    }

    /* Ensure the DEFS directory exists.                                    */
    mkdir("C:\\NOS\\NPKG");
    mkdir(NOS_INSTALL_DEFS_DIR);

    /* Build the repository URL: <REPO>/<category>/<ID>.NPKG               */
    strcpy(url, nos_repo_url());
    strcat(url, "/");
    strcat(url, category);
    strcat(url, "/");
    strcat(url, id);
    strcat(url, ".NPKG");

    printf("NPKG: fetching definition for %s...\r\n", id);

    /* Download via HTGET.                                                  */
    strcpy(cmd, SYS_PATH);
    strcat(cmd, "HTGET.EXE ");
    strcat(cmd, url);
    strcat(cmd, " ");
    strcat(cmd, local);

    rc = system(cmd);
    if (rc != 0) {
        printf("NPKG: failed to download definition for %s\r\n", id);
        return NOS_INSTALL_ERR_DEFETCH;
    }

    /* Verify the file actually arrived.                                    */
    probe = fopen(local, "r");
    if (!probe) {
        printf("NPKG: definition file not found after download\r\n");
        return NOS_INSTALL_ERR_NODEF;
    }
    fclose(probe);

    if (dest && dest_max > 0) scopy(dest, local, dest_max);
    return NOS_INSTALL_OK;
}

/* -----------------------------------------------------------------------
 * Internal: recursive directory creation
 * ----------------------------------------------------------------------- */

static void nos_mkdirs(const char *path)
{
    char tmp[65];
    char *p;

    scopy(tmp, path, (int)sizeof(tmp));
    for (p = tmp + 3; *p; p++) {   /* skip drive letter + colon + backslash */
        if (*p == '\\') {
            *p = '\0';
            mkdir(tmp);
            *p = '\\';
        }
    }
    mkdir(tmp);
}

/* -----------------------------------------------------------------------
 * Internal: variable substitution for POST-INSTALL lines
 *
 * Expands %INSTALLDIR%, %NPKGID%, %NPKGVER%, %NPKGDIR%, %DEFSDIR%
 * Writes the expanded string into `out` (max `out_max` bytes).
 * ----------------------------------------------------------------------- */

static void expand_vars(const char *src, char *out, int out_max,
                        const nos_pkg_t *pkg)
{
    const char *p = src;
    int         n = 0;
    int         max = out_max - 1;

    while (*p && n < max) {
        if (*p == '%') {
            /* Check each known variable.                                   */
            const char *repl = NULL;
            char        tmp[16];

            if (strncmp(p, "%INSTALLDIR%", 12) == 0) {
                repl = pkg->install_dir;
                p += 12;
            } else if (strncmp(p, "%NPKGID%", 8) == 0) {
                repl = pkg->id;
                p += 8;
            } else if (strncmp(p, "%NPKGVER%", 9) == 0) {
                repl = pkg->version;
                p += 9;
            } else if (strncmp(p, "%NPKGDIR%", 9) == 0) {
                repl = "C:\\NOS\\NPKG\\";
                p += 9;
            } else if (strncmp(p, "%DEFSDIR%", 9) == 0) {
                repl = NOS_INSTALL_DEFS_DIR "\\";
                p += 9;
            } else {
                /* Unknown %VAR% — pass through literally.                  */
                out[n++] = *p++;
                continue;
            }

            if (repl) {
                int rlen = (int)strlen(repl);
                if (n + rlen >= max) rlen = max - n;
                memcpy(out + n, repl, (size_t)rlen);
                n += rlen;
            }
            (void)tmp;  /* suppress unused warning */
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
}

/* -----------------------------------------------------------------------
 * Internal: run POST-INSTALL or REMOVE batch lines
 * ----------------------------------------------------------------------- */

static int nos_run_lines(const char lines[][NOS_PKG_MAX_LINE], int count,
                         const nos_pkg_t *pkg)
{
    char expanded[NOS_PKG_MAX_LINE];
    int  i;
    int  rc;

    for (i = 0; i < count; i++) {
        if (!lines[i][0])
            continue;
        expand_vars(lines[i], expanded, (int)sizeof(expanded), pkg);
        printf("  > %s\r\n", expanded);
        rc = system(expanded);
        if (rc != 0)
            printf("  (returned %d — continuing)\r\n", rc);
    }
    return NOS_INSTALL_OK;
}

/* -----------------------------------------------------------------------
 * Internal: extract archive
 * ----------------------------------------------------------------------- */

static int nos_do_extract(const nos_pkg_t *pkg, const char *archive_path)
{
    char  cmd[200];
    FILE *f;
    int   rc;

    if (nos_stricmp(pkg->extractor, "unzip") == 0) {
        /* UNZIP.EXE -o <archive> -d <install_dir>                         */
        strcpy(cmd, SYS_PATH);
        strcat(cmd, "UNZIP.EXE -o ");
        strcat(cmd, archive_path);
        strcat(cmd, " -d ");
        strcat(cmd, pkg->install_dir);
        rc = system(cmd);

    } else if (nos_stricmp(pkg->extractor, "pkzip") == 0) {
        /* PKUNZIP.EXE -o <archive> <install_dir>                          */
        strcpy(cmd, SYS_PATH);
        strcat(cmd, "PKUNZIP.EXE -o ");
        strcat(cmd, archive_path);
        strcat(cmd, " ");
        strcat(cmd, pkg->install_dir);
        rc = system(cmd);

    } else if (nos_stricmp(pkg->extractor, "unarj") == 0 ||
               nos_stricmp(pkg->extractor, "lha")   == 0) {
        /* These tools require the CWD to be the target directory.         *
         * Write a temp batch file to perform: cd <dir> ; extract          */
        f = fopen(NOS_INSTALL_TMPBAT, "w");
        if (!f) {
            printf("NPKG: cannot write temp batch %s\r\n", NOS_INSTALL_TMPBAT);
            return NOS_INSTALL_ERR_EXTRACT;
        }
        fprintf(f, "@ECHO OFF\r\n");
        fprintf(f, "CD %s\r\n", pkg->install_dir);

        if (nos_stricmp(pkg->extractor, "unarj") == 0)
            fprintf(f, "%sUNARJ.EXE E %s\r\n", SYS_PATH, archive_path);
        else
            fprintf(f, "%sLHA.EXE E %s\r\n",   SYS_PATH, archive_path);

        fclose(f);
        rc = system(NOS_INSTALL_TMPBAT);
        remove(NOS_INSTALL_TMPBAT);

    } else if (nos_stricmp(pkg->extractor, "copy") == 0) {
        /* Copy the archive file as-is (e.g. single-file .EXE/.COM).       */
        strcpy(cmd, "COPY ");
        strcat(cmd, archive_path);
        strcat(cmd, " ");
        strcat(cmd, pkg->install_dir);
        rc = system(cmd);

    } else {
        printf("NPKG: unknown extractor '%s'\r\n", pkg->extractor);
        return NOS_INSTALL_ERR_EXTRACT;
    }

    /* PKUNZIP exits 1 for warnings (e.g. W10: directory already exists),
     * 2 for fatal errors.  Treat 0 and 1 as success.
     * Other extractors (UNZIP, UNARJ, LHA): 0 = ok, anything else = error. */
    if (nos_stricmp(pkg->extractor, "pkzip") == 0)
        return (rc <= 1) ? NOS_INSTALL_OK : NOS_INSTALL_ERR_EXTRACT;
    return (rc == 0) ? NOS_INSTALL_OK : NOS_INSTALL_ERR_EXTRACT;
}

/* -----------------------------------------------------------------------
 * Internal: append lines to AUTOEXEC.BAT
 * ----------------------------------------------------------------------- */

static int nos_update_autoexec(const nos_pkg_t *pkg)
{
    FILE *f;
    int   i;
    char  line[NOS_PKG_SETVAR_LEN + 8];

    if (!pkg->set_path && pkg->set_var_count == 0)
        return NOS_INSTALL_OK;

    f = fopen(NOS_INSTALL_AUTOEXEC, "a");
    if (!f) {
        printf("NPKG: cannot open %s for update\r\n", NOS_INSTALL_AUTOEXEC);
        return NOS_INSTALL_ERR_AUTOEXEC;
    }

    fprintf(f, "\r\nREM --- NPKG: %s %s ---\r\n", pkg->id, pkg->version);

    if (pkg->set_path) {
        fprintf(f, "SET PATH=%%PATH%%;%s\r\n", pkg->install_dir);
        printf("  PATH updated: appended %s\r\n", pkg->install_dir);
    }

    for (i = 0; i < pkg->set_var_count; i++) {
        /* set_vars[i] is "VAR=value"; prefix with SET              */
        fprintf(f, "SET %s\r\n", pkg->set_vars[i]);
        scopy(line, pkg->set_vars[i], (int)sizeof(line));
        printf("  SET %s\r\n", line);
    }

    fprintf(f, "REM --- NPKG end: %s ---\r\n", pkg->id);
    fclose(f);
    return NOS_INSTALL_OK;
}

/* -----------------------------------------------------------------------
 * nos_install_run
 * ----------------------------------------------------------------------- */

int nos_install_run(const nos_pkg_t *pkg)
{
    nos_fetch_src_t src;
    char            archive_path[NOS_FETCH_MAX_PATH];
    int             rc;

    /* Validate mandatory fields.                                           */
    if (!pkg->install_dir[0]) {
        printf("NPKG: InstallDir is missing in package definition\r\n");
        return NOS_INSTALL_ERR_PARSE;
    }
    if (!pkg->archive[0]) {
        printf("NPKG: Archive is missing in package definition\r\n");
        return NOS_INSTALL_ERR_PARSE;
    }

    /* ---- Step 1: download archive ------------------------------------ */
    memset(&src, 0, sizeof(src));
    scopy(src.archive, pkg->archive, (int)sizeof(src.archive));
    src.expected_bytes = pkg->expected_bytes;

    if (pkg->url1[0]) scopy(src.urls[src.url_count++], pkg->url1, NOS_FETCH_MAX_URL + 1);
    if (pkg->url2[0]) scopy(src.urls[src.url_count++], pkg->url2, NOS_FETCH_MAX_URL + 1);
    if (pkg->url3[0]) scopy(src.urls[src.url_count++], pkg->url3, NOS_FETCH_MAX_URL + 1);

    if (src.url_count == 0) {
        printf("NPKG: no download URLs in package definition\r\n");
        return NOS_INSTALL_ERR_FETCH;
    }

    printf("\r\nNPKG: installing %s %s\r\n", pkg->name, pkg->version);
    printf("      to %s\r\n\r\n", pkg->install_dir);

    rc = nos_fetch_archive(&src, archive_path, (int)sizeof(archive_path));
    if (rc != NOS_FETCH_OK) {
        printf("NPKG: download failed (error %d)\r\n", rc);
        return NOS_INSTALL_ERR_FETCH;
    }

    /* ---- Step 2: create install directory ---------------------------- */
    printf("\r\nNPKG: creating %s\r\n", pkg->install_dir);
    nos_mkdirs(pkg->install_dir);

    /* ---- Step 3: extract --------------------------------------------- */
    if (pkg->extract) {
        printf("NPKG: extracting with %s...\r\n", pkg->extractor);
        rc = nos_do_extract(pkg, archive_path);
        if (rc != NOS_INSTALL_OK) {
            printf("NPKG: extraction failed\r\n");
            return rc;
        }
        printf("NPKG: extraction complete\r\n");
    } else {
        printf("NPKG: skipping extraction (Extract=no)\r\n");
    }

    /* ---- Step 4: POST-INSTALL ---------------------------------------- */
    if (pkg->post_count > 0) {
        printf("\r\nNPKG: running post-install steps...\r\n");
        nos_run_lines(
            (const char (*)[NOS_PKG_MAX_LINE])pkg->post_lines,
            pkg->post_count, pkg);
    }

    /* ---- Step 5: AUTOEXEC.BAT updates -------------------------------- */
    rc = nos_update_autoexec(pkg);
    if (rc != NOS_INSTALL_OK)
        return rc;

    printf("\r\nNPKG: %s installed successfully.\r\n", pkg->id);
    if (pkg->set_path || pkg->set_var_count > 0)
        printf("      Reboot or run AUTOEXEC.BAT to apply environment changes.\r\n");

    return NOS_INSTALL_OK;
}

/* -----------------------------------------------------------------------
 * Launcher registration helpers
 * ----------------------------------------------------------------------- */

int nos_install_register_launcher(const nos_pkg_t *pkg)
{
    FILE *fp;
    char  line[128];
    char  marker[24]; /* "#NPKG:" + id (8) + NUL */
    int   already;

    if (!pkg->has_launcher) return NOS_INSTALL_OK;

    /* Build the marker string we look for */
    strcpy(marker, "#NPKG:");
    strcat(marker, pkg->id);

    /* Check for duplicate */
    already = 0;
    fp = fopen(NOS_LAUNCHER_CFG, "r");
    if (fp) {
        while (fgets(line, (int)sizeof(line), fp)) {
            int len = (int)strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';
            if (strcmp(line, marker) == 0) { already = 1; break; }
        }
        fclose(fp);
    }
    if (already) return NOS_INSTALL_OK;

    /* Append marker + entry */
    fp = fopen(NOS_LAUNCHER_CFG, "a");
    if (!fp) return NOS_INSTALL_ERR_AUTOEXEC;

    fprintf(fp, "%s\r\n", marker);

    /* Games with a [GAME] section are launched via NOSPLAY so that the
     * correct CPU / memory / sound profile is applied before exec.
     * Non-game packages retain a direct exec entry.                     */
    if (pkg->has_game) {
        /* Format: Label|Dir|C:\NOS\SYSTEM\NOSPLAY.EXE <id>             */
        char nosplay_cmd[80];
        strcpy(nosplay_cmd, "C:\\NOS\\SYSTEM\\NOSPLAY.EXE ");
        strcat(nosplay_cmd, pkg->id);
        if (pkg->launch_dir[0] != '\0') {
            fprintf(fp, "%s|%s|%s\r\n",
                    pkg->launch_label, pkg->launch_dir, nosplay_cmd);
        } else {
            fprintf(fp, "%s|%s\r\n",
                    pkg->launch_label, nosplay_cmd);
        }
    } else {
        if (pkg->launch_dir[0] != '\0') {
            fprintf(fp, "%s|%s|%s\r\n",
                    pkg->launch_label, pkg->launch_dir, pkg->launch_exec);
        } else {
            fprintf(fp, "%s|%s\r\n",
                    pkg->launch_label, pkg->launch_exec);
        }
    }

    fclose(fp);
    return NOS_INSTALL_OK;
}

int nos_install_remove_launcher(const char *id)
{
    FILE *in;
    FILE *out;
    char  line[128];
    char  marker[24];
    int   skip_next;
    int   len;

    strcpy(marker, "#NPKG:");
    strcat(marker, id);

    in = fopen(NOS_LAUNCHER_CFG, "r");
    if (!in) return NOS_INSTALL_OK; /* nothing to remove */

    out = fopen(NOS_LAUNCHER_TMP, "w");
    if (!out) { fclose(in); return NOS_INSTALL_ERR_AUTOEXEC; }

    skip_next = 0;
    while (fgets(line, (int)sizeof(line), in)) {
        len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (strcmp(line, marker) == 0) {
            skip_next = 1;
            continue;
        }
        if (skip_next) {
            skip_next = 0;
            continue;
        }
        fprintf(out, "%s\r\n", line);
    }

    fclose(in);
    fclose(out);

    remove(NOS_LAUNCHER_CFG);
    rename(NOS_LAUNCHER_TMP, NOS_LAUNCHER_CFG);
    return NOS_INSTALL_OK;
}
