/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Make GTK's widgets look like the platform's own on Windows and macOS:
   the system UI font, the system accent colour, light/dark from the
   system setting, and controls styled like Windows 11 (Fluent) or macOS
   (Aqua). GTK draws everything itself, so this is CSS on top of the
   built-in theme. On Linux nothing changes: the user's GTK theme is the
   native look there. gui_native_look = 0 turns it off. */

#include <string.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "platform-style.h"

#ifdef G_OS_WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

#ifdef G_OS_WIN32
/* Windows: apps use light or dark per this registry value (Settings >
   Personalisation > Colours > "Choose your app mode") */
static gboolean
win32_apps_dark (void)
{
	DWORD v = 1, sz = sizeof v;

	if (RegGetValueW (HKEY_CURRENT_USER,
							L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
							L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS)
		return FALSE;
	return v == 0;
}

/* the accent colour the user picked (0xAABBGGRR in the registry) */
static gboolean
win32_accent (GdkRGBA *c)
{
	DWORD v, sz = sizeof v;
	BOOL opaque;

	if (RegGetValueW (HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM",
							L"AccentColor", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS)
	{
		c->red = (v & 0xff) / 255.0;
		c->green = ((v >> 8) & 0xff) / 255.0;
		c->blue = ((v >> 16) & 0xff) / 255.0;
		c->alpha = 1;
		return TRUE;
	}
	if (DwmGetColorizationColor (&v, &opaque) == S_OK)
	{
		c->red = ((v >> 16) & 0xff) / 255.0;
		c->green = ((v >> 8) & 0xff) / 255.0;
		c->blue = (v & 0xff) / 255.0;
		c->alpha = 1;
		return TRUE;
	}
	return FALSE;
}

/* the font Windows uses for UI text ("Segoe UI 9" on 10/11) */
static char *
win32_ui_font (void)
{
	NONCLIENTMETRICSW ncm = { sizeof ncm };
	HDC dc;
	int pt, dpi = 96;
	char *face;

	if (!SystemParametersInfoW (SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
		return g_strdup ("Segoe UI 9");
	dc = GetDC (NULL);
	if (dc)
	{
		dpi = GetDeviceCaps (dc, LOGPIXELSY);
		ReleaseDC (NULL, dc);
	}
	pt = MAX (8, -MulDiv (ncm.lfMessageFont.lfHeight, 72, dpi));
	face = g_utf16_to_utf8 (ncm.lfMessageFont.lfFaceName, -1, NULL, NULL, NULL);
	if (!face || !*face)
	{
		g_free (face);
		face = g_strdup ("Segoe UI");
	}
	{
		char *r = g_strdup_printf ("%s %d", face, pt);
		g_free (face);
		return r;
	}
}
#endif

gboolean
platform_style_dark (void)
{
#ifdef G_OS_WIN32
	return win32_apps_dark ();
#else
	return FALSE;
#endif
}

static void
rgba_css (GString *s, const char *name, const GdkRGBA *c)
{
	g_string_append_printf (s, "@define-color %s rgb(%d,%d,%d);\n", name,
									(int) (c->red * 255 + 0.5), (int) (c->green * 255 + 0.5),
									(int) (c->blue * 255 + 0.5));
}

/* Windows 11 (Fluent) controls: 4px corners, 1px borders with a darker
   bottom edge on buttons and an accent underline on focused entries,
   the accent colour for selection, checks and default buttons */
static void
fluent_css (GString *s, gboolean dark, const GdkRGBA *accent)
{
	GdkRGBA a = *accent;

	/* colours from the WinUI 3 / Fluent token set */
	if (dark)
		g_string_append (s,
			"@define-color nx_win_bg #202020;\n"
			"@define-color nx_win_layer #2b2b2b;\n"
			"@define-color nx_win_ctrl #2d2d2d;\n"
			"@define-color nx_win_ctrl_hover #323232;\n"
			"@define-color nx_win_ctrl_press #272727;\n"
			"@define-color nx_win_stroke rgba(255,255,255,0.0698);\n"
			"@define-color nx_win_stroke_bottom rgba(255,255,255,0.0465);\n"
			"@define-color nx_win_text #ffffff;\n"
			"@define-color nx_win_text2 rgba(255,255,255,0.786);\n"
			"@define-color nx_win_field #1f1f1f;\n"
			"@define-color nx_win_border rgba(255,255,255,0.08);\n");
	else
		g_string_append (s,
			"@define-color nx_win_bg #f3f3f3;\n"
			"@define-color nx_win_layer #fbfbfb;\n"
			"@define-color nx_win_ctrl #fbfbfb;\n"
			"@define-color nx_win_ctrl_hover #f6f6f6;\n"
			"@define-color nx_win_ctrl_press #f5f5f5;\n"
			"@define-color nx_win_stroke rgba(0,0,0,0.0578);\n"
			"@define-color nx_win_stroke_bottom rgba(0,0,0,0.1622);\n"
			"@define-color nx_win_text #1a1a1a;\n"
			"@define-color nx_win_text2 rgba(0,0,0,0.6063);\n"
			"@define-color nx_win_field #ffffff;\n"
			"@define-color nx_win_border rgba(0,0,0,0.0803);\n");
	rgba_css (s, "nx_win_accent", &a);

	g_string_append (s,
		/* the theme's own colour names, so everything built on them follows */
		"@define-color theme_bg_color @nx_win_bg;\n"
		"@define-color window_bg_color @nx_win_bg;\n"
		"@define-color theme_fg_color @nx_win_text;\n"
		"@define-color window_fg_color @nx_win_text;\n"
		"@define-color theme_base_color @nx_win_field;\n"
		"@define-color view_bg_color @nx_win_field;\n"
		"@define-color theme_text_color @nx_win_text;\n"
		"@define-color view_fg_color @nx_win_text;\n"
		"@define-color theme_selected_bg_color @nx_win_accent;\n"
		"@define-color accent_bg_color @nx_win_accent;\n"
		"@define-color accent_color @nx_win_accent;\n"
		"@define-color theme_selected_fg_color #ffffff;\n"
		"@define-color accent_fg_color #ffffff;\n"
		"@define-color borders @nx_win_border;\n"
		"@define-color headerbar_bg_color @nx_win_bg;\n"
		"@define-color popover_bg_color @nx_win_layer;\n"
		"@define-color card_bg_color @nx_win_layer;\n"

		"window, window.background, dialog { background-color: @nx_win_bg; color: @nx_win_text; }\n"

		/* buttons: WinUI "standard" button */
		"button { background-image: none; background-color: @nx_win_ctrl; color: @nx_win_text;"
		" border: 1px solid @nx_win_stroke; border-bottom-color: @nx_win_stroke_bottom;"
		" border-radius: 4px; box-shadow: none; text-shadow: none; min-height: 22px; }\n"
		"button:hover { background-color: @nx_win_ctrl_hover; }\n"
		"button:active, button:checked { background-color: @nx_win_ctrl_press; color: @nx_win_text2;"
		" border-bottom-color: @nx_win_stroke; }\n"
		"button:disabled { color: alpha(@nx_win_text, 0.36); }\n"
		/* the default button (OK, Connect): accent filled */
		"button.default, button.suggested-action { background-color: @nx_win_accent; color: white;"
		" border-color: alpha(white, 0.08); border-bottom-color: alpha(black, 0.4); }\n"
		"button.default:hover, button.suggested-action:hover { background-color: alpha(@nx_win_accent, 0.9); }\n"
		/* flat buttons (toolbar, search bar) stay flat until hovered */
		"button.flat { background-color: transparent; border-color: transparent; }\n"
		"button.flat:hover { background-color: alpha(@nx_win_text, 0.06); }\n"

		/* entries: a field with an accent underline when focused */
		"entry, spinbutton, dropdown > button, combobox > box > button {"
		" background-color: @nx_win_field; color: @nx_win_text; background-image: none;"
		" border: 1px solid @nx_win_stroke; border-bottom: 1px solid @nx_win_stroke_bottom;"
		" border-radius: 4px; box-shadow: none; outline: none; }\n"
		"entry:focus-within, spinbutton:focus-within { border-bottom: 2px solid @nx_win_accent;"
		" padding-bottom: 0; }\n"
		"entry:disabled { color: alpha(@nx_win_text, 0.36); }\n"
		"entry selection, text selection, label selection { background-color: @nx_win_accent; color: white; }\n"

		/* check boxes and radios: accent filled when on */
		"check, radio { background-image: none; background-color: @nx_win_field;"
		" border: 1px solid alpha(@nx_win_text, 0.45); box-shadow: none; }\n"
		"check { border-radius: 3px; }\n"
		"check:checked, check:indeterminate, radio:checked { background-color: @nx_win_accent;"
		" border-color: @nx_win_accent; color: white; }\n"
		"check:hover, radio:hover { background-color: alpha(@nx_win_text, 0.04); }\n"
		"check:checked:hover, radio:checked:hover { background-color: alpha(@nx_win_accent, 0.9); }\n"

		/* scrollbars: Windows 11's thin overlay-style bar */
		"scrollbar { background-color: transparent; border: none; }\n"
		"scrollbar slider { background-color: alpha(@nx_win_text, 0.45); border: none;"
		" border-radius: 6px; min-width: 3px; min-height: 3px; margin: 3px; }\n"
		"scrollbar:hover slider { background-color: alpha(@nx_win_text, 0.6); min-width: 6px; min-height: 6px; }\n"

		/* menus: Fluent flyouts */
		"popover.menu > contents, popover > contents { background-color: @nx_win_layer;"
		" border: 1px solid @nx_win_border; border-radius: 8px; padding: 4px 0; }\n"
		"popover.menu modelbutton { border-radius: 4px; margin: 0 4px; }\n"
		"popover.menu modelbutton:hover, popover.menu modelbutton:selected {"
		" background-color: alpha(@nx_win_text, 0.06); color: @nx_win_text; }\n"
		"popovermenubar { background-color: @nx_win_bg; }\n"
		"popovermenubar > item { border-radius: 4px; }\n"
		"popovermenubar > item:selected { background-color: alpha(@nx_win_text, 0.06); }\n"

		/* lists: accent selection with the system's rounded pill marker feel
		   kept square like the classic UI, but accent coloured */
		"columnview.nx-list, treeview, listview { background-color: @nx_win_field; color: @nx_win_text; }\n"
		"columnview.nx-list > header > button { color: @nx_win_text2; }\n"

		/* frames and separators: the soft Fluent stroke */
		"frame > border, scrolledwindow.frame, notebook, notebook > header {"
		" border-color: @nx_win_border; }\n"
		"separator { background-color: @nx_win_border; }\n"
		"paned > separator { background-color: @nx_win_bg; }\n"

		/* sliders: Windows 11's thin track with a round thumb and an accent dot */
		"scale trough { background-color: alpha(@nx_win_text, 0.45); min-height: 4px; border-radius: 2px; border: none; }\n"
		"scale highlight { background-color: @nx_win_accent; border-radius: 2px; border: none; }\n"
		"scale slider { background-color: @nx_win_ctrl; min-width: 20px; min-height: 20px; margin: -9px;"
		" border-radius: 50%; border: 1px solid @nx_win_stroke;"
		" box-shadow: inset 0 0 0 5px @nx_win_ctrl, inset 0 0 0 10px @nx_win_accent; }\n"
		"scale slider:hover { box-shadow: inset 0 0 0 4px @nx_win_ctrl, inset 0 0 0 10px @nx_win_accent; }\n"

		/* spin buttons: the field with flat +/- inside */
		"spinbutton > button { background-color: transparent; border: none; border-radius: 4px; }\n"
		"spinbutton > button:hover { background-color: alpha(@nx_win_text, 0.06); }\n"

		/* tooltips: Fluent tooltip */
		"tooltip, tooltip.background { background-color: @nx_win_layer; color: @nx_win_text;"
		" border: 1px solid @nx_win_border; border-radius: 4px; box-shadow: none; }\n"
		"tooltip > box { padding: 4px 8px; }\n"

		/* notebook tabs (Preferences pages, the tab bar layout) */
		"notebook > header > tabs > tab { border-radius: 4px 4px 0 0; }\n"
		"notebook > header > tabs > tab:checked { color: @nx_win_text; box-shadow: inset 0 -2px @nx_win_accent; }\n");
}

/* macOS (Aqua): system blue accent, 13pt UI font, rounded controls */
static void
aqua_css (GString *s, gboolean dark)
{
	if (dark)
		g_string_append (s,
			"@define-color nx_mac_bg #323232;\n"
			"@define-color nx_mac_ctrl #656565;\n"
			"@define-color nx_mac_field #1e1e1e;\n"
			"@define-color nx_mac_text #ffffff;\n"
			"@define-color nx_mac_border rgba(255,255,255,0.12);\n"
			"@define-color nx_mac_accent #0a84ff;\n");
	else
		g_string_append (s,
			"@define-color nx_mac_bg #ececec;\n"
			"@define-color nx_mac_ctrl #ffffff;\n"
			"@define-color nx_mac_field #ffffff;\n"
			"@define-color nx_mac_text #000000;\n"
			"@define-color nx_mac_border rgba(0,0,0,0.15);\n"
			"@define-color nx_mac_accent #007aff;\n");
	g_string_append (s,
		"@define-color theme_bg_color @nx_mac_bg;\n"
		"@define-color window_bg_color @nx_mac_bg;\n"
		"@define-color theme_fg_color @nx_mac_text;\n"
		"@define-color theme_base_color @nx_mac_field;\n"
		"@define-color view_bg_color @nx_mac_field;\n"
		"@define-color theme_selected_bg_color @nx_mac_accent;\n"
		"@define-color accent_bg_color @nx_mac_accent;\n"
		"@define-color theme_selected_fg_color #ffffff;\n"
		"window, window.background { background-color: @nx_mac_bg; color: @nx_mac_text; }\n"
		"button { background-image: none; background-color: @nx_mac_ctrl; color: @nx_mac_text;"
		" border: 0.5px solid @nx_mac_border; border-radius: 5px;"
		" box-shadow: 0 0.5px 1px alpha(black, 0.15); min-height: 20px; }\n"
		"button:active, button:checked { background-color: shade(@nx_mac_ctrl, 0.92); }\n"
		"button.default, button.suggested-action { background-color: @nx_mac_accent; color: white; }\n"
		"entry { background-color: @nx_mac_field; border: 1px solid @nx_mac_border; border-radius: 4px;"
		" box-shadow: none; }\n"
		"entry:focus-within { outline: 3px solid alpha(@nx_mac_accent, 0.5); outline-offset: 0; }\n"
		"check, radio { background-image: none; background-color: @nx_mac_field;"
		" border: 0.5px solid alpha(@nx_mac_text, 0.3); }\n"
		"check { border-radius: 3px; }\n"
		"check:checked, radio:checked { background-color: @nx_mac_accent; border-color: @nx_mac_accent; color: white; }\n"
		"scrollbar slider { background-color: alpha(@nx_mac_text, 0.35); border: none; border-radius: 6px;"
		" min-width: 7px; min-height: 7px; margin: 2px; }\n"
		"popover > contents { background-color: @nx_mac_field; border-radius: 6px;"
		" border: 0.5px solid @nx_mac_border; }\n");
}

/* CSS for the platform's look, or "" (Linux, or turned off). Goes in
   before nexchat's own layout rules. */
void
platform_style_css (GString *s)
{
	if (!prefs.hex_gui_native_look)
		return;
#ifdef G_OS_WIN32
	{
		GdkRGBA accent = { 0, 0.373, 0.722, 1 };	/* Windows' default #005fb8 */
		char *font = win32_ui_font ();
		PangoFontDescription *fd = pango_font_description_from_string (font);

		win32_accent (&accent);
		fluent_css (s, win32_apps_dark (), &accent);
		/* the UI font everywhere but the chat view (that's text_font) */
		g_string_append_printf (s, "window { font-family: \"%s\"; font-size: %dpt; }\n",
										pango_font_description_get_family (fd),
										pango_font_description_get_size (fd) / PANGO_SCALE);
		pango_font_description_free (fd);
		g_free (font);
	}
#elif defined(__APPLE__)
	aqua_css (s, FALSE);
	g_string_append (s, "window { font-family: \".AppleSystemUIFont\", \"Helvetica Neue\"; font-size: 13px; }\n");
#else
	(void) fluent_css;
	(void) aqua_css;
#endif
}

#ifdef G_OS_WIN32
/* Windows 11: the title bar follows light/dark, and uses Mica if there */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

void
platform_style_window_hwnd (void *hwnd)
{
	BOOL dark;
	int round = 2;	/* DWMWCP_ROUND */

	if (!hwnd || !prefs.hex_gui_native_look)
		return;
	dark = win32_apps_dark ();
	DwmSetWindowAttribute (hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
	DwmSetWindowAttribute (hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof round);
}

#if GTK_CHECK_VERSION(4, 0, 0)
#include "gtkutil.h"

/* every window, as it maps: its title bar matches the app's light/dark */
static gboolean
map_hook (GSignalInvocationHint *ih, guint n, const GValue *v, gpointer d)
{
	GtkWidget *w = g_value_get_object (&v[0]);

	if (GTK_IS_WINDOW (w))
		platform_style_window_hwnd (gtkutil_window_hwnd (GTK_WINDOW (w)));
	return TRUE;
}
#endif
#endif

void
platform_style_init (void)
{
#if defined(G_OS_WIN32) && GTK_CHECK_VERSION(4, 0, 0)
	g_type_class_unref (g_type_class_ref (GTK_TYPE_WINDOW));
	g_signal_add_emission_hook (g_signal_lookup ("map", GTK_TYPE_WIDGET), 0, map_hook, NULL, NULL);
#endif
}
