/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_PLATFORM_STYLE_H
#define NEXCHAT_PLATFORM_STYLE_H

#include <gtk/gtk.h>

/* hook windows as they map (Windows: title bar colour) */
void platform_style_init (void);
/* append the platform's native look (Windows 11 / macOS) to the app CSS;
   nothing on Linux or with gui_native_look off */
void platform_style_css (GString *css);
/* the bits that must come after nexchat's own rules */
void platform_style_css_late (GString *css);
/* the system asks for dark mode */
gboolean platform_style_dark (void);
/* macOS: tell GTK the system's current light/dark (GTK doesn't follow
   NSAppearance itself); nothing elsewhere, where GTK gets it from the
   desktop */
void platform_style_sync_scheme (void);
#ifdef G_OS_WIN32
/* dark title bar and rounded corners to match (Windows 11) */
void platform_style_window_hwnd (void *hwnd);
#endif

#endif
