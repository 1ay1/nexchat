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

/* the user's accent colour (System Settings > Appearance), 0..1 rgb */
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
