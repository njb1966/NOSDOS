/* NOS-DOS: NOS-DETECT
 * detect.c - First-boot hardware detection orchestrator.
 *
 * Calls each detection module in turn, displays a summary, generates
 * CONFIG.SYS / AUTOEXEC.BAT / NOS-HW.CFG via genconf.c, then reboots.
 *
 * Usage:
 *   DETECT.EXE              Normal first-boot run; reboots on success.
 *   DETECT.EXE /NOREBOOT    Write configs but do not reboot (for testing).
 *   DETECT.EXE /FORCE       Re-detect even if NOS-HW.CFG already exists.
 *   DETECT.EXE /?           Show help.
 *
 * Exit codes:
 *   0  Success (or /NOREBOOT path)
 *   1  Config generation failed
 *   2  Bad argument
 *
 * Template and output paths are the fixed NOS-DOS layout on C:.
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include "memory.h"
#include "video.h"
#include "mouse.h"
#include "sound.h"
#include "network.h"
#include "genconf.h"

#include <dos.h>    /* int86 for reboot and keypress */
#include <stdio.h>  /* printf, fprintf, fopen, fclose */
#include <string.h> /* strcmp, strupr */
#include <stdlib.h> /* exit */

/* Fixed NOS-DOS directory layout on C: */
#define PATH_CONFIG_TPL    "C:\\NOS\\SYSTEM\\CONFIG.TPL"
#define PATH_AUTOEXEC_TPL  "C:\\NOS\\SYSTEM\\AUTOEXEC.TPL"
#define PATH_CONFIG_OUT    "C:\\CONFIG.SYS"
#define PATH_AUTOEXEC_OUT  "C:\\AUTOEXEC.BAT"
#define PATH_HWCFG_OUT     "C:\\NOS\\SYSTEM\\NOS-HW.CFG"

#define NOS_DETECT_VERSION "0.1"

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static void print_sep(void)
{
    printf("------------------------------------------------------------\r\n");
}

static void print_banner(void)
{
    printf("============================================================\r\n");
    printf("  NOS-DOS Hardware Detection v%s\r\n", NOS_DETECT_VERSION);
    printf("============================================================\r\n");
    printf("\r\n");
}

static void wait_key(void)
{
    union REGS r;
    r.h.ah = 0x00; /* BIOS keyboard read — blocks until keypress */
    int86(0x16, &r, &r);
}

static void reboot(void)
{
    union REGS r;
    /* INT 19h: Bootstrap Loader — warm-ish reboot without POST */
    int86(0x19, &r, &r);
    /* Should not return; if it does, fall through to exit */
}

