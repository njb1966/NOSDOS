/* NOS-DOS: NOS-PLAY
 * nosplay.c - Game launcher with profile application.
 *
 * Launch sequence (NOSPLAY <id>):
 *   1. Load game profile from cached .npkg definition.
 *   2. Apply memory profile: run NOSMEM /<profile> if MemProfile != "".
 *   3. Apply CPU throttle: run THROTTLE /L<n> for the CPUPreset (install
 *      if not present; skip if preset is OFF or empty).
 *   4. Set sound environment variables from SoundEnv= line.
 *   5. chdir to game directory, exec game.
 *   6. After game exits: remove throttle (THROTTLE /U) if we installed it.
 *      Memory profile persists -- the user reboots or runs NOSMEM /STD.
 *
 * Interactive mode (NOSPLAY with no args):
 *   Shows a numbered list of installed games; user types a number to launch.
 *
 * Commands:
 *   NOSPLAY                 interactive game selector
 *   NOSPLAY <id>            launch game immediately
 *   NOSPLAY LIST            list installed games (text, non-interactive)
 *   NOSPLAY INFO <id>       show game profile details
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <dos.h>      /* int86, union REGS */
#include <stdio.h>    /* printf, scanf */
#include <string.h>   /* strcmp, strcpy, strcat, strlen, strncpy */
#include <stdlib.h>   /* system, exit, atoi */
#include <ctype.h>    /* toupper, isdigit */
#include <direct.h>   /* chdir */
#include "profiles.h"

/* NOSMEM and THROTTLE paths */
#define NOSMEM_EXE    "C:\\NOS\\SYSTEM\\NOSMEM.EXE"
#define THROTTLE_EXE  "C:\\NOS\\SYSTEM\\THROTTLE.COM"
#define TCTL_EXE      "C:\\NOS\\SYSTEM\\TCTL.EXE"

/* Static globals to avoid stack overflow. */
static nos_game_profile_t g_profile;
static nos_game_list_t    g_games;

/* -----------------------------------------------------------------------
 * vcpi_present -- returns 1 if a VCPI server (JEMMEX / EMM386) is loaded.
 *
 * THROTTLE.COM hooks INT 08h directly from real/V86 mode.  Under a VCPI
 * host this triggers a protection fault (JEMMEX exception 0Eh).  We detect
 * the VCPI server via INT 67h AH=DEh AL=00h: AH=00h on return means a VCPI
 * server responded and THROTTLE must not be installed.
 * ----------------------------------------------------------------------- */

static int vcpi_present(void)
{
    union REGS r;
    r.h.ah = 0xDE;
    r.h.al = 0x00;
    int86(0x67, &r, &r);
    return (r.h.ah == 0x00);
}

/* -----------------------------------------------------------------------
 * preset_to_level -- map CPUPreset name to THROTTLE level 0-5
 * ----------------------------------------------------------------------- */

static int preset_to_level(const char *preset)
{
    if (!preset || !preset[0])  return -1;  /* no preset */
    if (strcmp(preset, "OFF")     == 0) return 0;
    if (strcmp(preset, "SLOW100") == 0) return 1;
    if (strcmp(preset, "SLOW66")  == 0) return 2;
    if (strcmp(preset, "SLOW33")  == 0) return 3;
    if (strcmp(preset, "SLOW10")  == 0) return 4;
    if (strcmp(preset, "SLOW477") == 0) return 5;
    return -1;
}

/* -----------------------------------------------------------------------
 * throttle_installed -- returns 1 if THROTTLE.COM is already resident.
 * Uses the same signature check as TCTL: checks INT 08h vector segment.
 * ----------------------------------------------------------------------- */

