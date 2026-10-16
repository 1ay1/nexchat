/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Drag and drop, the two kinds nexchat has:
 *
 *  - layout drags: the channel tree and the user list can be dragged (within
 *    this process only) onto the top or bottom half of a pane side to move
 *    them there;
 *  - file drops (text/uri-list) for DCC sends.
 *
 * Callers get plain values; gtkdnd.c knows GTK3's drag signals and GTK4's
 * GtkDragSource / GtkDropTarget.
 */

#ifndef NEXCHAT_GTKDND_H
#define NEXCHAT_GTKDND_H

#include <gtk/gtk.h>

typedef enum
{
	NX_DND_CHANVIEW = 1 << 0,	/* the channel tree ("HEXCHAT_CHANVIEW") */
	NX_DND_USERLIST = 1 << 1,	/* the user list ("HEXCHAT_USERLIST") */
	/* nx_dnd_layout_dest flag: no theme highlight around the whole widget
	   (GTK3: GTK_DEST_DEFAULT_HIGHLIGHT left out) */
	NX_DND_NO_THEME_HIGHLIGHT = 1 << 8
} nx_dnd_kind;

/* a layout drag of `kind` is over w at y (widget-relative) / dropped there */
typedef void (*nx_dnd_layout_cb) (GtkWidget *w, double y, nx_dnd_kind kind, gpointer ud);
/* a drag left w */
typedef void (*nx_dnd_leave_cb) (GtkWidget *w, gpointer ud);
/* a drag is over w at x/y */
typedef void (*nx_dnd_point_cb) (GtkWidget *w, double x, double y, gpointer ud);
/* files dropped at x/y; uris is a text/uri-list ("uri\r\nuri\r\n") */
typedef void (*nx_dnd_files_cb) (GtkWidget *w, double x, double y, const char *uris,
											gpointer ud);

/* w can be dragged as `kind` (the drag icon is a shrunk picture of w) */
void nx_dnd_layout_source (GtkWidget *w, nx_dnd_kind kind);
/* w takes layout drags of the kinds in `kinds` (| NX_DND_NO_THEME_HIGHLIGHT) */
void nx_dnd_layout_dest (GtkWidget *w, guint kinds, nx_dnd_layout_cb motion,
								 nx_dnd_leave_cb leave, nx_dnd_layout_cb drop, gpointer ud);
/* w takes dropped files. motion/leave (may be NULL) see every drag w
   accepts on GTK3, file drags only on GTK4. Call before nx_dnd_layout_dest
   on the same widget, so GTK3's handler order stays what it was. */
void nx_dnd_file_dest (GtkWidget *w, nx_dnd_files_cb drop, nx_dnd_point_cb motion,
							  nx_dnd_leave_cb leave, gpointer ud);

#endif
