/* Check that every 16-bit channel value survives colors.conf -> GdkRGBA ->
   colors.conf unchanged, and that the 8-bit values used in CSS/markup are
   what GdkColor >> 8 gave. Built against palette.c's helper only. */
#include <stdio.h>
#include <glib.h>

guint16 palette_c16 (double channel);

int
main (void)
{
	unsigned int v;
	int bad = 0;

	for (v = 0; v <= 0xffff; v++)
	{
		double d = v / 65535.0;
		guint16 back = palette_c16 (d);
		if (back != v)
		{
			if (bad++ < 5)
				printf ("16-bit %u -> %.17g -> %u\n", v, d, back);
		}
		if ((back >> 8) != (v >> 8))
		{
			if (bad++ < 5)
				printf ("8-bit %u: %u vs %u\n", v, back >> 8, v >> 8);
		}
	}
	if (bad)
	{
		printf ("FAIL: %d mismatches\n", bad);
		return 1;
	}
	printf ("palette round-trip: ok (65536 values)\n");
	return 0;
}
