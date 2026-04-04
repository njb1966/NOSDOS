/* NOS-DOS: NOS-INSTALL
 * screen.h - Direct video memory screen rendering interface.
 *
 * Shared with NOS-SHELL. Writes character+attribute pairs directly to the
 * CGA text buffer at B800:0000.
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

/* -----------------------------------------------------------------------
 * Box-drawing characters (IBM CP437)
 * ----------------------------------------------------------------------- */

/* Single-line box */
#define NOS_CH_TL   0xDA  /* top-left     */
#define NOS_CH_TR   0xBF  /* top-right    */
#define NOS_CH_BL   0xC0  /* bottom-left  */
#define NOS_CH_BR   0xD9  /* bottom-right */
#define NOS_CH_H    0xC4  /* horizontal   */
#define NOS_CH_V    0xB3  /* vertical     */

/* Double-line box */
#define NOS_CH_DTL  0xC9  /* double top-left  */
#define NOS_CH_DTR  0xBB  /* double top-right */
#define NOS_CH_DBL  0xC8  /* double bot-left  */
#define NOS_CH_DBR  0xBC  /* double bot-right */
#define NOS_CH_DH   0xCD  /* double horizontal */
#define NOS_CH_DV   0xBA  /* double vertical   */

/* -----------------------------------------------------------------------
 * Screen state (read-only after nos_scr_init)
 * ----------------------------------------------------------------------- */

extern int g_scr_cols;
extern int g_scr_rows;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

void nos_scr_init(void);
void nos_scr_restore(void);
void nos_scr_clear(unsigned char attr);
void nos_scr_putchar(int col, int row, unsigned char ch, unsigned char attr);
void nos_scr_puts(int col, int row, const char *s, unsigned char attr);
void nos_scr_putn(int col, int row, const char *s, int n, unsigned char attr);
void nos_scr_fill(int col, int row, int w, int h,
                  unsigned char ch, unsigned char attr);
void nos_scr_box(int col, int row, int w, int h, unsigned char attr);
void nos_scr_dbox(int col, int row, int w, int h, unsigned char attr);
void nos_scr_hline(int col, int row, int len,
                   unsigned char ch, unsigned char attr);
void nos_scr_cursor(int col, int row);
void nos_scr_hide_cursor(void);

#endif /* NOS_SCREEN_H */
