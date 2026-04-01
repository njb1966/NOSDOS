/* NOS-DOS: NOS-DETECT
 * genconf.h - Config file generator interface.
 * License: GPL-2.0
 */

#ifndef NOS_DETECT_GENCONF_H
#define NOS_DETECT_GENCONF_H

#include "memory.h"
#include "video.h"
#include "mouse.h"
#include "sound.h"
#include "network.h"

/*
 * nos_genconf - Generate CONFIG.SYS, AUTOEXEC.BAT, and NOS-HW.CFG.
 *
 * Template variables substituted:
 *   CONFIG.TPL   : {{JEMMEX_OPTS}}    JEMMEX command line options
 *                  {{MOUSE_LINE}}     Full DEVICE=CTMOUSE line, or empty
 *   AUTOEXEC.TPL : {{BLASTER_LINE}}   SET BLASTER=... line, or empty
 *                  {{PKT_DRIVER_LINE}} Packet driver load line, or REM
 *
 * Parameters:
 *   mem, vid, mou, snd, net  - detection results from the five modules
 *   config_tpl   - path to CONFIG.TPL  (e.g. "C:\NOS\SYSTEM\CONFIG.TPL")
 *   autoexec_tpl - path to AUTOEXEC.TPL
 *   config_out   - output path for CONFIG.SYS  (e.g. "C:\CONFIG.SYS")
 *   autoexec_out - output path for AUTOEXEC.BAT
 *   hwcfg_out    - output path for NOS-HW.CFG
 *
 * Returns 0 on success, non-zero on any I/O error.
 * On error, any partially-written output files are left in place —
 * the caller should not reboot until the return value is checked.
 */
int nos_genconf(
    const nos_meminfo_t   *mem,
    const nos_videoinfo_t *vid,
    const nos_mouseinfo_t *mou,
    const nos_soundinfo_t *snd,
    const nos_netinfo_t   *net,
    const char *config_tpl,
    const char *autoexec_tpl,
    const char *config_out,
    const char *autoexec_out,
    const char *hwcfg_out
);

#endif /* NOS_DETECT_GENCONF_H */
