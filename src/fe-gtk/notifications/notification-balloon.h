/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_NOTIFICATION_BALLOON_H
#define NEXCHAT_NOTIFICATION_BALLOON_H

/* Windows: notifications as a tray balloon (a toast on 10/11) */
int balloon_notification_init (void);
void balloon_notification_show (const char *title, const char *text);
void balloon_notification_deinit (void);

#endif
