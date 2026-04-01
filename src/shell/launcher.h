/* NOS-DOS: NOS-SHELL
 * launcher.h - Application launcher (F9).
 *
 * Reads C:\NOS\SHELL\LAUNCHER.CFG (pipe-delimited "Name|Command" lines)
 * and presents a scrollable selection list.  Executing a selection
 * temporarily restores the screen, runs the command, then returns.
 *
 * Config file format (one entry per line, '#' = comment):
 *   FreeDOS Editor|EDIT.COM
 *   mTCP Telnet|C:\NOS\SYSTEM\TELNET.EXE
 *
 * License: GPL-2.0
 */

#ifndef NOS_LAUNCHER_H
#define NOS_LAUNCHER_H

/*
 * nos_launcher_show -- open the launcher dialog.
 * Blocks until the user launches an app (which runs and returns)
 * or presses Esc.  Caller repaints the screen on return.
 */
void nos_launcher_show(void);

#endif /* NOS_LAUNCHER_H */
