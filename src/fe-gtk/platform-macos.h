/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_PLATFORM_MACOS_H
#define NEXCHAT_PLATFORM_MACOS_H

#include <glib.h>
#include <gdk/gdk.h>

/* bring the app to the front (started from a terminal it stays behind) */
void macos_activate (void);
/* inside nexchat.app: use the bundle's Resources for plugins, data and
   translations (textdomain is rebound there) */
void macos_bundle_setup (const char *textdomain);
/* tests: write the menu bar macOS shows to path */
void macos_dump_main_menu (const char *path);
/* tests: click "Menu/Item" in that menu bar */
gboolean macos_activate_menu_path (const char *path);
/* tests: print the frontmost app and our key window on stderr */
void macos_dump_focus (void);
/* bounce the Dock icon until the app is activated (TRUE) or stop (FALSE) */
void macos_request_attention (gboolean urgent);
/* the system is in dark mode */
gboolean macos_is_dark (void);
/* the app's windows light (0), dark (1), or as the system says (-1) */
void macos_set_dark (int dark);
/* NSColor's text background, text, selection, selected text */
void macos_text_colors (GdkRGBA out[4]);
/* the NSWindow (gdk_macos_surface_get_native_window) moved/placed; y
   counts down from the top of the main screen like elsewhere */
gboolean macos_window_get_position (void *nswindow, int *x, int *y);
void macos_window_move (void *nswindow, int x, int y);
void macos_window_place (void *nswindow, void *parent, int how);

/* macos-app.m: how a Mac app behaves (see the file's comment) */
typedef struct
{
	void (*reopen) (void);					/* Dock icon clicked / opened again */
	void (*open_url) (const char *url);	/* irc:// from another app */
	void (*activated) (void);				/* the app came to the front */
	void (*woke) (void);					/* back from sleep */
	void (*network_back) (void);			/* a network is reachable (again) */
	void (*appearance) (void);				/* light/dark switched */
} macos_app_hooks;
void macos_app_install (const macos_app_hooks *h);
/* the Dock icon's badge: a count, 0 for none */
void macos_set_badge (int count);

#endif
