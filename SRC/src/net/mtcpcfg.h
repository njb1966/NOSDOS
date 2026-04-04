/* NOS-DOS: NNET
 * mtcpcfg.h - mTCP configuration file reader.
 *
 * Parses C:\NOS\SYSTEM\MTCP.CFG (space-delimited KEY VALUE lines;
 * lines beginning with ';' are comments).
 *
 * License: GPL-2.0
 */

#ifndef NOS_MTCPCFG_H
#define NOS_MTCPCFG_H

#define MTCPCFG_PATH "C:\\NOS\\SYSTEM\\MTCP.CFG"

typedef struct {
    char         ipaddr[20];      /* IPADDR value, e.g. "192.168.1.100"  */
    char         netmask[20];     /* NETMASK value                        */
    char         gateway[20];     /* GATEWAY value                        */
    char         nameserver[20];  /* NAMESERVER value                     */
    char         hostname[64];    /* HOSTNAME value                       */
    unsigned char packetint;      /* PACKETINT value (0x60 etc.), 0=unset */
} nos_mtcpcfg_t;

/*
 * nos_mtcpcfg_read -- parse MTCP.CFG at path into cfg.
 * Returns 0 on success, -1 if file cannot be opened.
 * Fields not present in the file are left as empty strings / 0.
 */
int nos_mtcpcfg_read(const char *path, nos_mtcpcfg_t *cfg);

#endif /* NOS_MTCPCFG_H */
