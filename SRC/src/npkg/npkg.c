/* NOS-DOS: NPKG
 * npkg.c - Package manager command router (main entry point).
 *
 * Commands:
 *   NPKG UPDATE              -- refresh packages.idx from repository
 *   NPKG SEARCH [term]       -- search index by name/description
 *   NPKG INFO <id>           -- full package details from .npkg definition
 *   NPKG INSTALL <id>        -- download, extract, register
 *   NPKG REMOVE <id>         -- run [REMOVE] batch, clean AUTOEXEC, deregister
 *   NPKG LIST                -- show installed packages
 *   NPKG PROFILE <id>        -- hardware and memory requirements
 *
 * Global data layout (all in BSS -- never on the stack):
 *   g_index  ~20 KB   package index (nos_index_t)
 *   g_pkg     ~5 KB   parsed .npkg definition (nos_pkg_t)
 *   g_list    ~7 KB   registry list buffer (nos_reg_entry_t[])
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <stdio.h>    /* printf, fopen, fgets, fclose, remove, rename, getchar */
#include <string.h>   /* strcmp, strcpy, strcat, strlen, strncmp, strncpy */
#include <stdlib.h>   /* system, exit */
#include <ctype.h>    /* toupper, tolower */
#include "index.h"
#include "fetch.h"
#include "install.h"
#include "registry.h"

/* -----------------------------------------------------------------------
 * Global state (BSS -- never declare these locally)
 * ----------------------------------------------------------------------- */

static nos_index_t      g_index;
static nos_pkg_t        g_pkg;
static nos_reg_entry_t  g_list[NOS_REG_MAX_ENTRIES];

static int g_index_loaded = 0;

/* -----------------------------------------------------------------------
 * Display helpers
 * ----------------------------------------------------------------------- */

/*
 * print_trunc
 * Print string s padded/truncated to exactly `width` chars.
 */
static void print_trunc(const char *s, int width)
{
    int len = (int)strlen(s);
    int i;
    for (i = 0; i < width; i++) {
        if (i < len)
            putchar(s[i]);
        else
            putchar(' ');
    }
}

static void print_hline(int width)
{
    int i;
    for (i = 0; i < width; i++) putchar('-');
    printf("\r\n");
}

/* -----------------------------------------------------------------------
 * Index helpers
 * ----------------------------------------------------------------------- */

static int ensure_index(void)
{
    int rc;

    if (g_index_loaded)
        return 0;

    rc = nos_index_load(&g_index, NOS_INDEX_CACHE_PATH);
    if (rc == NOS_INDEX_ERR_OPEN) {
        printf("NPKG: no package index found.\r\n");
        printf("      Run 'NPKG UPDATE' to download it.\r\n");
        return -1;
    }
    if (rc == NOS_INDEX_ERR_FULL)
        printf("NPKG: warning: index truncated (too many packages)\r\n");

    g_index_loaded = 1;
    return 0;
}

/* -----------------------------------------------------------------------
 * AUTOEXEC.BAT cleanup for NPKG REMOVE
 *
 * Removes the block written by nos_update_autoexec() in install.c:
 *   REM --- NPKG: <ID> <VER> ---
 *   ...
 *   REM --- NPKG end: <ID> ---
 * ----------------------------------------------------------------------- */

static void remove_autoexec_block(const char *id)
{
    FILE *fin;
    FILE *fout;
    char  line[160];
    char  start[40];
    char  end[40];
    int   skipping = 0;

    /* Markers written by install.c:nos_update_autoexec().                  *
     * The trailing space after ID prevents matching DOOM2 when removing     *
     * DOOM ("REM --- NPKG: DOOM " vs "REM --- NPKG: DOOM2 ").             */
    strcpy(start, "REM --- NPKG: ");
    strcat(start, id);
    strcat(start, " ");

    strcpy(end, "REM --- NPKG end: ");
    strcat(end, id);
    strcat(end, " ");

    fin = fopen("C:\\AUTOEXEC.BAT", "r");
    if (!fin)
        return;

    fout = fopen("C:\\AUTOEXEC.TMP", "w");
    if (!fout) {
        fclose(fin);
        return;
    }

    while (fgets(line, (int)sizeof(line), fin)) {
        if (!skipping && strncmp(line, start, strlen(start)) == 0) {
            skipping = 1;
            continue;
        }
        if (skipping && strncmp(line, end, strlen(end)) == 0) {
            skipping = 0;
            continue;
        }
        if (!skipping)
            fputs(line, fout);
    }

    fclose(fin);
    fclose(fout);

    remove("C:\\AUTOEXEC.BAT");
    rename("C:\\AUTOEXEC.TMP", "C:\\AUTOEXEC.BAT");
}

