/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* nexchat looks the same everywhere: the classic HexChat look. The only
   thing taken from the system is light or dark mode, and this file is
   where that is read. */

#include <string.h>

#include "fe-gtk.h"
#include "platform-style.h"
#include "gtkcompat.h"
#include "nxmenu.h"
#if defined(__APPLE__)
#include "platform-macos.h"
#endif

#ifdef G_OS_WIN32
#include <windows.h>
#include <dwmapi.h>
#include "gtkutil.h"
#include "win32-menu.h"

/* Settings > Personalisation > Colours > "Choose your app mode" */
static gboolean
win32_apps_dark (void)
{
	DWORD v = 1, sz = sizeof v;

	if (RegGetValueW (HKEY_CURRENT_USER,
							L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
							L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS)
		return FALSE;
	return v == 0;
}
#endif

gboolean
platform_style_dark (void)
{
	/* NEXCHAT_SCHEME=light|dark: pin it, whatever the system says */
	const char *force = g_getenv ("NEXCHAT_SCHEME");
	if (force && !g_ascii_strcasecmp (force, "dark"))
		return TRUE;
	if (force && !g_ascii_strcasecmp (force, "light"))
		return FALSE;
#ifdef G_OS_WIN32
	return win32_apps_dark ();
#elif defined(__APPLE__)
	return macos_is_dark ();
#else
	/* the desktop's choice as GTK sees it: the portal's color-scheme (GTK
	   4.20+), else a dark theme asked for by name or setting */
	GtkSettings *gs = gtk_settings_get_default ();
	gboolean prefer = FALSE, dark;
	char *theme = NULL;

	if (!gs)
		return FALSE;
#if GTK_CHECK_VERSION(4, 20, 0)
	{
		GtkInterfaceColorScheme cs;
		g_object_get (gs, "gtk-interface-color-scheme", &cs, NULL);
		if (cs == GTK_INTERFACE_COLOR_SCHEME_DARK)
			return TRUE;
		if (cs == GTK_INTERFACE_COLOR_SCHEME_LIGHT)
			return FALSE;
	}
#endif
	g_object_get (gs, "gtk-application-prefer-dark-theme", &prefer, "gtk-theme-name", &theme, NULL);
	dark = prefer || (theme && g_str_has_suffix (theme, "-dark")) || (theme && strstr (theme, ":dark"));
	g_free (theme);
	return dark;
#endif
}

#ifdef G_OS_WIN32
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* the real title bar is light or dark with the app */
static void
window_title_bar (GtkWindow *w)
{
	HWND hwnd = gtkutil_window_hwnd (w);
	BOOL dark = platform_style_dark ();

	if (hwnd)
		DwmSetWindowAttribute (hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
}

static gboolean
map_hook (GSignalInvocationHint *ih, guint n, const GValue *v, gpointer d)
{
	GtkWidget *w = g_value_get_object (&v[0]);

	if (GTK_IS_WINDOW (w))
		window_title_bar (GTK_WINDOW (w));
	return TRUE;
}
#endif

/* GTK's own widgets (fields, labels, lists) must be light or dark exactly
   when colors[] is: tell GTK what platform_style_dark() says. On Windows
   and macOS GTK doesn't follow the system at all; on Linux it does, but
   NEXCHAT_SCHEME can pin the answer the other way. */
void
platform_style_sync_scheme (void)
{
	static int last = -1;
	GtkSettings *gs = gtk_settings_get_default ();
	gboolean dark = platform_style_dark ();

	if (!gs || dark == last)
		return;
	last = dark;
#if !defined(G_OS_WIN32) && !defined(__APPLE__)
	/* Linux: GTK already follows the desktop; writing the setting would
	   stop it following later changes, so only when it's pinned */
	if (!g_getenv ("NEXCHAT_SCHEME"))
		return;
#endif
#if GTK_CHECK_VERSION(4, 20, 0)
	g_object_set (gs, "gtk-interface-color-scheme",
					  dark ? GTK_INTERFACE_COLOR_SCHEME_DARK : GTK_INTERFACE_COLOR_SCHEME_LIGHT, NULL);
#else
	g_object_set (gs, "gtk-application-prefer-dark-theme", dark, NULL);
#endif
#ifdef G_OS_WIN32
	{
		GListModel *tops = gtk_window_get_toplevels ();
		guint i;

		for (i = 0; i < g_list_model_get_n_items (tops); i++)
		{
			GtkWindow *w = g_list_model_get_item (tops, i);
			window_title_bar (w);
			g_object_unref (w);
		}
	}
#endif
}

void
platform_style_init (void)
{
	platform_style_sync_scheme ();
#if defined(__APPLE__)
	/* the menu goes in the menu bar at the top of the screen */
	nx_menu_use_global_menubar (nx_app ());
#endif
#if defined(G_OS_WIN32)
	g_type_class_unref (g_type_class_ref (GTK_TYPE_WINDOW));
	g_signal_add_emission_hook (g_signal_lookup ("map", GTK_TYPE_WIDGET), 0, map_hook, NULL, NULL);
	/* the tray's Win32 menu goes dark with the system */
	win32_menu_follow_dark_mode ();
#endif
}
