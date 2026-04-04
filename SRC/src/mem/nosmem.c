/* NOS-DOS: NOS-MEM
 * nosmem.c - Memory profile switcher.
 *
 * Rewrites C:\CONFIG.SYS for the requested profile, then prompts for
 * reboot.  AUTOEXEC.BAT is untouched -- all profile differences are
 * in CONFIG.SYS (JEMMEX options, FILES, BUFFERS, STACKS, CTMOUSE).
 *
 * Profiles:
 *   STD   Standard.  JEMMEX NOEMS, CTMOUSE, full FILES/BUFFERS/STACKS.
 *         Target: 620KB+ conventional.
 *   MAX   Maximum conventional.  Strip mouse and reduce table sizes.
 *         Target: 635KB+ conventional.
 *   EMS   EMS enabled.  Remove NOEMS from JEMMEX, CTMOUSE present.
 *         Target: 588KB+ conventional + EMS pool.
 *   GAME  Game mode.  NOEMS, CTMOUSE (games need mouse), minimal tables.
 *         Target: 630KB+ conventional.
 *
 * Usage:
 *   NOSMEM /STD          Switch to STD profile and reboot.
 *   NOSMEM /MAX          Switch to MAX profile and reboot.
 *   NOSMEM /EMS          Switch to EMS profile and reboot.
 *   NOSMEM /GAME         Switch to GAME profile and reboot.
 *   NOSMEM /STATUS       Show current profile (no changes made).
 *   NOSMEM /NOREBOOT     Apply profile without rebooting (testing).
 *   NOSMEM /?            Show help.
 *
 * Exit codes: 0 success, 1 I/O error, 2 bad argument.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <dos.h>    /* int86 */
#include <stdio.h>  /* fopen, fclose, fgets, fputs, fprintf, printf */
#include <string.h> /* strcmp, strcpy, strcat, strchr, strlen, memset */

/* -----------------------------------------------------------------------
 * Paths
 * ----------------------------------------------------------------------- */

#define PATH_CONFIG_SYS  "C:\\CONFIG.SYS"
#define PATH_HWCFG       "C:\\NOS\\SYSTEM\\NOS-HW.CFG"
#define PATH_PROFILE     "C:\\NOS\\SYSTEM\\PROFILE.DAT"

/* -----------------------------------------------------------------------
 * Profile identifiers
 * ----------------------------------------------------------------------- */

#define PROF_NONE  0
#define PROF_STD   1
#define PROF_MAX   2
#define PROF_EMS   3
#define PROF_GAME  4

static const char *g_prof_name[] = { "", "STD", "MAX", "EMS", "GAME" };

/* -----------------------------------------------------------------------
 * Hardware info (subset of NOS-HW.CFG needed by NOSMEM)
 * ----------------------------------------------------------------------- */

typedef struct {
    int          mouse_present;
    unsigned int mouse_buttons;  /* 2 or 3 */
} nos_hw_t;

/* -----------------------------------------------------------------------
 * NOS-HW.CFG reader
 *
 * Parses the simple INI file written by NOS-DETECT.  Only extracts the
 * fields NOSMEM needs; unknown keys and sections are silently skipped.
 * ----------------------------------------------------------------------- */

static void strip_crlf(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' '))
        s[--n] = '\0';
}

