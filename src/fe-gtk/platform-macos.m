/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* macOS bits GTK4 doesn't cover: the Dock bounce for urgency, the
   system's light/dark appearance and accent colour, and the app's
   name in the menu bar. */

#import <AppKit/AppKit.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "platform-macos.h"

/* Cocoa's y grows upward from the bottom of the main screen; the rest
   of nexchat (and gui_win_top) counts down from the top */
static CGFloat
flip_y (NSWindow *w, CGFloat top_y)
{
	CGFloat h = NSMaxY ([[[NSScreen screens] firstObject] frame]);
	return h - top_y - (w ? NSHeight ([w frame]) : 0);
}

gboolean
macos_window_get_position (void *nswindow, int *x, int *y)
{
	NSWindow *w = (__bridge NSWindow *) nswindow;
	NSRect f;

	if (!w)
		return FALSE;
	f = [w frame];
	*x = (int) f.origin.x;
	*y = (int) flip_y (w, f.origin.y);
	return TRUE;
}

void
macos_window_move (void *nswindow, int x, int y)
{
	NSWindow *w = (__bridge NSWindow *) nswindow;

	if (w)
		[w setFrameOrigin:NSMakePoint (x, flip_y (w, y))];
}

/* how: 1 at the pointer, 2 centred on parent, else centred on screen */
void
macos_window_place (void *nswindow, void *parent, int how)
{
	NSWindow *w = (__bridge NSWindow *) nswindow;
	NSWindow *p = (__bridge NSWindow *) parent;
	NSRect f, area;
	NSPoint o;

	if (!w)
		return;
	f = [w frame];
	if (how == 2 && p)
	{
		NSRect pf = [p frame];
		o = NSMakePoint (NSMidX (pf) - NSWidth (f) / 2, NSMidY (pf) - NSHeight (f) / 2);
		area = [[p screen] visibleFrame];
	}
	else if (how == 1)
	{
		NSPoint m = [NSEvent mouseLocation];
		o = NSMakePoint (m.x - NSWidth (f) / 2, m.y - NSHeight (f) / 2);
		area = [[NSScreen mainScreen] visibleFrame];
		for (NSScreen *s in [NSScreen screens])
			if (NSPointInRect (m, [s frame]))
				area = [s visibleFrame];
	}
	else
	{
		[w center];
		return;
	}
	o.x = MAX (NSMinX (area), MIN (o.x, NSMaxX (area) - NSWidth (f)));
	o.y = MAX (NSMinY (area), MIN (o.y, NSMaxY (area) - NSHeight (f)));
	[w setFrameOrigin:o];
}

/* A program started from a terminal (not through Launch Services) stays
   behind it: make it a regular app with a Dock icon and bring it to the
   front, the way opening an .app does */
void
macos_activate (void)
{
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	if (@available (macOS 14.0, *))
		[NSApp activate];
	else
		[NSApp activateIgnoringOtherApps:YES];
}

static void
dump_menu (FILE *f, NSMenu *m, int depth)
{
	for (NSMenuItem *it in [m itemArray])
	{
		if ([it isSeparatorItem])
			fprintf (f, "%*s----\n", depth * 2, "");
		else
			fprintf (f, "%*s%s%s%s\n", depth * 2, "", [[it title] UTF8String],
						[[it keyEquivalent] length] ? "  [" : "",
						[[it keyEquivalent] length] ? [[[it keyEquivalent] stringByAppendingString:@"]"] UTF8String] : "");
		if ([it hasSubmenu] && depth < 2)
			dump_menu (f, [it submenu], depth + 1);
	}
}

/* tests: the menu bar macOS really shows (titles, separators, key
   equivalents), two levels deep */
void
macos_dump_main_menu (const char *path)
{
	FILE *f = fopen (path, "w");
	if (!f)
		return;
	dump_menu (f, [NSApp mainMenu], 0);
	fclose (f);
}

/* the action of the item labelled label (mnemonics ignored), searched
   through submenus and sections; NULL if none */
static char *
find_action (GMenuModel *m, const char *label)
{
	int i, n = g_menu_model_get_n_items (m);

	for (i = 0; i < n; i++)
	{
		char *l = NULL, *a = NULL, *found = NULL;
		GMenuModel *sub;

		g_menu_model_get_item_attribute (m, i, G_MENU_ATTRIBUTE_LABEL, "s", &l);
		g_menu_model_get_item_attribute (m, i, G_MENU_ATTRIBUTE_ACTION, "s", &a);
		if (l && a)
		{
			GString *plain = g_string_new (NULL);
			const char *c;
			for (c = l; *c; c++)
				if (*c != '_')
					g_string_append_c (plain, *c);
			if (!strcmp (plain->str, label))
				found = g_strdup (a);
			g_string_free (plain, TRUE);
		}
		g_free (l);
		g_free (a);
		if (found)
			return found;
		if ((sub = g_menu_model_get_item_link (m, i, G_MENU_LINK_SUBMENU)) ||
			 (sub = g_menu_model_get_item_link (m, i, G_MENU_LINK_SECTION)))
		{
			found = find_action (sub, label);
			g_object_unref (sub);
			if (found)
				return found;
		}
	}
	return NULL;
}

