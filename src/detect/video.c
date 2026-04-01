/* NOS-DOS: NOS-DETECT
 * video.c - Detect video adapter type and VESA BIOS support.
 *
 * Probes (in order):
 *   1. Current video mode  : INT 10h / AH=0Fh
 *   2. VGA / DCC           : INT 10h / AX=1A00h  (Display Combination Code)
 *   3. VESA BIOS           : INT 10h / AX=4F00h  (Get SuperVGA Info)
 *
 * For VESA detection we pass a 512-byte static buffer to the BIOS using
 * the data segment as ES (small model: all near data lives in DS).
 * We request VBE 2.0 info by pre-loading the signature "VBE2".
 *
 * Compiled with Open Watcom C, small model, 16-bit DOS target (-ms -bt=dos).
 * C89/C90 only: no // comments, vars declared at top of block.
 * License: GPL-2.0
 */

#include "video.h"

#include <dos.h>     /* union REGS, struct SREGS, int86, int86x */
#include <string.h>  /* memset, memcpy                          */

/* -----------------------------------------------------------------------
 * VbeInfoBlock — first 32 bytes of the 512-byte structure returned by
 * INT 10h/4F00h.  We only inspect the signature, version, and OEM ptr.
 * The buffer must be writable (BIOS fills it in).
 * ----------------------------------------------------------------------- */
#define VBE_BLOCK_SIZE 512

/* Static buffer for the VBE info block.
 * Must be in conventional memory accessible by the real-mode BIOS. */
static unsigned char g_vbe_buf[VBE_BLOCK_SIZE];

/* -----------------------------------------------------------------------
 * Current video mode — INT 10h / AH=0Fh
 *   Out: AL = current video mode
 *        AH = number of character columns
 *        BH = active display page
 * ----------------------------------------------------------------------- */
static void detect_current_mode(unsigned char *mode, unsigned char *cols)
{
    union REGS r;
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    *mode = r.h.al;
    *cols = r.h.ah;
}

/* -----------------------------------------------------------------------
 * VGA / Display Combination Code — INT 10h / AX=1A00h
 *
 * If AL returns as 1Ah the call is supported (VGA or later).
 * BL = active display code, BH = alternate display code.
 *
 * DCC 08h = VGA color, 07h = VGA mono.  Any value >= 07h indicates VGA.
 * Returns non-zero (VGA present) and fills *dcc.
 * ----------------------------------------------------------------------- */
static int detect_vga(unsigned char *dcc)
{
    union REGS r;

    r.x.ax = 0x1A00;
    r.x.bx = 0;
    int86(0x10, &r, &r);

    if (r.h.al != 0x1A) {
        *dcc = NOS_VID_DCC_NONE;
        return 0;
    }

    *dcc = r.h.bl; /* active display code */
    return 1;      /* INT 10h/1A00h only exists on VGA and later */
}

/* -----------------------------------------------------------------------
 * VESA BIOS Extensions — INT 10h / AX=4F00h (Get SuperVGA Info)
 *
 * On entry : ES:DI → 512-byte VbeInfoBlock buffer
 *            (write "VBE2" at offset 0 to request VBE 2.0 reply)
 * On return: AL = 4Fh (supported), AH = 00h (success)
 *            Buffer[0..3]  = "VESA" signature (BIOS confirms)
 *            Buffer[4..5]  = VESA version (BCD word)
 *
 * We set ES = DS because the buffer is a static in the data segment.
 * The value of DS is read via inline asm — FP_SEG on a near pointer
 * is unreliable in Open Watcom small model.
 * ----------------------------------------------------------------------- */
static int detect_vesa(unsigned int *version)
{
    union REGS  r;
    struct SREGS sr;
    unsigned int ds_val = 0;

    /* Initialise buffer and write the VBE2 request signature */
    memset(g_vbe_buf, 0, VBE_BLOCK_SIZE);
    memcpy(g_vbe_buf, "VBE2", 4);

    /* Get current DS so we can set ES = DS for the BIOS call */
    __asm { mov ds_val, ds }

    r.x.ax = 0x4F00;
    sr.es  = ds_val;                         /* ES = data segment          */
    r.x.di = (unsigned int)g_vbe_buf;        /* DI = near offset of buffer */

    int86x(0x10, &r, &r, &sr);

    /* Check for supported + success + "VESA" confirmation in buffer */
    if (r.h.al != 0x4F || r.h.ah != 0x00)
        return 0;
    if (memcmp(g_vbe_buf, "VESA", 4) != 0)
        return 0;

    /* Version is a little-endian word at offset 4 */
    *version = (unsigned int)g_vbe_buf[5] << 8 | (unsigned int)g_vbe_buf[4];
    return 1;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void nos_detect_video(nos_videoinfo_t *info)
{
    memset(info, 0, sizeof(*info));

    detect_current_mode(&info->current_mode, &info->text_cols);
    info->vga_present  = detect_vga(&info->display_code);

    if (detect_vesa(&info->vesa_version))
        info->vesa_present = 1;
}
