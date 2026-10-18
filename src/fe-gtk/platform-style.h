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
/* the system's colours for text: background, text, selection, selected
   text ("native" color scheme) */
void platform_style_text_colors (GdkRGBA out[4]);
/* make GTK's own widgets match platform_style_dark() (Windows/macOS: GTK
   doesn't follow the system there; Linux: only when NEXCHAT_SCHEME pins it) */
void platform_style_sync_scheme (void);

#endif
