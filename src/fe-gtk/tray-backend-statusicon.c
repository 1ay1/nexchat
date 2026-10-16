/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* GtkStatusIcon tray backend for GTK3. GtkStatusIcon is deprecated (and
   gone in GTK4) but is still the only tray GTK3 has on X11 and Windows, so
   this file - and only this file - is built with deprecation warnings off.
   Behaviour is exactly what plugin-tray.c did with GtkStatusIcon before. */

#include "config.h"
#undef GTK_DISABLE_DEPRECATED
#undef GDK_DISABLE_DEPRECATED
#define GDK_DISABLE_DEPRECATION_WARNINGS
#include <gtk/gtk.h>

#include "tray-backend.h"

struct _tray_backend
{
	GtkStatusIcon *icon;
	GdkPixbuf *pixbuf;	/* last set, for get_icon() pointer compares */
	tray_backend_callbacks cb;
	gpointer userdata;
};

static void
sb_activate (GtkStatusIcon *icon, tray_backend *tb)
{
	if (tb->cb.activate)
		tb->cb.activate (tb, tb->userdata);
}

static void
sb_popup (GtkStatusIcon *icon, guint button, guint time, tray_backend *tb)
{
	if (tb->cb.menu)
		tb->cb.menu (tb, button, time, tb->userdata);
}

static void
sb_embedded (GObject *obj, GParamSpec *pspec, tray_backend *tb)
{
	if (tb->cb.embedded_changed)
		tb->cb.embedded_changed (tb, gtk_status_icon_is_embedded (tb->icon), tb->userdata);
}

tray_backend *
tray_backend_new (GdkPixbuf *icon, const tray_backend_callbacks *cb, gpointer userdata)
{
	tray_backend *tb;
	GtkStatusIcon *si = gtk_status_icon_new_from_pixbuf (icon);

	if (!si)
		return NULL;

	tb = g_new0 (tray_backend, 1);
	tb->icon = si;
	tb->pixbuf = icon;
	tb->cb = *cb;
	tb->userdata = userdata;

	g_signal_connect (G_OBJECT (si), "popup-menu", G_CALLBACK (sb_popup), tb);
	g_signal_connect (G_OBJECT (si), "activate", G_CALLBACK (sb_activate), tb);
	g_signal_connect (G_OBJECT (si), "notify::embedded", G_CALLBACK (sb_embedded), tb);
	return tb;
}

void
tray_backend_free (tray_backend *tb)
{
	if (!tb)
		return;
	g_signal_handlers_disconnect_matched (tb->icon, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, tb);
	g_object_unref (tb->icon);
	g_free (tb);
}

void
tray_backend_set_icon (tray_backend *tb, GdkPixbuf *icon)
{
	tb->pixbuf = icon;
	gtk_status_icon_set_from_pixbuf (tb->icon, icon);
}

GdkPixbuf *
tray_backend_get_icon (tray_backend *tb)
{
	/* GtkStatusIcon hands back the pixbuf it was given, which is what the
	   blink code compared against; keep that exact behaviour */
	return gtk_status_icon_get_pixbuf (tb->icon);
}

void
tray_backend_set_tooltip (tray_backend *tb, const char *text)
{
	gtk_status_icon_set_tooltip_text (tb->icon, text);
}

gboolean
tray_backend_is_embedded (tray_backend *tb)
{
	return gtk_status_icon_is_embedded (tb->icon);
}

void
tray_backend_popup_menu (tray_backend *tb, GtkMenu *menu, guint button, guint32 time)
{
	/* plugin-tray.c always passed a NULL position function, i.e. the menu
	   opens at the pointer; keep that */
	gtk_menu_popup (menu, NULL, NULL, NULL, NULL, button, time);
}
