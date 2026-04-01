/* NOS-DOS: NOS-SHELL
 * dialog.h - Modal dialog boxes.
 *
 * All dialogs are blocking: they process input internally and return
 * when the user confirms or cancels.  The caller is responsible for
 * repainting the screen after any dialog closes.
 *
 * License: GPL-2.0
 */

#ifndef NOS_DIALOG_H
#define NOS_DIALOG_H

/*
 * nos_dlg_msg -- display a message and wait for any keypress.
 * title: shown in the top border (may be NULL for no title)
 * msg:   body text (up to ~44 chars; longer strings are clipped)
 */
void nos_dlg_msg(const char *title, const char *msg);

/*
 * nos_dlg_confirm -- ask a yes/no question.
 * Returns 1 if user presses Y/y, 0 if N/n or Esc.
 */
int nos_dlg_confirm(const char *title, const char *msg);

/*
 * nos_dlg_input -- single-line text input dialog.
 * prompt: label displayed above the input field
 * buf:    initial value on entry; holds result on return
 *         (must be at least maxlen bytes)
 * maxlen: maximum character count including NUL terminator
 * Returns 1 on Enter (buf updated), 0 on Esc (buf unchanged).
 */
int nos_dlg_input(const char *title, const char *prompt,
                  char *buf, int maxlen);

#endif /* NOS_DIALOG_H */
