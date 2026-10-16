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

/* the menu open now, if any: a new right-click closes it first */
static GtkWidget *open_menu;

#ifdef G_OS_WIN32
/* GtkMenu from a tray icon on Windows doesn't go away when the pointer
   leaves it: close it after 2s outside (what plugin-tray.c always did) */
static guint menu_timer;
static gint64 menu_inactivetime;

static gboolean
menu_enter_cb (GtkWidget *menu)
{
	menu_inactivetime = 0;
	return FALSE;
}

static gboolean
menu_left_cb (GtkWidget *menu)
{
	menu_inactivetime = g_get_real_time ();
	return FALSE;
}
#endif

static void
menu_destroy (GtkWidget *menu, gpointer userdata)
{
	if (open_menu == menu)
		open_menu = NULL;
	gtk_widget_destroy (menu);
	g_object_unref (menu);
#ifdef G_OS_WIN32
	if (menu_timer)
		g_source_remove (menu_timer);
	menu_timer = 0;
#endif
}

#ifdef G_OS_WIN32
static gboolean
menu_check_hide (GtkWidget *menu)
{
	if (menu_inactivetime && g_get_real_time () - menu_inactivetime >= 2000000)
	{
		menu_timer = 0;	/* this source ends by returning REMOVE */
		menu_destroy (menu, NULL);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}
#endif

void
tray_backend_show_menu (tray_backend *tb, nx_menu *nxmenu, guint button, guint32 time)
{
	/* GTK3: an nx_menu is the GtkMenu */
	GtkWidget *menu = (GtkWidget *) nxmenu;

	if (open_menu)
		menu_destroy (open_menu, NULL);
	open_menu = menu;

	g_object_ref (menu);
	g_object_ref_sink (menu);
	g_object_unref (menu);
	g_signal_connect (G_OBJECT (menu), "selection-done", G_CALLBACK (menu_destroy), NULL);
#ifdef G_OS_WIN32
	menu_inactivetime = 0;
	g_signal_connect (G_OBJECT (menu), "leave-notify-event", G_CALLBACK (menu_left_cb), NULL);
	g_signal_connect (G_OBJECT (menu), "enter-notify-event", G_CALLBACK (menu_enter_cb), NULL);
	menu_timer = g_timeout_add (500, (GSourceFunc) menu_check_hide, menu);
#endif
	/* plugin-tray.c always passed a NULL position function, i.e. the menu
	   opens at the pointer; keep that */
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, button, time);
}
