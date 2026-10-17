/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* macOS menu bar icon for GTK4: an NSStatusItem. A click on it shows the
   tray menu as a real NSMenu built from the nx_menu (the way every menu
   bar extra works on macOS); Option-click shows/hides the window, like
   the left click elsewhere. */

#import <AppKit/AppKit.h>
#include <string.h>

#include "tray-backend.h"

struct _tray_backend
{
	NSStatusItem *item;
	id target;
	GdkPixbuf *icon;
	tray_backend_callbacks cb;
	gpointer userdata;
	GPtrArray *cmds;			/* nx_item * by NSMenuItem tag */
};

@interface NxTrayTarget : NSObject
{
@public
	tray_backend *tb;
}
- (void)clicked:(id)sender;
- (void)itemChosen:(NSMenuItem *)mi;
@end

@implementation NxTrayTarget
- (void)clicked:(id)sender
{
	NSEvent *e = [NSApp currentEvent];

	/* Option-click or a right click: show/hide; a plain click: the menu */
	if (([e modifierFlags] & NSEventModifierFlagOption) && tb->cb.activate)
		tb->cb.activate (tb, tb->userdata);
	else if (tb->cb.menu)
		tb->cb.menu (tb, 3, 0, tb->userdata);
}
- (void)itemChosen:(NSMenuItem *)mi
{
	NSInteger i = [mi tag];

	if (i >= 0 && (guint) i < tb->cmds->len)
		nx_item_activate (g_ptr_array_index (tb->cmds, i));
}
@end

/* a GdkPixbuf as a template-free NSImage at menu bar size */
static NSImage *
pixbuf_to_nsimage (GdkPixbuf *pb)
{
	GdkPixbuf *rgba;
	NSBitmapImageRep *rep;
	NSImage *img;
	int w, h, rs, y;

	if (!pb)
		return nil;
	rgba = gdk_pixbuf_add_alpha (pb, FALSE, 0, 0, 0);
	w = gdk_pixbuf_get_width (rgba);
	h = gdk_pixbuf_get_height (rgba);
	rs = gdk_pixbuf_get_rowstride (rgba);
	rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
		bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
		colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w * 4 bitsPerPixel:32];
	for (y = 0; y < h; y++)
		memcpy ([rep bitmapData] + y * w * 4, gdk_pixbuf_read_pixels (rgba) + y * rs, w * 4);
	img = [[NSImage alloc] initWithSize:NSMakeSize (18, 18)];
	[img addRepresentation:rep];
	g_object_unref (rgba);
	return img;
}

/* GTK's "_File" mnemonic: macOS menus have none, drop the underscore */
static NSString *
mac_label (nx_item *it)
{
	const char *t = nx_item_get_text (it);
	return [NSString stringWithUTF8String:t ? t : ""];
}

static NSMenu *
build_menu (tray_backend *tb, nx_menu *m)
{
	NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
	int i, n = nx_menu_get_n_items (m);

	[menu setAutoenablesItems:NO];
	for (i = 0; i < n; i++)
	{
		nx_item *it = nx_menu_get_item (m, i);
		nx_item_kind kind = nx_item_get_kind (it);
		NSMenuItem *mi;
		nx_menu *sub;

		if (kind == NX_KIND_SEPARATOR)
		{
			[menu addItem:[NSMenuItem separatorItem]];
			continue;
		}
		mi = [[NSMenuItem alloc] initWithTitle:mac_label (it) action:nil keyEquivalent:@""];
		[mi setEnabled:nx_item_get_sensitive (it)];
		if ((kind == NX_KIND_CHECK || kind == NX_KIND_RADIO) && nx_item_get_active (it))
			[mi setState:NSControlStateValueOn];
		sub = nx_item_get_submenu (it);
		if (sub)
			[mi setSubmenu:build_menu (tb, sub)];
		else
		{
			[mi setTarget:tb->target];
			[mi setAction:@selector(itemChosen:)];
			[mi setTag:tb->cmds->len];
			g_ptr_array_add (tb->cmds, it);
		}
		[menu addItem:mi];
	}
	return menu;
}

tray_backend *
tray_backend_new (GdkPixbuf *icon, const tray_backend_callbacks *cb, gpointer userdata)
{
	tray_backend *tb = g_new0 (tray_backend, 1);
	NxTrayTarget *t = [[NxTrayTarget alloc] init];

	tb->cb = *cb;
	tb->userdata = userdata;
	tb->cmds = g_ptr_array_new ();
	t->tb = tb;
	tb->target = t;
	tb->item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
	[[tb->item button] setTarget:t];
	[[tb->item button] setAction:@selector(clicked:)];
	[[tb->item button] sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
	[[tb->item button] setToolTip:@"nexchat"];
	tray_backend_set_icon (tb, icon);
	if (tb->cb.embedded_changed)
		tb->cb.embedded_changed (tb, TRUE, tb->userdata);
	return tb;
}

void
tray_backend_free (tray_backend *tb)
{
	if (!tb)
		return;
	[[NSStatusBar systemStatusBar] removeStatusItem:tb->item];
	tb->item = nil;
	tb->target = nil;
	if (tb->icon)
		g_object_unref (tb->icon);
	g_ptr_array_free (tb->cmds, TRUE);
	g_free (tb);
}

void
tray_backend_set_icon (tray_backend *tb, GdkPixbuf *icon)
{
	if (icon)
		g_object_ref (icon);
	if (tb->icon)
		g_object_unref (tb->icon);
	tb->icon = icon;
	[[tb->item button] setImage:pixbuf_to_nsimage (icon)];
}

GdkPixbuf *
tray_backend_get_icon (tray_backend *tb)
{
	return tb->icon;
}

void
tray_backend_set_tooltip (tray_backend *tb, const char *text)
{
	[[tb->item button] setToolTip:[NSString stringWithUTF8String:text ? text : ""]];
}

gboolean
tray_backend_is_embedded (tray_backend *tb)
{
	return tb->item != nil;
}

void
tray_backend_show_menu (tray_backend *tb, nx_menu *menu, guint button, guint32 time)
{
	NSMenu *m;

	g_ptr_array_set_size (tb->cmds, 0);
	m = build_menu (tb, menu);
	/* drops down from the menu bar icon like every other menu bar extra;
	   runs modally, the chosen item's action has run when it returns */
	[m popUpMenuPositioningItem:nil
						  atLocation:NSMakePoint (0, NSHeight ([[tb->item button] bounds]) + 4)
								inView:[tb->item button]];
	g_ptr_array_set_size (tb->cmds, 0);
	nx_menu_free (menu);
}
