/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* What plugin-tray.c needs from a tray icon, and nothing else.
 *
 * GTK3 backend (tray-backend-statusicon.c): GtkStatusIcon. It's deprecated
 * in GTK3 and gone in GTK4, so it is the only file allowed to use it.
 * Phase 4 adds a StatusNotifierItem backend for Linux and Shell_NotifyIcon
 * for Windows behind this same interface (ROADMAP section 9). */

#ifndef NEXCHAT_TRAY_BACKEND_H
#define NEXCHAT_TRAY_BACKEND_H

#include <gtk/gtk.h>

typedef struct _tray_backend tray_backend;

typedef struct
{
	/* left click */
	void (*activate) (tray_backend *tb, gpointer userdata);
	/* right click: show the tray menu; button/time are the click's, for
	   backends that pop up a GtkMenu themselves */
	void (*menu) (tray_backend *tb, guint button, guint32 time, gpointer userdata);
	/* the icon appeared in / vanished from the panel */
	void (*embedded_changed) (tray_backend *tb, gboolean embedded, gpointer userdata);
} tray_backend_callbacks;

/* NULL if this platform/session has no tray */
tray_backend *tray_backend_new (GdkPixbuf *icon, const tray_backend_callbacks *cb, gpointer userdata);
void tray_backend_free (tray_backend *tb);

/* the pixbuf is not copied; the backend keeps a ref. get returns the last
   one set, so plugin-tray.c can compare by pointer as it always did */
void tray_backend_set_icon (tray_backend *tb, GdkPixbuf *icon);
GdkPixbuf *tray_backend_get_icon (tray_backend *tb);
void tray_backend_set_tooltip (tray_backend *tb, const char *text);
gboolean tray_backend_is_embedded (tray_backend *tb);
/* pop up a GtkMenu for the tray, positioned the way the backend's panel
   expects (GtkStatusIcon: next to the icon) */
void tray_backend_popup_menu (tray_backend *tb, GtkMenu *menu, guint button, guint32 time);

#endif