static int throttle_installed(void)
{
    void (far *vec)(void);
    char far   *sig;

    vec = (void (far *)(void))_dos_getvect(0x08);
    sig = (char far *)MK_FP(FP_SEG(vec), 0x100);
    return (sig[0]=='T' && sig[1]=='H' && sig[2]=='R' && sig[3]=='O') ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * active_mem_profile -- read current profile name from PROFILE.DAT.
 * Writes profile name into buf (up to buf_max-1 chars) and NUL-terminates.
 * Returns 1 if found, 0 if file missing or unreadable.
 * ----------------------------------------------------------------------- */

#define PATH_PROFILE "C:\\NOS\\SYSTEM\\PROFILE.DAT"

static int active_mem_profile(char *buf, int buf_max)
{
    FILE *f;
    int   i;

    f = fopen(PATH_PROFILE, "r");
    if (!f) return 0;
    if (!fgets(buf, buf_max, f)) { fclose(f); buf[0] = '\0'; return 0; }
    fclose(f);
    for (i = 0; buf[i] && buf[i] != '\r' && buf[i] != '\n'; i++) ;
    buf[i] = '\0';
    return (buf[0] != '\0');
}

/* -----------------------------------------------------------------------
 * apply_profile -- set up environment before launching the game
 * Returns 1 if we installed THROTTLE (so we know to remove it on exit).
 * ----------------------------------------------------------------------- */

static int apply_profile(const nos_game_profile_t *p)
{
    char   cmd[128];
    char   cur_prof[16];
    int    level;
    int    we_installed_throttle = 0;

    /* Step 1: Memory profile — skip if already active to avoid reboot. */
    if (p->mem_profile[0]) {
        if (active_mem_profile(cur_prof, sizeof(cur_prof)) &&
            strcmp(cur_prof, p->mem_profile) == 0) {
            printf("NOSPLAY: memory profile %s already active.\r\n",
                   p->mem_profile);
        } else {
            printf("NOSPLAY: applying memory profile %s...\r\n",
                   p->mem_profile);
            strcpy(cmd, NOSMEM_EXE);
            strcat(cmd, " /");
            strcat(cmd, p->mem_profile);
            system(cmd);
            /* NOSMEM reboots — execution does not continue past here
             * unless /NOREBOOT was used.  Game will not launch this run. */
            return 0;
        }
    }

    /* Step 2: CPU throttle.
     * Skip if a VCPI server (JEMMEX/EMM386) is loaded -- THROTTLE.COM hooks
     * INT 08h directly from V86 mode which triggers a protection fault. */
    level = preset_to_level(p->cpu_preset);
    if (level > 0) {
        if (vcpi_present()) {
            printf("NOSPLAY: VCPI/JEMMEX detected -- skipping THROTTLE"
                   " (INT 08h unsafe in V86 mode).\r\n");
        } else if (!throttle_installed()) {
            printf("NOSPLAY: installing THROTTLE at level %d (%s)...\r\n",
                   level, p->cpu_preset);
            sprintf(cmd, "%s /L%d", THROTTLE_EXE, level);
            system(cmd);
            we_installed_throttle = 1;
        } else {
            printf("NOSPLAY: setting THROTTLE to %s...\r\n", p->cpu_preset);
            sprintf(cmd, "%s SET %s", TCTL_EXE, p->cpu_preset);
            system(cmd);
        }
    }

    /* Step 3: Sound environment (SoundEnv=VAR=VAL VAR2=VAL2 ...) */
    if (p->sound_env[0]) {
        /* Each space-separated token is VAR=VALUE; emit as SET commands. */
        char buf[NOS_PROF_SND_LEN];
        char *tok;
        strncpy(buf, p->sound_env, NOS_PROF_SND_LEN - 1);
        tok = buf;
        while (*tok) {
            char *next = tok;
            char *sp;
            while (*next && *next != ' ') next++;
            sp = next;
            if (*sp) *sp = '\0';
            if (strchr(tok, '=')) {
                sprintf(cmd, "SET %s", tok);
                system(cmd);
            }
            if (*sp) tok = sp + 1;
            else break;
        }
    }

    return we_installed_throttle;
}

/* -----------------------------------------------------------------------
 * launch_game
 * ----------------------------------------------------------------------- */

static void launch_game(const nos_game_profile_t *p)
{
    int we_throttled;

    printf("NOSPLAY: launching %s...\r\n", p->name[0] ? p->name : p->id);

    we_throttled = apply_profile(p);

    /* chdir and exec */
    if (p->dir[0])
        chdir(p->dir);

    system(p->exec);

    /* Restore throttle state */
    if (we_throttled) {
        printf("NOSPLAY: removing THROTTLE...\r\n");
        system(THROTTLE_EXE " /U");
    }

    printf("NOSPLAY: returned from %s.\r\n", p->id);
}

/* -----------------------------------------------------------------------
 * cmd_launch -- NOSPLAY <id>
 * ----------------------------------------------------------------------- */

static void cmd_launch(const char *id)
{
    int rc;
    rc = nos_profile_load(id, &g_profile);
    if (rc == NOS_PROF_ERR_NODEF) {
        printf("NOSPLAY: definition for '%s' not found.\r\n", id);
        printf("  Is it installed?  Run: NPKG LIST\r\n");
        return;
    }
    if (rc != NOS_PROF_OK) {
        printf("NOSPLAY: error loading profile for '%s' (code %d)\r\n", id, rc);
        return;
    }
    if (!g_profile.exec[0]) {
        printf("NOSPLAY: no Exec path in definition for '%s'\r\n", id);
        return;
    }
    launch_game(&g_profile);
}

/* -----------------------------------------------------------------------
 * cmd_list -- NOSPLAY LIST
 * ----------------------------------------------------------------------- */

static void cmd_list(void)
{
    int rc, i;
    rc = nos_profile_list_games(&g_games);
    if (rc == NOS_PROF_ERR_NOREG || rc == 0) {
        printf("No games installed.  Run: NPKG INSTALL <game_id>\r\n");
        return;
    }
    printf("Installed games:\r\n");
    printf("  %-8s  %s\r\n", "ID", "Version");
    printf("  %-8s  %s\r\n", "--------", "-------");
    for (i = 0; i < g_games.count; i++)
        printf("  %-8s  %s\r\n",
               g_games.entries[i].id,
               g_games.entries[i].version);
}

/* -----------------------------------------------------------------------
 * cmd_info -- NOSPLAY INFO <id>
 * ----------------------------------------------------------------------- */

static void cmd_info(const char *id)
{
    int rc;
    rc = nos_profile_load(id, &g_profile);
    if (rc == NOS_PROF_ERR_NODEF) {
        printf("NOSPLAY: definition for '%s' not found.\r\n", id);
        return;
    }
    if (rc != NOS_PROF_OK) {
        printf("NOSPLAY: error (code %d)\r\n", rc);
        return;
    }
    printf("Game:       %s\r\n", g_profile.name[0] ? g_profile.name : id);
    printf("ID:         %s\r\n", g_profile.id);
    printf("Exec:       %s\r\n", g_profile.exec);
    printf("Dir:        %s\r\n", g_profile.dir);
    if (g_profile.has_game) {
        printf("CPU preset: %s\r\n",
               g_profile.cpu_preset[0] ? g_profile.cpu_preset : "(none)");
        printf("Mem prof:   %s\r\n",
               g_profile.mem_profile[0] ? g_profile.mem_profile : "(none)");
        printf("Sound env:  %s\r\n",
               g_profile.sound_env[0]   ? g_profile.sound_env   : "(none)");
        if (g_profile.notes[0])
            printf("Notes:      %s\r\n", g_profile.notes);
    } else {
        printf("(No [GAME] section in definition -- no profile applied)\r\n");
    }
}

/* -----------------------------------------------------------------------
 * cmd_interactive -- run when NOSPLAY is called with no arguments
 * ----------------------------------------------------------------------- */

static void cmd_interactive(void)
{
    int  rc, i, choice;
    char input[8];

    rc = nos_profile_list_games(&g_games);
    if (rc == NOS_PROF_ERR_NOREG || rc == 0) {
        printf("NOS-PLAY: No games installed.\r\n");
        printf("Install games with: NPKG INSTALL <id>\r\n");
        printf("Available: DOOM DOOM2 WOLF3D PRINCE KEEN1 HERETIC DUKE3D\r\n");
        printf("           QUAKE DESCENT TYRIAN\r\n");
        return;
    }

    printf("\r\nNOS-PLAY -- Game Launcher\r\n");
    printf("=========================\r\n");
    for (i = 0; i < g_games.count; i++)
        printf("  %2d. %s\r\n", i + 1, g_games.entries[i].id);
    printf("\r\n");
    printf("Enter number (0 to cancel): ");

    if (!fgets(input, (int)sizeof(input), stdin)) return;
    choice = atoi(input);
    if (choice < 1 || choice > g_games.count) {
        printf("Cancelled.\r\n");
        return;
    }

    cmd_launch(g_games.entries[choice - 1].id);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    char cmd[16];
    int  i;

    if (argc < 2) {
        cmd_interactive();
        return 0;
    }

    for (i = 0; argv[1][i] && i < 15; i++)
        cmd[i] = (char)toupper((unsigned char)argv[1][i]);
    cmd[i] = '\0';

    if (strcmp(cmd, "LIST") == 0) { cmd_list(); return 0; }
    if (strcmp(cmd, "INFO") == 0) {
        if (argc >= 3) cmd_info(argv[2]);
        else printf("Usage: NOSPLAY INFO <id>\r\n");
        return 0;
    }

    /* Any other first argument is treated as a game ID. */
    cmd_launch(argv[1]);
    return 0;
}
