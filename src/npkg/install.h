/* NOS-DOS: NPKG
 * install.h - Package installer types and interface.
 *
 * Declares nos_pkg_t (the in-memory representation of a parsed .npkg file)
 * and the functions that drive the install workflow:
 *   nos_npkg_parse()        -- parse .npkg definition into nos_pkg_t
 *   nos_install_fetch_def() -- download .npkg definition if not cached
 *   nos_install_run()       -- execute the full install sequence
 *
 * IMPORTANT: nos_pkg_t is ~5 KB.  Declare it globally in npkg.c; never
 * as a local variable (stack overflow risk in small model).
 *
 * Registration in INSTALLED.DB is the caller's responsibility (npkg.c
 * calls nos_registry_add() after nos_install_run() returns OK).
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#ifndef NOS_INSTALL_H
#define NOS_INSTALL_H

/* -----------------------------------------------------------------------
 * Limits (must match packages/README.md field constraints)
 * ----------------------------------------------------------------------- */

#define NOS_PKG_MAX_SETVARS         8    /* SetVar entries in [INSTALL]     */
#define NOS_PKG_MAX_POST_LINES      16   /* batch lines in [POST-INSTALL]   */
#define NOS_PKG_MAX_REMOVE_LINES    8    /* batch lines in [REMOVE]         */
#define NOS_PKG_MAX_LINE            128  /* max batch line length (DOS)     */
#define NOS_PKG_SETVAR_LEN          65   /* VAR=value string length         */
#define NOS_PKG_URL_LEN             101  /* URL1/URL2/URL3 + NUL            */

/* -----------------------------------------------------------------------
 * Paths
 * ----------------------------------------------------------------------- */

#define NOS_INSTALL_DEFS_DIR        "C:\\NOS\\NPKG\\DEFS"
#define NOS_INSTALL_AUTOEXEC        "C:\\AUTOEXEC.BAT"
#define NOS_INSTALL_TMPBAT          "C:\\NOS\\NPKG\\XTRCT.BAT"

/* -----------------------------------------------------------------------
 * Return codes
 * ----------------------------------------------------------------------- */

#define NOS_INSTALL_OK              0
#define NOS_INSTALL_ERR_PARSE      -1   /* .npkg file missing or malformed  */
#define NOS_INSTALL_ERR_FETCH      -2   /* archive download failed          */
#define NOS_INSTALL_ERR_MKDIR      -3   /* cannot create InstallDir         */
#define NOS_INSTALL_ERR_EXTRACT    -4   /* archive extraction failed        */
#define NOS_INSTALL_ERR_NODEF      -5   /* definition file not found        */
#define NOS_INSTALL_ERR_DEFETCH    -6   /* definition download failed       */
#define NOS_INSTALL_ERR_AUTOEXEC   -7   /* cannot update AUTOEXEC.BAT       */

/* -----------------------------------------------------------------------
 * nos_pkg_t -- full parsed representation of a .npkg definition file
 * ----------------------------------------------------------------------- */

typedef struct {

    /* [PACKAGE] --------------------------------------------------------- */
    char         id[9];
    char         name[41];
    char         version[13];
    char         category[17];
    char         description[61];
    char         author[41];
    char         year[5];
    char         license[13];
    char         requires[81];
    char         tags[81];

    /* [HARDWARE] -------------------------------------------------------- */
    unsigned int memory_kb;
    unsigned int ems_kb;
    unsigned int xms_kb;
    unsigned int disk_kb;
    char         sound[8];
    char         cpu_speed[8];
    char         mouse;             /* 1 = required */
    char         vga;               /* 1 = VGA required */

    /* [SOURCE] ---------------------------------------------------------- */
    char         url1[NOS_PKG_URL_LEN];
    char         url2[NOS_PKG_URL_LEN];
    char         url3[NOS_PKG_URL_LEN];
    char         archive[13];       /* 8.3 filename */
    long         expected_bytes;    /* 0 = unknown */
    char         md5[33];           /* hex, empty if absent */
    char         redirect;          /* 1 = archive.org-style redirect */

    /* [INSTALL] --------------------------------------------------------- */
    char         install_dir[65];
    char         extractor[9];      /* unzip|unarj|lha|pkzip|copy */
    char         extract;           /* 1 = extract (default) */
    char         strip_dir;         /* 1 = strip top-level archive dir */
    char         set_path;          /* 1 = append InstallDir to PATH */
    char         set_vars[NOS_PKG_MAX_SETVARS][NOS_PKG_SETVAR_LEN];
    int          set_var_count;

    /* [POST-INSTALL] ---------------------------------------------------- */
    char         post_lines[NOS_PKG_MAX_POST_LINES][NOS_PKG_MAX_LINE];
    int          post_count;

    /* [LAUNCHER] -------------------------------------------------------- */
    char         has_launcher;
    char         launch_label[21];
    char         launch_exec[65];
    char         launch_dir[65];
    unsigned int launch_icon;

    /* [GAME] ------------------------------------------------------------ */
    char         has_game;
    char         game_cpu_preset[11];
    char         game_mem_profile[5];
    char         game_sound_env[81];
    char         game_music_ext[5];
    char         game_notes[201];
    char         game_save_dir[65];

    /* [REMOVE] ---------------------------------------------------------- */
    char         remove_lines[NOS_PKG_MAX_REMOVE_LINES][NOS_PKG_MAX_LINE];
    int          remove_count;

} nos_pkg_t;

