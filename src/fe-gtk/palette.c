/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.h"
#include "palette.h"

#include "../common/hexchat.h"
#include "../common/util.h"
#include "../common/cfgfiles.h"
#include "../common/typedef.h"


/* 16-bit channel values, same numbers as HexChat's GdkColor table and the
   colors.conf file, so nothing a user saved changes. */
#define C16(r,g,b) { (r) / 65535.0, (g) / 65535.0, (b) / 65535.0, 1.0 }
#define C8(r,g,b) C16 ((r) * 0x101, (g) * 0x101, (b) * 0x101)

GdkRGBA colors[MAX_COL + 1];

/* HexChat's own defaults, used while the system is in light mode */
static const GdkRGBA defaults_light[MAX_COL + 1] = {
	/* colors for xtext */
	C16 (0xd3d3, 0xd7d7, 0xcfcf), /* 0 white */
	C16 (0x2e2e, 0x3434, 0x3636), /* 1 black */
	C16 (0x3434, 0x6565, 0xa4a4), /* 2 blue */
	C16 (0x4e4e, 0x9a9a, 0x0606), /* 3 green */
	C16 (0xcccc, 0x0000, 0x0000), /* 4 red */
	C16 (0x8f8f, 0x3939, 0x0202), /* 5 light red */
	C16 (0x5c5c, 0x3535, 0x6666), /* 6 purple */
	C16 (0xcece, 0x5c5c, 0x0000), /* 7 orange */
	C16 (0xc4c4, 0xa0a0, 0x0000), /* 8 yellow */
	C16 (0x7373, 0xd2d2, 0x1616), /* 9 green */
	C16 (0x1111, 0xa8a8, 0x7979), /* 10 aqua */
	C16 (0x5858, 0xa1a1, 0x9d9d), /* 11 light aqua */
	C16 (0x5757, 0x7979, 0x9e9e), /* 12 blue */
	C16 (0xa0d0, 0x42d4, 0x6562), /* 13 light purple */
	C16 (0x5555, 0x5757, 0x5353), /* 14 grey */
	C16 (0x8888, 0x8a8a, 0x8585), /* 15 light grey */

	C16 (0xd3d3, 0xd7d7, 0xcfcf), /* 16 white */
	C16 (0x2e2e, 0x3434, 0x3636), /* 17 black */
	C16 (0x3434, 0x6565, 0xa4a4), /* 18 blue */
	C16 (0x4e4e, 0x9a9a, 0x0606), /* 19 green */
	C16 (0xcccc, 0x0000, 0x0000), /* 20 red */
	C16 (0x8f8f, 0x3939, 0x0202), /* 21 light red */
	C16 (0x5c5c, 0x3535, 0x6666), /* 22 purple */
	C16 (0xcece, 0x5c5c, 0x0000), /* 23 orange */
	C16 (0xc4c4, 0xa0a0, 0x0000), /* 24 yellow */
	C16 (0x7373, 0xd2d2, 0x1616), /* 25 green */
	C16 (0x1111, 0xa8a8, 0x7979), /* 26 aqua */
	C16 (0x5858, 0xa1a1, 0x9d9d), /* 27 light aqua */
	C16 (0x5757, 0x7979, 0x9e9e), /* 28 blue */
	C16 (0xa0d0, 0x42d4, 0x6562), /* 29 light purple */
	C16 (0x5555, 0x5757, 0x5353), /* 30 grey */
	C16 (0x8888, 0x8a8a, 0x8585), /* 31 light grey */

	C16 (0xd3d3, 0xd7d7, 0xcfcf), /* 32 marktext Fore (white) */
	C16 (0x2020, 0x4a4a, 0x8787), /* 33 marktext Back (blue) */
	C16 (0x2512, 0x29e8, 0x2b85), /* 34 foreground (black) */
	C16 (0xfae0, 0xfae0, 0xf8c4), /* 35 background (white) */
	C16 (0x8f8f, 0x3939, 0x0202), /* 36 marker line (red) */

	/* colors for GUI */
	C16 (0x3434, 0x6565, 0xa4a4), /* 37 tab New Data (dark red) */
	C16 (0x4e4e, 0x9a9a, 0x0606), /* 38 tab Nick Mentioned (blue) */
	C16 (0xcece, 0x5c5c, 0x0000), /* 39 tab New Message (red) */
	C16 (0x8888, 0x8a8a, 0x8585), /* 40 away user (grey) */
	C16 (0xa4a4, 0x0000, 0x0000), /* 41 spell checker color (red) */
};

