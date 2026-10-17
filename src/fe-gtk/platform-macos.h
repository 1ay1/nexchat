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
/* the system accent colour */
gboolean macos_accent (double *r, double *g, double *b);
/* the NSWindow (gdk_macos_surface_get_native_window) moved/placed; y
   counts down from the top of the main screen like elsewhere */
gboolean macos_window_get_position (void *nswindow, int *x, int *y);
void macos_window_move (void *nswindow, int x, int y);
void macos_window_place (void *nswindow, void *parent, int how);

#endif