static int parse_int(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

static void read_hwcfg(nos_hw_t *hw)
{
    FILE   *f;
    char    line[128];
    char    section[32];
    char   *eq;
    char    key[48];
    char    val[48];
    int     klen;

    memset(hw, 0, sizeof(*hw));
    section[0] = '\0';

    f = fopen(PATH_HWCFG, "r");
    if (!f)
        return; /* Hardware info unavailable -- defaults (no mouse) will be used */

    while (fgets(line, sizeof(line), f)) {
        strip_crlf(line);
        if (line[0] == '\0' || line[0] == ';')
            continue;

        if (line[0] == '[') {
            /* Section header: extract name between [ ] */
            int i = 0;
            while (line[i+1] && line[i+1] != ']' && i < 30) {
                section[i] = line[i+1];
                i++;
            }
            section[i] = '\0';
            continue;
        }

        eq = strchr(line, '=');
        if (!eq)
            continue;

        klen = (int)(eq - line);
        if (klen <= 0 || klen >= 48)
            continue;
        memcpy(key, line, (size_t)klen);
        key[klen] = '\0';
        strcpy(val, eq + 1);

        if (strcmp(section, "MOUSE") == 0) {
            if (strcmp(key, "PRESENT") == 0)
                hw->mouse_present = parse_int(val);
            else if (strcmp(key, "BUTTONS") == 0)
                hw->mouse_buttons = (unsigned int)parse_int(val);
        }
    }

    fclose(f);
}

/* -----------------------------------------------------------------------
 * Profile file (PROFILE.DAT)
 * Returns PROF_NONE if file absent or unrecognised.
 * ----------------------------------------------------------------------- */

static int read_profile(void)
{
    FILE *f;
    char  line[16];
    int   i;

    f = fopen(PATH_PROFILE, "r");
    if (!f)
        return PROF_NONE;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return PROF_NONE;
    }
    fclose(f);
    strip_crlf(line);

    for (i = PROF_STD; i <= PROF_GAME; i++)
        if (strcmp(line, g_prof_name[i]) == 0)
            return i;

    return PROF_NONE;
}

