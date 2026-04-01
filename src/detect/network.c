/* NOS-DOS: NOS-DETECT
 * network.c - Detect a packet driver in the INT 60h-80h range.
 *
 * Detection method — Crynwr/FTP Software signature scan:
 *
 *   All Crynwr-compatible packet drivers (NE2000, RTL8139, 3c509, etc.)
 *   place the ASCII string "PKT DRVR" at exactly offset +2 from their
 *   interrupt handler's entry point:
 *
 *       handler+0  EB xx        ; short jump to real handler body
 *       handler+2  "PKT DRVR"  ; 8-byte identification signature
 *       handler+10 00           ; null terminator
 *
 *   For each candidate interrupt (60h-80h):
 *     1. Read the far pointer from the IVT via _dos_getvect().
 *     2. Skip if the vector is null (unhooked).
 *     3. Compare 8 bytes at handler+2 against "PKT DRVR".
 *     4. On match, record the interrupt number and stop scanning.
 *
 * Why only presence + interrupt number:
 *   Retrieving the MAC address requires calling INT n with AH=06h
 *   (get_address), which needs a live handle and dynamic INT dispatch.
 *   Dynamic INT invocation in 16-bit C requires either self-modifying
 *   code or a NASM trampoline — out of scope for a detection module.
 *   genconf.c only needs the interrupt number to write mTCP's pktint
 *   setting; MAC retrieval is deferred to the NNET component.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include "network.h"

#include <dos.h>    /* _dos_getvect, MK_FP, FP_SEG, FP_OFF */
#include <string.h> /* _fmemcmp, memset                     */

/* The 8-byte signature placed by all Crynwr-compatible packet drivers
 * at offset +2 from their interrupt handler entry point. */
static char g_pktdrvr_sig[] = "PKT DRVR";
#define PKT_SIG_LEN  8
#define PKT_SIG_OFFS 2

/* -----------------------------------------------------------------------
 * Signature check
 *
 * Reads 8 bytes at (handler + PKT_SIG_OFFS) via a far pointer and
 * compares against g_pktdrvr_sig.  Using _fmemcmp avoids assuming the
 * handler is in the same segment as our data.
 *
 * Returns 1 if signature matches, 0 otherwise.
 * ----------------------------------------------------------------------- */
static int has_pkt_signature(void __far *handler)
{
    unsigned char __far *p;
    p = (unsigned char __far *)handler + PKT_SIG_OFFS;
    return (_fmemcmp(p, g_pktdrvr_sig, PKT_SIG_LEN) == 0) ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void nos_detect_network(nos_netinfo_t *info)
{
    unsigned char n;
    void __far *vec;

    memset(info, 0, sizeof(*info));

    for (n = NOS_NET_INT_MIN; n <= NOS_NET_INT_MAX; n++) {
        vec = _dos_getvect(n);

        /* Skip null (unhooked) vectors */
        if (FP_SEG(vec) == 0 && FP_OFF(vec) == 0)
            continue;

        if (has_pkt_signature(vec)) {
            info->present = 1;
            info->intr    = n;
            return; /* stop at first match */
        }
    }
}
