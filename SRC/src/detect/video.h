/* NOS-DOS: NOS-DETECT
 * video.h - Video adapter detection interface.
 * License: GPL-2.0
 */

#ifndef NOS_DETECT_VIDEO_H
#define NOS_DETECT_VIDEO_H

/* Display Combination Code values returned by INT 10h/AX=1A00h.
 * Stored in nos_videoinfo_t.display_code. */
#define NOS_VID_DCC_NONE      0x00  /* No display                  */
#define NOS_VID_DCC_MDA       0x01  /* MDA/Hercules mono           */
#define NOS_VID_DCC_CGA       0x02  /* CGA                         */
#define NOS_VID_DCC_EGA_COLOR 0x04  /* EGA with color monitor      */
#define NOS_VID_DCC_EGA_MONO  0x05  /* EGA with mono monitor       */
#define NOS_VID_DCC_VGA_MONO  0x07  /* VGA with mono monitor       */
#define NOS_VID_DCC_VGA_COLOR 0x08  /* VGA with color monitor      */

typedef struct {
    int           vga_present;   /* Non-zero if VGA adapter present          */
    int           vesa_present;  /* Non-zero if VESA BIOS Extensions present */
    unsigned int  vesa_version;  /* VESA version, BCD: 0x0200 = VBE 2.0     */
    unsigned char display_code;  /* DCC active display (NOS_VID_DCC_*)       */
    unsigned char current_mode;  /* Video mode at time of detection          */
    unsigned char text_cols;     /* Text columns (usually 80)                */
} nos_videoinfo_t;

/* Fill *info with detected video adapter information. */
void nos_detect_video(nos_videoinfo_t *info);

#endif /* NOS_DETECT_VIDEO_H */
