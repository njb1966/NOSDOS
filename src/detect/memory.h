/* NOS-DOS: NOS-DETECT
 * memory.h - Memory detection interface.
 * License: GPL-2.0
 */

#ifndef NOS_DETECT_MEMORY_H
#define NOS_DETECT_MEMORY_H

/* All memory quantities are in kilobytes unless noted otherwise. */
typedef struct {
    unsigned int conv_kb;        /* Conventional memory (INT 12h)           */
    int          xms_present;    /* Non-zero if XMS driver present           */
    unsigned int xms_total_kb;   /* Total free XMS (DX from XMS fn 08h)     */
    unsigned int xms_largest_kb; /* Largest contiguous free XMS block        */
    int          ems_present;    /* Non-zero if EMS manager present          */
    unsigned int ems_total_kb;   /* Total EMS in KB  (DX pages * 16)        */
    unsigned int ems_free_kb;    /* Free  EMS in KB  (BX pages * 16)        */
} nos_meminfo_t;

/* Fill *info with detected memory figures.
 * Zeroes the struct first; absent subsystems leave their fields at 0. */
void nos_detect_memory(nos_meminfo_t *info);

#endif /* NOS_DETECT_MEMORY_H */
