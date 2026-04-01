/* NOS-DOS: NOS-DETECT
 * memory.c - Detect conventional, XMS, and EMS memory.
 *
 * Probes:
 *   Conventional : INT 12h                          (AX = KB present)
 *   XMS          : INT 2Fh/4300h + 4310h, then fn 08h via driver entry
 *   EMS          : open "EMMXXXX0" device, then INT 67h/42h page count
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include "memory.h"

#include <dos.h>     /* union REGS, struct SREGS, int86, int86x, MK_FP */
#include <fcntl.h>   /* O_RDONLY (used with _dos_open)                  */
#include <string.h>  /* memset                                          */

/* Far pointer to the XMS driver entry point.
 * Populated by xms_init(); remains NULL if XMS absent. */
static void (__far *g_xms_driver)(void) = 0;

/* -----------------------------------------------------------------------
 * Conventional memory
 * INT 12h — no inputs; AX = total conventional memory in KB.
 * Under a DOS extender or VM this reflects the reported base RAM figure.
 * ----------------------------------------------------------------------- */
static unsigned int detect_conventional(void)
{
    union REGS r;
    int86(0x12, &r, &r);
    return r.x.ax;
}

/* -----------------------------------------------------------------------
 * XMS — eXtended Memory Specification
 *
 * Presence check : INT 2Fh, AX=4300h → AL=80h means driver installed.
 * Entry point    : INT 2Fh, AX=4310h → ES:BX = far call address.
 * Query (fn 08h) : AH=08h, call driver entry.
 *                  Returns: AX = largest free block (KB)
 *                           DX = total free XMS (KB)
 *                           BL = 0 on success
 * ----------------------------------------------------------------------- */
static int xms_init(void)
{
    union REGS  r;
    struct SREGS sr;

    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if ((r.h.al & 0x80) == 0)
        return 0; /* XMS manager not installed */

    r.x.ax = 0x4310;
    int86x(0x2F, &r, &r, &sr);
    g_xms_driver = (void (__far *)(void))MK_FP(sr.es, r.x.bx);
    return 1;
}

static void xms_query(unsigned int *total_kb, unsigned int *largest_kb)
{
    unsigned int ax_val = 0, dx_val = 0;
    unsigned char bl_val = 0;

    /* Call the XMS driver entry point with AH=08h (Query Free XMS).
     * Open Watcom __asm lets us reference C locals by name.
     * The driver does a far return (RETF); call dword ptr issues CALLF. */
    __asm {
        mov  ah, 08h
        call dword ptr [g_xms_driver]
        mov  ax_val, ax
        mov  dx_val, dx
        mov  bl_val, bl
    }

    if (bl_val == 0) {
        *largest_kb = ax_val;
        *total_kb   = dx_val;
    } else {
        *largest_kb = 0;
        *total_kb   = 0;
    }
}

/* -----------------------------------------------------------------------
 * EMS — Expanded Memory Specification (LIM 4.0)
 *
 * Presence: attempt to open the device "EMMXXXX0" via _dos_open().
 *   A real EMM driver registers this device name; if open succeeds the
 *   driver is present. This method is more reliable than INT 67h/40h
 *   alone because it avoids false positives from stale vector entries.
 *
 * Page count: INT 67h, AH=42h.
 *   BX = free pages, DX = total pages (1 EMS page = 16 KB), AH=0 on ok.
 * ----------------------------------------------------------------------- */
static int ems_present(void)
{
    int handle;
    unsigned rc;

    rc = _dos_open("EMMXXXX0", O_RDONLY, &handle);
    if (rc != 0)
        return 0; /* open failed — no EMS */

    _dos_close(handle);
    return 1;
}

static void ems_query(unsigned int *total_kb, unsigned int *free_kb)
{
    union REGS r;

    r.h.ah = 0x42; /* Get Page Count */
    int86(0x67, &r, &r);

    if (r.h.ah == 0) {
        *total_kb = (unsigned int)r.x.dx * 16U; /* pages → KB */
        *free_kb  = (unsigned int)r.x.bx * 16U;
    } else {
        *total_kb = 0;
        *free_kb  = 0;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void nos_detect_memory(nos_meminfo_t *info)
{
    memset(info, 0, sizeof(*info));

    info->conv_kb = detect_conventional();

    if (xms_init()) {
        info->xms_present = 1;
        xms_query(&info->xms_total_kb, &info->xms_largest_kb);
    }

    if (ems_present()) {
        info->ems_present = 1;
        ems_query(&info->ems_total_kb, &info->ems_free_kb);
    }
}