/* the same roles for dark mode: the text box on the system's dark text
   background, and the Tango palette's light tones, which keep nick,
   event and mIRC colours readable on it */
static const GdkRGBA defaults_dark[MAX_COL + 1] = {
	C8 (0xee, 0xee, 0xec), /* 0 white */
	C8 (0x2e, 0x34, 0x36), /* 1 black */
	C8 (0x72, 0x9f, 0xcf), /* 2 blue */
	C8 (0x73, 0xd2, 0x16), /* 3 green */
	C8 (0xef, 0x29, 0x29), /* 4 red */
	C8 (0xc1, 0x7d, 0x11), /* 5 light red */
	C8 (0xad, 0x7f, 0xa8), /* 6 purple */
	C8 (0xfc, 0xaf, 0x3e), /* 7 orange */
	C8 (0xfc, 0xe9, 0x4f), /* 8 yellow */
	C8 (0x8a, 0xe2, 0x34), /* 9 green */
	C8 (0x2e, 0xc4, 0xa0), /* 10 aqua */
	C8 (0x34, 0xe2, 0xe2), /* 11 light aqua */
	C8 (0x9d, 0xb8, 0xd2), /* 12 blue */
	C8 (0xe0, 0x7b, 0xb0), /* 13 light purple */
	C8 (0x88, 0x8a, 0x85), /* 14 grey */
	C8 (0xba, 0xbd, 0xb6), /* 15 light grey */

	C8 (0xee, 0xee, 0xec), /* 16 white */
	C8 (0x2e, 0x34, 0x36), /* 17 black */
	C8 (0x72, 0x9f, 0xcf), /* 18 blue */
	C8 (0x73, 0xd2, 0x16), /* 19 green */
	C8 (0xef, 0x29, 0x29), /* 20 red */
	C8 (0xc1, 0x7d, 0x11), /* 21 light red */
	C8 (0xad, 0x7f, 0xa8), /* 22 purple */
	C8 (0xfc, 0xaf, 0x3e), /* 23 orange */
	C8 (0xfc, 0xe9, 0x4f), /* 24 yellow */
	C8 (0x8a, 0xe2, 0x34), /* 25 green */
	C8 (0x2e, 0xc4, 0xa0), /* 26 aqua */
	C8 (0x34, 0xe2, 0xe2), /* 27 light aqua */
	C8 (0x9d, 0xb8, 0xd2), /* 28 blue */
	C8 (0xe0, 0x7b, 0xb0), /* 29 light purple */
	C8 (0x88, 0x8a, 0x85), /* 30 grey */
	C8 (0xba, 0xbd, 0xb6), /* 31 light grey */

	C8 (0xff, 0xff, 0xff), /* 32 marktext Fore */
	C8 (0x2a, 0x5d, 0x9a), /* 33 marktext Back */
	C8 (0xe4, 0xe4, 0xe2), /* 34 foreground */
	C8 (0x1e, 0x1e, 0x1e), /* 35 background */
	C8 (0xcc, 0x66, 0x33), /* 36 marker line */

	C8 (0x72, 0x9f, 0xcf), /* 37 tab New Data */
	C8 (0x8a, 0xe2, 0x34), /* 38 tab Nick Mentioned */
	C8 (0xfc, 0xaf, 0x3e), /* 39 tab New Message */
	C8 (0x88, 0x8a, 0x85), /* 40 away user */
	C8 (0xef, 0x29, 0x29), /* 41 spell checker color */
};

