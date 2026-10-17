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

/* follow the system's light/dark from startup */
void platform_style_init (void);
/* the system asks for dark mode */
gboolean platform_style_dark (void);
/* Windows/macOS: tell GTK the system's current light/dark (GTK doesn't
   follow it there); nothing on Linux, where GTK gets it from the desktop */
void platform_style_sync_scheme (void);

#endif
