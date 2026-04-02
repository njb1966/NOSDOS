/* NOS-DOS: NOS-THROTTLE
 * tctl.c - Throttle control utility.
 *
 * Locates the THROTTLE TSR in memory (via INT 08h vector + signature check),
 * then reads or writes its shared data area directly using far pointers.
 *
 * Shared data layout (CS-relative offsets in THROTTLE.COM):
 *   0x100: signature "THROTTLE" (8 bytes)
 *   0x108: speed_level  (word)
 *   0x10A: delay_count  (word)
 *   0x10C: presets[6]   (word * 6 = 12 bytes)
 *   0x118: old_int08    (dword)
 *   0x11C: old_int09    (dword)
 *
 * Commands:
 *   TCTL STATUS            show current throttle level
 *   TCTL SET <preset>      set level by name (OFF SLOW100 SLOW66 SLOW33
 *                          SLOW10 SLOW477) or number (0-5)
 *   TCTL CALIBRATE         measure timer tick, compute and write presets
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include <dos.h>      /* _dos_getvect, int86, union REGS, MK_FP */
#include <i86.h>      /* MK_FP (Open Watcom) */
#include <stdio.h>    /* printf */
#include <string.h>   /* strcmp, strncmp, strcpy */
#include <stdlib.h>   /* atoi */
#include <ctype.h>    /* toupper */

/* -----------------------------------------------------------------------
 * Offsets into THROTTLE.COM resident segment (see throttle.asm)
 * ----------------------------------------------------------------------- */

#define THROTTLE_SIG_OFF    0x100
#define THROTTLE_LEVEL_OFF  0x108
#define THROTTLE_COUNT_OFF  0x10A
#define THROTTLE_PSET_OFF   0x10C    /* presets[6], each a word */

/* -----------------------------------------------------------------------
 * Preset table
 * ----------------------------------------------------------------------- */

static const char *preset_names[6] = {
    "OFF", "SLOW100", "SLOW66", "SLOW33", "SLOW10", "SLOW477"
};

/* -----------------------------------------------------------------------
 * find_throttle_seg
 *
 * Reads the INT 08h vector and checks if the handler's segment starts with
 * the "THROTTLE" signature at offset 0x100.
 * Returns the segment value, or 0 if not found.
 * ----------------------------------------------------------------------- */

static unsigned int find_throttle_seg(void)
{
    void (far *vec)(void);
    unsigned int seg;
    char far    *sig;

    vec = (void (far *)(void))_dos_getvect(0x08);
    seg = FP_SEG(vec);
    sig = (char far *)MK_FP(seg, THROTTLE_SIG_OFF);
    if (strncmp(sig, "THROTTLE", 8) == 0)
        return seg;
    return 0;
}

/* -----------------------------------------------------------------------
 * read/write helpers
 * ----------------------------------------------------------------------- */

static unsigned int thr_get_level(unsigned int seg)
{
    unsigned int far *p = (unsigned int far *)MK_FP(seg, THROTTLE_LEVEL_OFF);
    return *p;
}

static void thr_set_level(unsigned int seg, unsigned int level)
{
    unsigned int far *lvl  = (unsigned int far *)MK_FP(seg, THROTTLE_LEVEL_OFF);
    unsigned int far *cnt  = (unsigned int far *)MK_FP(seg, THROTTLE_COUNT_OFF);
    unsigned int far *pset = (unsigned int far *)MK_FP(seg, THROTTLE_PSET_OFF);
    *lvl = level;
    *cnt = pset[level];
}

static void thr_set_preset(unsigned int seg, unsigned int level, unsigned int val)
{
    unsigned int far *pset = (unsigned int far *)MK_FP(seg, THROTTLE_PSET_OFF);
    pset[level] = val;
}

static unsigned int thr_get_preset(unsigned int seg, unsigned int level)
{
    unsigned int far *pset = (unsigned int far *)MK_FP(seg, THROTTLE_PSET_OFF);
    return pset[level];
}

/* -----------------------------------------------------------------------
 * cmd_status
 * ----------------------------------------------------------------------- */

static void cmd_status(void)
{
    unsigned int seg, level;
    int i;

    seg = find_throttle_seg();
    if (!seg) {
        printf("THROTTLE: not installed.\r\n");
        return;
    }
    level = thr_get_level(seg);
    printf("THROTTLE: level %u = %s\r\n", level,
           level <= 5 ? preset_names[level] : "?");
    printf("Preset counts:\r\n");
    for (i = 0; i < 6; i++)
        printf("  %u %-8s  %u\r\n", i, preset_names[i],
               thr_get_preset(seg, (unsigned int)i));
}

/* -----------------------------------------------------------------------
 * cmd_set
 * ----------------------------------------------------------------------- */

