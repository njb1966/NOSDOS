/* NOS-DOS: NOS-DETECT
 * mouse.c - Detect mouse driver presence, button count, and type.
 *
 * Probes:
 *   INT 33h / AX=0000h  Mouse Reset — AX=FFFFh means driver installed;
 *                        BX = number of buttons.
 *   INT 33h / AX=0024h  Get Driver Version — BH=major, BL=minor,
 *                        CH=mouse type, CL=IRQ.  Not all drivers support
 *                        this call; we validate by re-checking AX=FFFFh.
 *
 * CTMOUSE (used by NOS-DOS) supports both calls.  Older OEM drivers may
 * return garbage from AX=0024h; we guard against this below.
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include "mouse.h"

#include <dos.h>    /* union REGS, int86 */
#include <string.h> /* memset            */

/* -----------------------------------------------------------------------
 * Mouse reset — INT 33h / AX=0000h
 *
 * AX = FFFFh  driver installed and mouse hardware present
 * AX = 0000h  driver not installed (or no hardware)
 * BX          number of buttons (when AX = FFFFh)
 *
 * Note: some BIOSes reflect INT 33h to a default handler that returns
 * AX=0 even when a driver is later loaded; calling this at boot time
 * (after AUTOEXEC.BAT has loaded CTMOUSE) is reliable.
 * ----------------------------------------------------------------------- */
static int mouse_reset(unsigned int *buttons)
{
    union REGS r;

    r.x.ax = 0x0000;
    int86(0x33, &r, &r);

    if (r.x.ax != 0xFFFF)
        return 0; /* driver absent */

    *buttons = r.x.bx;
    return 1;
}

/* -----------------------------------------------------------------------
 * Driver version and type — INT 33h / AX=0024h
 *
 * BH = major version, BL = minor version (BCD)
 * CH = mouse type  (see NOS_MOUSE_TYPE_* in mouse.h)
 * CL = IRQ number  (0 = polled / PS/2 / unknown)
 *
 * Guard: repeat the reset check after this call.  If AX is no longer
 * FFFFh a buggy driver clobbered registers; discard the result.
 * ----------------------------------------------------------------------- */
static int mouse_get_version(unsigned char *major, unsigned char *minor,
                              unsigned char *type,  unsigned char *irq)
{
    union REGS r;

    r.x.ax = 0x0024;
    int86(0x33, &r, &r);

    /* Sanity check: a real driver returns AX=FFFFh from this call too */
    if (r.x.ax != 0xFFFF)
        return 0;

    *major = r.h.bh;
    *minor = r.h.bl;
    *type  = r.h.ch;
    *irq   = r.h.cl;
    return 1;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void nos_detect_mouse(nos_mouseinfo_t *info)
{
    memset(info, 0, sizeof(*info));

    if (!mouse_reset(&info->buttons))
        return; /* no driver — all fields stay zero */

    info->present = 1;

    /* Best-effort: get driver version and hardware type.
     * Failure here is non-fatal; type and irq remain 0. */
    mouse_get_version(&info->driver_major, &info->driver_minor,
                      &info->mouse_type,  &info->irq);
}
