/* NOS-DOS: NOS-DETECT
 * sound.h - Sound Blaster detection interface.
 * License: GPL-2.0
 */

#ifndef NOS_DETECT_SOUND_H
#define NOS_DETECT_SOUND_H

/* BLASTER T= card type codes */
#define NOS_SND_TYPE_UNKNOWN  0x00
#define NOS_SND_TYPE_SB10     0x01  /* Sound Blaster 1.0          */
#define NOS_SND_TYPE_SB15     0x02  /* Sound Blaster 1.5          */
#define NOS_SND_TYPE_SBPRO1   0x03  /* Sound Blaster Pro (mono)   */
#define NOS_SND_TYPE_SB20     0x04  /* Sound Blaster 2.0          */
#define NOS_SND_TYPE_SBPRO2   0x05  /* Sound Blaster Pro 2        */
#define NOS_SND_TYPE_SB16     0x06  /* Sound Blaster 16           */

typedef struct {
    int           present;    /* Non-zero if SB-compatible card found    */
    int           from_env;   /* Non-zero if source was BLASTER env var  */
    unsigned int  port;       /* Base I/O port (e.g. 0x220)              */
    unsigned char irq;        /* IRQ number                              */
    unsigned char dma_low;    /* 8-bit DMA channel                       */
    unsigned char dma_high;   /* 16-bit DMA channel (SB16; 0 if N/A)    */
    unsigned char card_type;  /* NOS_SND_TYPE_* (0 if not parsed)        */
} nos_soundinfo_t;

/* Fill *info with detected sound card information. */
void nos_detect_sound(nos_soundinfo_t *info);

#endif /* NOS_DETECT_SOUND_H */
