/* NOS-DOS: NOS-DETECT
 * genconf.c - Generate CONFIG.SYS, AUTOEXEC.BAT, and NOS-HW.CFG.
 *
 * Template engine:
 *   Reads each template file line by line.  For each line, replaces every
 *   occurrence of {{KEY}} with the computed value for KEY.  Unknown keys
 *   are silently replaced with an empty string.  At most one substitution
 *   per line is expected (templates are designed with this constraint).
 *
 * Template variables:
 *   {{JEMMEX_OPTS}}     JEMMEX.EXE command-line options (always NOEMS for
 *                       STD profile; EMS profile handled by nosmem.c later)
 *   {{MOUSE_LINE}}      Full DEVICE= line for CTMOUSE, or empty string
 *   {{BLASTER_LINE}}    SET BLASTER= line if SB card found, or empty string
 *   {{PKT_DRIVER_LINE}} REM noting packet driver INT if found, or empty
 *
 * NOS-HW.CFG:
 *   Written directly (no template) as a simple INI file.  This is the
 *   authoritative hardware record read by nosmem.c, NNET, and NOSPLAY.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include "genconf.h"

#include <stdio.h>   /* fopen, fclose, fgets, fputs, fprintf */
#include <string.h>  /* strcpy, strcat, strlen, strcmp, memcpy */
#include <stdlib.h>  /* sprintf (via stdio) */

/* -----------------------------------------------------------------------
 * Substitution table
 * ----------------------------------------------------------------------- */

#define MAX_SUBST    8
#define MAX_KEY_LEN  32
#define MAX_VAL_LEN  192   /* PKT_DRIVER_LINE expands to 4 lines ~140 chars */
#define MAX_LINE_LEN 256

typedef struct {
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];
} subst_t;

static int find_subst(const subst_t *tbl, int n, const char *key)
{
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(tbl[i].key, key) == 0)
            return i;
    return -1;
}

/* Replace all {{KEY}} occurrences in 'in', write result to 'out'.
 * 'outsz' is the size of the out buffer.  Truncates silently if needed. */
static void substitute_line(const char *in, char *out, int outsz,
                              const subst_t *tbl, int n)
{
    const char *p   = in;
    char       *q   = out;
    char       *end = out + outsz - 1;
    static char key[MAX_KEY_LEN];
    int         ki, idx;
    const char *v;
    int         vlen;

    while (*p && q < end) {
        if (p[0] == '{' && p[1] == '{') {
            p += 2;
            ki = 0;
            while (*p && !(p[0] == '}' && p[1] == '}')) {
                if (ki < MAX_KEY_LEN - 1)
                    key[ki++] = *p;
                p++;
            }
            key[ki] = '\0';
            if (p[0] == '}' && p[1] == '}')
                p += 2;

            idx = find_subst(tbl, n, key);
            if (idx >= 0) {
                v    = tbl[idx].val;
                vlen = (int)strlen(v);
                if (q + vlen < end) {
                    memcpy(q, v, (size_t)vlen);
                    q += vlen;
                }
            }
            /* Unknown key → substitute nothing */
        } else {
            *q++ = *p++;
        }
    }
    *q = '\0';
}

/* -----------------------------------------------------------------------
 * Template processor
 * ----------------------------------------------------------------------- */

static int process_template(const char *tpl_path, const char *out_path,
                              const subst_t *tbl, int n)
{
    FILE *fin;
    FILE *fout;
    static char in_line[MAX_LINE_LEN];
    static char out_line[MAX_LINE_LEN + MAX_VAL_LEN]; /* headroom for expansion */

    fin = fopen(tpl_path, "r");
    if (!fin) {
        fprintf(stderr, "[genconf] ERROR: cannot open template: %s\r\n",
                tpl_path);
        return -1;
    }

    fout = fopen(out_path, "w");
    if (!fout) {
        fprintf(stderr, "[genconf] ERROR: cannot write output: %s\r\n",
                out_path);
        fclose(fin);
        return -1;
    }

    while (fgets(in_line, sizeof(in_line), fin)) {
        substitute_line(in_line, out_line, sizeof(out_line), tbl, n);
        fputs(out_line, fout);
    }

    fclose(fin);
    fclose(fout);
    return 0;
}

/* -----------------------------------------------------------------------
 * Substitution value builders
 * ----------------------------------------------------------------------- */

/* JEMMEX_OPTS: STD profile always uses NOEMS (EMS disabled).
 * nosmem.c rewrites CONFIG.SYS when the user switches to an EMS profile. */