/* -----------------------------------------------------------------------
 * Interface
 * ----------------------------------------------------------------------- */

/*
 * nos_npkg_parse
 *
 * Parses the .npkg definition file at `path` into `pkg`.
 * Clears `pkg` before parsing; unknown keys are silently ignored.
 * Defaults: Extract=yes, Extractor=unzip.
 *
 * Returns NOS_INSTALL_OK on success, NOS_INSTALL_ERR_PARSE on failure.
 */
int nos_npkg_parse(const char *path, nos_pkg_t *pkg);

/*
 * nos_install_fetch_def
 *
 * Checks whether C:\NOS\NPKG\DEFS\<id>.NPK already exists.  If not,
 * downloads it from the repository at:
 *   NOS_INDEX_REPO_URL/<category>/<id>.NPKG
 *
 * On success, `dest` receives the local .NPK path (caller provides a
 * buffer of at least `dest_max` bytes).
 *
 * Returns NOS_INSTALL_OK on success, NOS_INSTALL_ERR_* on failure.
 */
int nos_install_fetch_def(const char *id, const char *category,
                          char *dest, int dest_max);

/*
 * nos_install_run
 *
 * Executes the full install sequence for a parsed package:
 *   1. Download archive (nos_fetch_archive)
 *   2. Create InstallDir
 *   3. Extract archive
 *   4. Run POST-INSTALL batch lines (with variable substitution)
 *   5. Append PATH update to AUTOEXEC.BAT if SetPath=yes
 *   6. Append SET lines to AUTOEXEC.BAT for each SetVar
 *
 * Registration in INSTALLED.DB is NOT done here; call nos_registry_add()
 * after this function returns OK.
 *
 * Returns NOS_INSTALL_OK on success, NOS_INSTALL_ERR_* on failure.
 */
int nos_install_run(const nos_pkg_t *pkg);

/* Launcher integration paths */
#define NOS_LAUNCHER_CFG  "C:\\NOS\\SHELL\\LAUNCHER.CFG"
#define NOS_LAUNCHER_TMP  "C:\\NOS\\SHELL\\LAUNCH.TMP"

/*
 * nos_install_register_launcher
 *
 * Appends a #NPKG:<id> marker and a 3-field entry to LAUNCHER.CFG for
 * packages that declare a [LAUNCHER] section.  Idempotent: does nothing if
 * the marker already exists.
 *
 * Returns NOS_INSTALL_OK or NOS_INSTALL_ERR_AUTOEXEC on I/O failure.
 */
int nos_install_register_launcher(const nos_pkg_t *pkg);

/*
 * nos_install_remove_launcher
 *
 * Removes the #NPKG:<id> marker line and the immediately following entry
 * line from LAUNCHER.CFG via a TMP-then-rename atomic rewrite.
 *
 * Returns NOS_INSTALL_OK or NOS_INSTALL_ERR_AUTOEXEC on I/O failure.
 */
int nos_install_remove_launcher(const char *id);

#endif /* NOS_INSTALL_H */
