/* HexChat
 * Copyright (C) 2015 Patrick Griffis.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef HEXCHAT_PLUGIN_NOTIFICATION_BACKEND_H
#define HEXCHAT_PLUGIN_NOTIFICATION_BACKEND_H

int notification_backend_supported (void);
void notification_backend_show (const char *title, const char *text);
int notification_backend_init (const char **error);
void notification_backend_deinit (void);

/* backends that can: the notification is about a tab ("network/channel");
   clicking it calls the click callback with that, and withdraw takes a
   tab's notifications back once it's been read. Others ignore the tab. */
typedef void (*notification_click_cb) (const char *tab);
void notification_backend_show_for (const char *title, const char *text, const char *tab);
void notification_backend_set_click (notification_click_cb cb);
void notification_backend_withdraw (const char *tab);

#endif
