/* nexchat TUI — desktop notifications through the terminal (OSC 777 / 9),
   the backend for plugin-notification.c */

#include "config.h"
#include <glib.h>

#include "../fe-gtk/notifications/notification-backend.h"
#include "bridge.h"

int notification_backend_supported (void) { return 1; }
int notification_backend_init (const char **error) { return 1; }
void notification_backend_deinit (void) {}

void
notification_backend_show (const char *title, const char *text)
{
	nx_ui_notify (title, text);
}

void
notification_backend_show_for (const char *title, const char *text, const char *tab)
{
	nx_ui_notify (title, text);
}

void notification_backend_set_click (notification_click_cb cb) {}
void notification_backend_withdraw (const char *tab) {}
