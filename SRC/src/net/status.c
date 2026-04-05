/* NOS-DOS: NNET
 * status.c - NNET STATUS subcommand.
 *
 * Packet driver detection mirrors NOS-DETECT's network.c:
 *   Crynwr signature "PKT DRVR" at handler+2, scanned on INT 60h-80h.
 *
 * License: GPL-2.0
 */

#include <dos.h>     /* _dos_getvect, MK_FP, FP_SEG, FP_OFF */
#include <string.h>  /* _fmemcmp, memset */
#include <stdio.h>   /* printf */
#include "mtcpcfg.h"
#include "status.h"

/* -----------------------------------------------------------------------
 * Packet driver detection
 * ----------------------------------------------------------------------- */

static int pkt_driver_present(unsigned char *intr_out)
{
    unsigned char       n;
    void          __far *vec;
    unsigned char __far *p;
    char                 sig[] = "PKT DRVR";

    for (n = 0x60; n <= 0x80; n++) {
        unsigned char off;
        vec = _dos_getvect(n);
        if (FP_SEG(vec) == 0 && FP_OFF(vec) == 0) continue;
        /* Crynwr drivers place "PKT DRVR" at offset 1 (0xFF marker variant)
         * or offset 3 (short-jump variant).  Check both. */
        for (off = 1; off <= 4; off++) {
            p = (unsigned char __far *)vec + off;
            if (_fmemcmp(p, sig, 8) == 0) {
                if (intr_out) *intr_out = n;
                return 1;
            }
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void nos_status_show(void)
{
    nos_mtcpcfg_t cfg;
    unsigned char pkt_int = 0;
    int           pkt     = pkt_driver_present(&pkt_int);
    int           have_ip;

    printf("\r\nNOS-DOS Network Status\r\n");
    printf("======================\r\n\r\n");

    /* Packet driver */
    if (pkt)
        printf("  Packet Driver  : INT %02Xh  [FOUND]\r\n", (unsigned)pkt_int);
    else
        printf("  Packet Driver  : [NOT FOUND]\r\n");

    /* MTCP.CFG */
    if (nos_mtcpcfg_read(MTCPCFG_PATH, &cfg) == 0) {
        printf("  MTCP Config    : %s\r\n", MTCPCFG_PATH);
        if (cfg.packetint)
            printf("  Packet INT     : 0x%02X\r\n", (unsigned)cfg.packetint);

        have_ip = (cfg.ipaddr[0] != '\0');
        if (have_ip) {
            printf("\r\n");
            printf("  IP Address     : %s\r\n", cfg.ipaddr);
            if (cfg.netmask[0])
                printf("  Netmask        : %s\r\n", cfg.netmask);
            if (cfg.gateway[0])
                printf("  Gateway        : %s\r\n", cfg.gateway);
            if (cfg.nameserver[0])
                printf("  DNS Server     : %s\r\n", cfg.nameserver);
            if (cfg.hostname[0])
                printf("  Hostname       : %s\r\n", cfg.hostname);
        }
    } else {
        printf("  MTCP Config    : not configured\r\n");
        have_ip = 0;
    }

    printf("\r\n");
    if (pkt && have_ip) {
        printf("  >>> CONNECTED <<<\r\n");
    } else if (pkt && !have_ip) {
        printf("  Driver present but no IP — run:  NNET DHCP\r\n");
    } else {
        printf("  >>> NO NETWORK <<<\r\n");
        printf("\r\n");
        printf("  To configure networking:\r\n");
        printf("    1. Load a packet driver in AUTOEXEC.BAT\r\n");
        printf("    2. Run: NNET DHCP  (to obtain IP from DHCP server)\r\n");
        printf("    3. Or:  NNET CONFIG  (to manually edit MTCP.CFG)\r\n");
    }
    printf("\r\n");
}
