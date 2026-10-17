/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_WIN32_MENU_H
#define NEXCHAT_WIN32_MENU_H

#include <gtk/gtk.h>
#include "nxmenu.h"

/* show menu as a Win32 popup at the pointer, owned by the HWND owner;
   returns the item picked (not run yet) or NULL */
nx_item *win32_menu_track (nx_menu *menu, void *owner);
/* popup menus go dark with the system's dark mode */
void win32_menu_follow_dark_mode (void);
/* the Windows font / colour dialogs (NULL / FALSE: cancelled) */
char *win32_choose_font (void *owner, const char *font);
gboolean win32_choose_color (void *owner, GdkRGBA *color);

#endif
