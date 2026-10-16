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

GdkRGBA colors[] = {
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

void
palette_alloc (GtkWidget * widget)
{
	/* GTK3 is always TrueColor: no colormap allocation needed */
	(void) widget;
}

void
palette_load (void)
{
	int i, j, fh;
	char prefname[256];
	struct stat st;
	char *cfg;
	guint16 red, green, blue;

	fh = hexchat_open_file ("colors.conf", O_RDONLY, 0, 0);
	if (fh != -1)
	{
		fstat (fh, &st);
		cfg = g_malloc0 (st.st_size + 1);
		read (fh, cfg, st.st_size);

		/* mIRC colors 0-31 are here */
		for (i = 0; i < 32; i++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_get_color (cfg, prefname, &red, &green, &blue);
			colors[i].red = red / 65535.0;
			colors[i].green = green / 65535.0;
			colors[i].blue = blue / 65535.0;
		}

		/* our special colors are mapped at 256+ */
		for (i = 256, j = 32; j < MAX_COL+1; i++, j++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_get_color (cfg, prefname, &red, &green, &blue);
			colors[j].red = red / 65535.0;
			colors[j].green = green / 65535.0;
			colors[j].blue = blue / 65535.0;
		}
		g_free (cfg);
		close (fh);
	}
}

void
palette_save (void)
{
	int i, j, fh;
	char prefname[256];

	fh = hexchat_open_file ("colors.conf", O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		/* mIRC colors 0-31 are here */
		for (i = 0; i < 32; i++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_put_color (fh, C16R (colors[i]), C16G (colors[i]), C16B (colors[i]), prefname);
		}

		/* our special colors are mapped at 256+ */
		for (i = 256, j = 32; j < MAX_COL+1; i++, j++)
		{
			g_snprintf (prefname, sizeof prefname, "color_%d", i);
			cfg_put_color (fh, C16R (colors[j]), C16G (colors[j]), C16B (colors[j]), prefname);
		}

		close (fh);
	}
}
