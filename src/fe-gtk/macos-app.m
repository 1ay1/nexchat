/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* How a Mac app behaves, where GTK doesn't do it:

   - closing the window doesn't quit: the app stays in the Dock, and
     clicking the Dock icon (or opening the app again) brings it back;
   - irc:// and ircs:// links from other apps open in the running app
     (Apple events, not a second process);
   - after sleep, connections are checked right away instead of waiting
     minutes for a ping timeout; after a network change too;
   - a light/dark switch is followed the moment it happens (the system's
     notification, no polling);
   - the Dock icon shows how many highlights and private messages are
     waiting, like Messages or Mail.

   GTK owns NSApp's delegate (GtkApplicationQuartzDelegate). Its class gets
   our methods added at startup with class_addMethod, so both keep working:
   GTK's Quit handling and ours. */

#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#import <Network/Network.h>
#include <glib.h>

#include "platform-macos.h"

static macos_app_hooks hooks;

/* -applicationShouldHandleReopen:hasVisibleWindows: the Dock icon was
   clicked, or the app opened again from Finder or Spotlight */
static BOOL
nx_reopen (id self, SEL _cmd, NSApplication *app, BOOL visible)
{
	if (hooks.reopen)
		hooks.reopen ();
	return NO;	/* we've shown the window; AppKit doesn't need to */
}

/* -application:openURLs: irc://, ircs:// from a browser or `open` */
static void
nx_open_urls (id self, SEL _cmd, NSApplication *app, NSArray<NSURL *> *urls)
{
	for (NSURL *u in urls)
		if (hooks.open_url && [u absoluteString])
			hooks.open_url ([[u absoluteString] UTF8String]);
}

/* -applicationDidBecomeActive: the user came back to nexchat */
static void
nx_became_active (id self, SEL _cmd, NSNotification *n)
{
	if (hooks.activated)
		hooks.activated ();
}

static void
add_method (Class c, SEL sel, IMP imp, const char *types)
{
	/* replace an existing one only if GTK ever grows it: then ours wins
	   for these few, which GTK doesn't implement */
	if (!class_addMethod (c, sel, imp, types))
		class_replaceMethod (c, sel, imp, types);
}

@interface NxSystemObserver : NSObject
@end

@implementation NxSystemObserver
- (void)didWake:(NSNotification *)n
{
	if (hooks.woke)
		hooks.woke ();
}
- (void)themeChanged:(NSNotification *)n
{
	/* the distributed notification arrives before NSApp's appearance has
	   caught up: look on the next turn of the loop */
	dispatch_async (dispatch_get_main_queue (), ^{
		if (hooks.appearance)
			hooks.appearance ();
	});
}
@end

static NxSystemObserver *observer;
static nw_path_monitor_t monitor;
static int path_seen;				/* 0: none yet, else 1 + was satisfied */
static char *path_key;				/* which interfaces: a new network is a change too */

/* the network path changed: back after being down, or another network
   (Wi-Fi to Ethernet, another Wi-Fi). Connections made over the old one
   are dead even if no error has come yet. */
static void
path_changed (nw_path_t path)
{
	gboolean up = nw_path_get_status (path) == nw_path_status_satisfied;
	GString *k = g_string_new (NULL);
	gboolean changed;

	nw_path_enumerate_interfaces (path, ^bool (nw_interface_t i) {
		g_string_append_printf (k, "%s,", nw_interface_get_name (i));
		return true;
	});
	changed = path_seen && up && (path_seen == 1 || g_strcmp0 (k->str, path_key) != 0);
	path_seen = up ? 2 : 1;
	g_free (path_key);
	path_key = g_string_free (k, FALSE);
	if (changed && hooks.network_back)
		hooks.network_back ();
}

void
macos_app_install (const macos_app_hooks *h)
{
	id delegate = [NSApp delegate];
	Class c;

	hooks = *h;
	if (delegate)
	{
		c = [delegate class];
		add_method (c, @selector(applicationShouldHandleReopen:hasVisibleWindows:),
						(IMP) nx_reopen, "c@:@c");
		add_method (c, @selector(application:openURLs:), (IMP) nx_open_urls, "v@:@@");
		add_method (c, @selector(applicationDidBecomeActive:), (IMP) nx_became_active, "v@:@");
	}

	observer = [[NxSystemObserver alloc] init];
	[[[NSWorkspace sharedWorkspace] notificationCenter]
		addObserver:observer selector:@selector(didWake:)
				 name:NSWorkspaceDidWakeNotification object:nil];
	[[NSDistributedNotificationCenter defaultCenter]
		addObserver:observer selector:@selector(themeChanged:)
				 name:@"AppleInterfaceThemeChangedNotification" object:nil];

	/* told when the network comes, goes or changes */
	monitor = nw_path_monitor_create ();
	nw_path_monitor_set_queue (monitor, dispatch_get_main_queue ());
	nw_path_monitor_set_update_handler (monitor, ^(nw_path_t p) { path_changed (p); });
	nw_path_monitor_start (monitor);
}

void
macos_set_badge (int count)
{
	NSString *s = count > 0 ? (count > 99 ? @"99+" : [NSString stringWithFormat:@"%d", count]) : nil;

	if (![[[NSApp dockTile] badgeLabel] isEqualToString:s] && ([[NSApp dockTile] badgeLabel] || s))
		[[NSApp dockTile] setBadgeLabel:s];
}
