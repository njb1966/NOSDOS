/* NOS-DOS: NOS-SHELL
 * input.h - Keyboard and mouse input interface.
 *
 * Provides a unified event model: nos_inp_poll() returns one event per
 * call.  Events are either key presses or mouse actions.
 *
 * Key codes:
 *   Printable ASCII (0x20-0x7E) are returned directly as NOS_KEY_CHAR.
 *   Extended / special keys use NOS_KEY_* constants below.
 *
 * Mouse:
 *   Requires INT 33h (CTMOUSE or compatible).  If no mouse driver is
 *   loaded, mouse events are never returned; keyboard still works.
 *
 * License: GPL-2.0
 */

#ifndef NOS_INPUT_H
#define NOS_INPUT_H

/* -----------------------------------------------------------------------
 * Event type
 * ----------------------------------------------------------------------- */

#define NOS_EVT_NONE   0   /* no event pending */
#define NOS_EVT_KEY    1   /* keyboard event */
#define NOS_EVT_MOUSE  2   /* mouse event */

/* -----------------------------------------------------------------------
 * Key codes
 *
 * ASCII printable characters (0x20..0x7E) → NOS_KEY_CHAR, key.ch set.
 * Everything else uses a named constant.
 * ----------------------------------------------------------------------- */

/* Sentinel — "no key" */
#define NOS_KEY_NONE      0

/* Control keys */
#define NOS_KEY_ENTER     0x0D
#define NOS_KEY_ESC       0x1B
#define NOS_KEY_BACKSPACE 0x08
#define NOS_KEY_TAB       0x09

/* Function keys (0x100 | BIOS scan code) */
#define NOS_KEY_F1        0x13B
#define NOS_KEY_F2        0x13C
#define NOS_KEY_F3        0x13D
#define NOS_KEY_F4        0x13E
#define NOS_KEY_F5        0x13F
#define NOS_KEY_F6        0x140
#define NOS_KEY_F7        0x141
#define NOS_KEY_F8        0x142
#define NOS_KEY_F9        0x143
#define NOS_KEY_F10       0x144
#define NOS_KEY_F11       0x185  /* enhanced keyboard only */
#define NOS_KEY_F12       0x186

/* Arrow keys */
#define NOS_KEY_UP        0x148
#define NOS_KEY_DOWN      0x150
#define NOS_KEY_LEFT      0x14B
#define NOS_KEY_RIGHT     0x14D

/* Navigation */
#define NOS_KEY_HOME      0x147
#define NOS_KEY_END       0x14F
#define NOS_KEY_PGUP      0x149
#define NOS_KEY_PGDN      0x151
#define NOS_KEY_INS       0x152
#define NOS_KEY_DEL       0x153

/* Alt+letter: NOS_KEY_ALT_A through NOS_KEY_ALT_Z */
#define NOS_KEY_ALT_A     0x11E
#define NOS_KEY_ALT_B     0x130
#define NOS_KEY_ALT_C     0x12E
#define NOS_KEY_ALT_D     0x120
#define NOS_KEY_ALT_E     0x112
#define NOS_KEY_ALT_F     0x121
#define NOS_KEY_ALT_F1    0x168  /* Alt+F1 — switch left panel drive */
#define NOS_KEY_ALT_F2    0x169  /* Alt+F2 — switch right panel drive */
#define NOS_KEY_ALT_F4    0x16B  /* Alt+F4 — quit */
#define NOS_KEY_ALT_F7    0x16E  /* Alt+F7 — find */
#define NOS_KEY_ALT_F10   0x171  /* Alt+F10 — menu */

/* Ctrl+letter (low ASCII, 0x01..0x1A) */
#define NOS_KEY_CTRL_A    0x01
#define NOS_KEY_CTRL_C    0x03
#define NOS_KEY_CTRL_D    0x04
#define NOS_KEY_CTRL_R    0x12
#define NOS_KEY_CTRL_X    0x18

/* Mouse button bits (used in nos_mouse_event_t.buttons) */
#define NOS_MBTN_LEFT     0x01
#define NOS_MBTN_RIGHT    0x02
#define NOS_MBTN_MIDDLE   0x04

/* -----------------------------------------------------------------------
 * Event structures
 * ----------------------------------------------------------------------- */

typedef struct {
    int           code;   /* NOS_KEY_* or ASCII value */
    unsigned char ch;     /* ASCII char (valid when code < 0x100) */
} nos_key_event_t;

typedef struct {
    int col;              /* column (0-based, in character cells) */
    int row;              /* row (0-based) */
    int buttons;          /* NOS_MBTN_* bitmask of currently-pressed buttons */
    int clicked;          /* NOS_MBTN_* bitmask of buttons clicked this event */
} nos_mouse_event_t;

typedef struct {
    int type;             /* NOS_EVT_* */
    nos_key_event_t   key;
    nos_mouse_event_t mouse;
} nos_event_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/*
 * nos_inp_init — detect mouse driver and enable mouse events.
 * Safe to call even if no mouse driver is loaded (mouse events won't fire).
 */
void nos_inp_init(void);

/*
 * nos_inp_poll — check for a pending event without blocking.
 * Fills *evt and returns NOS_EVT_KEY, NOS_EVT_MOUSE, or NOS_EVT_NONE.
 */
int nos_inp_poll(nos_event_t *evt);

/*
 * nos_inp_wait — block until an event is available, then return it.
 */
int nos_inp_wait(nos_event_t *evt);

/*
 * nos_inp_flush — discard any pending keyboard input.
 */
void nos_inp_flush(void);

#endif /* NOS_INPUT_H */
