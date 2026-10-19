/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* macOS notifications through UNUserNotificationCenter (Notification
   Center, Focus modes, the per-app settings in System Settings). A
   notification remembers the tab it's about; clicking it brings nexchat to
   the front on that tab, like Messages does. Notifications for a tab are
   taken back from Notification Center once it's been read.

   Needs a bundle (nexchat.app): an unbundled binary has no identity for
   the notification center, and then nothing is shown (supported() says
   so, and the plugin says notifications aren't available). */

#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>
#include <glib.h>

#include "notification-backend.h"

static notification_click_cb click_cb;
static gboolean authorized;

@interface NxNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation NxNotificationDelegate
/* shown even while nexchat is in front? only when its window isn't the
   key window (another app's window covering it, or minimized): what the
   plugin decided to send is meant to be seen */
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
		 willPresentNotification:(UNNotification *)n
			withCompletionHandler:(void (^)(UNNotificationPresentationOptions))done
{
	if ([NSApp keyWindow] != nil && [NSApp isActive])
		done (UNNotificationPresentationOptionNone);
	else
		done (UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound |
				UNNotificationPresentationOptionList);
}

/* clicked: to the front, on its tab */
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
 didReceiveNotificationResponse:(UNNotificationResponse *)response
			withCompletionHandler:(void (^)(void))done
{
	NSString *tab = response.notification.request.content.threadIdentifier;

	if ([response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier] && click_cb)
	{
		const char *t = tab ? [tab UTF8String] : NULL;
		char *copy = g_strdup (t);
		/* GTK isn't reentrant: on the main queue (= GLib's main loop here) */
		dispatch_async (dispatch_get_main_queue (), ^{
			click_cb (copy);
			g_free (copy);
		});
	}
	done ();
}
@end

static NxNotificationDelegate *delegate;

int
notification_backend_supported (void)
{
	return [[NSBundle mainBundle] bundleIdentifier] != nil;
}

int
notification_backend_init (const char **error)
{
	UNUserNotificationCenter *c;

	if (!notification_backend_supported ())
	{
		*error = "notifications need nexchat.app (run from the bundle)";
		return 0;
	}
	c = [UNUserNotificationCenter currentNotificationCenter];
	delegate = [[NxNotificationDelegate alloc] init];
	c.delegate = delegate;
	/* asks once; System Settings > Notifications > nexchat after that */
	[c requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound |
												  UNAuthorizationOptionBadge)
						completionHandler:^(BOOL granted, NSError *e) {
							authorized = granted;
						}];
	return 1;
}

void
notification_backend_deinit (void)
{
	[[UNUserNotificationCenter currentNotificationCenter] setDelegate:nil];
	delegate = nil;
}

void
notification_backend_set_click (notification_click_cb cb)
{
	click_cb = cb;
}

void
notification_backend_show_for (const char *title, const char *text, const char *tab)
{
	UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
	UNNotificationRequest *req;

	content.title = [NSString stringWithUTF8String:title ? title : ""];
	content.body = [NSString stringWithUTF8String:text ? text : ""];
	content.sound = [UNNotificationSound defaultSound];
	/* grouped per tab in Notification Center, and found again on click */
	if (tab)
		content.threadIdentifier = [NSString stringWithUTF8String:tab];
	req = [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
															 content:content trigger:nil];
	[[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:req
																				withCompletionHandler:nil];
}

void
notification_backend_show (const char *title, const char *text)
{
	notification_backend_show_for (title, text, NULL);
}

void
notification_backend_withdraw (const char *tab)
{
	UNUserNotificationCenter *c = [UNUserNotificationCenter currentNotificationCenter];
	NSString *t;

	if (!tab)
		return;
	t = [NSString stringWithUTF8String:tab];
	/* the ones for this tab are read now: out of Notification Center */
	[c getDeliveredNotificationsWithCompletionHandler:^(NSArray<UNNotification *> *list) {
		NSMutableArray *ids = [NSMutableArray array];
		for (UNNotification *n in list)
			if ([n.request.content.threadIdentifier isEqualToString:t])
				[ids addObject:n.request.identifier];
		if (ids.count)
			[c removeDeliveredNotificationsWithIdentifiers:ids];
	}];
}
