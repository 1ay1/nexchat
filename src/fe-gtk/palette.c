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

#include <string.h>
#include <math.h>

#include "fe-gtk.h"
#include "palette.h"
#include "palette-schemes.h"
#include "platform-style.h"

#include "../common/hexchat.h"
#include "../common/util.h"
#include "../common/cfgfiles.h"
#include "../common/typedef.h"


/* 16-bit channel values, same numbers as HexChat's GdkColor table and the
   colors.conf file, so nothing a user saved changes. */
#define C16(r,g,b) { (r) / 65535.0, (g) / 65535.0, (b) / 65535.0, 1.0 }
#define C8(r,g,b) C16 ((r) * 0x101, (g) * 0x101, (b) * 0x101)

GdkRGBA colors[MAX_COL + 1];

/* HexChat's light and dark defaults and the schemes: palette-schemes.c */

/* which colours the user picked: those stay put when the system switches
   between light and dark, the rest follow it */
static gboolean custom[MAX_COL + 1];
static gboolean palette_is_dark;
static gboolean palette_ready;

/* the colour scheme: "" HexChat's, "native", "pair:Label" or a name */
static char scheme[64];
static GdkRGBA scheme_cols[MAX_COL + 1];


static double
luma (const GdkRGBA *c)
{
	return 0.299 * c->red + 0.587 * c->green + 0.114 * c->blue;
}

static int
scheme_find (const char *name)
{
	int i;

	for (i = 0; i < nx_scheme_n; i++)
		if (!strcmp (nx_scheme_list[i].name, name))
			return i;
	return -1;
}

/* the colours of the current scheme, for the current light/dark mode;
   whichever it is, they go through palette_make_readable, so every scheme
   (and the system's own text colours) ends up legible */
static void
scheme_pick (GdkRGBA *out)
{
	int i = -1;

	if (!strcmp (scheme, "native"))
	{
		GdkRGBA t[4];

		platform_style_text_colors (t);
		/* the rest from HexChat's own, light or dark to match */
		memcpy (out, luma (&t[0]) < 0.5 ? nx_defaults_dark : nx_defaults_light, sizeof scheme_cols);
		out[COL_BG] = t[0];
		out[COL_FG] = t[1];
		out[COL_MARK_BG] = t[2];
		out[COL_MARK_FG] = t[3];
		return;
	}
	if (!strncmp (scheme, "pair:", 5))
	{
		int p;

		for (p = 0; p < nx_pair_n; p++)
			if (!strcmp (nx_pair_list[p].label, scheme + 5))
				i = palette_is_dark ? nx_pair_list[p].dark : nx_pair_list[p].light;
	}
	else if (scheme[0])
		i = scheme_find (scheme);

	if (i >= 0)
		palette_scheme_fill (out, i);
	else
		memcpy (out, palette_is_dark ? nx_defaults_dark : nx_defaults_light, sizeof scheme_cols);
}

static void
scheme_compute (GdkRGBA *out)
{
	scheme_pick (out);
	palette_make_readable (out);
}

/* a single scheme or a pair (not HexChat's own, not "native") */
static gboolean
scheme_colours_ui (void)
{
	return scheme[0] && strcmp (scheme, "native");
}

int
palette_ui_dark (void)
{
	if (!scheme_colours_ui ())
		return -1;
	return luma (&colors[COL_BG]) < 0.5;
}

int
palette_scheme_short_count (void)
{
	return 2 + nx_pair_n;
}


static void
css_rgb (GString *css, const char *fmt, const GdkRGBA *c)
{
	char v[32];
	g_snprintf (v, sizeof v, "rgb(%d,%d,%d)", (int) (c->red * 255 + .5),
	            (int) (c->green * 255 + .5), (int) (c->blue * 255 + .5));
	g_string_append_printf (css, fmt, v);
}