/* -----------------------------------------------------------------------
 * Usage
 * ----------------------------------------------------------------------- */

static void print_usage(void)
{
    printf("NOS-DOS Package Manager v0.1\r\n");
    printf("Usage: NPKG <command> [options]\r\n\r\n");
    printf("  UPDATE              Download package index from repository\r\n");
    printf("  SEARCH [term]       Search packages by name or description\r\n");
    printf("  INFO <id>           Show full package details\r\n");
    printf("  INSTALL <id>        Download, install, and register package\r\n");
    printf("  REMOVE <id>         Uninstall package and deregister\r\n");
    printf("  LIST                List installed packages\r\n");
    printf("  PROFILE <id>        Show hardware and memory requirements\r\n");
}

/* -----------------------------------------------------------------------
 * NPKG UPDATE
 * ----------------------------------------------------------------------- */

static int cmd_update(void)
{
    char url[160];
    int  rc;

    strcpy(url, nos_repo_url());
    strcat(url, "/packages.idx");

    printf("NPKG: updating index from %s\r\n", nos_repo_url());
    rc = nos_index_download(url, NOS_INDEX_CACHE_PATH);
    if (rc != NOS_INDEX_OK) {
        printf("NPKG: download failed (error %d)\r\n", rc);
        printf("      Check network connection: NNET STATUS\r\n");
        return 1;
    }

    rc = nos_index_load(&g_index, NOS_INDEX_CACHE_PATH);
    if (rc == NOS_INDEX_ERR_OPEN) {
        printf("NPKG: index downloaded but could not be read\r\n");
        return 1;
    }

    g_index_loaded = 1;
    printf("NPKG: index updated - %d package(s) available.\r\n", g_index.count);
    return 0;
}

/* -----------------------------------------------------------------------
 * NPKG SEARCH [term]
 * ----------------------------------------------------------------------- */

static int cmd_search(int argc, char *argv[])
{
    const nos_pkginfo_t *results[NOS_INDEX_MAX_ENTRIES];
    const char          *term = "";
    const char          *cat  = NULL;
    int                  n;
    int                  i;

    if (ensure_index() != 0) return 1;

    /* Optional: NPKG SEARCH <term> [category]                              */
    if (argc >= 3) term = argv[2];
    if (argc >= 4) cat  = argv[3];

    n = nos_index_search_cat(&g_index, term, cat,
                             results, NOS_INDEX_MAX_ENTRIES);

    if (n == 0) {
        printf("NPKG: no packages match '%s'.\r\n", term);
        if (cat) printf("      Category filter: %s\r\n", cat);
        return 0;
    }

    /* Header */
    printf("\r\n");
    print_trunc("ID",       9);
    print_trunc("Category", 14);
    print_trunc("Name",     24);
    print_trunc("Ver",      8);
    printf("KB     License\r\n");
    print_hline(78);

    for (i = 0; i < n && i < NOS_INDEX_MAX_ENTRIES; i++) {
        const nos_pkginfo_t *e = results[i];
        char kb_str[8];
        sprintf(kb_str, "%u", e->size_kb);
        print_trunc(e->id,          9);
        print_trunc(e->category,    14);
        print_trunc(e->name,        24);
        print_trunc(e->version,     8);
        print_trunc(kb_str,         7);
        printf("%s\r\n", e->license);
    }

    printf("\r\n%d package(s) found", n);
    if (n > NOS_INDEX_MAX_ENTRIES)
        printf(" (showing first %d)", NOS_INDEX_MAX_ENTRIES);
    printf(".\r\n");

    return 0;
}

/* -----------------------------------------------------------------------
 * NPKG INFO <id>
 * ----------------------------------------------------------------------- */

