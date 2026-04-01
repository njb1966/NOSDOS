/* NOS-DOS: NOS-SHELL
 * shellcfg.h - Shell configuration (F2).
 *
 * Reads and writes C:\NOS\SHELL\SHELL.CFG (simple key=value format).
 * Currently manages: sort order for both panels.
 *
 * Config file format:
 *   sort=name        (or ext / size / date)
 *
 * License: GPL-2.0
 */

#ifndef NOS_SHELLCFG_H
#define NOS_SHELLCFG_H

/*
 * nos_cfg_load -- read SHELL.CFG and return the stored sort mode
 * (NOS_SORT_NAME / EXT / SIZE / DATE from panel.h).
 * Returns NOS_SORT_NAME if the file does not exist or is unreadable.
 */
int nos_cfg_load(void);

/*
 * nos_cfg_save -- write the current sort mode to SHELL.CFG.
 */
void nos_cfg_save(int sort_mode);

/*
 * nos_cfg_sort_dialog -- display a sort-order selection dialog.
 * current: the sort mode currently in use (highlighted by default).
 * Returns the newly selected sort mode, or -1 if the user pressed Esc.
 */
int nos_cfg_sort_dialog(int current);

/*
 * nos_hwcfg_net_present -- check NOS-HW.CFG for a detected network adapter.
 * Returns 1 if [NETWORK] PRESENT=1 is in C:\NOS\SYSTEM\NOS-HW.CFG,
 * 0 if absent, unreadable, or no network detected.
 */
int nos_hwcfg_net_present(void);

#endif /* NOS_SHELLCFG_H */
