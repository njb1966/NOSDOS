/* NOS-DOS: NNET
 * mtcpcfg.c - mTCP configuration file reader.
 *
 * Format (subset of mTCP's MTCP.CFG):
 *   ; comment line
 *   PACKETINT 0x60
 *   IPADDR 192.168.1.100
 *   NETMASK 255.255.255.0
 *   GATEWAY 192.168.1.1
 *   NAMESERVER 8.8.8.8
 *   HOSTNAME mypc
 *
 * License: GPL-2.0
 */

#include <stdio.h>   /* fopen, fgets, fclose, sscanf */
#include <string.h>  /* strcmp, strlen, strncpy, memset */
#include "mtcpcfg.h"

int nos_mtcpcfg_read(const char *path, nos_mtcpcfg_t *cfg)
{
    FILE *fp;
    char  line[128];
    char  key[32], val[96];
    int   n;
    unsigned int pktint;

    memset(cfg, 0, sizeof(*cfg));

    fp = fopen(path, "r");
    if (!fp) return -1;

    while (fgets(line, (int)sizeof(line), fp)) {
        /* Skip comments and blank lines */
        if (line[0] == ';' || line[0] == '\n' || line[0] == '\r') continue;

        n = sscanf(line, "%31s %95s", key, val);
        if (n < 2) continue;

        if (strcmp(key, "IPADDR")     == 0) strncpy(cfg->ipaddr,      val, 19);
        else if (strcmp(key, "NETMASK")    == 0) strncpy(cfg->netmask,    val, 19);
        else if (strcmp(key, "GATEWAY")    == 0) strncpy(cfg->gateway,    val, 19);
        else if (strcmp(key, "NAMESERVER") == 0) strncpy(cfg->nameserver, val, 19);
        else if (strcmp(key, "HOSTNAME")   == 0) strncpy(cfg->hostname,   val, 63);
        else if (strcmp(key, "PACKETINT")  == 0) {
            sscanf(val, "%x", &pktint);
            cfg->packetint = (unsigned char)pktint;
        }
    }

    fclose(fp);
    return 0;
}
