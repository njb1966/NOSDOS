/* NOS-DOS: NOS-DETECT
 * network.h - Packet driver detection interface.
 * License: GPL-2.0
 */

#ifndef NOS_DETECT_NETWORK_H
#define NOS_DETECT_NETWORK_H

/* Packet driver interrupt scan range (Crynwr convention) */
#define NOS_NET_INT_MIN 0x60
#define NOS_NET_INT_MAX 0x80

typedef struct {
    int           present;  /* Non-zero if a packet driver was found         */
    unsigned char intr;     /* Interrupt vector the driver is hooked to      */
} nos_netinfo_t;

/* Fill *info with detected packet driver information. */
void nos_detect_network(nos_netinfo_t *info);

#endif /* NOS_DETECT_NETWORK_H */