/* runs a GAction from an NSMenuItem */
@interface NxActionItem : NSObject
{
@public
	GtkApplication *app;
	char *action;		/* without "app." */
}
- (void)run:(id)sender;
@end

@implementation NxActionItem
- (void)run:(id)sender
{
	g_action_group_activate_action (G_ACTION_GROUP (app), action, NULL);
}
- (void)dealloc
{
	g_free (action);
}
@end

/* AppKit doesn't retain an item's target */
static NSMutableArray *action_targets;

static NxActionItem *
action_target (GtkApplication *app, const char *action)
{
	NxActionItem *t = [[NxActionItem alloc] init];
	t->app = app;
	t->action = g_strdup (g_str_has_prefix (action, "app.") ? action + 4 : action);
	if (!action_targets)
		action_targets = [[NSMutableArray alloc] init];
	[action_targets addObject:t];
	return t;
}

static void
point_item (NSMenuItem *it, GtkApplication *app, const char *action)
{
	if (!it || !action)
		return;
	[it setTarget:action_target (app, action)];
	[it setAction:@selector(run:)];
	[it setEnabled:YES];
}

static gboolean
is_quit (NSMenuItem *it)
{
	return [[it keyEquivalent] isEqualToString:@"q"] &&
			 ([it keyEquivalentModifierMask] & NSEventModifierFlagCommand);
}

/* After GTK built the menu bar (gtk_application_set_menubar with the whole
   bar): item 0 is GTK's application menu (About, Settings, Services, Hide,
   Quit -- About and Settings run app.about/app.preferences, which nexchat
   doesn't have), item 1 the bar's own "nexchat" menu. Move item 1's entries
   (Network List, New, Load Plugin, Detach, Close) into the application
   menu after Settings, drop its Quit (the application menu has the
   standard one) and remove the now empty item 1. About, Settings and Quit
   run Help > About, Settings > Preferences and nexchat > Quit (which asks
   first when connected). GTK's items keep their own tracking of the
   actions, so shortcuts, sensitivity and the Detach/Close labels work. */
void
macos_merge_app_menu (GtkApplication *app, GMenuModel *bar)
{
	NSMenu *main_menu = [NSApp mainMenu];
	NSMenu *app_menu, *ours;
	GMenuModel *first;
	char *about, *prefs, *quit;
	NSInteger at;

	if (!main_menu || [main_menu numberOfItems] < 2 || !bar || g_menu_model_get_n_items (bar) < 1)
		return;
	app_menu = [[main_menu itemAtIndex:0] submenu];
	ours = [[main_menu itemAtIndex:1] submenu];
	first = g_menu_model_get_item_link (bar, 0, G_MENU_LINK_SUBMENU);
	if (!app_menu || !ours || !first)
	{
		g_clear_object (&first);
		return;
	}
	about = find_action (bar, "About");
	prefs = find_action (bar, "Preferences");
	quit = find_action (first, "Quit");
	g_object_unref (first);

	for (NSMenuItem *it in [app_menu itemArray])
	{
		if ([[it title] hasPrefix:@"About"])
			point_item (it, app, about);
		else if ([[it keyEquivalent] isEqualToString:@","])
			point_item (it, app, prefs);
		else if (is_quit (it))
			point_item (it, app, quit);
	}

	/* the nexchat menu without its last section (Quit) */
	while ([ours numberOfItems] && (is_quit ([ours itemAtIndex:[ours numberOfItems] - 1]) ||
			 [[ours itemAtIndex:[ours numberOfItems] - 1] isSeparatorItem]))
		[ours removeItemAtIndex:[ours numberOfItems] - 1];

	/* in front of Services' separator */
	at = [app_menu indexOfItemWithTitle:@"Services"];
	if (at < 0)
		at = [app_menu numberOfItems];
	else if (at > 0 && [[app_menu itemAtIndex:at - 1] isSeparatorItem])
		at--;
	[app_menu insertItem:[NSMenuItem separatorItem] atIndex:at++];
	while ([ours numberOfItems])
	{
		NSMenuItem *it = [ours itemAtIndex:0];
		[ours removeItemAtIndex:0];
		[app_menu insertItem:it atIndex:at++];
	}
	[main_menu removeItemAtIndex:1];

	g_free (about);
	g_free (prefs);
	g_free (quit);
}

/* tests: perform the menu item at "Menu/Item" of the real main menu,
   the way a click does; FALSE if there's no such item or it's disabled */
