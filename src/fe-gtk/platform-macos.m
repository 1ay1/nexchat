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
#include <glib.h>

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

/* the user's accent colour (System Settings > Appearance), 0..1 rgb */

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
		NSAppearanceName n = [[NSApp effectiveAppearance]
			bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
		return [n isEqualToString:NSAppearanceNameDarkAqua];
	}
	return FALSE;
}

gboolean
macos_accent (double *r, double *g, double *b)
{
	if (@available (macOS 10.14, *))
	{
		NSColor *c = [[NSColor controlAccentColor] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
		if (c)
		{
			*r = [c redComponent];
			*g = [c greenComponent];
			*b = [c blueComponent];
			return TRUE;
		}
	}
	return FALSE;
}
