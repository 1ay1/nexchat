/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* The colour schemes and the one rule every one of them goes through
   before it reaches the screen (palette_make_readable). Kept apart from
   palette.c, which needs the whole app, so tests/palette-contrast.c checks
   every scheme on its own. */

#ifndef NEXCHAT_PALETTE_SCHEMES_H
#define NEXCHAT_PALETTE_SCHEMES_H

#include <gtk/gtk.h>
#include "palette.h"

typedef struct
{
	const char *name;
	guint32 c[20];	/* mIRC 0-15, background, text, selection, selected text */
} nx_scheme;

typedef struct
{
	const char *label;
	int light, dark;
} nx_scheme_pair;

extern const nx_scheme *const nx_scheme_list;
extern const int nx_scheme_n;
extern const nx_scheme_pair *const nx_pair_list;
extern const int nx_pair_n;

/* HexChat's own colours, for light and dark mode */
extern const GdkRGBA nx_defaults_light[MAX_COL + 1];
extern const GdkRGBA nx_defaults_dark[MAX_COL + 1];

/* the minimum contrast (WCAG ratio, 1-21) each kind of colour gets */
#define NX_MIN_TEXT 4.5			/* the text, nick and event colours, tab activity */
#define NX_MIN_MIRC 3.0			/* the 16 colours others pick with ^C */
#define NX_MIN_LINE 3.0			/* the marker line, spelling underline, away nicks */
#define NX_MIN_SELECTION 1.3	/* the selection against the background */

double palette_contrast (const GdkRGBA *a, const GdkRGBA *b);
/* c, or the nearest colour of the same hue (towards black or white) that
   has at least min contrast against bg */
GdkRGBA palette_readable (const GdkRGBA *c, const GdkRGBA *bg, double min);
/* all MAX_COL + 1 colours of scheme i */
void palette_scheme_fill (GdkRGBA *out, int i);
/* every colour readable on the background, the selection visible and
   readable; colours that already are stay exactly as they are */
void palette_make_readable (GdkRGBA *c);

/* the window around the text box, from its colours (palette_ui_css): the
   chrome a step off the text background, its text, dimmed text, borders,
   hover/pressed steps, and the accent (checks, default button, focus) */
typedef struct
{
	GdkRGBA view, text;		/* lists, entries: the text box's own */
	GdkRGBA bg, fg;			/* windows, dialogs, buttons */
	GdkRGBA hover, active;
	GdkRGBA dim;				/* dim labels, insensitive text */
	GdkRGBA border;
	GdkRGBA accent, accent_fg;
	gboolean dark;
} nx_ui_colors;

#define NX_MIN_DIM 3.0			/* dim labels still say something */
#define NX_MIN_ACCENT 3.0		/* a checked box, the focus ring: WCAG's UI minimum */
#define NX_MIN_BORDER 1.4		/* entry and frame edges you can find */

void palette_ui_colors (const GdkRGBA *c, nx_ui_colors *ui);

#endif
