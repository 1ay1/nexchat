/* Every colour scheme, after palette_make_readable, the way the app shows
   it: the text, nick/event colours, tab colours, the marker, a visible
   selection with readable text in it, the ^C colours legible. And nothing
   that was already fine is touched (a scheme keeps its own look). */
#include <stdio.h>
#include <string.h>
#include "palette-schemes.h"

static int bad, shown;

static void
need (const char *scheme, const GdkRGBA *c, int i, const GdkRGBA *on, double min, const char *what)
{
	double r = palette_contrast (&c[i], on);

	if (r + 1e-6 < min)
	{
		if (shown++ < 20)
			printf ("FAIL: %s: %s (colour %d) %.2f:1, want %.1f:1\n", scheme, what, i, r, min);
		bad++;
	}
}

static void
check (const char *name, const GdkRGBA *orig)
{
	GdkRGBA c[MAX_COL + 1];
	int i;

	memcpy (c, orig, sizeof c);
	palette_make_readable (c);
	need (name, c, COL_FG, &c[COL_BG], NX_MIN_TEXT, "text");
	for (i = 2; i < 16; i++)
		need (name, c, i, &c[COL_BG], NX_MIN_MIRC, "mIRC colour");
	for (i = 18; i < 32; i++)
		need (name, c, i, &c[COL_BG], NX_MIN_TEXT, "event/nick colour");
	need (name, c, COL_NEW_DATA, &c[COL_BG], NX_MIN_TEXT, "tab: new data");
	need (name, c, COL_HILIGHT, &c[COL_BG], NX_MIN_TEXT, "tab: highlight");
	need (name, c, COL_NEW_MSG, &c[COL_BG], NX_MIN_TEXT, "tab: new message");
	need (name, c, COL_MARKER, &c[COL_BG], NX_MIN_LINE, "marker line");
	need (name, c, COL_AWAY, &c[COL_BG], NX_MIN_LINE, "away nick");
	need (name, c, COL_SPELL, &c[COL_BG], NX_MIN_LINE, "spelling underline");
	need (name, c, COL_MARK_BG, &c[COL_BG], NX_MIN_SELECTION, "selection");
	need (name, c, COL_MARK_FG, &c[COL_MARK_BG], NX_MIN_TEXT, "selected text");

	/* the window around it */
	{
		nx_ui_colors ui;
		struct { const GdkRGBA *a, *on; double min; const char *what; } t[] = {
			{ &ui.fg, &ui.bg, NX_MIN_TEXT, "window text" },
			{ &ui.fg, &ui.hover, NX_MIN_TEXT, "text on a hovered button" },
			{ &ui.fg, &ui.active, NX_MIN_TEXT, "text on a pressed button" },
			{ &ui.text, &ui.view, NX_MIN_TEXT, "list/entry text" },
			{ &ui.dim, &ui.bg, NX_MIN_DIM, "dim label" },
			{ &ui.border, &ui.bg, NX_MIN_BORDER, "border on the window" },
			{ &ui.border, &ui.view, NX_MIN_BORDER, "border round an entry" },
			{ &ui.accent, &ui.bg, NX_MIN_ACCENT, "accent on the window" },
			{ &ui.accent, &ui.view, NX_MIN_ACCENT, "accent in a list" },
			{ &ui.accent_fg, &ui.accent, NX_MIN_TEXT, "text on the accent" },
		};
		gsize j;

		palette_ui_colors (c, &ui);
		for (j = 0; j < G_N_ELEMENTS (t); j++)
		{
			double r = palette_contrast (t[j].a, t[j].on);
			if (r + 1e-6 < t[j].min)
			{
				if (shown++ < 20)
					printf ("FAIL: %s: %s %.2f:1, want %.1f:1\n", name, t[j].what, r, t[j].min);
				bad++;
			}
		}
	}

	/* the background is never changed, and a colour that was readable
	   enough stays exactly the scheme's */
	if (memcmp (&c[COL_BG], &orig[COL_BG], sizeof (GdkRGBA)))
	{
		printf ("FAIL: %s: background changed\n", name);
		bad++;
	}
	for (i = 2; i < 16; i++)
		if (palette_contrast (&orig[i], &orig[COL_BG]) >= NX_MIN_MIRC &&
			 memcmp (&c[i], &orig[i], sizeof (GdkRGBA)))
		{
			printf ("FAIL: %s: readable colour %d changed\n", name, i);
			bad++;
		}
	/* 0 and 1 are the ^C0,1 / ^C1,0 pair colours: pure */
	if (memcmp (&c[0], &orig[0], 2 * sizeof (GdkRGBA)))
	{
		printf ("FAIL: %s: colour 0/1 changed\n", name);
		bad++;
	}
}

int
main (void)
{
	GdkRGBA c[MAX_COL + 1];
	int i, n = 0;

	check ("HexChat (light)", nx_defaults_light);
	check ("HexChat (dark)", nx_defaults_dark);
	n += 2;
	for (i = 0; i < nx_scheme_n; i++, n++)
	{
		palette_scheme_fill (c, i);
		check (nx_scheme_list[i].name, c);
	}
	if (bad)
	{
		printf ("FAIL: %d problems\n", bad);
		return 1;
	}
	printf ("palette contrast: ok (%d schemes)\n", n);
	return 0;
}
