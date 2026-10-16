/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* No tray: tray_backend_new () says so, and plugin-tray.c runs without an
   icon (as on a desktop without a panel). Used by the GTK4 build until the
   StatusNotifierItem backend exists. */

#include "tray-backend.h"

tray_backend *
tray_backend_new (GdkPixbuf *icon, const tray_backend_callbacks *cb, gpointer userdata)
{
	return NULL;
}

void
tray_backend_free (tray_backend *tb)
{
}

void
tray_backend_set_icon (tray_backend *tb, GdkPixbuf *icon)
{
}

GdkPixbuf *
tray_backend_get_icon (tray_backend *tb)
{
	return NULL;
}

void
tray_backend_set_tooltip (tray_backend *tb, const char *text)
{
}

gboolean
tray_backend_is_embedded (tray_backend *tb)
{
	return FALSE;
}

void
tray_backend_show_menu (tray_backend *tb, nx_menu *menu, guint button, guint32 time)
{
#if GTK_CHECK_VERSION(4, 0, 0)
	nx_menu_free (menu);
#else
	g_object_ref_sink (menu);
	g_object_unref (menu);
#endif
}