static int cmd_info(int argc, char *argv[])
{
    const nos_pkginfo_t *entry;
    char def_path[64];
    int  rc;

    if (argc < 3) {
        printf("Usage: NPKG INFO <id>\r\n");
        return 1;
    }

    if (ensure_index() != 0) return 1;

    entry = nos_index_find(&g_index, argv[2]);
    if (!entry) {
        printf("NPKG: '%s' not found. Run NPKG UPDATE or check spelling.\r\n",
               argv[2]);
        return 1;
    }

    /* Fetch definition for full detail.                                    */
    rc = nos_install_fetch_def(entry->id, entry->category,
                               def_path, (int)sizeof(def_path));
    if (rc != NOS_INSTALL_OK) {
        /* Fall back to index-only info.                                    */
        printf("\r\n[%s] %s  v%s  (%s)\r\n",
               entry->category, entry->name, entry->version, entry->license);
        printf("%s\r\n", entry->description);
        if (entry->size_kb)
            printf("Download: ~%u KB\r\n", entry->size_kb);
        printf("\r\n(Full details unavailable - definition download failed.)\r\n");
        return 0;
    }

    rc = nos_npkg_parse(def_path, &g_pkg);
    if (rc != NOS_INSTALL_OK) {
        printf("NPKG: could not parse package definition.\r\n");
        return 1;
    }

    printf("\r\n");
    printf("Package  : %s\r\n", g_pkg.name);
    printf("ID       : %s\r\n", g_pkg.id);
    printf("Version  : %s\r\n", g_pkg.version);
    printf("Category : %s\r\n", g_pkg.category);
    printf("License  : %s\r\n", g_pkg.license);
    if (g_pkg.author[0])  printf("Author   : %s\r\n", g_pkg.author);
    if (g_pkg.year[0])    printf("Year     : %s\r\n", g_pkg.year);
    printf("\r\n%s\r\n", g_pkg.description);
    if (g_pkg.tags[0])    printf("Tags     : %s\r\n", g_pkg.tags);
    if (g_pkg.requires[0])printf("Requires : %s\r\n", g_pkg.requires);

    printf("\r\nInstall to: %s\r\n", g_pkg.install_dir);
    if (g_pkg.expected_bytes)
        printf("Download  : ~%ld bytes\r\n", g_pkg.expected_bytes);

    if (nos_registry_is_installed(g_pkg.id))
        printf("\r\nStatus: INSTALLED\r\n");
    else
        printf("\r\nStatus: not installed  (NPKG INSTALL %s)\r\n", g_pkg.id);

    return 0;
}

/* -----------------------------------------------------------------------
 * NPKG INSTALL <id>
 * ----------------------------------------------------------------------- */

static int cmd_install(int argc, char *argv[])
{
    const nos_pkginfo_t *entry;
    char def_path[64];
    int  rc;

    if (argc < 3) {
        printf("Usage: NPKG INSTALL <id>\r\n");
        return 1;
    }

    /* Already installed?                                                   */
    if (nos_registry_is_installed(argv[2])) {
        printf("NPKG: %s is already installed.\r\n", argv[2]);
        printf("      Use 'NPKG REMOVE %s' first to reinstall.\r\n", argv[2]);
        return 1;
    }

    if (ensure_index() != 0) return 1;

    entry = nos_index_find(&g_index, argv[2]);
    if (!entry) {
        printf("NPKG: '%s' not found in index.\r\n", argv[2]);
        printf("      Run 'NPKG UPDATE' to refresh, or check spelling.\r\n");
        return 1;
    }

    /* Fetch and parse .npkg definition.                                    */
    rc = nos_install_fetch_def(entry->id, entry->category,
                               def_path, (int)sizeof(def_path));
    if (rc != NOS_INSTALL_OK) {
        printf("NPKG: could not retrieve definition for %s\r\n", entry->id);
        return 1;
    }

    rc = nos_npkg_parse(def_path, &g_pkg);
    if (rc != NOS_INSTALL_OK) {
        printf("NPKG: could not parse package definition\r\n");
        return 1;
    }

    /* Check required packages (warn only; do not auto-install).            */
    if (g_pkg.requires[0]) {
        printf("NPKG: requires: %s\r\n", g_pkg.requires);
        printf("      Install prerequisite packages if not already done.\r\n");
    }

    /* Run the installer.                                                   */
    rc = nos_install_run(&g_pkg);
    if (rc != NOS_INSTALL_OK) {
        printf("NPKG: installation failed (error %d)\r\n", rc);
        return 1;
    }

    /* Register in installed database.                                      */
    rc = nos_registry_add(g_pkg.id, g_pkg.version,
                          g_pkg.install_dir, g_pkg.category);
    if (rc != NOS_REG_OK)
        printf("NPKG: warning: could not update registry (error %d)\r\n", rc);

    /* Register in launcher (no-op if no [LAUNCHER] section).              */
    rc = nos_install_register_launcher(&g_pkg);
    if (rc != NOS_INSTALL_OK)
        printf("NPKG: warning: could not update launcher (error %d)\r\n", rc);

    return 0;
}

