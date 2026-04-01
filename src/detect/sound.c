/* NOS-DOS: NOS-DETECT
 * sound.c - Detect Sound Blaster compatible audio hardware.
 *
 * Detection strategy (two stages):
 *
 *   Stage 1 — Environment variable
 *     Read the BLASTER variable (set by the SB driver or AUTOEXEC.BAT).
 *     Parse A (hex port), I (IRQ), D (8-bit DMA), H (16-bit DMA), T (type).
 *     Then hardware-verify the port via a DSP reset probe (stage 2).
 *
 *   Stage 2 — DSP reset probe
 *     Write 1 then 0 to port+06h to reset the DSP.
 *     Poll port+0Eh (Data Available, bit 7) until the DSP signals ready.
 *     Read port+0Ah: the DSP returns 0AAh when initialised successfully.
 *     If stage 1 succeeded but the probe fails the env var is stale or
 *     the driver is not yet loaded; we report not present.
 *     If stage 1 was skipped, probe the four standard base addresses:
 *     220h, 240h, 260h, 280h.
 *
 * I/O port access: Open Watcom inp()/outp() from <conio.h>.
 * Delay after DSP reset assert: read port 80h three times (~1µs each).
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include "sound.h"

#include <conio.h>   /* inp, outp                        */
#include <ctype.h>   /* isdigit, isxdigit, toupper       */
#include <stdlib.h>  /* getenv                           */
#include <string.h>  /* memset                           */

/* Standard SB base I/O ports to probe when BLASTER is absent */
static unsigned int g_probe_ports[] = { 0x220, 0x240, 0x260, 0x280 };
#define NUM_PROBE_PORTS 4

/* -----------------------------------------------------------------------
 * DSP reset probe
 *
 * Returns 1 if an SB-compatible DSP is present at 'port', 0 otherwise.
 *
 * Timing notes:
 *   The SB hardware datasheet specifies >= 3µs between asserting and
 *   deasserting the reset line.  Reading port 80h three times gives
 *   ~3µs on typical ISA bus speeds without requiring a timer.
 *   The ready poll is bounded at 256 iterations to avoid an infinite
 *   loop on a port with floating bus lines.
 * ----------------------------------------------------------------------- */
static int sb_probe(unsigned int port)
{
    int i;
    unsigned char status;

    /* Assert DSP reset */
    outp(port + 0x06, 0x01);

    /* ~3µs delay via three reads of the POST diagnostic port */
    inp(0x80); inp(0x80); inp(0x80);

    /* Deassert DSP reset */
    outp(port + 0x06, 0x00);

    /* Poll Data Available (port+0Eh bit 7) up to 256 times */
    for (i = 0; i < 256; i++) {
        status = inp(port + 0x0E);
        if (status & 0x80) {
            /* Data ready — read and verify the DSP ready byte */
            return (inp(port + 0x0A) == 0xAA) ? 1 : 0;
        }
    }

    return 0; /* timeout — no DSP response */
}

/* -----------------------------------------------------------------------
 * BLASTER environment variable parser
 *
 * Format: "A220 I5 D1 H5 P330 T6"  (fields may be in any order,
 *          space-separated, uppercase or lowercase letters).
 *
 * A = base port (hexadecimal)
 * I = IRQ        (decimal)
 * D = 8-bit DMA  (decimal)
 * H = 16-bit DMA (decimal, SB16 only)
 * T = card type  (decimal, NOS_SND_TYPE_* values)
 * P = MPU-401 port (decimal, ignored here)
 *
 * Returns 1 if at least the A field was parsed, 0 if BLASTER is unset
 * or contains no recognisable fields.
 * ----------------------------------------------------------------------- */
static unsigned int parse_hex(const char **pp)
{
    unsigned int val = 0;
    unsigned char c;
    const char *p = *pp;

    while (isxdigit(*p)) {
        c = (unsigned char)toupper(*p);
        val = val * 16U + (c >= 'A' ? (unsigned)(c - 'A' + 10)
                                    : (unsigned)(c - '0'));
        p++;
    }
    *pp = p;
    return val;
}

static unsigned int parse_dec(const char **pp)
{
    unsigned int val = 0;
    const char *p = *pp;

    while (isdigit(*p)) {
        val = val * 10U + (unsigned)(*p - '0');
        p++;
    }
    *pp = p;
    return val;
}

static int parse_blaster(nos_soundinfo_t *info)
{
    const char *env;
    const char *p;
    int got_port = 0;

    env = getenv("BLASTER");
    if (!env)
        return 0;

    p = env;
    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        switch (toupper(*p)) {
        case 'A':
            p++;
            info->port = parse_hex(&p);
            got_port = 1;
            break;
        case 'I':
            p++;
            info->irq = (unsigned char)parse_dec(&p);
            break;
        case 'D':
            p++;
            info->dma_low = (unsigned char)parse_dec(&p);
            break;
        case 'H':
            p++;
            info->dma_high = (unsigned char)parse_dec(&p);
            break;
        case 'T':
            p++;
            info->card_type = (unsigned char)parse_dec(&p);
            break;
        default:
            /* Unknown field (e.g. P=MPU port) — skip token */
            while (*p && *p != ' ' && *p != '\t') p++;
            break;
        }
    }

    return got_port;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void nos_detect_sound(nos_soundinfo_t *info)
{
    int i;

    memset(info, 0, sizeof(*info));

    if (parse_blaster(info)) {
        /* Env var found — verify the hardware is actually there */
        if (sb_probe(info->port)) {
            info->present  = 1;
            info->from_env = 1;
        }
        /* If probe fails: stale BLASTER var or driver not yet loaded.
         * Leave present=0; genconf.c will not emit SB settings. */
        return;
    }

    /* No BLASTER variable — blind-probe standard addresses */
    for (i = 0; i < NUM_PROBE_PORTS; i++) {
        if (sb_probe(g_probe_ports[i])) {
            info->present = 1;
            info->port    = g_probe_ports[i];
            /* IRQ, DMA, and type are unknown without the env var;
             * leave as 0.  genconf.c will emit a conservative default. */
            return;
        }
    }
}
