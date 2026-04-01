/* NOS-DOS: NOS-DETECT
 * mouse.h - Mouse detection interface.
 * License: GPL-2.0
 */

#ifndef NOS_DETECT_MOUSE_H
#define NOS_DETECT_MOUSE_H

/* Mouse type codes returned by INT 33h / AX=0024h (CH register).
 * Stored in nos_mouseinfo_t.mouse_type. */
#define NOS_MOUSE_TYPE_UNKNOWN  0x00
#define NOS_MOUSE_TYPE_BUS      0x01
#define NOS_MOUSE_TYPE_SERIAL   0x02
#define NOS_MOUSE_TYPE_INPORT   0x03
#define NOS_MOUSE_TYPE_PS2      0x04
#define NOS_MOUSE_TYPE_HP       0x05

typedef struct {
    int           present;      /* Non-zero if mouse driver installed       */
    unsigned int  buttons;      /* Number of buttons (typically 2 or 3)     */
    unsigned char mouse_type;   /* NOS_MOUSE_TYPE_* (0 if unavailable)      */
    unsigned char driver_major; /* Driver version major (0 if unavailable)  */
    unsigned char driver_minor; /* Driver version minor (0 if unavailable)  */
    unsigned char irq;          /* Mouse IRQ number     (0 if unavailable)  */
} nos_mouseinfo_t;

/* Fill *info with detected mouse information. */
void nos_detect_mouse(nos_mouseinfo_t *info);

#endif /* NOS_DETECT_MOUSE_H */