/* -----------------------------------------------------------------------
 * NPKG REMOVE <id>
 * ----------------------------------------------------------------------- */

static int cmd_remove(int argc, char *argv[])
{
    nos_reg_entry_t  installed;
    char             def_path[64];
    char             confirm[4];
    int              rc;
    int              i;

    if (argc < 3) {
        printf("Usage: NPKG REMOVE <id>\r\n");
        return 1;
    }

    /* Verify the package is installed.                                     */
    rc = nos_registry_find(argv[2], &installed);
    if (rc == NOS_REG_ERR_NOTFOUND) {
        printf("NPKG: %s is not installed.\r\n", argv[2]);
        return 1;
    }

    printf("Remove %s %s from %s? (Y/N) ",
           installed.id, installed.version, installed.install_dir);
    fflush(stdout);

    if (!fgets(confirm, (int)sizeof(confirm), stdin))
        return 1;
    if (confirm[0] != 'Y' && confirm[0] != 'y') {
        printf("NPKG: removal cancelled.\r\n");
        return 0;
    }

    /* Fetch definition to get [REMOVE] batch lines.                        *
     * Use cached .NPK if available; skip gracefully if not found.          */
    rc = nos_install_fetch_def(installed.id, installed.category,
                               def_path, (int)sizeof(def_path));
    if (rc == NOS_INSTALL_OK) {
        rc = nos_npkg_parse(def_path, &g_pkg);
        if (rc == NOS_INSTALL_OK && g_pkg.remove_count > 0) {
            printf("\r\nNPKG: running removal steps...\r\n");
            for (i = 0; i < g_pkg.remove_count; i++) {
                if (!g_pkg.remove_lines[i][0]) continue;
                printf("  > %s\r\n", g_pkg.remove_lines[i]);
                system(g_pkg.remove_lines[i]);
            }
        }
    } else {
        printf("NPKG: definition not cached; skipping [REMOVE] batch.\r\n");
        printf("      Files in %s must be removed manually.\r\n",
               installed.install_dir);
    }

    /* Clean AUTOEXEC.BAT.                                                  */
    printf("NPKG: cleaning AUTOEXEC.BAT...\r\n");
    remove_autoexec_block(installed.id);

    /* Remove from launcher.                                                */
    nos_install_remove_launcher(installed.id);

    /* Deregister.                                                          */
    rc = nos_registry_remove(installed.id);
    if (rc != NOS_REG_OK)
        printf("NPKG: warning: registry update failed (error %d)\r\n", rc);

    printf("NPKG: %s removed.\r\n", installed.id);
    return 0;
}

/* -----------------------------------------------------------------------
 * NPKG LIST
 * ----------------------------------------------------------------------- */

static int cmd_list(void)
{
    int n;
    int i;

    n = nos_registry_list(g_list, NOS_REG_MAX_ENTRIES);
    if (n < 0) {
        printf("NPKG: could not read registry (error %d)\r\n", n);
        return 1;
    }
    if (n == 0) {
        printf("NPKG: no packages installed.\r\n");
        printf("      Run 'NPKG SEARCH' to browse available packages.\r\n");
        return 0;
    }

    printf("\r\n");
    print_trunc("ID",       9);
    print_trunc("Version",  9);
    print_trunc("Category", 14);
    print_trunc("Date",     12);
    printf("Install Directory\r\n");
    print_hline(78);

    for (i = 0; i < n; i++) {
        print_trunc(g_list[i].id,          9);
        print_trunc(g_list[i].version,      9);
        print_trunc(g_list[i].category,    14);
        print_trunc(g_list[i].date,        12);
        printf("%s\r\n", g_list[i].install_dir);
    }

    printf("\r\n%d package(s) installed.\r\n", n);
    return 0;
}

