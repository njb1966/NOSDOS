/* NOS-DOS: NOS-BRIDGE
 * bridge.h - Host bridge management utility interface.
 *
 * NBRIDGE manages the shared-folder bridge between the DOS VM and the host.
 * The convention:
 *   H:\INBOX\   -- host places files here for DOS to consume
 *   H:\OUTBOX\  -- DOS places files here for the host to collect
 *   H:\PRINT\   -- NOSLPT spools LPT1 output here as PRINTnnn.PRN files
 *   H:\CLIP\    -- NOSCLIP exchanges clipboard text via CLIP.TXT
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS (-ms -bt=dos).
 * C89 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#ifndef NOS_BRIDGE_H
#define NOS_BRIDGE_H

/* Bridge directory paths */
#define BRIDGE_ROOT     "H:\\"
#define BRIDGE_INBOX    "H:\\INBOX"
#define BRIDGE_OUTBOX   "H:\\OUTBOX"
#define BRIDGE_PRINT    "H:\\PRINT"
#define BRIDGE_CLIP     "H:\\CLIP"
#define BRIDGE_CLIP_TXT "H:\\CLIP\\CLIP.TXT"

/* Maximum print file index */
#define BRIDGE_PRINT_MAX 999

/*
 * nos_bridge_h_mounted
 * Returns 1 if H:\ is a valid, accessible drive; 0 otherwise.
 * Uses INT 21h AH=36h (get disk free space); AX=FFFFh means invalid drive.
 */
int nos_bridge_h_mounted(void);

/*
 * nos_bridge_ensure_dirs
 * Creates INBOX, OUTBOX, PRINT, and CLIP under H:\ if they do not exist.
 * Returns 0 if all directories are present (or were created), -1 on failure.
 */
int nos_bridge_ensure_dirs(void);

#endif /* NOS_BRIDGE_H */