static void build_jemmex_opts(char *val)
{
    strcpy(val, "NOEMS X=TEST");
}

/* MOUSE_LINE: Load CTMOUSE with /P (PS/2 forced) when mouse hardware is
 * detected.  /P skips serial port probing, which is all we need in a VM.
 * Empty string when no mouse is present. */
static void build_mouse_line(const nos_mouseinfo_t *mou, char *val)
{
    if (mou->present)
        strcpy(val, "C:\\NOS\\SYSTEM\\CTMOUSE.EXE /P");
    else
        val[0] = '\0';
}

/* BLASTER_LINE: SET BLASTER= if card found, empty otherwise.
 * Emits a full line when from_env (all fields known) or port-only when
 * found by blind probe (IRQ and DMA unknown). */
static void build_blaster_line(const nos_soundinfo_t *snd, char *val)
{
    char tmp[MAX_VAL_LEN];

    if (!snd->present) {
        val[0] = '\0';
        return;
    }

    if (snd->from_env && snd->irq > 0) {
        /* Full BLASTER string from parsed env var */
        sprintf(tmp, "SET BLASTER=A%X I%u D%u H%u T%u",
                (unsigned)snd->port,
                (unsigned)snd->irq,
                (unsigned)snd->dma_low,
                (unsigned)snd->dma_high,
                (unsigned)snd->card_type);
    } else {
        /* Port only — IRQ/DMA unknown from blind probe */
        sprintf(tmp, "SET BLASTER=A%X", (unsigned)snd->port);
    }

    if (strlen(tmp) < MAX_VAL_LEN)
        strcpy(val, tmp);
    else
        val[0] = '\0'; /* safety: truncate to empty rather than overflow */
}

/* PKT_DRIVER_LINE: when network present, configure mTCP and obtain an IP.
 * Three lines embedded in the substitution value so the template needs only
 * one {{PKT_DRIVER_LINE}} marker.  The trailing CR+LF between lines is part
 * of the value; the template line's own CR+LF terminates the last line.
 * The packet driver load (PCNTPK.COM) is written unconditionally in
 * AUTOEXEC.TPL — this substitution only adds the mTCP config when a driver
 * was detected.  When no packet driver is found, emit an empty string so the
 * three mTCP lines disappear completely from the generated AUTOEXEC.BAT. */
static void build_pkt_driver_line(const nos_netinfo_t *net, char *val)
{
    char tmp[MAX_VAL_LEN];

    if (!net->present) {
        val[0] = '\0';
        return;
    }

    /* Three-line block (PCNTPK.COM load is in the template, not here):
     *   SET MTCPCFG=C:\NOS\SYSTEM\MTCP.CFG
     *   C:\NOS\SYSTEM\DHCP.EXE >NUL
     *   C:\NOS\SYSTEM\SNTP.EXE >NUL      <- template line CR+LF terminates this */
    sprintf(tmp,
            "SET MTCPCFG=C:\\NOS\\SYSTEM\\MTCP.CFG\r\n"
            "C:\\NOS\\SYSTEM\\DHCP.EXE >NUL\r\n"
            "C:\\NOS\\SYSTEM\\SNTP.EXE >NUL");

    if (strlen(tmp) < MAX_VAL_LEN)
        strcpy(val, tmp);
    else
        val[0] = '\0';
}

/* Write a minimal MTCP.CFG so mTCP tools know which packet driver INT to use.
 * DHCP.EXE will add IPADDR/NETMASK/GATEWAY/NAMESERVER on first run.
 * Path is fixed: C:\NOS\SYSTEM\MTCP.CFG */
static void write_mtcpcfg(const nos_netinfo_t *net)
{
    FILE *f;

    if (!net->present)
        return;

    f = fopen("C:\\NOS\\SYSTEM\\MTCP.CFG", "w");
    if (!f)
        return;

    fprintf(f, "PACKETINT 0x%02X\r\n", (unsigned)net->intr);
    fprintf(f, "HOSTNAME NOS-DOS\r\n");
    fclose(f);
}

/* -----------------------------------------------------------------------
 * NOS-HW.CFG writer
 *
 * Simple INI format, no template needed.  Fields mirror the detection
 * structs exactly so other components can parse this file at runtime.
 * ----------------------------------------------------------------------- */