/* Returns non-zero if NOS-HW.CFG already exists on disk. */
static int hwcfg_exists(void)
{
    FILE *f = fopen(PATH_HWCFG_OUT, "r");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* Case-insensitive argument match against /SWITCH or -SWITCH. */
static int arg_match(const char *arg, const char *sw)
{
    char buf[32];
    int i;
    if (arg[0] != '/' && arg[0] != '-')
        return 0;
    for (i = 0; arg[i+1] && i < 30; i++)
        buf[i] = (arg[i+1] >= 'a' && arg[i+1] <= 'z')
                  ? arg[i+1] - 32 : arg[i+1];
    buf[i] = '\0';
    return strcmp(buf, sw) == 0;
}

static void show_help(void)
{
    printf("NOS-DETECT v%s -- NOS-DOS first-boot hardware detection\r\n\r\n",
           NOS_DETECT_VERSION);
    printf("Usage:\r\n");
    printf("  DETECT.EXE           Detect hardware, write configs, reboot\r\n");
    printf("  DETECT.EXE /NOREBOOT Write configs but do not reboot\r\n");
    printf("  DETECT.EXE /FORCE    Re-detect even if NOS-HW.CFG exists\r\n");
    printf("  DETECT.EXE /?        Show this help\r\n");
}

/* -----------------------------------------------------------------------
 * Detection summary display
 * ----------------------------------------------------------------------- */

static void show_memory(const nos_meminfo_t *m)
{
    printf("  Memory\r\n");
    printf("    Conventional : %u KB\r\n", m->conv_kb);

    if (m->xms_present)
        printf("    XMS          : %u KB  (largest block: %u KB)\r\n",
               m->xms_total_kb, m->xms_largest_kb);
    else
        printf("    XMS          : not present\r\n");

    if (m->ems_present)
        printf("    EMS          : %u KB total  %u KB free\r\n",
               m->ems_total_kb, m->ems_free_kb);
    else
        printf("    EMS          : not present\r\n");
}

static void show_video(const nos_videoinfo_t *v)
{
    printf("  Video\r\n");
    if (v->vga_present)
        printf("    Adapter      : VGA  DCC=%02Xh\r\n",
               (unsigned)v->display_code);
    else
        printf("    Adapter      : pre-VGA or unknown\r\n");

    if (v->vesa_present)
        printf("    VESA         : yes  version %u.%u\r\n",
               (v->vesa_version >> 8) & 0xFF, v->vesa_version & 0xFF);
    else
        printf("    VESA         : not present\r\n");

    printf("    Current mode : %02Xh  cols: %u\r\n",
           (unsigned)v->current_mode, (unsigned)v->text_cols);
}

static void show_mouse(const nos_mouseinfo_t *m)
{
    printf("  Mouse\r\n");
    if (!m->present) {
        printf("    Status       : not present\r\n");
        return;
    }
    printf("    Buttons      : %u\r\n", m->buttons);
    printf("    Type         : %u  IRQ: %u  driver: v%u.%u\r\n",
           (unsigned)m->mouse_type, (unsigned)m->irq,
           (unsigned)m->driver_major, (unsigned)m->driver_minor);
}

static void show_sound(const nos_soundinfo_t *s)
{
    printf("  Sound\r\n");
    if (!s->present) {
        printf("    Status       : not present\r\n");
        return;
    }
    printf("    Port         : %03Xh%s\r\n",
           (unsigned)s->port, s->from_env ? "  (from BLASTER)" : "  (probed)");
    if (s->irq > 0)
        printf("    IRQ: %u  DMA: %u/%u  Type: %u\r\n",
               (unsigned)s->irq, (unsigned)s->dma_low,
               (unsigned)s->dma_high, (unsigned)s->card_type);
}

static void show_network(const nos_netinfo_t *n)
{
    printf("  Network\r\n");
    if (n->present)
        printf("    Packet driver: INT %02Xh\r\n", (unsigned)n->intr);
    else
        printf("    Status       : no packet driver (INT 60h-80h)\r\n");
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int flag_noreboot = 0;
    int flag_force    = 0;
    int i, rc;

    nos_meminfo_t mem;
    nos_videoinfo_t vid;
    nos_mouseinfo_t mou;
    nos_soundinfo_t snd;
    nos_netinfo_t   net;

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (arg_match(argv[i], "?") || arg_match(argv[i], "H") ||
                arg_match(argv[i], "HELP")) {
            show_help();
            return 0;
        } else if (arg_match(argv[i], "NOREBOOT")) {
            flag_noreboot = 1;
        } else if (arg_match(argv[i], "FORCE")) {
            flag_force = 1;
        } else {
            fprintf(stderr, "Unknown argument: %s\r\n", argv[i]);
            fprintf(stderr, "Run DETECT /? for help.\r\n");
            return 2;
        }
    }

    print_banner();

    /* First-boot guard: skip if NOS-HW.CFG already exists */
    if (!flag_force && hwcfg_exists()) {
        printf("NOS-HW.CFG already present -- system already configured.\r\n");
        printf("Use /FORCE to re-detect hardware.\r\n");
        return 0;
    }

    printf("  Detecting hardware...\r\n\r\n");

    /* Run detection modules */
    printf("  [ Memory  ]\r\n");
    nos_detect_memory(&mem);
    show_memory(&mem);
    printf("\r\n");

    printf("  [ Video   ]\r\n");
    nos_detect_video(&vid);
    show_video(&vid);
    printf("\r\n");

    printf("  [ Mouse   ]\r\n");
    nos_detect_mouse(&mou);
    show_mouse(&mou);
    printf("\r\n");

    printf("  [ Sound   ]\r\n");
    nos_detect_sound(&snd);
    show_sound(&snd);
    printf("\r\n");

    printf("  [ Network ]\r\n");
    nos_detect_network(&net);
    show_network(&net);
    printf("\r\n");

    /* Memory check: warn if below 580KB (something loaded before us) */
    if (mem.conv_kb > 0 && mem.conv_kb < 580)
        printf("  WARNING: Low conventional memory (%u KB). "
               "Drivers may be loaded.\r\n\r\n", mem.conv_kb);

    /* Generate configuration files */
    print_sep();
    printf("  Writing configuration files...\r\n");

    rc = nos_genconf(
        &mem, &vid, &mou, &snd, &net,
        PATH_CONFIG_TPL,
        PATH_AUTOEXEC_TPL,
        PATH_CONFIG_OUT,
        PATH_AUTOEXEC_OUT,
        PATH_HWCFG_OUT
    );

    if (rc != 0) {
        printf("\r\n  ERROR: Configuration generation failed (rc=%d)\r\n", rc);
        printf("  Check that C:\\NOS\\SYSTEM\\ exists and templates are present.\r\n");
        print_sep();
        return 1;
    }

    printf("    CONFIG.SYS   : OK\r\n");
    printf("    AUTOEXEC.BAT : OK\r\n");
    printf("    NOS-HW.CFG   : OK\r\n");
    print_sep();

    /* Done */
    if (flag_noreboot) {
        printf("\r\n  Detection complete (/NOREBOOT -- skipping reboot).\r\n");
        return 0;
    }

    printf("\r\n  Detection complete. System will reboot.\r\n");
    printf("  Press any key to reboot...\r\n");
    wait_key();
    reboot();

    return 0; /* unreachable */
}
