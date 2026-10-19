/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

#ifndef HEXCHAT_PALETTE_H
#define HEXCHAT_PALETTE_H

extern GdkRGBA colors[];

/* colors[] used to be GdkColor (16-bit channels). colors.conf, Pango
   attributes and a few printf("#%02x...") sites still want integers; these
   give back exactly the old numbers. */
guint16 palette_c16 (double channel);
#define C16R(c) palette_c16 ((c).red)
#define C16G(c) palette_c16 ((c).green)
#define C16B(c) palette_c16 ((c).blue)
#define C8R(c) (C16R (c) >> 8)
#define C8G(c) (C16G (c) >> 8)
#define C8B(c) (C16B (c) >> 8)

#define COL_MARK_FG 32
#define COL_MARK_BG 33
#define COL_FG 34
#define COL_BG 35
#define COL_MARKER 36
#define COL_NEW_DATA 37
#define COL_HILIGHT 38
#define COL_NEW_MSG 39
#define COL_AWAY 40
#define COL_SPELL 41
#define MAX_COL 41

void palette_alloc (GtkWidget * widget);
/* colors[] from the built-in defaults for the current light/dark mode plus
   the user's own picks in colors.conf */
void palette_load (void);
/* writes only the colours that differ from the current defaults */
void palette_save (void);
/* light or dark defaults for every colour the user didn't pick; TRUE if
   any colour changed ("native" also follows a theme change) */
gboolean palette_set_dark (gboolean dark);
gboolean palette_dark (void);
/* any colour picked by the user */
gboolean palette_is_custom (void);
/* forget the user's picks: every colour follows the system */
void palette_use_defaults (void);

/* color schemes: "" is HexChat's (follows light/dark), "native" takes
   the system's text colours, "pair:X" a light/dark pair, else a scheme
   name. Setting one drops the user's picks. */
const char *palette_scheme (void);
void palette_set_scheme (const char *id);
/* the list Preferences shows: id and label of entry i */
int palette_scheme_count (void);
char *palette_scheme_id (int i);
char *palette_scheme_label (int i);
/* Preferences keeps these to undo on Cancel */
void palette_backup (void);
void palette_restore (void);

/* the whole window in the scheme's colours: GTK's own theme colours
   (@theme_bg_color, borders, selection...) and the widgets that draw
   their own, from the text box's background, text and blue. Appended
   to the app CSS; nothing for the default and "native" schemes, which
   follow the system theme. */
void palette_ui_css (GString *css);
/* -1 the system decides, else 0 light / 1 dark: the scheme's background */
int palette_ui_dark (void);
/* how many of the list's entries are the short list (default, native,
   the light/dark pairs); the rest are every single scheme */
int palette_scheme_short_count (void);

#endif