static int write_hwcfg(const char *path,
                        const nos_meminfo_t   *mem,
                        const nos_videoinfo_t *vid,
                        const nos_mouseinfo_t *mou,
                        const nos_soundinfo_t *snd,
                        const nos_netinfo_t   *net)
{
    FILE *f;

    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[genconf] ERROR: cannot write NOS-HW.CFG: %s\r\n",
                path);
        return -1;
    }

    fprintf(f, "[MEMORY]\r\n");
    fprintf(f, "CONV=%u\r\n",         mem->conv_kb);
    fprintf(f, "XMS_PRESENT=%d\r\n",  mem->xms_present);
    fprintf(f, "XMS_TOTAL=%u\r\n",    mem->xms_total_kb);
    fprintf(f, "XMS_LARGEST=%u\r\n",  mem->xms_largest_kb);
    fprintf(f, "EMS_PRESENT=%d\r\n",  mem->ems_present);
    fprintf(f, "EMS_TOTAL=%u\r\n",    mem->ems_total_kb);
    fprintf(f, "EMS_FREE=%u\r\n",     mem->ems_free_kb);
    fprintf(f, "\r\n");

    fprintf(f, "[VIDEO]\r\n");
    fprintf(f, "VGA=%d\r\n",          vid->vga_present);
    fprintf(f, "VESA=%d\r\n",         vid->vesa_present);
    fprintf(f, "VESA_VER=%04X\r\n",   vid->vesa_version);
    fprintf(f, "MODE=%02X\r\n",       (unsigned)vid->current_mode);
    fprintf(f, "COLS=%u\r\n",         (unsigned)vid->text_cols);
    fprintf(f, "DCC=%02X\r\n",        (unsigned)vid->display_code);
    fprintf(f, "\r\n");

    fprintf(f, "[MOUSE]\r\n");
    fprintf(f, "PRESENT=%d\r\n",      mou->present);
    fprintf(f, "BUTTONS=%u\r\n",      mou->buttons);
    fprintf(f, "TYPE=%u\r\n",         (unsigned)mou->mouse_type);
    fprintf(f, "IRQ=%u\r\n",          (unsigned)mou->irq);
    fprintf(f, "\r\n");

    fprintf(f, "[SOUND]\r\n");
    fprintf(f, "PRESENT=%d\r\n",      snd->present);
    fprintf(f, "PORT=%03X\r\n",       (unsigned)snd->port);
    fprintf(f, "IRQ=%u\r\n",          (unsigned)snd->irq);
    fprintf(f, "DMA=%u\r\n",          (unsigned)snd->dma_low);
    fprintf(f, "HDMA=%u\r\n",         (unsigned)snd->dma_high);
    fprintf(f, "TYPE=%u\r\n",         (unsigned)snd->card_type);
    fprintf(f, "\r\n");

    fprintf(f, "[NETWORK]\r\n");
    fprintf(f, "PRESENT=%d\r\n",      net->present);
    fprintf(f, "INT=%02X\r\n",        (unsigned)net->intr);
    fprintf(f, "\r\n");

    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

int nos_genconf(
    const nos_meminfo_t   *mem,
    const nos_videoinfo_t *vid,
    const nos_mouseinfo_t *mou,
    const nos_soundinfo_t *snd,
    const nos_netinfo_t   *net,
    const char *config_tpl,
    const char *autoexec_tpl,
    const char *config_out,
    const char *autoexec_out,
    const char *hwcfg_out)
{
    static subst_t tbl[MAX_SUBST];
    int            n = 0;
    int            rc;

    /* Build substitution table */
    strcpy(tbl[n].key, "JEMMEX_OPTS");
    build_jemmex_opts(tbl[n].val);
    n++;

    strcpy(tbl[n].key, "MOUSE_LINE");
    build_mouse_line(mou, tbl[n].val);
    n++;

    strcpy(tbl[n].key, "BLASTER_LINE");
    build_blaster_line(snd, tbl[n].val);
    n++;

    strcpy(tbl[n].key, "PKT_DRIVER_LINE");
    build_pkt_driver_line(net, tbl[n].val);
    n++;

    /* Process templates */
    rc = process_template(config_tpl, config_out, tbl, n);
    if (rc != 0)
        return rc;

    rc = process_template(autoexec_tpl, autoexec_out, tbl, n);
    if (rc != 0)
        return rc;

    /* Write hardware profile */
    rc = write_hwcfg(hwcfg_out, mem, vid, mou, snd, net);
    if (rc != 0)
        return rc;

    /* Write minimal MTCP.CFG when a packet driver was detected */
    write_mtcpcfg(net);

    return 0;
}