static void cmd_set(const char *arg)
{
    unsigned int seg, level;
    int i;
    char upper[12];

    seg = find_throttle_seg();
    if (!seg) {
        printf("THROTTLE: not installed.  Run THROTTLE first.\r\n");
        return;
    }

    if (!arg || !*arg) {
        printf("Usage: TCTL SET <OFF|SLOW100|SLOW66|SLOW33|SLOW10|SLOW477|0-5>\r\n");
        return;
    }

    /* Try numeric 0-5 first. */
    if (arg[0] >= '0' && arg[0] <= '5' && arg[1] == '\0') {
        level = (unsigned int)(arg[0] - '0');
        thr_set_level(seg, level);
        printf("THROTTLE: set to level %u (%s)\r\n", level, preset_names[level]);
        return;
    }

    /* Uppercase and match name. */
    for (i = 0; arg[i] && i < 11; i++)
        upper[i] = (char)toupper((unsigned char)arg[i]);
    upper[i] = '\0';

    for (i = 0; i < 6; i++) {
        if (strcmp(upper, preset_names[i]) == 0) {
            thr_set_level(seg, (unsigned int)i);
            printf("THROTTLE: set to level %d (%s)\r\n", i, preset_names[i]);
            return;
        }
    }

    printf("TCTL: unknown preset '%s'\r\n", arg);
}

/* -----------------------------------------------------------------------
 * cmd_calibrate
 *
 * Measures how many outer-loop iterations (of 1024 inner NOPs) can execute
 * in one timer tick (54.9 ms at 18.2 Hz).
 *
 * Method:
 *   1. Read tick counter via INT 1Ah AH=00h; wait for it to change.
 *   2. Run outer loop (counting) until the next tick.
 *   3. max_count = iterations per tick.
 *   4. Preset N wastes (fraction_N * max_count) iterations:
 *        OFF      = 0
 *        SLOW100  = 0
 *        SLOW66   = max * 0.33
 *        SLOW33   = max * 0.67
 *        SLOW10   = max * 0.90
 *        SLOW477  = max * 0.993
 * ----------------------------------------------------------------------- */

static void cmd_calibrate(void)
{
    union REGS     r;
    unsigned long  tick_start, tick_end;
    unsigned long  count;
    unsigned int   seg;
    unsigned long  max_c;

    seg = find_throttle_seg();
    if (!seg) {
        printf("THROTTLE: not installed.  Run THROTTLE first.\r\n");
        return;
    }

    printf("Calibrating... (takes ~0.1 s)\r\n");

    /* Read current tick low word, wait for it to change. */
    r.h.ah = 0x00;
    int86(0x1A, &r, &r);
    tick_start = (unsigned long)r.x.dx;

    do {
        r.h.ah = 0x00;
        int86(0x1A, &r, &r);
        tick_end = (unsigned long)r.x.dx;
    } while (tick_end == tick_start);

    /* Now count outer iterations in exactly one tick. */
    tick_start = tick_end;
    count = 0;
    for (;;) {
        unsigned int inner = 1024;
        /* inner loop -- mirrors the resident loop body */
        while (inner--) { /* empty */ }
        count++;
        r.h.ah = 0x00;
        int86(0x1A, &r, &r);
        if ((unsigned long)r.x.dx != tick_start)
            break;
    }

    max_c = count;
    printf("  %lu outer iterations per timer tick.\r\n", max_c);

    /* Write calibrated presets. */
    thr_set_preset(seg, 0, 0);                            /* OFF      */
    thr_set_preset(seg, 1, 0);                            /* SLOW100  */
    thr_set_preset(seg, 2, (unsigned int)(max_c * 33 / 100));  /* SLOW66   */
    thr_set_preset(seg, 3, (unsigned int)(max_c * 67 / 100));  /* SLOW33   */
    thr_set_preset(seg, 4, (unsigned int)(max_c * 90 / 100));  /* SLOW10   */
    thr_set_preset(seg, 5, (unsigned int)(max_c * 99 / 100));  /* SLOW477  */

    /* Clamp to word max (unlikely but safe). */
    if (max_c * 99 / 100 > 65535UL)
        thr_set_preset(seg, 5, 65535);

    /* Re-apply current level with new preset. */
    thr_set_level(seg, thr_get_level(seg));

    printf("Calibration complete.  New presets:\r\n");
    {
        int i;
        for (i = 0; i < 6; i++)
            printf("  %-8s  %u\r\n", preset_names[i],
                   thr_get_preset(seg, (unsigned int)i));
    }
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int  i;
    char cmd[16];

    if (argc < 2) {
        printf("TCTL - Throttle control for THROTTLE.COM\r\n");
        printf("Usage: TCTL <STATUS|SET <preset>|CALIBRATE>\r\n");
        printf("Presets: OFF  SLOW100  SLOW66  SLOW33  SLOW10  SLOW477\r\n");
        return 0;
    }

    for (i = 0; argv[1][i] && i < 15; i++)
        cmd[i] = (char)toupper((unsigned char)argv[1][i]);
    cmd[i] = '\0';

    if (strcmp(cmd, "STATUS")    == 0) { cmd_status(); return 0; }
    if (strcmp(cmd, "SET")       == 0) { cmd_set(argc >= 3 ? argv[2] : NULL); return 0; }
    if (strcmp(cmd, "CALIBRATE") == 0) { cmd_calibrate(); return 0; }

    printf("TCTL: unknown command '%s'\r\n", argv[1]);
    return 1;
}
