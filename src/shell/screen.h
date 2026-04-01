/* NOS-DOS: NOS-SHELL
 * screen.h - Direct video memory screen rendering interface.
 *
 * Writes character+attribute pairs directly to the CGA text buffer at
 * B800:0000.  Faster than INT 10h and avoids BIOS cursor overhead.
 *
 * Supports 80x25 and 80x50 text modes.  Call nos_scr_init() first;
 * it detects the active mode and caches geometry in g_scr_cols/rows.
 *
 * Attribute byte layout (CGA/EGA/VGA text mode):
 *   Bits 7   : blink (or bright background when blink disabled)
 *   Bits 6-4 : background colour (3 bits)
 *   Bits 3-0 : foreground colour (4 bits)
 *
 * Colour constants (foreground or background):
 *   NOS_BLACK=0  NOS_BLUE=1   NOS_GREEN=2  NOS_CYAN=3
 *   NOS_RED=4    NOS_MAGENTA=5 NOS_BROWN=6 NOS_LGRAY=7
 *   NOS_DGRAY=8  NOS_LBLUE=9  NOS_LGREEN=10 NOS_LCYAN=11
 *   NOS_LRED=12  NOS_LMAGENTA=13 NOS_YELLOW=14 NOS_WHITE=15
 *
 * License: GPL-2.0
 */

#ifndef NOS_SCREEN_H
#define NOS_SCREEN_H

/* -----------------------------------------------------------------------
 * Colour constants
 * ----------------------------------------------------------------------- */

#define NOS_BLACK      0
#define NOS_BLUE       1
#define NOS_GREEN      2
#define NOS_CYAN       3
#define NOS_RED        4
#define NOS_MAGENTA    5
#define NOS_BROWN      6
#define NOS_LGRAY      7
#define NOS_DGRAY      8
#define NOS_LBLUE      9
#define NOS_LGREEN    10
#define NOS_LCYAN     11
#define NOS_LRED      12
#define NOS_LMAGENTA  13
#define NOS_YELLOW    14
#define NOS_WHITE     15

/* Build an attribute byte from foreground + background colour constants. */
#define NOS_ATTR(fg, bg)   ((unsigned char)(((bg) << 4) | ((fg) & 0x0F)))

/* Common attribute presets */
#define NOS_ATTR_NORMAL    NOS_ATTR(NOS_LGRAY,  NOS_BLACK)
#define NOS_ATTR_SELECTED  NOS_ATTR(NOS_BLACK,  NOS_CYAN)
#define NOS_ATTR_HEADER    NOS_ATTR(NOS_BLACK,  NOS_CYAN)
#define NOS_ATTR_FOOTER    NOS_ATTR(NOS_BLACK,  NOS_CYAN)
#define NOS_ATTR_BORDER    NOS_ATTR(NOS_CYAN,   NOS_BLACK)
#define NOS_ATTR_TITLE     NOS_ATTR(NOS_YELLOW, NOS_BLACK)
#define NOS_ATTR_ERROR     NOS_ATTR(NOS_WHITE,  NOS_RED)
#define NOS_ATTR_DIM       NOS_ATTR(NOS_DGRAY,  NOS_BLACK)

/* -----------------------------------------------------------------------
 * Box-drawing characters (IBM CP437)
 * ----------------------------------------------------------------------- */

/* Single-line box */
#define NOS_CH_TL   0xDA  /* top-left     ┌ */
#define NOS_CH_TR   0xBF  /* top-right    ┐ */
#define NOS_CH_BL   0xC0  /* bottom-left  └ */
#define NOS_CH_BR   0xD9  /* bottom-right ┘ */
#define NOS_CH_H    0xC4  /* horizontal   ─ */
#define NOS_CH_V    0xB3  /* vertical     │ */

/* Double-line box */
#define NOS_CH_DTL  0xC9  /* ╔ */
#define NOS_CH_DTR  0xBB  /* ╗ */
#define NOS_CH_DBL  0xC8  /* ╚ */
#define NOS_CH_DBR  0xBC  /* ╝ */
#define NOS_CH_DH   0xCD  /* ═ */
#define NOS_CH_DV   0xBA  /* ║ */

/* -----------------------------------------------------------------------
 * Screen state (read-only after nos_scr_init)
 * ----------------------------------------------------------------------- */

extern int g_scr_cols;   /* active column count (80) */
extern int g_scr_rows;   /* active row count (25 or 50) */

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/*
 * nos_scr_init — detect the current video mode, cache geometry, and save
 * the original cursor shape so nos_scr_restore() can reset it.
 * Must be called before any other nos_scr_* function.
 */
void nos_scr_init(void);

/*
 * nos_scr_restore — undo changes made by nos_scr_init: restore the saved
 * cursor shape.  Call when exiting the shell back to DOS.
 */
void nos_scr_restore(void);

/*
 * nos_scr_clear — fill the whole screen with space+attr.
 */
void nos_scr_clear(unsigned char attr);

/*
 * nos_scr_putchar — write one character+attribute at (col, row).
 * Out-of-range coordinates are silently ignored.
 */
void nos_scr_putchar(int col, int row, unsigned char ch, unsigned char attr);

/*
 * nos_scr_puts — write a NUL-terminated string starting at (col, row).
 * Stops at the right edge of the screen; does not wrap.
 */
void nos_scr_puts(int col, int row, const char *s, unsigned char attr);

/*
 * nos_scr_putn — write exactly n characters from s at (col, row).
 * Pads with spaces if n > strlen(s).  Does not wrap.
 */
void nos_scr_putn(int col, int row, const char *s, int n, unsigned char attr);

/*
 * nos_scr_fill — fill a rectangle with ch+attr.
 * (col,row) is the top-left corner; w=width, h=height.
 * Clipped to screen bounds.
 */
void nos_scr_fill(int col, int row, int w, int h,
                  unsigned char ch, unsigned char attr);

/*
 * nos_scr_box — draw a single-line box border.
 * Interior is NOT cleared; use nos_scr_fill for that.
 * w and h include the border characters (minimum 2x2).
 */
void nos_scr_box(int col, int row, int w, int h, unsigned char attr);

/*
 * nos_scr_dbox — draw a double-line box border.
 */
void nos_scr_dbox(int col, int row, int w, int h, unsigned char attr);

/*
 * nos_scr_hline — draw a horizontal line of ch+attr.
 */
void nos_scr_hline(int col, int row, int len,
                   unsigned char ch, unsigned char attr);

/*
 * nos_scr_cursor — move the hardware text cursor to (col, row).
 */
void nos_scr_cursor(int col, int row);

/*
 * nos_scr_hide_cursor — move cursor off-screen (row 25 in 25-line mode).
 * Use during screen redraws to eliminate flicker.
 */
void nos_scr_hide_cursor(void);

#endif /* NOS_SCREEN_H */
