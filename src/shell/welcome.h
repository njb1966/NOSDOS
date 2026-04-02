/* NOS-DOS: NOS-SHELL
 * welcome.h - First-boot welcome screen interface.
 * License: GPL-2.0
 */

#ifndef NOS_WELCOME_H
#define NOS_WELCOME_H

/*
 * nos_welcome_needed
 * Returns 1 if the welcome screen has not yet been shown (flag file absent).
 */
int nos_welcome_needed(void);

/*
 * nos_welcome_show
 * Draws and runs the full-screen welcome dialog.
 * Blocks until the user presses any key.
 */
void nos_welcome_show(void);

/*
 * nos_welcome_mark_shown
 * Creates the flag file so subsequent boots skip the welcome screen.
 */
void nos_welcome_mark_shown(void);

#endif /* NOS_WELCOME_H */