gboolean
macos_activate_menu_path (const char *path)
{
	char **parts = g_strsplit (path, "/", -1);
	NSMenu *m = [NSApp mainMenu];
	gboolean ok = FALSE;
	int i;

	for (i = 0; parts[i] && m; i++)
	{
		NSString *want = [NSString stringWithUTF8String:parts[i]];
		NSInteger at = -1;
		NSInteger j;
		for (j = 0; j < [m numberOfItems]; j++)
			if ([[[m itemAtIndex:j] title] isEqualToString:want])
			{
				at = j;
				break;
			}
		if (at < 0)
			break;
		if (!parts[i + 1])
		{
			[m update];
			if ([[m itemAtIndex:at] isEnabled])
			{
				[m performActionForItemAtIndex:at];
				ok = TRUE;
			}
			break;
		}
		m = [[m itemAtIndex:at] submenu];
	}
	g_strfreev (parts);
	return ok;
}

/* tests: the frontmost app's pid and the title of our key window */
void
macos_dump_focus (void)
{
	NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
	NSWindow *key = [NSApp keyWindow];
	g_printerr ("focus: front-app-pid=%d ours=%d key-window=\"%s\"\n",
					(int) [front processIdentifier], (int) getpid (),
					key ? [[key title] UTF8String] : "(none)");
}

/* Running from nexchat.app (Contents/MacOS/nexchat): point GLib, GTK,
   gettext and the plugin loader at Contents/Resources, so the bundle works
   wherever it is copied to. Variables already set by the user win. */
void
macos_bundle_setup (const char *textdomain)
{
	NSString *res;
	const char *r;
	char *p;

	if (![[[NSBundle mainBundle] bundlePath] hasSuffix:@".app"])
		return;
	res = [[NSBundle mainBundle] resourcePath];
	if (!res)
		return;
	r = [res fileSystemRepresentation];

#define SET(var, ...) \
	if (!g_getenv (var)) \
	{ \
		p = g_build_filename (r, __VA_ARGS__, NULL); \
		g_setenv (var, p, TRUE); \
		g_free (p); \
	}
	SET ("NEXCHAT_LIBDIR", "lib", "nexchat", "plugins");
	SET ("XDG_DATA_DIRS", "share");
	SET ("GSETTINGS_SCHEMA_DIR", "share", "glib-2.0", "schemas");
	SET ("GDK_PIXBUF_MODULE_FILE", "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders.cache");
	SET ("GIO_MODULE_DIR", "lib", "gio", "modules");
	SET ("OPENSSL_MODULES", "lib", "ossl-modules");
#undef SET

	p = g_build_filename (r, "share", "locale", NULL);
	bindtextdomain (textdomain, p);
	g_free (p);
}

static NSInteger attention_req;

void
macos_request_attention (gboolean urgent)
{
	if (urgent)
	{
		if (!attention_req && ![NSApp isActive])
			attention_req = [NSApp requestUserAttention:NSInformationalRequest];
	}
	else if (attention_req)
	{
		[NSApp cancelUserAttentionRequest:attention_req];
		attention_req = 0;
	}
}

gboolean
macos_is_dark (void)
{
	if (@available (macOS 10.14, *))
	{
		/* the system's choice, not ours: NSApp.appearance may be pinned by
		   macos_set_dark() for a scheme */
		NSString *style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
		if (NSApp.appearance == nil)
		{
			NSAppearanceName n = [[NSApp effectiveAppearance]
				bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
			return [n isEqualToString:NSAppearanceNameDarkAqua];
		}
		return [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
	}
	return FALSE;
}

void
macos_set_dark (int dark)
{
	static int last = -2;

	/* setting NSApp.appearance redraws every window (and GTK's macOS
	   backend restarts their frames): only when it really changes */
	if (dark == last)
		return;
	last = dark;
	if (@available (macOS 10.14, *))
	{
		/* -1: follow the system (window title bars, menus, sheets) */
		NSApp.appearance = dark < 0 ? nil
			: [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
	}
}

static GdkRGBA
macos_rgba (NSColor *c)
{
	GdkRGBA r = { 0, 0, 0, 1 };
	NSColor *s = [c colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
	if (s)
	{
		r.red = [s redComponent];
		r.green = [s greenComponent];
		r.blue = [s blueComponent];
	}
	return r;
}

void
macos_text_colors (GdkRGBA out[4])
{
	/* dynamic colours resolve against the current appearance */
	void (^get) (void) = ^{
		out[0] = macos_rgba ([NSColor textBackgroundColor]);
		out[1] = macos_rgba ([NSColor textColor]);
		out[2] = macos_rgba ([NSColor selectedTextBackgroundColor]);
		out[3] = macos_rgba ([NSColor selectedTextColor]);
	};
	if (@available (macOS 11.0, *))
		[[NSApp effectiveAppearance] performAsCurrentDrawingAppearance:get];
	else
		get ();
}