/* -----------------------------------------------------------------------
 * NPKG PROFILE <id>
 * ----------------------------------------------------------------------- */

static int cmd_profile(int argc, char *argv[])
{
    const nos_pkginfo_t *entry;
    char def_path[64];
    int  rc;

    if (argc < 3) {
        printf("Usage: NPKG PROFILE <id>\r\n");
        return 1;
    }

    if (ensure_index() != 0) return 1;

    entry = nos_index_find(&g_index, argv[2]);
    if (!entry) {
        printf("NPKG: '%s' not found in index.\r\n", argv[2]);
        return 1;
    }

    rc = nos_install_fetch_def(entry->id, entry->category,
                               def_path, (int)sizeof(def_path));
    if (rc != NOS_INSTALL_OK) {
        printf("NPKG: could not retrieve definition for %s\r\n", entry->id);
        return 1;
    }

    rc = nos_npkg_parse(def_path, &g_pkg);
    if (rc != NOS_INSTALL_OK) {
        printf("NPKG: could not parse package definition\r\n");
        return 1;
    }

    printf("\r\nHardware profile: %s %s\r\n\r\n",
           g_pkg.name, g_pkg.version);

    printf("Conventional memory : ");
    if (g_pkg.memory_kb) printf("%u KB minimum\r\n", g_pkg.memory_kb);
    else                  printf("no requirement\r\n");

    printf("EMS memory          : ");
    if (g_pkg.ems_kb)    printf("%u KB\r\n", g_pkg.ems_kb);
    else                  printf("not required\r\n");

    printf("XMS memory          : ");
    if (g_pkg.xms_kb)    printf("%u KB\r\n", g_pkg.xms_kb);
    else                  printf("not required\r\n");

    printf("Disk space          : ");
    if (g_pkg.disk_kb)   printf("%u KB\r\n", g_pkg.disk_kb);
    else                  printf("unknown\r\n");

    printf("Sound               : %s\r\n",
           g_pkg.sound[0] ? g_pkg.sound : "none");
    printf("Mouse               : %s\r\n", g_pkg.mouse ? "required" : "optional");
    printf("VGA                 : %s\r\n", g_pkg.vga   ? "required" : "not required");
    printf("CPU speed           : %s\r\n",
           g_pkg.cpu_speed[0] ? g_pkg.cpu_speed : "any");

    if (g_pkg.has_game) {
        printf("\r\nGame settings:\r\n");
        printf("  CPU preset    : %s\r\n", g_pkg.game_cpu_preset);
        printf("  Mem profile   : NOSMEM /%s\r\n", g_pkg.game_mem_profile);
        if (g_pkg.game_sound_env[0])
            printf("  Sound env     : SET %s\r\n", g_pkg.game_sound_env);
        if (g_pkg.game_music_ext[0])
            printf("  Music         : %s\r\n", g_pkg.game_music_ext);
        if (g_pkg.game_notes[0])
            printf("  Notes         : %s\r\n", g_pkg.game_notes);
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    char cmd[16];
    int  i;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    /* Uppercase the subcommand for comparison.                             */
    for (i = 0; i < 15 && argv[1][i]; i++)
        cmd[i] = (char)toupper((unsigned char)argv[1][i]);
    cmd[i] = '\0';

    if (strcmp(cmd, "UPDATE")  == 0) return cmd_update();
    if (strcmp(cmd, "SEARCH")  == 0) return cmd_search(argc, argv);
    if (strcmp(cmd, "INFO")    == 0) return cmd_info(argc, argv);
    if (strcmp(cmd, "INSTALL") == 0) return cmd_install(argc, argv);
    if (strcmp(cmd, "REMOVE")  == 0) return cmd_remove(argc, argv);
    if (strcmp(cmd, "LIST")    == 0) return cmd_list();
    if (strcmp(cmd, "PROFILE") == 0) return cmd_profile(argc, argv);

    printf("NPKG: unknown command '%s'\r\n\r\n", argv[1]);
    print_usage();
    return 1;
}