/* which colours the user picked: those stay put when the system switches
   between light and dark, the rest follow it */
static gboolean custom[MAX_COL + 1];
static gboolean palette_is_dark;
static gboolean palette_ready;

static const GdkRGBA *
palette_defaults (void)
{
	return palette_is_dark ? defaults_dark : defaults_light;
}

static gboolean
same_color (const GdkRGBA *a, const GdkRGBA *b)
{
	return C16R (*a) == C16R (*b) && C16G (*a) == C16G (*b) && C16B (*a) == C16B (*b);
}

static void
palette_reset (void)
{
	const GdkRGBA *def = palette_defaults ();
	int i;

	for (i = 0; i <= MAX_COL; i++)
		if (!custom[i])
			colors[i] = def[i];
}

/* colors.conf name of colors[i]: mIRC 0-31 as is, ours from 256 */
static void
palette_name (int i, char *buf, gsize len)
{
	g_snprintf (buf, len, "color_%d", i < 32 ? i : i - 32 + 256);
}

gboolean
palette_set_dark (gboolean dark)
{
	if (palette_ready && palette_is_dark == !!dark)
		return FALSE;
	palette_is_dark = !!dark;
	palette_reset ();
	palette_ready = TRUE;
	return TRUE;
}

gboolean
palette_dark (void)
{
	return palette_is_dark;
}

gboolean
palette_is_custom (void)
{
	int i;

	for (i = 0; i <= MAX_COL; i++)
		if (custom[i])
			return TRUE;
	return FALSE;
}

void
palette_use_defaults (void)
{
	memset (custom, 0, sizeof custom);
	palette_reset ();
}

void
palette_alloc (GtkWidget * widget)
{
	/* GTK3 is always TrueColor: no colormap allocation needed */
	(void) widget;
}

void
palette_load (void)
{
	int i, fh;
	char prefname[256];
	struct stat st;
	char *cfg;
	guint16 red, green, blue;

	memset (custom, 0, sizeof custom);
	palette_ready = TRUE;
	palette_reset ();

	fh = hexchat_open_file ("colors.conf", O_RDONLY, 0, 0);
	if (fh != -1)
	{
		fstat (fh, &st);
		cfg = g_malloc0 (st.st_size + 1);
		read (fh, cfg, st.st_size);

		for (i = 0; i <= MAX_COL; i++)
		{
			GdkRGBA c = { 0, 0, 0, 1.0 };

			palette_name (i, prefname, sizeof prefname);
			/* a missing name keeps the default (this used to reuse the
			   previous colour's numbers) */
			if (!cfg_get_color (cfg, prefname, &red, &green, &blue))
				continue;
			c.red = red / 65535.0;
			c.green = green / 65535.0;
			c.blue = blue / 65535.0;
			/* older files list every colour: one that's a built-in default
			   wasn't picked, so it follows the system */
			if (same_color (&c, &defaults_light[i]) || same_color (&c, &defaults_dark[i]))
				continue;
			colors[i] = c;
			custom[i] = TRUE;
		}
		g_free (cfg);
		close (fh);
	}
}

void
palette_save (void)
{
	const GdkRGBA *def = palette_defaults ();
	int i, fh;
	char prefname[256];

	/* only the colours that aren't the current default: the rest follow
	   the system's light/dark mode */
	for (i = 0; i <= MAX_COL; i++)
		custom[i] = !same_color (&colors[i], &def[i]);

	fh = hexchat_open_file ("colors.conf", O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		for (i = 0; i <= MAX_COL; i++)
		{
			if (!custom[i])
				continue;
			palette_name (i, prefname, sizeof prefname);
			cfg_put_color (fh, C16R (colors[i]), C16G (colors[i]), C16B (colors[i]), prefname);
		}

		close (fh);
	}
}