void
palette_ui_css (GString *css)
{
	GdkRGBA text, bg, fg, sel, selfg, bord, dim, hover, active, view;
	nx_ui_colors ui;

	if (!scheme_colours_ui ())
		return;
	/* from the text box (palette-schemes.c: the same contrast rules as the
	   chat, checked for every scheme by tests/palette-contrast.c) */
	palette_ui_colors (colors, &ui);
	view = ui.view;
	text = ui.text;
	bg = ui.bg;
	fg = ui.fg;
	hover = ui.hover;
	active = ui.active;
	dim = ui.dim;
	bord = ui.border;
	sel = ui.accent;
	selfg = ui.accent_fg;

	/* the theme's named colours: everything written with @theme_* follows */
	css_rgb (css, "@define-color theme_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color theme_fg_color %s;\n", &fg);
	css_rgb (css, "@define-color theme_base_color %s;\n", &view);
	css_rgb (css, "@define-color theme_text_color %s;\n", &text);
	css_rgb (css, "@define-color theme_selected_bg_color %s;\n", &sel);
	css_rgb (css, "@define-color theme_selected_fg_color %s;\n", &selfg);
	css_rgb (css, "@define-color theme_unfocused_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color theme_unfocused_fg_color %s;\n", &dim);
	css_rgb (css, "@define-color theme_unfocused_base_color %s;\n", &view);
	css_rgb (css, "@define-color theme_unfocused_text_color %s;\n", &text);
	css_rgb (css, "@define-color theme_unfocused_selected_bg_color %s;\n", &sel);
	css_rgb (css, "@define-color theme_unfocused_selected_fg_color %s;\n", &selfg);
	css_rgb (css, "@define-color borders %s;\n", &bord);
	css_rgb (css, "@define-color unfocused_borders %s;\n", &bord);
	css_rgb (css, "@define-color insensitive_fg_color %s;\n", &dim);
	css_rgb (css, "@define-color insensitive_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color insensitive_base_color %s;\n", &view);
	css_rgb (css, "@define-color content_view_bg %s;\n", &view);
	css_rgb (css, "@define-color text_view_bg %s;\n", &view);
	/* libadwaita's names (GTK4 themes and a user's gtk.css use these):
	   without them, cards, headers, popovers and the accent come from the
	   desktop theme, not the scheme */
	css_rgb (css, "@define-color window_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color window_fg_color %s;\n", &fg);
	css_rgb (css, "@define-color view_bg_color %s;\n", &view);
	css_rgb (css, "@define-color view_fg_color %s;\n", &text);
	css_rgb (css, "@define-color accent_color %s;\n", &sel);
	css_rgb (css, "@define-color accent_bg_color %s;\n", &sel);
	css_rgb (css, "@define-color accent_fg_color %s;\n", &selfg);
	css_rgb (css, "@define-color headerbar_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color headerbar_fg_color %s;\n", &fg);
	css_rgb (css, "@define-color headerbar_border_color %s;\n", &bord);
	css_rgb (css, "@define-color headerbar_backdrop_color %s;\n", &bg);
	css_rgb (css, "@define-color card_bg_color %s;\n", &view);
	css_rgb (css, "@define-color card_fg_color %s;\n", &text);
	css_rgb (css, "@define-color dialog_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color dialog_fg_color %s;\n", &fg);
	css_rgb (css, "@define-color popover_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color popover_fg_color %s;\n", &fg);
	css_rgb (css, "@define-color sidebar_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color sidebar_fg_color %s;\n", &fg);
	css_rgb (css, "@define-color secondary_sidebar_bg_color %s;\n", &bg);
	css_rgb (css, "@define-color secondary_sidebar_fg_color %s;\n", &fg);
	css_rgb (css, "@define-color thumbnail_bg_color %s;\n", &view);
	css_rgb (css, "@define-color thumbnail_fg_color %s;\n", &text);
	css_rgb (css, "@define-color scrollbar_outline_color %s;\n", &bord);
	/* and the widgets that write them out literally in a gtk.css */
	g_string_append (css,
		".card { background-color: @card_bg_color; color: @card_fg_color; }\n"
		"button.suggested-action, button.default { background-color: @theme_selected_bg_color;"
		" color: @theme_selected_fg_color; }\n"
		"switch:checked, checkbutton check:checked, checkbutton radio:checked {"
		" background-color: @theme_selected_bg_color; border-color: @theme_selected_bg_color;"
		" color: @theme_selected_fg_color; }\n"
		"scale > trough > highlight, progressbar > trough > progress {"
		" background-color: @theme_selected_bg_color; }\n"
		"tooltip { background-color: @theme_bg_color; color: @theme_fg_color; border-color: @borders; }\n"
		"listview > row:hover:not(:selected), row.activatable:hover:not(:selected) {"
		" background-color: alpha(@theme_fg_color, 0.06); }\n"
		"entry:focus, entry:focus-within, spinbutton:focus-within {"
		" outline-color: alpha(@theme_selected_bg_color, 0.5); border-color: @theme_selected_bg_color; }\n");

	/* GTK's Default theme writes most widgets with fixed greys, not the
	   names above: give each kind of surface its colour */
	g_string_append (css,
		"window, window.background, dialog, popover > contents, popover > arrow,"
		" headerbar, .titlebar, actionbar, notebook > header"
		" { background-color: @theme_bg_color; background-image: none; box-shadow: none; }\n"
		"headerbar, .titlebar { border-color: @borders; }\n"
		"popover > contents { border: 1px solid @borders; }\n"
		/* text and list areas */
		"textview, textview > text, .view, columnview, listview, treeview, list,"
		" columnview > listview, iconview, entry, spinbutton, searchbar > revealer > box"
		" { background-color: @theme_base_color; color: @theme_text_color; }\n"
		"entry, spinbutton, dropdown > button, combobox > box > button"
		" { background-image: none; border-color: @borders; box-shadow: none; }\n"
		"entry:focus-within, spinbutton:focus-within"
		" { border-color: @theme_selected_bg_color; outline-color: alpha(@theme_selected_bg_color, 0.5); }\n"
		"entry placeholder, entry > text > placeholder { color: @insensitive_fg_color; }\n"
		/* buttons */
		"button, dropdown > button, menubutton > button, spinbutton > button, switch,"
		" scale > trough, progressbar > trough, scrollbar > range > trough"
		" { background-image: none; box-shadow: none; text-shadow: none; -gtk-icon-shadow: none;"
		" border-color: @borders; color: @theme_fg_color; }\n"
		"button, dropdown > button, menubutton > button, combobox > box > button"
		" { background-color: @theme_bg_color; }\n"
		"spinbutton > button { background-color: transparent; border-color: @borders; }\n"
		"button.flat, button.image-button.flat, .toolbar button, headerbar button:not(.suggested-action):not(.destructive-action)"
		" { background-color: transparent; border-color: transparent; }\n");
	css_rgb (css, "button:hover, dropdown > button:hover, menubutton > button:hover, spinbutton > button:hover,"
		" button.flat:hover, notebook > header tab:hover { background-color: %s; }\n", &hover);
	css_rgb (css, "button:active, button:checked, dropdown > button:active, menubutton > button:checked,"
		" button.flat:active, button.flat:checked { background-color: %s; }\n", &active);
	g_string_append (css,
		"button:disabled, entry:disabled, spinbutton:disabled, label:disabled"
		" { color: @insensitive_fg_color; background-color: transparent; }\n"
		"button.suggested-action, button.default, switch:checked, scale > trough > highlight,"
		" progressbar > trough > progress, levelbar block.filled"
		" { background-color: @theme_selected_bg_color; color: @theme_selected_fg_color;"
		" border-color: @theme_selected_bg_color; background-image: none; }\n"
		"button.destructive-action { background-image: none; }\n"
		/* check boxes and radios */
		"check, radio { background-color: @theme_base_color; background-image: none;"
		" border-color: @borders; box-shadow: none; color: @theme_selected_fg_color; }\n"
		"check:checked, radio:checked, check:indeterminate, radio:indeterminate"
		" { background-color: @theme_selected_bg_color; border-color: @theme_selected_bg_color; }\n"
		"switch { background-color: @borders; }\n"
		"switch > slider { background-color: @theme_fg_color; background-image: none;"
		" border-color: transparent; box-shadow: none; }\n"
		/* lists and selection */
		"row:selected, .view:selected, columnview > listview > row:selected,"
		" listview > row:selected, treeview:selected, text selection, label > selection,"
		" entry > text > selection, flowboxchild:selected,"
		/* the cells in a selected row too: a desktop theme's *:selected
		   matches them, and would paint its own colour over ours */
		" listview > row:selected *:selected, listview > row:selected > cell,"
		" columnview > listview > row:selected > cell"
		" { background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }\n");
	css_rgb (css, "row:hover:not(:selected), listview > row:hover:not(:selected)"
		" { background-color: %s; }\n", &hover);
	g_string_append (css,
		"columnview > header > button, treeview > header > button"
		" { background-color: @theme_bg_color; color: @theme_fg_color; border-color: @borders;"
		" background-image: none; }\n"
		/* separators, frames, tabs, scrollbars */
		"separator, paned > separator { background-color: @borders; }\n"
		"frame > border, .frame { border-color: @borders; }\n"
		"notebook > header { border-color: @borders; }\n"
		"notebook > header > tabs > tab { color: @insensitive_fg_color; }\n"
		"notebook > header > tabs > tab:checked { color: @theme_fg_color; box-shadow: inset 0 -3px @theme_selected_bg_color; }\n"
		"scrollbar { background-color: transparent; border-color: transparent; }\n"
		"scrollbar > range > trough { background-color: transparent; border: none; }\n"
		/* only the colour: themes (Adwaita) give the slider a transparent
		   8px border its -8px margin cancels; taking the border away left
		   a negative size (GTK warns, the slider misdraws) */
		"scrollbar > range > trough > slider { background-color: alpha(@theme_fg_color, 0.35); }\n"
		"scrollbar > range > trough > slider:hover { background-color: alpha(@theme_fg_color, 0.55); }\n"
		"scale > trough { background-color: @borders; }\n"
		"scale > trough > slider { background-color: @theme_fg_color; background-image: none; border-color: @borders; box-shadow: none; }\n"
		/* menus, tooltips */
		"popover.menu > contents, popover.menu modelbutton"
		" { background-color: @theme_bg_color; color: @theme_fg_color; }\n"
		"popover.menu modelbutton:hover, popover.menu modelbutton:focus"
		" { background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }\n"
		"tooltip, tooltip.background { background-color: alpha(@theme_bg_color, 0.97);"
		" color: @theme_fg_color; border: 1px solid @borders; box-shadow: none; }\n"
		"tooltip > box, tooltip label { background: none; color: @theme_fg_color; }\n"
		"dim-label, .dim-label, label.dim-label { color: @insensitive_fg_color; opacity: 1; }\n"
		"link, label link { color: @theme_selected_bg_color; }\n");
}

