/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* For backends that can't tie a notification to a tab: shown plainly,
   clicks do what the desktop does, nothing to withdraw. */

#include "notification-backend.h"

void
notification_backend_show_for (const char *title, const char *text, const char *tab)
{
	notification_backend_show (title, text);
}

void
notification_backend_set_click (notification_click_cb cb)
{
}

void
notification_backend_withdraw (const char *tab)
{
}
