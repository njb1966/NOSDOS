/* NOS-DOS: NOS-SHELL
 * viewer.h - Quick file viewer (F3).
 *
 * Supports text mode (line-based) and hex mode (F4 to toggle).
 * Loads up to VWR_BUF bytes of the file; larger files are truncated
 * with a notice in the status bar.
 *
 * Call nos_viewer_open() and block until the user presses Esc / F3 / Enter.
 * The caller is responsible for repainting the shell screen afterwards.
 *
 * License: GPL-2.0
 */

#ifndef NOS_VIEWER_H
#define NOS_VIEWER_H

/* Maximum file data loaded into the viewer buffer (bytes). */
#define VWR_BUF       16384
/* Maximum number of text lines indexed. */
#define VWR_MAX_LINES  1000

/*
 * nos_viewer_open -- open and view the file at path.
 * Blocks until the user exits the viewer.
 * If the file cannot be opened, displays a brief error message and returns.
 */
void nos_viewer_open(const char *path);

#endif /* NOS_VIEWER_H */