static const GdkRGBA *
palette_defaults (void)
{
	return scheme_cols;
}

static gboolean
same_color (const GdkRGBA *a, const GdkRGBA *b)
{
	return C16R (*a) == C16R (*b) && C16G (*a) == C16G (*b) && C16B (*a) == C16B (*b);
}

/* recompute the scheme's colours and apply those not picked by the user;
   TRUE if any colour changed */
static gboolean
palette_reset (void)
{
	GdkRGBA old[MAX_COL + 1];
	int i;

	memcpy (old, colors, sizeof old);
	scheme_compute (scheme_cols);
	for (i = 0; i <= MAX_COL; i++)
		if (!custom[i])
			colors[i] = scheme_cols[i];
	for (i = 0; i <= MAX_COL; i++)
		if (!same_color (&old[i], &colors[i]))
			return TRUE;
	return FALSE;
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
	gboolean changed = !palette_ready || palette_is_dark != !!dark;

	/* "native" also changes with the GTK theme, at the same moments */
	if (!changed && strcmp (scheme, "native"))
		return FALSE;
	palette_is_dark = !!dark;
	palette_ready = TRUE;
	return palette_reset () || changed;
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

const char *
palette_scheme (void)
{
	return scheme;
}

void
palette_set_scheme (const char *id)
{
	g_strlcpy (scheme, id ? id : "", sizeof scheme);
	memset (custom, 0, sizeof custom);
	palette_reset ();
}

/* "" and "native", the pairs, then every scheme */
int
palette_scheme_count (void)
{
	return 2 + nx_pair_n + nx_scheme_n;
}

char *
palette_scheme_id (int i)
{
	if (i == 0)
		return g_strdup ("");
	if (i == 1)
		return g_strdup ("native");
	i -= 2;
	if (i < nx_pair_n)
		return g_strdup_printf ("pair:%s", nx_pair_list[i].label);
	i -= nx_pair_n;
	return g_strdup (nx_scheme_list[i].name);
}

char *
palette_scheme_label (int i)
{
	if (i == 0)
		return g_strdup (_("nexchat (follows light/dark)"));
	if (i == 1)
		return g_strdup (_("Native (system text colors)"));
	i -= 2;
	if (i < nx_pair_n)
		return g_strdup_printf (_("%s (follows light/dark)"), nx_pair_list[i].label);
	i -= nx_pair_n;
	return g_strdup (nx_scheme_list[i].name);
}

static struct
{
	char scheme[sizeof scheme];
	gboolean custom[MAX_COL + 1];
	GdkRGBA colors[MAX_COL + 1];
} backup;

void
palette_backup (void)
{
	memcpy (backup.scheme, scheme, sizeof scheme);
	memcpy (backup.custom, custom, sizeof custom);
	memcpy (backup.colors, colors, sizeof backup.colors);
}

void
palette_restore (void)
{
	memcpy (scheme, backup.scheme, sizeof scheme);
	memcpy (custom, backup.custom, sizeof custom);
	scheme_compute (scheme_cols);
	memcpy (colors, backup.colors, sizeof backup.colors);
}

/* a colour that isn't a choice: HexChat's light/dark or the scheme's */
static gboolean
is_default (int i, const GdkRGBA *c)
{
	return same_color (c, &scheme_cols[i]) ||
		(!scheme[0] && (same_color (c, &nx_defaults_light[i]) || same_color (c, &nx_defaults_dark[i])));
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
	scheme[0] = 0;
	palette_ready = TRUE;

	fh = hexchat_open_file ("colors.conf", O_RDONLY, 0, 0);
	if (fh == -1)
	{
		palette_reset ();
		return;
	}
	fstat (fh, &st);
	cfg = g_malloc0 (st.st_size + 1);
	read_all (fh, cfg, st.st_size);

	if (!cfg_get_str (cfg, "scheme", scheme, sizeof scheme))
		scheme[0] = 0;
	palette_reset ();

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
		/* older files list every colour: a default one wasn't picked */
		if (is_default (i, &c))
			continue;
		colors[i] = c;
		custom[i] = TRUE;
	}
	g_free (cfg);
	close (fh);
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
		if (scheme[0])
		{
			char *line = g_strdup_printf ("scheme = %s\n", scheme);
			if (write (fh, line, strlen (line)) < 0)
				perror ("colors.conf");
			g_free (line);
		}
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