static int write_profile(int prof)
{
    FILE *f;
    f = fopen(PATH_PROFILE, "w");
    if (!f)
        return -1;
    fprintf(f, "%s\r\n", g_prof_name[prof]);
    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * CONFIG.SYS writer
 *
 * Profile differences:
 *
 *            JEMMEX opts     CTMOUSE  FILES  BUFFERS  STACKS   ENV
 *   STD      NOEMS X=TEST    YES      40     20       9,256    512
 *   MAX      NOEMS X=TEST    NO       20     10       0,0      256
 *   EMS      X=TEST          YES      40     20       9,256    512
 *   GAME     NOEMS X=TEST    YES      20      5       0,0      256
 * ----------------------------------------------------------------------- */

typedef struct {
    const char *jemmex_opts;
    int         load_mouse;    /* non-zero to emit DEVICE=CTMOUSE line */
    int         files;
    int         buffers;
    int         stacks_n;
    int         stacks_sz;
    int         env_size;
} prof_cfg_t;

static prof_cfg_t g_profiles[] = {
    /* PROF_NONE -- unused placeholder */
    { "",                0,  0,   0, 0,   0,   0 },
    /* PROF_STD  */
    { "NOEMS X=TEST",   1, 40,  20, 9, 256, 512 },
    /* PROF_MAX  */
    { "NOEMS X=TEST",   0, 20,  10, 0,   0, 256 },
    /* PROF_EMS  */
    { "X=TEST",         1, 40,  20, 9, 256, 512 },
    /* PROF_GAME */
    { "NOEMS X=TEST",   1, 20,   5, 0,   0, 256 },
};

static int write_config_sys(int prof, const nos_hw_t *hw)
{
    FILE          *f;
    prof_cfg_t    *p;

    (void)hw; /* mouse driver disabled — hw not consulted until compatible driver available */

    if (prof < PROF_STD || prof > PROF_GAME)
        return -1;

    p = &g_profiles[prof];
    f = fopen(PATH_CONFIG_SYS, "w");
    if (!f) {
        fprintf(stderr, "[nosmem] ERROR: cannot write %s\r\n", PATH_CONFIG_SYS);
        return -1;
    }

    fprintf(f, "REM NOS-DOS CONFIG.SYS - Profile: %s\r\n",  g_prof_name[prof]);
    fprintf(f, "REM Written by NOSMEM. Do not edit by hand.\r\n");
    fprintf(f, "DOS=HIGH,UMB\r\n");
    fprintf(f, "DEVICE=C:\\NOS\\SYSTEM\\JEMMEX.EXE %s\r\n", p->jemmex_opts);

    /* CTMOUSE 2.1 disabled: uses LOCK on a register operand (LOCK SHL AX,2)
     * which is #UD on 386+ under JEMMEX V86 mode.  Mouse driver loading is
     * deferred until a compatible version is available. */

    fprintf(f, "FILES=%d\r\n",   p->files);
    fprintf(f, "BUFFERS=%d\r\n", p->buffers);

    if (p->stacks_n > 0)
        fprintf(f, "STACKS=%d,%d\r\n", p->stacks_n, p->stacks_sz);
    else
        fprintf(f, "STACKS=0,0\r\n");

    fprintf(f, "SHELL=C:\\COMMAND.COM C:\\ /P /E:%d\r\n", p->env_size);

    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static int arg_match(const char *arg, const char *sw)
{
    char buf[16];
    int i;
    if (arg[0] != '/' && arg[0] != '-')
        return 0;
    for (i = 0; arg[i+1] && i < 14; i++)
        buf[i] = (arg[i+1] >= 'a' && arg[i+1] <= 'z')
                  ? arg[i+1] - 32 : arg[i+1];
    buf[i] = '\0';
    return strcmp(buf, sw) == 0;
}

static void wait_key(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
}

static void reboot(void)
{
    union REGS r;
    int86(0x19, &r, &r);
}

static void show_help(void)
{
    printf("NOSMEM v0.1 -- NOS-DOS memory profile switcher\r\n\r\n");
    printf("Usage:\r\n");
    printf("  NOSMEM /STD      Standard profile  (620KB+ conventional)\r\n");
    printf("  NOSMEM /MAX      Maximum memory    (635KB+ conventional)\r\n");
    printf("  NOSMEM /EMS      EMS enabled       (588KB+ + EMS pool)\r\n");
    printf("  NOSMEM /GAME     Game profile      (630KB+ conventional)\r\n");
    printf("  NOSMEM /STATUS   Show current profile\r\n");
    printf("  NOSMEM /NOREBOOT Apply without rebooting\r\n");
    printf("  NOSMEM /?        Show this help\r\n");
    printf("\r\nProfiles rewrite C:\\CONFIG.SYS. Reboot required to take effect.\r\n");
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int      target_prof  = PROF_NONE;
    int      flag_noreboot = 0;
    int      flag_status   = 0;
    int      i, rc;
    int      cur_prof;
    nos_hw_t hw;

    for (i = 1; i < argc; i++) {
        if (arg_match(argv[i], "?") || arg_match(argv[i], "HELP")) {
            show_help();
            return 0;
        } else if (arg_match(argv[i], "STD"))      { target_prof = PROF_STD; }
        else if (arg_match(argv[i], "MAX"))         { target_prof = PROF_MAX; }
        else if (arg_match(argv[i], "EMS"))         { target_prof = PROF_EMS; }
        else if (arg_match(argv[i], "GAME"))        { target_prof = PROF_GAME; }
        else if (arg_match(argv[i], "STATUS"))      { flag_status = 1; }
        else if (arg_match(argv[i], "NOREBOOT"))    { flag_noreboot = 1; }
        else {
            fprintf(stderr, "Unknown argument: %s\r\n", argv[i]);
            fprintf(stderr, "Run NOSMEM /? for help.\r\n");
            return 2;
        }
    }

    /* /STATUS: just show current profile and exit */
    if (flag_status || (target_prof == PROF_NONE && !flag_status)) {
        cur_prof = read_profile();
        if (cur_prof == PROF_NONE)
            printf("Current profile: unknown (PROFILE.DAT not found)\r\n");
        else
            printf("Current profile: %s\r\n", g_prof_name[cur_prof]);

        if (target_prof == PROF_NONE)
            return 0; /* status-only run, no profile specified */
    }

    /* Apply profile */
    read_hwcfg(&hw);

    printf("NOSMEM: switching to %s profile...\r\n", g_prof_name[target_prof]);

    rc = write_config_sys(target_prof, &hw);
    if (rc != 0) {
        printf("ERROR: failed to write %s\r\n", PATH_CONFIG_SYS);
        return 1;
    }

    rc = write_profile(target_prof);
    if (rc != 0)
        printf("WARNING: could not update PROFILE.DAT\r\n");

    printf("CONFIG.SYS updated for %s profile.\r\n", g_prof_name[target_prof]);

    if (flag_noreboot) {
        printf("(/NOREBOOT -- reboot manually to apply changes)\r\n");
        return 0;
    }

    printf("Press any key to reboot...\r\n");
    wait_key();
    reboot();

    return 0;
}
