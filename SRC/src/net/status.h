/* NOS-DOS: NNET
 * status.h - NNET STATUS subcommand.
 * License: GPL-2.0
 */

#ifndef NOS_STATUS_H
#define NOS_STATUS_H

/*
 * nos_status_show -- print network status to stdout.
 * Scans INT 60h-80h for a Crynwr packet driver, reads MTCP.CFG,
 * and displays IP/GW/DNS/hostname or a "not connected" message.
 */
void nos_status_show(void);

#endif /* NOS_STATUS_H */
