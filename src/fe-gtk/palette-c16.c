/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Kept out of palette.c (which needs GTK) so tests/palette-roundtrip.c can
   link it on its own. */

#include <glib.h>

guint16
palette_c16 (double channel)
{
	/* exact round-trip for every 16-bit value C16() / colors.conf produce */
	if (channel <= 0)
		return 0;
	if (channel >= 1)
		return 0xffff;
	return (guint16) (channel * 65535.0 + 0.5);
}
