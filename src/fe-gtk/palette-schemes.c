/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <string.h>
#include "palette-schemes.h"

#define C16(r,g,b) { (r) / 65535.0, (g) / 65535.0, (b) / 65535.0, 1.0 }
#define C8(r,g,b) C16 ((r) * 0x101, (g) * 0x101, (b) * 0x101)

#include "palette-presets.h"

const nx_scheme *const nx_scheme_list = nx_schemes;
const int nx_scheme_n = G_N_ELEMENTS (nx_schemes);
const nx_scheme_pair *const nx_pair_list = nx_scheme_pairs;
const int nx_pair_n = G_N_ELEMENTS (nx_scheme_pairs);

/* HexChat's own defaults, used while the system is in light mode */
const GdkRGBA nx_defaults_light[MAX_COL + 1] = {
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
const GdkRGBA nx_defaults_dark[MAX_COL + 1] = {
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

static GdkRGBA
rgb24 (guint32 c)
{
	GdkRGBA r = { ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, 1.0 };
	return r;
}

void
palette_scheme_fill (GdkRGBA *out, int s)
{
	const guint32 *c = nx_schemes[s].c;
	int i;

	for (i = 0; i < 16; i++)
		out[i] = out[i + 16] = rgb24 (c[i]);
	out[COL_BG] = rgb24 (c[16]);
	out[COL_FG] = rgb24 (c[17]);
	out[COL_MARK_BG] = rgb24 (c[18]);
	out[COL_MARK_FG] = rgb24 (c[19]);
	out[COL_MARKER] = out[4];
	out[COL_NEW_DATA] = out[12];
	out[COL_HILIGHT] = out[9];
	out[COL_NEW_MSG] = out[7];
	out[COL_AWAY] = out[14];
	out[COL_SPELL] = out[4];
}

/* WCAG 2 relative luminance and contrast ratio */
static double
lin (double v)
{
	return v <= 0.04045 ? v / 12.92 : pow ((v + 0.055) / 1.055, 2.4);
}

static double
lum (const GdkRGBA *c)
{
	return 0.2126 * lin (c->red) + 0.7152 * lin (c->green) + 0.0722 * lin (c->blue);
}

double
palette_contrast (const GdkRGBA *a, const GdkRGBA *b)
{
	double x = lum (a), y = lum (b);

	return x > y ? (x + 0.05) / (y + 0.05) : (y + 0.05) / (x + 0.05);
}

static GdkRGBA
towards (const GdkRGBA *c, double to, double t)
{
	GdkRGBA r = { c->red + (to - c->red) * t, c->green + (to - c->green) * t,
					  c->blue + (to - c->blue) * t, 1.0 };
	return r;
}

/* the result goes through colors.conf's 16-bit numbers: check it as stored */
static GdkRGBA
q16 (GdkRGBA c)
{
	c.red = palette_c16 (c.red) / 65535.0;
	c.green = palette_c16 (c.green) / 65535.0;
	c.blue = palette_c16 (c.blue) / 65535.0;
	return c;
}

GdkRGBA
palette_readable (const GdkRGBA *c, const GdkRGBA *bg, double min)
{
	GdkRGBA best = *c, k = { 0, 0, 0, 1 }, w = { 1, 1, 1, 1 };
	double to, lo = 0, hi = 1;
	int i;

	if (palette_contrast (c, bg) >= min)
		return *c;
	/* away from the background: darker on a light one, lighter on a dark one
	   (whichever end can get there; a mid-grey background may only allow one) */
	to = palette_contrast (&k, bg) >= palette_contrast (&w, bg) ? 0 : 1;
	if (palette_contrast (to ? &w : &k, bg) < min)
		return to ? w : k;	/* nothing reaches it: the most there is */
	/* the least change that's enough, so the colour stays as much itself
	   as it can (mixing with black keeps the hue; with white it pales) */
	for (i = 0; i < 24; i++)
	{
		double t = (lo + hi) / 2;
		GdkRGBA m = q16 (towards (c, to, t));

		if (palette_contrast (&m, bg) >= min)
		{
			hi = t;
			best = m;
		}
		else
			lo = t;
	}
	if (palette_contrast (&best, bg) < min)
		best = q16 (towards (c, to, hi));
	return best;
}

static void
fix (GdkRGBA *c, int i, const GdkRGBA *bg, double min)
{
	c[i] = palette_readable (&c[i], bg, min);
}

void
palette_make_readable (GdkRGBA *c)
{
	const GdkRGBA *bg = &c[COL_BG];
	GdkRGBA bg_copy;
	int i;

	fix (c, COL_FG, bg, NX_MIN_TEXT);
	/* what others colour with ^C: 0 and 1 stay pure, they're the usual
	   backgrounds of ^C0,1 / ^C1,0 pairs; the rest at least legible */
	for (i = 2; i < 16; i++)
		fix (c, i, bg, NX_MIN_MIRC);
	/* nexchat's own copies (16-31) colour events and nicks on the plain
	   background: as readable as the text (16/17 are the headers' white
	   on black, a pair) */
	for (i = 18; i < 32; i++)
		fix (c, i, bg, NX_MIN_TEXT);
	fix (c, COL_MARKER, bg, NX_MIN_LINE);
	fix (c, COL_SPELL, bg, NX_MIN_LINE);
	fix (c, COL_AWAY, bg, NX_MIN_LINE);
	fix (c, COL_NEW_DATA, bg, NX_MIN_TEXT);
	fix (c, COL_HILIGHT, bg, NX_MIN_TEXT);
	fix (c, COL_NEW_MSG, bg, NX_MIN_TEXT);

	/* a selection you can see, with text you can read in it */
	bg_copy = *bg;
	if (palette_contrast (&c[COL_MARK_BG], &bg_copy) < NX_MIN_SELECTION)
	{
		/* towards the text colour: a selection always looks like one */
		double t;

		for (t = 0.05; t <= 1.0; t += 0.05)
		{
			GdkRGBA m = q16 ((GdkRGBA) {
				c[COL_MARK_BG].red + (c[COL_FG].red - c[COL_MARK_BG].red) * t,
				c[COL_MARK_BG].green + (c[COL_FG].green - c[COL_MARK_BG].green) * t,
				c[COL_MARK_BG].blue + (c[COL_FG].blue - c[COL_MARK_BG].blue) * t, 1.0 });
			if (palette_contrast (&m, &bg_copy) >= NX_MIN_SELECTION)
			{
				c[COL_MARK_BG] = m;
				break;
			}
		}
	}
	if (palette_contrast (&c[COL_MARK_FG], &c[COL_MARK_BG]) < NX_MIN_TEXT)
	{
		GdkRGBA k = { 0, 0, 0, 1 }, w = { 1, 1, 1, 1 };
		GdkRGBA keep = palette_readable (&c[COL_MARK_FG], &c[COL_MARK_BG], NX_MIN_TEXT);

		/* its own hue if that can get there, else black or white */
		if (palette_contrast (&keep, &c[COL_MARK_BG]) >= NX_MIN_TEXT)
			c[COL_MARK_FG] = keep;
		else
			c[COL_MARK_FG] = palette_contrast (&k, &c[COL_MARK_BG]) >= palette_contrast (&w, &c[COL_MARK_BG]) ? k : w;
	}
}

static GdkRGBA
mix (const GdkRGBA *a, const GdkRGBA *b, double t)
{
	GdkRGBA r = { a->red + (b->red - a->red) * t, a->green + (b->green - a->green) * t,
	              a->blue + (b->blue - a->blue) * t, 1.0 };
	return r;
}

/* the least mix of a towards b that reaches min contrast against on */
static GdkRGBA
mix_until (const GdkRGBA *a, const GdkRGBA *b, const GdkRGBA *on, double from, double min)
{
	double t;

	for (t = from; t < 1.0; t += 0.02)
	{
		GdkRGBA m = mix (a, b, t);
		if (palette_contrast (&m, on) >= min)
			return m;
	}
	return *b;
}

/* c readable on every one of n surfaces: the least move towards black or
   white (trying first the side c is already on) that clears them all */
static GdkRGBA
readable_on (const GdkRGBA *c, const GdkRGBA *const *on, int n, double min)
{
	GdkRGBA ends[2] = { { 0, 0, 0, 1 }, { 1, 1, 1, 1 } };
	int side, first, i;
	double t;

	for (i = 0; i < n && palette_contrast (c, on[i]) >= min; i++)
		;
	if (i == n)
		return *c;
	/* lighter than the surfaces: whiter; darker: blacker */
	first = palette_contrast (c, &ends[0]) > palette_contrast (c, &ends[1]) ? 1 : 0;
	for (side = 0; side < 2; side++)
	{
		const GdkRGBA *to = &ends[side ? !first : first];

		for (t = 0.02; t <= 1.0001; t += 0.02)
		{
			GdkRGBA m = q16 (mix (c, to, t));

			for (i = 0; i < n && palette_contrast (&m, on[i]) >= min; i++)
				;
			if (i == n)
				return m;
		}
	}
	return ends[first];
}

void
palette_ui_colors (const GdkRGBA *c, nx_ui_colors *ui)
{
	GdkRGBA w = { 1, 1, 1, 1 }, k = { 0, 0, 0, 1 };
	const GdkRGBA *away, *cand[3];
	double best = 0;
	int i;

	ui->view = c[COL_BG];
	ui->text = c[COL_FG];
	ui->dark = palette_contrast (&ui->view, &w) > palette_contrast (&ui->view, &k);
	away = ui->dark ? &w : &k;
	/* chrome: a little lighter in dark schemes, a little darker in light */
	ui->bg = mix (&ui->view, away, ui->dark ? 0.06 : 0.04);
	ui->hover = mix (&ui->bg, away, ui->dark ? 0.08 : 0.06);
	ui->active = mix (&ui->bg, away, ui->dark ? 0.14 : 0.11);
	/* mid-tone backgrounds: that way can run into the text's own tone
	   (light text on a mid-dark one); step the other way then */
	if (palette_contrast (&ui->text, &ui->active) < NX_MIN_TEXT &&
		 palette_contrast (&ui->text, &ui->active) < palette_contrast (&ui->text, &ui->bg))
	{
		const GdkRGBA *back = away == &w ? &k : &w;
		ui->hover = mix (&ui->bg, back, ui->dark ? 0.08 : 0.06);
		ui->active = mix (&ui->bg, back, ui->dark ? 0.14 : 0.11);
	}
	/* the text on the chrome, and on a hovered or pressed button, as on
	   the text box */
	{
		const GdkRGBA *on[] = { &ui->bg, &ui->hover, &ui->active };
		ui->fg = readable_on (&ui->text, on, 3, NX_MIN_TEXT);
	}
	/* dim: halfway to the background, or as far as still reads */
	ui->dim = mix (&ui->bg, &ui->fg, 0.5);
	if (palette_contrast (&ui->dim, &ui->bg) < NX_MIN_DIM)
		ui->dim = mix_until (&ui->bg, &ui->fg, &ui->bg, 0.5, NX_MIN_DIM);
	ui->border = mix (&ui->bg, &ui->fg, ui->dark ? 0.22 : 0.20);
	if (palette_contrast (&ui->border, &ui->bg) < NX_MIN_BORDER ||
		 palette_contrast (&ui->border, &ui->view) < NX_MIN_BORDER)
	{
		GdkRGBA b = mix_until (&ui->bg, &ui->fg, &ui->bg, 0.2, NX_MIN_BORDER);
		ui->border = palette_contrast (&b, &ui->view) >= NX_MIN_BORDER ? b
			: mix_until (&ui->bg, &ui->fg, &ui->view, 0.2, NX_MIN_BORDER);
	}

	/* the accent: the scheme's blue (its accent in nearly every scheme),
	   else its green, else the selection; the first that stands out from
	   both the text box and the chrome, else the one that stands out most,
	   moved until it does */
	cand[0] = &c[12];
	cand[1] = &c[3];
	cand[2] = &c[COL_MARK_BG];
	ui->accent = *cand[0];
	for (i = 0; i < 3; i++)
	{
		double r = MIN (palette_contrast (cand[i], &ui->view), palette_contrast (cand[i], &ui->bg));
		if (r >= NX_MIN_ACCENT)
		{
			ui->accent = *cand[i];
			break;
		}
		if (r > best)
		{
			best = r;
			ui->accent = *cand[i];
		}
	}
	if (i == 3)
	{
		const GdkRGBA *on[] = { &ui->bg, &ui->view };
		ui->accent = readable_on (&ui->accent, on, 2, NX_MIN_ACCENT);
	}
	/* text on the accent (the default button, a selected row): white or
	   near-black, whichever reads better; a mid-tone accent neither reads
	   on is moved, as little as it takes, away from that text while it
	   still stands out from the window */
	{
		GdkRGBA nk = { 0.08, 0.08, 0.08, 1 };
		const GdkRGBA *on[] = { &ui->bg, &ui->view };
		double t;

		ui->accent_fg = palette_contrast (&w, &ui->accent) >= palette_contrast (&nk, &ui->accent) ? w : nk;
		for (t = 0.02; t <= 1.0001 && palette_contrast (&ui->accent_fg, &ui->accent) < NX_MIN_TEXT; t += 0.02)
		{
			const GdkRGBA *other = ui->accent_fg.red > 0.5 ? &k : &w;
			GdkRGBA m = q16 (mix (&ui->accent, other, 0.02));
			int j;

			for (j = 0; j < 2 && palette_contrast (&m, on[j]) >= NX_MIN_ACCENT; j++)
				;
			if (j < 2)
			{
				/* that way it sinks into the window: the other text instead */
				ui->accent_fg = ui->accent_fg.red > 0.5 ? nk : w;
				if (palette_contrast (&ui->accent_fg, &ui->accent) >= NX_MIN_TEXT)
					break;
				other = ui->accent_fg.red > 0.5 ? &k : &w;
				m = q16 (mix (&ui->accent, other, 0.02));
			}
			ui->accent = m;
		}
	}
}
