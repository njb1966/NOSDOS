/* NOS-DOS: NOS-SHELL
 * input.c - Keyboard and mouse input.
 *
 * Keyboard: INT 16h
 *   AH=01h  peek (non-blocking): ZF=1 → no key, ZF=0 → key in AX
 *   AH=00h  read (blocking): returns AX (AH=scan, AL=ASCII)
 *
 * Extended keys (AL=00h or AL=E0h on enhanced keyboards) have their
 * scan code in AH.  We encode these as (0x100 | AH) to distinguish them
 * from ASCII.  This matches the NOS_KEY_* constants in input.h.
 *
 * Mouse: INT 33h
 *   AX=0000h  reset / detect     → AX=FFFFh if present
 *   AX=0003h  read position/buttons
 *             BX=buttons, CX=x pixels, DX=y pixels
 *   AX=0020h  set sensitivity
 *   We convert pixel coordinates to character cell coordinates by
 *   dividing CX by 8 (char width) and DX by 8 (char height in 25-line).
 *   Adjust NOS_MOUSE_CHAR_W/H if running in a different font size.
 *
 * License: GPL-2.0
 */

#include <dos.h>    /* int86, union REGS */
#include "input.h"
#include "screen.h" /* g_scr_rows — for char cell height calculation */

/* Character cell size used to convert mouse pixel coords to cells.
 * Standard VGA text mode: 8x16 pixels per char (25 rows), 8x8 (50 rows). */
#define NOS_MOUSE_CHAR_W   8

static int  g_mouse_present   = 0;
static int  g_mouse_prev_btn  = 0;

/* -----------------------------------------------------------------------
 * Internal: keyboard
 * ----------------------------------------------------------------------- */

/* Return 1 if a key is waiting.
 * INT 21h / AH=0Bh: DOS keyboard status — AL=0xFF (key ready) or 0x00 (none).
 * Avoids needing the ZF (zero flag) which Open Watcom's WORDREGS doesn't expose;
 * only cflag (carry) is available there. */
static int kb_peek(void)
{
    union REGS r;
    r.h.ah = 0x0B;
    intdos(&r, &r);
    return (r.h.al == 0xFF);
}

/* Read one key from the BIOS buffer (blocking). Returns encoded key code. */
static int kb_read(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x16, &r, &r);

    if (r.h.al == 0x00 || r.h.al == 0xE0) {
        /* Extended key: encode scan code as 0x100 + scan */
        return 0x100 | (int)(unsigned char)r.h.ah;
    }
    return (int)(unsigned char)r.h.al;
}

static void fill_key_event(nos_event_t *evt, int code)
{
    evt->type     = NOS_EVT_KEY;
    evt->key.code = code;
    evt->key.ch   = (code > 0 && code < 0x100) ? (unsigned char)code : 0;
}

/* -----------------------------------------------------------------------
 * Internal: mouse
 * ----------------------------------------------------------------------- */

/* char_height: 8 for 50-line mode, 16 for 25-line mode. */
static int mouse_char_h(void)
{
    return (g_scr_rows == 50) ? 8 : 16;
}

static void read_mouse(int *col, int *row, int *buttons)
{
    union REGS r;
    r.x.ax = 0x0003;
    int86(0x33, &r, &r);
    *buttons = (int)r.x.bx;
    *col     = (int)r.x.cx / NOS_MOUSE_CHAR_W;
    *row     = (int)r.x.dx / mouse_char_h();
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void nos_inp_init(void)
{
    union REGS r;

    /* Reset mouse driver (INT 33h / AX=0000h).
     * AX=FFFFh on return → driver present. */
    r.x.ax = 0x0000;
    int86(0x33, &r, &r);
    g_mouse_present = (r.x.ax == 0xFFFF) ? 1 : 0;
    g_mouse_prev_btn = 0;
}

int nos_inp_poll(nos_event_t *evt)
{
    int col, row, buttons, clicked;

    evt->type = NOS_EVT_NONE;

    /* Keyboard takes priority over mouse. */
    if (kb_peek()) {
        fill_key_event(evt, kb_read());
        return NOS_EVT_KEY;
    }

    /* Mouse */
    if (g_mouse_present) {
        read_mouse(&col, &row, &buttons);
        clicked = buttons & ~g_mouse_prev_btn;  /* newly-pressed buttons */
        g_mouse_prev_btn = buttons;

        if (clicked || buttons) {
            evt->type           = NOS_EVT_MOUSE;
            evt->mouse.col      = col;
            evt->mouse.row      = row;
            evt->mouse.buttons  = buttons;
            evt->mouse.clicked  = clicked;
            return NOS_EVT_MOUSE;
        }
    }

    return NOS_EVT_NONE;
}

int nos_inp_wait(nos_event_t *evt)
{
    int rc;
    do {
        rc = nos_inp_poll(evt);
    } while (rc == NOS_EVT_NONE);
    return rc;
}

void nos_inp_flush(void)
{
    /* Drain the BIOS keyboard buffer without processing. */
    while (kb_peek())
        kb_read();
}
