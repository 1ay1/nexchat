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
#include <string.h>
#include <stdlib.h>

#include "fe-gtk.h"
#include "platform-style.h"
#ifdef __APPLE__
#include "platform-macos.h"
#include <dlfcn.h>
#endif

#ifdef WIN32
#include <gdk/win32/gdkwin32.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/text.h"
#include "../common/cfgfiles.h"
#include "../common/hexchatc.h"
#include "../common/plugin.h"
#include "../common/server.h"
#include "../common/url.h"
#include "../common/userlist.h"
#include "../common/outbound.h"
#include "gtkutil.h"
#include "upload.h"
#include "replybar.h"
#include "maingui.h"
#include "bncsetup.h"
#include "bncdetect.h"
#include "bncwin.h"
#include "../common/servlist.h"
#include "chanview.h"
#include "cmdpopup.h"
#include "pixmaps.h"
#include "chanlist.h"
#include "joind.h"
#include "xtext.h"
#include "palette.h"
#include "menu.h"
#include "notifygui.h"
#include "textgui.h"
#include "fkeys.h"
#include "plugin-tray.h"
#include "urlgrab.h"
#include "setup.h"
#include "plugin-notification.h"

#ifdef USE_LIBCANBERRA
#include <canberra.h>
#endif

GdkPixbuf *channelwin_pix;

#ifdef USE_LIBCANBERRA
static ca_context *ca_con;
#endif


/* === command-line parameter parsing : requires glib 2.6 === */

static char *arg_cfgdir = NULL;
static gint arg_show_autoload = 0;
static gint arg_show_config = 0;
static gint arg_show_version = 0;
static gint arg_minimize = 0;

static const GOptionEntry gopt_entries[] = 
{
 {"no-auto",	'a', 0, G_OPTION_ARG_NONE,	&arg_dont_autoconnect, N_("Don't auto connect to servers"), NULL},
 {"cfgdir",	'd', 0, G_OPTION_ARG_STRING,	&arg_cfgdir, N_("Use a different config directory"), "PATH"},
 {"no-plugins",	'n', 0, G_OPTION_ARG_NONE,	&arg_skip_plugins, N_("Don't auto load any plugins"), NULL},
 {"plugindir",	'p', 0, G_OPTION_ARG_NONE,	&arg_show_autoload, N_("Show plugin/script auto-load directory"), NULL},
 {"configdir",	'u', 0, G_OPTION_ARG_NONE,	&arg_show_config, N_("Show user config directory"), NULL},
 {"url",	 0,  G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &arg_url, N_("Open an irc://server:port/channel?key URL"), "URL"},
 {"command",	'c', 0, G_OPTION_ARG_STRING,	&arg_command, N_("Execute command:"), "COMMAND"},
#ifdef USE_DBUS
 {"existing",	'e', 0, G_OPTION_ARG_NONE,	&arg_existing, N_("Open URL or execute command in an existing nexchat"), NULL},
#endif
 {"minimize",	 0,  0, G_OPTION_ARG_INT,	&arg_minimize, N_("Begin minimized. Level 0=Normal 1=Iconified 2=Tray"), N_("level")},
 {"version",	'v', 0, G_OPTION_ARG_NONE,	&arg_show_version, N_("Show version information"), NULL},
 {G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_STRING_ARRAY, &arg_urls, N_("Open an irc://server:port/channel?key URL"), "URL"},
 {NULL}
};

#ifdef WIN32
/* --help, --version, bad options: a GUI app has no console, so show them
   in a native message box (GTK isn't up yet) */
static void
create_msg_dialog (gchar *title, gchar *message)
{
	wchar_t *t = g_utf8_to_utf16 (title, -1, NULL, NULL, NULL);
	wchar_t *m = g_utf8_to_utf16 (message, -1, NULL, NULL, NULL);

	MessageBoxW (NULL, m, t, MB_OK | MB_ICONINFORMATION);
	g_free (t);
	g_free (m);
}
#endif

#if defined(__APPLE__)
/* GTK's macOS backend, not nexchat, logs these:
   - "gdk_surface_thaw_updates: assertion 'surface->update_freeze_count >
     0' failed": _gdk_macos_surface_show() thaws a surface each time it's
     mapped, but hiding never re-freezes it, so every popover, tooltip or
     dialog shown a second time thaws a count already at 0 (reproduced in a
     40-line GTK-only program; fixed by osx/gtk-patches/0001, sent upstream);
   - "Duplicate call to gdk_frame_timings_presented()" / "called on skipped
     frame": 4.24's frame presentation, rewritten in GTK 4.25.
   nexchat built against osx/build-gtk.sh's GTK has neither, and nothing
   is hidden there. With an unpatched GTK those exact lines are dropped so
   real warnings stay visible; NEXCHAT_GDK_NOISE=1 shows them anyway. */
static GLogWriterOutput
quiet_gdk_writer (GLogLevelFlags level, const GLogField *fields, gsize n, gpointer data)
{
	gsize i;
	const char *domain = NULL, *msg = NULL;

	for (i = 0; i < n; i++)
		if (!strcmp (fields[i].key, "GLIB_DOMAIN"))
			domain = fields[i].value;
		else if (!strcmp (fields[i].key, "MESSAGE"))
			msg = fields[i].value;
	if (domain && msg && !strcmp (domain, "Gdk") &&
		 (strstr (msg, "called on skipped frame") ||
		  strstr (msg, "Duplicate call to gdk_frame_timings_presented") ||
		  strstr (msg, "gdk_surface_thaw_updates: assertion")))
		return G_LOG_WRITER_HANDLED;
	return g_log_writer_default (level, fields, n, data);
}

/* the GTK we run on has osx/gtk-patches applied: osx/build-gtk.sh leaves
   share/nexchat/gtk-patched in its prefix, next to lib/libgtk-4 */
static gboolean
gtk_has_macos_fixes (void)
{
	const char *v = g_getenv ("NEXCHAT_GTK_PATCHED");
	Dl_info info;
	gboolean yes = FALSE;

	if (v)
		return *v == '1';
	if (dladdr ((void *) gtk_get_major_version, &info) && info.dli_fname)
	{
		char *lib = g_path_get_dirname (info.dli_fname);
		char *mark = g_build_filename (lib, "..", "share", "nexchat", "gtk-patched", NULL);
		yes = g_file_test (mark, G_FILE_TEST_EXISTS);
		g_free (mark);
		g_free (lib);
	}
	return yes;
}
#endif

int
fe_args (int argc, char *argv[])
{
	GError *error = NULL;
	GOptionContext *context;
	char *buffer;

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif
#ifdef __APPLE__
	/* inside nexchat.app: plugins, translations, icons, schemas and pixbuf
	   loaders come from the bundle, wherever it was copied to */
	macos_bundle_setup (GETTEXT_PACKAGE);
#endif

	context = g_option_context_new (NULL);
#ifdef WIN32
	g_option_context_set_help_enabled (context, FALSE);	/* disable stdout help as stdout is unavailable for subsystem:windows */
#endif
	g_option_context_add_main_entries (context, gopt_entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);

#ifdef WIN32
	if (error)											/* workaround for argv not being available when using subsystem:windows */
	{
		if (error->message)								/* the error message contains argv so search for patterns in that */
		{
			if (strstr (error->message, "--help-all") != NULL)
			{
				buffer = g_option_context_get_help (context, FALSE, NULL);
				create_msg_dialog ("Long Help", buffer);
				g_free (buffer);
				return 0;
			}
			else if (strstr (error->message, "--help") != NULL || strstr (error->message, "-?") != NULL)
			{
				buffer = g_option_context_get_help (context, TRUE, NULL);
				create_msg_dialog ("Help", buffer);
				g_free (buffer);
				return 0;
			}
			else 
			{
				buffer = g_strdup_printf ("%s\n", error->message);
				create_msg_dialog ("Error", buffer);
				g_free (buffer);
				return 1;
			}
		}
	}
#else
	if (error)
	{
		if (error->message)
			printf ("%s\n", error->message);
		return 1;
	}
#endif

	g_option_context_free (context);

	if (arg_show_version)
	{
		buffer = g_strdup_printf ("%s %s", PACKAGE_NAME, PACKAGE_VERSION);
#ifdef WIN32
		create_msg_dialog ("Version Information", buffer);
#else
		puts (buffer);
#endif
		g_free (buffer);

		return 0;
	}

	if (arg_show_autoload)
	{
		buffer = g_strdup_printf ("%s%caddons%c", get_xdir(), G_DIR_SEPARATOR, G_DIR_SEPARATOR);
#ifdef WIN32
		create_msg_dialog ("Plugin/Script Auto-load Directory", buffer);
#else
		puts (buffer);
#endif
		g_free (buffer);

		return 0;
	}

	if (arg_show_config)
	{
		buffer = g_strdup_printf ("%s%c", get_xdir(), G_DIR_SEPARATOR);
#ifdef WIN32
		create_msg_dialog ("User Config Directory", buffer);
#else
		puts (buffer);
#endif
		g_free (buffer);

		return 0;
	}

#ifdef WIN32
	/* this is mainly for irc:// URL handling. When windows calls us from */
	/* I.E, it doesn't give an option of "Start in" directory, like short */
	/* cuts can. So we have to set the current dir manually, to the path  */
	/* of the exe. */
	{
		char *tmp = g_strdup (argv[0]);
		char *sl;

		sl = strrchr (tmp, G_DIR_SEPARATOR);
		if (sl)
		{
			*sl = 0;
			chdir (tmp);
		}
		g_free (tmp);
	}
#endif

#if defined(__APPLE__)
	/* NEXCHAT_GDK_NOISE=1: show them anyway (to find what sets them off) */
	if (!g_getenv ("NEXCHAT_GDK_NOISE") && !gtk_has_macos_fixes ())
		g_log_set_writer_func (quiet_gdk_writer, NULL, NULL);
#endif

#if defined(WIN32)
	/* the real Windows title bar, borders, snap and shadow instead of
	   GTK's drawn ones */
	if (!g_getenv ("GTK_CSD"))
		g_setenv ("GTK_CSD", "0", TRUE);
#endif
	nx_gtk_init (&argc, &argv);
	platform_style_init ();


	return -1;
}

/* GTK2 kept the text box font in a GtkStyle that the input box and user
   list used. GTK3 styles them from CSS (fe_apply_css), so all that's left
   is the user-visible part: complain once if the font doesn't load. */
void
fe_check_font (void)
{
	char buf[256];
	PangoFontDescription *fd = pango_font_description_from_string (prefs.hex_text_font);

	if (pango_font_description_get_size (fd) == 0)
	{
		g_snprintf (buf, sizeof (buf), _("Failed to open font:\n\n%s"), prefs.hex_text_font);
		fe_message (buf, FE_MSG_ERROR);
	}
	pango_font_description_free (fd);
}

/* ---- per-widget CSS ----

   GTK2 gave the input boxes, user list and channel tree the text box font
   and colours with gtk_widget_set_style / gtk_widget_modify_* and a bit of
   gtkrc. GTK3 ignores GtkStyle for most widgets, so the same thing is now a
   generated stylesheet. It only matches our own widget names
   (#hexchat-inputbox, #hexchat-userlist, #hexchat-tree); the rest of the
   theme is left alone. Rebuilt whenever prefs or colours change. */

static GtkCssProvider *fe_css_provider;

static void
css_append_color (GString *css, const char *prop, const GdkRGBA *c)
{
	g_string_append_printf (css, "%s: rgb(%d,%d,%d);", prop, C8R (*c), C8G (*c), C8B (*c));
}

static void
css_append_font (GString *css, const PangoFontDescription *fd)
{
	const char *family = pango_font_description_get_family (fd);
	int size = pango_font_description_get_size (fd);
	PangoFontMask set = pango_font_description_get_set_fields (fd);

	if (family && *family)
	{
		/* pango allows "A,B" fallback lists; quote each name for CSS */
		char **names = g_strsplit (family, ",", -1);
		int i;

		g_string_append (css, "font-family: ");
		for (i = 0; names[i]; i++)
		{
			char *n = g_strstrip (names[i]);
			if (!*n)
				continue;
			if (i)
				g_string_append (css, ", ");
			g_string_append_printf (css, "\"%s\"", n);
		}
		g_string_append (css, ";");
		g_strfreev (names);
	}
	if (size > 0)
		g_string_append_printf (css, "font-size: %g%s;", (double)size / PANGO_SCALE,
										pango_font_description_get_size_is_absolute (fd) ? "px" : "pt");
	if (set & PANGO_FONT_MASK_WEIGHT)
		g_string_append_printf (css, "font-weight: %d;", (int)pango_font_description_get_weight (fd));
	if (set & PANGO_FONT_MASK_STYLE)
	{
		switch (pango_font_description_get_style (fd))
		{
		case PANGO_STYLE_ITALIC: g_string_append (css, "font-style: italic;"); break;
		case PANGO_STYLE_OBLIQUE: g_string_append (css, "font-style: oblique;"); break;
		default: break;
		}
	}
}

static void
css_parsing_error (GtkCssProvider *p, GtkCssSection *section, GError *err, gpointer d)
{
	char *where = gtk_css_section_to_string (section);
	g_warning ("nexchat css: %s: %s", where, err->message);
	g_free (where);
}

void
fe_apply_css (void)
{
	GString *css;
	GdkDisplay *screen = gdk_display_get_default ();
	GError *err = NULL;
	PangoFontDescription *fd;

	if (!screen)
		return;
	css = g_string_new (NULL);

	/* same font + fallback fe_check_font() uses */
	fd = pango_font_description_from_string (prefs.hex_text_font);
	if (pango_font_description_get_size (fd) == 0)
	{
		pango_font_description_free (fd);
		fd = pango_font_description_from_string ("sans 11");
	}

	if (!fe_css_provider)
	{
		fe_css_provider = gtk_css_provider_new ();
		g_signal_connect (fe_css_provider, "parsing-error", G_CALLBACK (css_parsing_error), NULL);
		gtk_style_context_add_provider_for_display (screen,
					GTK_STYLE_PROVIDER (fe_css_provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}

	if (prefs.hex_gui_input_style)
	{
		g_string_append (css, "#hexchat-inputbox {");
		if (fd)
			css_append_font (css, fd);
		/* text-box colours; background-image: none is what the old
		   "adwaita workaround" gtkrc did, so the flat colour shows */
		css_append_color (css, "color", &colors[COL_FG]);
		css_append_color (css, "caret-color", &colors[COL_FG]);
		css_append_color (css, "background-color", &colors[COL_BG]);
		/* GTK2 drew a flat, square box; GTK3 themes add a rounded frame
		   and a focus glow. Keep the theme's 1px border colour. */
		g_string_append (css, "background-image: none; border-radius: 0;"
							  " box-shadow: none; outline: none; }\n");
		g_string_append (css, "#hexchat-inputbox:focus { box-shadow: none; }\n");
	}

	if (prefs.hex_gui_ulist_style)
	{
		g_string_append (css, "#hexchat-userlist, #hexchat-tree {");
		if (fd)
			css_append_font (css, fd);
		g_string_append (css, "}\n");
		/* treeview.view is the row area; :selected rows keep the theme's
		   selection, as GTK2 only overrode the NORMAL state */
		g_string_append (css, "#hexchat-userlist.view:not(:selected), #hexchat-tree.view:not(:selected) {");
		css_append_color (css, "color", &colors[COL_FG]);
		css_append_color (css, "background-color", &colors[COL_BG]);
		g_string_append (css, "}\n");
		/* GTK2 themes drew the selected row grey; Adwaita uses its accent
		   blue. Use the text box's own "selected text" colours, which is
		   what HexChat users already picked for a selection. */
		g_string_append (css, "#hexchat-userlist.view:selected, #hexchat-tree.view:selected {");
		css_append_color (css, "color", &colors[COL_MARK_FG]);
		css_append_color (css, "background-color", &colors[COL_MARK_BG]);
		g_string_append (css, "}\n");
		/* the same on nxlist's column views: the list area and its rows */
		g_string_append (css, "#hexchat-userlist > listview, #hexchat-tree > listview,"
									 " #hexchat-userlist > listview > row:not(:selected),"
									 " #hexchat-tree > listview > row:not(:selected) {");
		css_append_color (css, "color", &colors[COL_FG]);
		css_append_color (css, "background-color", &colors[COL_BG]);
		g_string_append (css, "}\n");
		g_string_append (css, "#hexchat-userlist > listview > row:selected,"
									 " #hexchat-tree > listview > row:selected {");
		css_append_color (css, "color", &colors[COL_MARK_FG]);
		css_append_color (css, "background-color", &colors[COL_MARK_BG]);
		g_string_append (css, "}\n");
	}

	/* the chat view scrollbar: GTK3 themes make it a thin overlay-looking
	   slider; GTK2 had a full-width trough with stepper arrows. The arrows
	   are a style property, set below; this keeps the slider full width.
	   (GTK4 scrollbars have no steppers.) */
	g_string_append (css,
		"#hexchat-scrollbar slider { min-width: 10px; min-height: 24px; }\n");
	/* lag and throttle meters under the user list: GTK4's progress bar is
	   a rounded trough with its own minimum sizes, which at this size drew
	   as two hollow outlines with a speck of colour. Flat 4px bars: a
	   track and its fill, nothing else. */
	g_string_append (css,
		"progressbar.nx-meter { min-height: 4px; padding: 0; margin: 2px 4px; }\n"
		"progressbar.nx-meter.horizontal > trough { min-height: 4px; min-width: 0; border: none;"
		" border-radius: 2px; background-color: alpha(@theme_fg_color, 0.12); box-shadow: none; }\n"
		"progressbar.nx-meter.horizontal > trough > progress { min-height: 4px; min-width: 0; border: none;"
		" border-radius: 2px; background-image: none; box-shadow: none; }\n"
		"progressbar.nx-meter.nx-throttle.horizontal > trough > progress { background-color: #e5a50a; }\n");
	/* "Replying to alice: ..." over the input box: a quiet strip with an
	   accent edge, like the quote in chat apps */
	g_string_append (css,
		".nx-replybar { padding: 2px 4px 2px 8px; margin: 2px 0;"
		" border-left: 3px solid @theme_selected_bg_color;"
		" background-color: alpha(@theme_fg_color, 0.06); }\n"
		".nx-replybar button { min-height: 0; min-width: 0; padding: 0 4px; }\n");

	/* Channel tabs, the nick button and the "3 ops, 16 total" box: GTK2
	   buttons were text + a few px; GTK3 themes pad buttons to ~32px tall,
	   which makes the tab bar half again as high. Shrink our own buttons
	   only, keep the theme's look otherwise. */
	g_string_append (css,
		"#hexchat-tab { padding: 1px 4px; min-height: 0; min-width: 0; border-radius: 0; }\n"
		/* GTK2 drew the current tab sunken with a dark edge; GTK3's :checked is
		   only a slightly darker fill. Add the inset edge back. */
		"#hexchat-tab:checked { box-shadow: inset 1px 1px 0 rgba(0,0,0,0.45); }\n"
		/* tabs never prelight (GTK3 blocks enter/leave on them) */
		"#hexchat-tab:hover:not(:checked) { background-image: none; }\n"
		"#hexchat-tab label { padding: 0; }\n"
		"#hexchat-nickbutton { padding: 1px 4px; min-height: 0; }\n"
		/* Character Chart: 28px-wide cells like GTK2 */
		"#hexchat-charbutton { padding: 0; min-width: 0; min-height: 23px; border-radius: 0; }\n"
		"#hexchat-ulistinfo label { padding: 1px 2px; }\n"
		/* the connecting indicator draws in its CSS colour */
		"#nexchat-connecting { color: @theme_selected_bg_color; }\n");

	/* GTK4 popover menus: make them plain GTK3 menus. Square, 1px border, no
	   shadow margin, 25px rows, items flush to the edge. */
	g_string_append (css,
		"popover.menu { padding: 0; margin: 0; box-shadow: none; }\n"
		"popover.menu > contents { padding: 0; border-radius: 0;"
		" border: 1px solid alpha(black, 0.23); box-shadow: none; }\n"
		"popover.menu > arrow { min-width: 0; min-height: 0; border: none; }\n"
		"popover.menu modelbutton { min-height: 17px; padding: 4px 8px 4px 30px;"
		" border-radius: 0; }\n"
		"popover.menu modelbutton:hover, popover.menu modelbutton:selected {"
		" background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }\n"
		"popover.menu separator { margin: 0; }\n"
		/* GTK3 menus drew a real box/circle for checks and radios */
		"popover.menu check, popover.menu radio { min-width: 14px; min-height: 14px;"
		" border: 1px solid alpha(currentColor, 0.45); background-color: @theme_base_color;"
		" margin: 0 8px 0 -22px; }\n"
		"popover.menu check { border-radius: 3px; }\n"
		"popover.menu radio { border-radius: 100%; }\n"
		"popover.menu modelbutton:selected check, popover.menu modelbutton:selected radio {"
		" color: @theme_fg_color; }\n"
		"popover.menu check:checked, popover.menu radio:checked {"
		" background-color: @theme_selected_bg_color; border-color: @theme_selected_bg_color;"
		" color: @theme_selected_fg_color; }\n"
		"popover.menu box.inline-buttons { padding: 0; }\n"
		"popover.menu modelbutton arrow { opacity: 1; }\n"
		/* item icons sit in the left gutter, like GTK3's image menu items */
		"popover.menu modelbutton image.nx-menu-icon { margin: 0 6px 0 -22px; min-width: 16px; }\n"
		/* the menu bar's items: GTK3 sizes, square highlight */
		"popovermenubar > item { padding: 4px 7px; border-radius: 0; }\n"
		/* frames (the chat view's): GTK3 drew them square */
		"frame, frame > border { border-radius: 0; }\n"
		/* a frame's label (nx_labelled_frame_new): on the window colour so
		   the line stops at it, as GTK3 drew it */
		"label.nx-frame-label { background: @theme_bg_color; padding-right: 1px; }\n"
		/* GTK3 lists showed focus with a dotted row, not a blue outline */
		"scrolledwindow, treeview { outline: none; }\n"
		/* entries and buttons: GTK3 Adwaita's 4px corners, not GTK4's 6px */
		"entry, button { border-radius: 4px; }\n"
		/* GTK3 Adwaita: buttons 9px each side (GTK4: 10px), the image 2px
		   from the label */
		"button.text-button, button.nx-image-text { padding-left: 9px; padding-right: 9px; }\n"
		"button.nx-image-text label { padding: 0; }\n"
		/* check/radio buttons: GTK3's 23px rows (GTK4 Adwaita pads them to 26) */
		"checkbutton { padding: 2px 2px; min-height: 0; }\n"
		/* GTK3: 3px from the edge to the box, 4px box to label */
		"checkbutton > check, checkbutton > radio { margin-left: 3px; }\n"
		/* a lone check box (no label, the alerts grid): GTK3 drew it 19px */
		"checkbutton.nx-lone { padding: 1px 2px 0 2px; }\n"
		/* nxlist views: GTK3 tree view look. GTK3 rows were the text plus the
		   renderer's 2px ypad and half the view's 4px vertical-separator,
		   top and bottom; no row separators, square selection */
		"columnview.nx-list > listview > row { padding: 0; min-height: 0; border-radius: 0; }\n"
		"columnview.nx-list > listview > row > cell { padding: 4px 4px 3px 4px; min-height: 0; }\n"
		"columnview.nx-list.nx-compact > listview > row > cell { padding-top: 2px; padding-bottom: 1px; }\n"
		"columnview.nx-list > listview > row:not(:selected) { background: none; }\n"
		/* GTK3's tree views kept the blue selection when the list didn't have
		   focus; GTK4's theme greys it */
		"columnview.nx-list > listview > row:selected { background-color: @theme_selected_bg_color;"
		" color: @theme_selected_fg_color; }\n"
		/* a GtkCellRendererPixbuf had 1px either side, not a text cell's 4 */
		"columnview.nx-list .nx-icon-cell { margin-left: -3px; margin-right: -3px; }\n"
		/* the "3 ops, 16 total" frame: the theme's frame > label margin is
		   meant for a frame's title, not a label inside it */
		"#hexchat-ulistinfo > label { margin: 0; padding: 1px 2px; min-height: 0; }\n"
		"columnview.nx-list > header { box-shadow: inset 0 -1px alpha(currentColor, 0.18); padding: 0; margin: 0; }\n"
		"columnview.nx-list > header > button { padding: 4px 6px 5px 6px; min-height: 0; border-radius: 0;"
		" border: none; background: none; box-shadow: inset -1px 0 alpha(currentColor, 0.18); font-weight: bold; }\n"
		"columnview.nx-list check { margin: 0; min-width: 14px; min-height: 14px; }\n"
		/* GTK3 drew ascending as a down arrow, at the right of the title */
		"columnview.nx-list > header > button sort-indicator.ascending { -gtk-icon-source: -gtk-icontheme('pan-down-symbolic'); }\n"
		"columnview.nx-list > header > button sort-indicator.descending { -gtk-icon-source: -gtk-icontheme('pan-up-symbolic'); }\n"
		"columnview.nx-list checkbutton { padding: 0; margin: 0; min-height: 0; }\n"
		/* trees: GTK3's 16px expander + 2px separator per level, arrow 2px in */
		"columnview.nx-list treeexpander { border-spacing: 2px; margin-left: -2px; }\n"
		"columnview.nx-list treeexpander indent { min-width: 17px; -gtk-icon-size: 16px; }\n"
		"columnview.nx-list treeexpander expander { min-width: 16px; min-height: 16px; }\n"
		/* no server rows (gui_tab_server off): the top level isn't indented */
		"columnview.nx-list.nx-unindented treeexpander { margin-left: -20px; }\n"
		"columnview.nx-list entry { min-height: 0; padding: 0 2px; margin: -2px 0; }\n"
		/* the About license text: GTK3 showed it a size smaller */
		"textview.nx-about-license, textview.nx-about-license > text { font-size: 9pt; }\n"
		/* the charset picker: a drop-down showing only its arrow */
		"dropdown.nx-combo-arrow > button > box > stack { min-width: 0; }\n"
		"dropdown.nx-combo-arrow > button > box > stack > * { opacity: 0; min-width: 0; }\n"
		/* NxDialog message dialogs: GTK3's full-width buttons */
		".message-buttons > button { min-height: 24px; padding-top: 4px; padding-bottom: 4px; }\n"
		/* the "/" command list over the input box: a plain square list with
		   the window's own background and a 1px border */
		".nx-cmdpopup { padding: 0; border-radius: 0; background-color: @theme_base_color;"
		" border: 1px solid alpha(currentColor, 0.3); }\n"
		".nx-cmdpopup list { background: none; }\n"
		".nx-cmdpopup list > row { padding: 2px 6px; min-height: 0; border-radius: 0; }\n"
		".nx-cmdpopup list > row:selected { background-color: @theme_selected_bg_color;"
		" color: @theme_selected_fg_color; }\n"
		".nx-cmdpopup list > row:selected .dim-label { opacity: 0.85; }\n"
		".nx-cmdpopup .nx-cmd-hint { padding: 2px 6px; }\n"
		/* group headings in the list, the selected command explained, keys */
		".nx-cmdpopup .nx-cmd-group { padding: 3px 6px 1px 6px; font-weight: bold;"
		" font-size: smaller; opacity: 0.7; }\n"
		".nx-cmdpopup .nx-cmd-details { padding: 3px 6px;"
		" border-top: 1px solid alpha(currentColor, 0.2); }\n"
		".nx-cmdpopup .nx-cmd-keys { padding: 1px 6px 2px 6px; font-size: smaller;"
		" border-top: 1px solid alpha(currentColor, 0.12); }\n");

	/* a scheme colours the whole window, not just the text box (last, so
	   it wins over the rules above that use the theme's names) */
	palette_ui_css (css);

	/* GTK4 loads what it can and reports errors through a signal */
	if (g_getenv ("NX_DUMP_CSS"))
		g_printerr ("---- nexchat css ----\n%s\n----\n", css->str);
	gtk_css_provider_load_from_string (fe_css_provider, css->str);
	(void) err;
	g_string_free (css, TRUE);
	pango_font_description_free (fd);
}

/* the colours follow the system's light/dark mode (the user's own picks
   stay): re-read it, and repaint everything that uses colors[] */
static void
fe_apply_scheme (void)
{
	GSList *list;

	if (!palette_set_dark (platform_style_dark ()))
		return;
	fe_apply_css ();
	for (list = sess_list; list; list = list->next)
	{
		session *sess = list->data;
		if (sess->gui && sess->gui->xtext)
		{
			gtk_xtext_set_palette (GTK_XTEXT (sess->gui->xtext), colors);
			gtk_xtext_refresh (GTK_XTEXT (sess->gui->xtext));
		}
		if (sess->gui)
			userlist_rehash (sess);
	}
	mg_apply_setup ();
}

static void
fe_scheme_changed (GObject *o, GParamSpec *p, gpointer d)
{
	fe_apply_scheme ();
}

#if defined(G_OS_WIN32)
/* Windows doesn't tell GTK when light/dark changes: look again every
   couple of seconds (cheap: nothing happens unless it flipped) */
static gboolean
fe_scheme_poll (gpointer d)
{
	platform_style_sync_scheme ();
	fe_apply_scheme ();
	return G_SOURCE_CONTINUE;
}
#endif

#ifdef __APPLE__
/* ---- how a Mac app behaves (macos-app.m) ---- */

/* the system switched light/dark: follow it now (it tells us; no polling) */
static void
fe_mac_appearance (void)
{
	platform_style_sync_scheme ();
	fe_apply_scheme ();
}

/* Dock icon clicked, or the app opened again: the main window back */
static void
fe_mac_reopen (void)
{
	GtkWidget *win = current_sess && current_sess->gui ? current_sess->gui->window : NULL;

	if (win)
	{
		gtk_widget_set_visible (win, TRUE);
		gtk_window_unminimize (GTK_WINDOW (win));
		gtk_window_present (GTK_WINDOW (win));
	}
}

/* irc://server/channel from a browser or another app */
static void
fe_mac_open_url (const char *url)
{
	char *cmd;

	fe_mac_reopen ();
	if (!current_sess)
		return;
	cmd = g_strdup_printf ("URL %s", url);
	handle_command (current_sess, cmd, FALSE);
	g_free (cmd);
}

/* screen locked or the Mac going to sleep: away on every network where
   you weren't already, and back on just those once you're back (an away
   you set yourself is left alone either way) */
static void
fe_mac_locked (gboolean locked)
{
	GSList *l;

	if (!prefs.hex_away_on_lock)
		return;
	for (l = serv_list; l; l = l->next)
	{
		server *s = l->data;
		if (!s->connected || !s->server_session)
			continue;
		if (locked && !s->is_away)
		{
			handle_command (s->server_session, "AWAY", FALSE);
			s->away_by_lock = TRUE;
		}
		else if (!locked && s->away_by_lock)
		{
			/* is_away follows the server's 306/305: send BACK even if the
			   confirmation never came, it was our AWAY */
			s->away_by_lock = FALSE;
			s->p_set_back (s);
			g_free (s->last_away_reason);
			s->last_away_reason = NULL;
		}
	}
}

/* back from sleep, or on a network again: a connection made before is
   very likely dead even though no error came yet. Ask each server: one
   that doesn't answer within a few seconds is reconnected now, instead of
   after the full ping timeout (minutes). */
static gboolean
fe_mac_probe_timeout (gpointer d)
{
	GSList *l;

	for (l = serv_list; l; l = l->next)
	{
		server *s = l->data;
		if (s->connected && s->probe_sent && !s->probe_answered)
		{
			PrintText (s->server_session, _("* No answer after the network came back: reconnecting\n"));
			s->auto_reconnect (s, FALSE, -1);
		}
		s->probe_sent = FALSE;
	}
	*(guint *) d = 0;
	return G_SOURCE_REMOVE;
}

static void
fe_mac_network_check (void)
{
	GSList *l;
	gboolean any = FALSE;
	static guint tag;

	for (l = serv_list; l; l = l->next)
	{
		server *s = l->data;
		if (s->connected)
		{
			s->probe_sent = TRUE;
			s->probe_answered = FALSE;
			/* the lag meter's own ping: answered quietly */
			tcp_sendf (s, "PING LAG%lu\r\n", make_ping_time ());
			s->lag_sent = make_ping_time ();
			any = TRUE;
		}
		else if (!s->connecting && s->recondelay_tag)
		{
			/* waiting to reconnect: do it now, the network is here */
			fe_timeout_remove (s->recondelay_tag);
			s->recondelay_tag = 0;
			s->auto_reconnect (s, FALSE, -1);
		}
	}
	if (any)
	{
		if (tag)
			g_source_remove (tag);
		tag = g_timeout_add_seconds (8, fe_mac_probe_timeout, &tag);
	}
}

static void
fe_mac_activated (void)
{
	/* attention asked for while away: answered by coming back */
	macos_request_attention (FALSE);
}

/* Quit from the Dock, a logout or restart: nexchat's Quit (asks first if
   connected and the preference says so) */
static void
fe_mac_quit (void)
{
	mg_open_quit_dialog (FALSE);
}
#endif

#ifdef __APPLE__
/* tests: NEXCHAT_NSMENU_ACTIVATE="Menu/Item" clicks it in the real menu
   bar after startup */
static gboolean
fe_nsmenu_activate (gpointer d)
{
	const char *p = g_getenv ("NEXCHAT_NSMENU_ACTIVATE");
	if (!macos_activate_menu_path (p))
		g_printerr ("NEXCHAT_NSMENU_ACTIVATE: no enabled item %s\n", p);
	return G_SOURCE_REMOVE;
}
#endif

void
fe_init (void)
{
	GtkSettings *gs = gtk_settings_get_default ();

	gtkutil_force_images ();
	palette_set_dark (platform_style_dark ());
	palette_load ();
	/* a saved scheme decides light/dark for the windows too */
	platform_style_sync_scheme ();
	key_init ();
	pixmaps_init ();

	channelwin_pix = pixmap_load_from_file (prefs.hex_text_background);
	fe_check_font ();
	fe_apply_css ();

	if (gs)
	{
#if GTK_CHECK_VERSION(4, 20, 0)
		g_signal_connect (gs, "notify::gtk-interface-color-scheme", G_CALLBACK (fe_scheme_changed), NULL);
#endif
		g_signal_connect (gs, "notify::gtk-application-prefer-dark-theme", G_CALLBACK (fe_scheme_changed), NULL);
		g_signal_connect (gs, "notify::gtk-theme-name", G_CALLBACK (fe_scheme_changed), NULL);
	}
#if defined(G_OS_WIN32)
	g_timeout_add_seconds (2, fe_scheme_poll, NULL);
#endif
#ifdef __APPLE__
	{
		static const macos_app_hooks h = {
			fe_mac_reopen, fe_mac_open_url, fe_mac_activated,
			fe_mac_network_check, fe_mac_network_check, fe_mac_appearance,
			fe_mac_locked
		};
		macos_app_install (&h);
		nx_menu_set_app_quit (fe_mac_quit);
	}
	if (g_getenv ("NEXCHAT_NSMENU_ACTIVATE"))
		g_timeout_add (1500, fe_nsmenu_activate, NULL);
#endif
}


/* tests: NEXCHAT_KEYS="<Meta>2;<Meta><Shift>bracketleft;..." presses each
   shortcut on the main window in turn (through the window's shortcut
   controllers, as a key press would), then prints the front tab */
static gboolean
fe_keys_step (gpointer data)
{
	char **keys = data;
	static int i;
	GtkWidget *win = current_sess && current_sess->gui ? current_sess->gui->window : NULL;
	char *name;

	if (!win)
		return G_SOURCE_CONTINUE;
	if (!keys[i])
	{
		g_strfreev (keys);
		return G_SOURCE_REMOVE;
	}
	{
		guint key;
		GdkModifierType mods;
		GListModel *ctl = gtk_widget_observe_controllers (win);
		guint n, done = FALSE;

		if (gtk_accelerator_parse (keys[i], &key, &mods))
			for (n = 0; n < g_list_model_get_n_items (ctl) && !done; n++)
			{
				GObject *c = g_list_model_get_item (ctl, n);
				if (GTK_IS_SHORTCUT_CONTROLLER (c))
				{
					GListModel *sl = G_LIST_MODEL (c);
					guint j;
					for (j = 0; j < g_list_model_get_n_items (sl) && !done; j++)
					{
						GtkShortcut *s = g_list_model_get_item (sl, j);
						GtkShortcutTrigger *t = gtk_shortcut_get_trigger (s);
						if (GTK_IS_KEYVAL_TRIGGER (t) &&
							 gtk_keyval_trigger_get_keyval (GTK_KEYVAL_TRIGGER (t)) == key &&
							 gtk_keyval_trigger_get_modifiers (GTK_KEYVAL_TRIGGER (t)) == mods)
						{
							gtk_shortcut_action_activate (gtk_shortcut_get_action (s), 0, win,
																	gtk_shortcut_get_arguments (s));
							done = TRUE;
						}
						g_object_unref (s);
					}
				}
				g_object_unref (c);
			}
		g_object_unref (ctl);
		name = current_sess->res && current_sess->res->tab ? chan_get_name (current_sess->res->tab) : NULL;
		g_printerr ("keys: %s -> %s%s\n", keys[i], name ? name : "?", done ? "" : " (no shortcut)");
		g_free (name);
	}
	i++;
	return G_SOURCE_CONTINUE;
}

/* tests: NEXCHAT_TABS_DUMP=1 prints every tab's text on stderr each second,
   each round after a "tabs:" line; =text also prints the front window's
   lines ("line: ...") */
static void
fe_dump_line (GtkXText *xtext, unsigned char *text, void *d)
{
	g_printerr ("line: %s\n", text);
}

static gboolean
fe_tabs_dump (gpointer d)
{
	GSList *l;

	g_printerr ("tabs:\n");
	/* tests: NEXCHAT_TAB_CLICK=FILE: when FILE names a tab, it's clicked */
	{
		const char *cf = g_getenv ("NEXCHAT_TAB_CLICK");
		char *want = NULL;
		if (cf && g_file_get_contents (cf, &want, NULL, NULL))
		{
			g_unlink (cf);
			g_strstrip (want);
			for (l = sess_list; l; l = l->next)
			{
				session *s = l->data;
				char *name = s->res && s->res->tab ? chan_get_name (s->res->tab) : NULL;
				gboolean hit = name && !strcmp (name, want);
				g_free (name);
				if (hit)
				{
					chan_click (s->res->tab);
					break;
				}
			}
			g_free (want);
		}
	}
	if (current_sess)
		g_printerr ("front: %s\n", current_sess->channel);
	if (!g_strcmp0 (g_getenv ("NEXCHAT_TABS_DUMP"), "text") && current_sess && current_sess->res)
		gtk_xtext_foreach (current_sess->res->buffer, fe_dump_line, NULL);
	if (current_sess && current_sess->gui && current_sess->gui->xtext)
	{
		const char *tip = gtk_widget_get_tooltip_text (current_sess->gui->xtext);
		char *pop = cmdpopup_describe (current_sess->gui->input_box);
		g_printerr ("tooltip: %s\n", tip ? tip : "");
		g_printerr ("cmdpopup: %s\n", pop);
		g_free (pop);
		pop = cmdpopup_geometry (current_sess->gui->input_box);
		g_printerr ("cmdgeom: %s\n", pop);
		g_printerr ("input: %s\n", gtk_editable_get_text (GTK_EDITABLE (current_sess->gui->input_box)));
		g_printerr ("title: %s\n", gtk_window_get_title (GTK_WINDOW (current_sess->gui->window)));
		{
			char *st = bncsetup_describe ();
			g_printerr ("bncsetup: %s\n", st ? st : "");
			g_free (st);
			st = bncsetup_describe_more ();
			g_printerr ("%s\n", st);
			g_free (st);
			st = bncwin_describe ();
			if (st)
				g_strdelimit (st, "\n", ' ');
			g_printerr ("bncwin: %s\n", st ? st : "");
			g_free (st);
		}
		g_printerr ("typing: %s\n", current_sess->gui->typing_label && gtk_widget_get_visible (current_sess->gui->typing_label)
			? gtk_label_get_text (GTK_LABEL (current_sess->gui->typing_label)) : "");
		g_free (pop);
	}
	for (l = sess_list; l; l = l->next)
	{
		session *s = l->data;
		if (s->res && s->res->tab)
		{
			char *name = chan_get_name (s->res->tab);
			g_printerr ("tab: %s\n", name ? name : "");
			g_free (name);
		}
	}
	return G_SOURCE_CONTINUE;
}

/* NEXCHAT_SHOT=path[:seconds]: write the main window to path as a PNG after
   the given time (default 4s), then go on. For comparing GTK3 and GTK4
   builds on systems where a screen grab isn't possible (macOS without the
   screen-recording permission, Wayland). */
static gboolean
fe_self_shot (gpointer data)
{
	char *path = data;
	GtkWidget *win = current_sess && current_sess->gui ? current_sess->gui->window : NULL;

#ifdef __APPLE__
	/* NEXCHAT_NSMENU_DUMP=path: the global menu bar as macOS has it */
	if (g_getenv ("NEXCHAT_NSMENU_DUMP"))
		macos_dump_main_menu (g_getenv ("NEXCHAT_NSMENU_DUMP"));
#endif
	/* NEXCHAT_WINDOWS_DUMP=1: every toplevel on stderr, for "what is that
	   window" questions */
	if (g_getenv ("NEXCHAT_WINDOWS_DUMP"))
	{
		GListModel *tl = gtk_window_get_toplevels ();
		guint i, n = g_list_model_get_n_items (tl);
#ifdef __APPLE__
		macos_dump_focus ();
#endif
		for (i = 0; i < n; i++)
		{
			GtkWidget *w = g_list_model_get_item (tl, i);
			GtkWidget *child = GTK_IS_WINDOW (w) ? gtk_window_get_child (GTK_WINDOW (w)) : NULL;
			g_printerr ("window %u: %s title=\"%s\" visible=%d mapped=%d %dx%d child=%s%s\n", i,
							G_OBJECT_TYPE_NAME (w),
							GTK_IS_WINDOW (w) && gtk_window_get_title (GTK_WINDOW (w)) ? gtk_window_get_title (GTK_WINDOW (w)) : "",
							gtk_widget_get_visible (w), gtk_widget_get_mapped (w),
							gtk_widget_get_width (w), gtk_widget_get_height (w),
							child ? G_OBJECT_TYPE_NAME (child) : "(none)",
							w == (current_sess && current_sess->gui ? current_sess->gui->window : NULL) ? " [main]" : "");
			g_object_unref (w);
		}
	}

	/* NEXCHAT_SHOT_WIN=1: the newest other toplevel instead (the dialog
	   NEXCHAT_MENU_ACTIVATE or -c opened), for checking dialogs natively */
	if (g_getenv ("NEXCHAT_SHOT_WIN"))
	{
		GListModel *tl = gtk_window_get_toplevels ();
		guint i, n = g_list_model_get_n_items (tl);
		for (i = 0; i < n; i++)
		{
			GtkWidget *w = g_list_model_get_item (tl, i);
			if (w != win && gtk_widget_get_visible (w) && GTK_IS_WINDOW (w)
				 && !GTK_IS_POPOVER (w))
			{
				g_object_unref (w);
				win = w;
				break;
			}
			g_object_unref (w);
		}
	}

	/* NEXCHAT_SHOT_POPUP=1: the input box's command list (drawn in the
	   window over the chat; shot on its own) */
	if (g_getenv ("NEXCHAT_SHOT_POPUP") && current_sess && current_sess->gui)
	{
		GtkWidget *pop = cmdpopup_widget (current_sess->gui->input_box);
		if (pop && gtk_widget_get_visible (pop))
			win = pop;
	}
	/* NEXCHAT_SHOT_PREVIEW=1: the image link preview popover */
	if (g_getenv ("NEXCHAT_SHOT_PREVIEW") && current_sess && current_sess->gui)
	{
		GtkWidget *c;
		for (c = gtk_widget_get_first_child (current_sess->gui->xtext); c; c = gtk_widget_get_next_sibling (c))
			if (GTK_IS_POPOVER (c) && gtk_widget_get_visible (c))
				win = c;
	}
	/* NEXCHAT_SHOT_REPLYPICK=1: the reply picker over the input box */
	if (g_getenv ("NEXCHAT_SHOT_REPLYPICK") && current_sess && current_sess->gui)
	{
		GtkWidget *c;
		for (c = gtk_widget_get_first_child (current_sess->gui->input_box); c; c = gtk_widget_get_next_sibling (c))
			if (GTK_IS_POPOVER (c) && gtk_widget_get_visible (c))
				win = c;
	}

	if (win)
	{
		int w = gtk_widget_get_width (win), h = gtk_widget_get_height (win);
		GdkPaintable *p = gtk_widget_paintable_new (win);
		GtkSnapshot *s = gtk_snapshot_new ();
		GskRenderNode *node;
		/* NEXCHAT_SHOT_SCALE=2: render at twice the size (crisp on HiDPI,
		   e.g. for the README); default 1, what the tests compare */
		const char *sc = g_getenv ("NEXCHAT_SHOT_SCALE");
		double scale = sc ? g_ascii_strtod (sc, NULL) : 1;

		if (scale < 1)
			scale = 1;
		gtk_snapshot_scale (s, scale, scale);
		gdk_paintable_snapshot (p, s, w, h);
		node = gtk_snapshot_free_to_node (s);
		if (node)
		{
			GskRenderer *r = gtk_native_get_renderer (GTK_NATIVE (win));
			graphene_rect_t rect = GRAPHENE_RECT_INIT (0, 0, w * scale, h * scale);
			GdkTexture *t = gsk_renderer_render_texture (r, node, &rect);
			gdk_texture_save_to_png (t, path);
			g_object_unref (t);
			gsk_render_node_unref (node);
		}
		g_object_unref (p);
	}
	else
		g_printerr ("NEXCHAT_SHOT: no main window yet\n");
	g_free (path);
	return G_SOURCE_REMOVE;
}

/* tests: NEXCHAT_SHOT_ON=FILE: when FILE appears it names a PNG; the window
   is shot there (with NEXCHAT_SHOT_WIN, the newest dialog), FILE removed */
static gboolean
fe_shot_on (gpointer f)
{
	char *path = NULL;

	if (g_file_get_contents (f, &path, NULL, NULL))
	{
		g_unlink (f);
		fe_self_shot (g_strdup (g_strstrip (path)));
		g_free (path);
	}
	return G_SOURCE_CONTINUE;
}

/* NEXCHAT_TIMING: the first frame GTK drew for a window (what the user
   sees), and the first time the main loop had nothing else to do */
static gboolean
fe_timing_first_frame (GtkWidget *w, GdkFrameClock *clock, gpointer d)
{
	nx_mark ("first frame drawn");
	return G_SOURCE_REMOVE;
}

static gboolean
fe_timing_idle (gpointer d)
{
	nx_mark ("main loop idle");
	return G_SOURCE_REMOVE;
}

/* tests: NEXCHAT_ZOOM="+ + - 0" runs those zoom steps (Ctrl+= / Ctrl+-
   / Ctrl+0), one every 300 ms, starting 2 s after startup */
/* tests: NEXCHAT_XDRAG="press 200 30;move 260 60;...;release 300 90"
   plays those events on the text box one per frame (as a hand would),
   then prints "xdrag-diff: N" (pixels that differ from a full redraw;
   0 = nothing left half-drawn). NEXCHAT_XDRAG_PNG=prefix saves both. */
static gboolean
fe_xdrag_step (gpointer data)
{
	char **steps = data;
	static int i;
	GtkXText *xt;

	if (!current_sess || !current_sess->gui || !current_sess->gui->xtext)
		return G_SOURCE_CONTINUE;
	xt = GTK_XTEXT (current_sess->gui->xtext);
	/* the last step asked for a redraw: let the frame happen first, as it
	   would between two real events */
	if (gtk_xtext_redraw_pending (xt))
		return G_SOURCE_CONTINUE;
	if (steps[i])
	{
		char what[16];
		double x, y;
		static int waited;
		/* "wait N": N frames (of 30 ms) before the next step */
		if (sscanf (steps[i], "wait %lf", &x) == 1)
		{
			if (++waited < (int) x)
				return G_SOURCE_CONTINUE;
			waited = 0;
		}
		else if (sscanf (steps[i], "%15s %lf %lf", what, &x, &y) == 3)
		{
			gtk_xtext_test_event (xt, what, x, y);
			/* "check": what a release left, now (several drags per run) */
			if (!strcmp (what, "pos"))
				g_printerr ("xdrag-pos: %d %.3f %d\n", (int) x, gtk_xtext_scroll_value (xt),
							gtk_xtext_backing_diff (xt, NULL, NULL));
			if (!strcmp (what, "cursor"))
			{
				GdkCursor *c = gtk_widget_get_cursor (GTK_WIDGET (xt));
				g_printerr ("xdrag-at: %d %d %s\n", (int) x, (int) y,
							c && gdk_cursor_get_name (c) ? gdk_cursor_get_name (c) : "(default)");
			}
			if (!strcmp (what, "check"))
			{
				int n = 0;
				char *sel = gtk_xtext_selection_text (xt, &n);
				g_printerr ("xdrag-step: %d %d %d ", (int) x, (int) y,
							gtk_xtext_backing_diff (xt, NULL, NULL) + 100000 * gtk_xtext_stray_marks (xt));
				if (sel)
					fwrite (sel, 1, n > 0 ? n : strlen (sel), stderr);
				g_printerr ("\n");
				g_free (sel);
			}
		}
		i++;
		return G_SOURCE_CONTINUE;
	}
	{
		const char *p = g_getenv ("NEXCHAT_XDRAG_PNG");
		char *a = p ? g_strdup_printf ("%s-now.png", p) : NULL;
		char *b = p ? g_strdup_printf ("%s-full.png", p) : NULL;
		g_printerr ("xdrag-diff: %d\n", gtk_xtext_backing_diff (xt, a, b));
		g_printerr ("xdrag-stray: %d\n", gtk_xtext_stray_marks (xt));
		{
			GdkCursor *c = gtk_widget_get_cursor (GTK_WIDGET (xt));
			g_printerr ("xdrag-cursor: %s\n", c && gdk_cursor_get_name (c) ? gdk_cursor_get_name (c) : "(default)");
		}
		{
			int n = 0;
			char *sel = gtk_xtext_selection_text (xt, &n);
			g_printerr ("xdrag-sel: %s\n", sel ? sel : "");
			g_free (sel);
		}
		g_free (a);
		g_free (b);
	}
	g_strfreev (steps);
	if (g_getenv ("NEXCHAT_XDRAG_QUIT"))
		hexchat_exit ();
	return G_SOURCE_REMOVE;
}

/* tests: NEXCHAT_XCHECK=FILE: whenever FILE appears (after real mouse
   input), print "xcheck: DIFF TEXT" (leftover pixels vs a full redraw,
   the selected text) and remove it */
static gboolean
fe_xcheck_poll (gpointer data)
{
	const char *f = data;
	GtkXText *xt;
	char *sel;
	int n = 0;

	if (!current_sess || !current_sess->gui)
		return G_SOURCE_CONTINUE;
	/* once: where the text box is on screen, for a test to aim the mouse */
	{
		static gboolean told;
		int wx, wy;
		graphene_point_t p = GRAPHENE_POINT_INIT (0, 0), q;
		GtkWidget *win = current_sess->gui->window;
		if (!told && gtk_widget_get_mapped (current_sess->gui->xtext) &&
			 gtkutil_window_get_position (GTK_WINDOW (win), &wx, &wy) &&
			 gtk_widget_compute_point (current_sess->gui->xtext, win, &p, &q))
		{
			double sx = 0, sy = 0;
			GtkNative *nat = gtk_widget_get_native (win);
			if (nat)
				gtk_native_get_surface_transform (nat, &sx, &sy);
			g_printerr ("xtext-at: %d %d %d %d\n", wx + (int) (q.x + sx), wy + (int) (q.y + sy),
							gtk_widget_get_width (current_sess->gui->xtext), gtk_widget_get_height (current_sess->gui->xtext));
			told = TRUE;
		}
	}
	if (!g_file_test (f, G_FILE_TEST_EXISTS))
		return G_SOURCE_CONTINUE;
	g_unlink (f);
	xt = GTK_XTEXT (current_sess->gui->xtext);
	sel = gtk_xtext_selection_text (xt, &n);
	g_printerr ("xcheck: %d ", gtk_xtext_backing_diff (xt, NULL, NULL));
	if (sel)
		fwrite (sel, 1, n > 0 ? n : strlen (sel), stderr);
	g_printerr ("\n");
	g_free (sel);
	return G_SOURCE_CONTINUE;
}

static gboolean
fe_zoom_step (gpointer data)
{
	char **steps = data;
	static int i;

	if (!steps[i])
	{
		const char *sw = g_getenv ("NEXCHAT_SWITCH");
		g_strfreev (steps);
		/* tests: then show this channel, zoomed while in the background */
		if (sw && *sw)
		{
			GSList *l;
			for (l = sess_list; l; l = l->next)
			{
				session *s = l->data;
				if (!g_ascii_strcasecmp (s->channel, sw))
					fe_ctrl_gui (s, FE_GUI_FOCUS, 0);
			}
		}
		return G_SOURCE_REMOVE;
	}
	mg_zoom (!strcmp (steps[i], "+") ? 1 : !strcmp (steps[i], "-") ? -1 : 0);
	i++;
	return G_SOURCE_CONTINUE;
}

static gboolean
fe_zoom_start (gpointer data)
{
	g_timeout_add (300, fe_zoom_step, data);
	return G_SOURCE_REMOVE;
}

static gboolean
fe_bnc_setup_open (gpointer d)
{
	const char *from = g_getenv ("NEXCHAT_BNC_SETUP");
	ircnet *net = from && strcmp (from, "1") ? servlist_net_find ((char *) from, NULL, strcmp) : NULL;

	bncsetup_open (current_sess && current_sess->gui ? GTK_WINDOW (current_sess->gui->window) : NULL, net, NULL);
	return G_SOURCE_REMOVE;
}

static gboolean
fe_bnc_win_open (gpointer d)
{
	const char *from = g_getenv ("NEXCHAT_BNC_WIN");
	ircnet *net = from && strcmp (from, "1") ? servlist_net_find ((char *) from, NULL, strcmp) : NULL;

	bncwin_open (current_sess && current_sess->gui ? GTK_WINDOW (current_sess->gui->window) : NULL, net);
	return G_SOURCE_REMOVE;
}

static void
fe_detect_print (bnc_found *f, gpointer d)
{
	char *s = bnc_found_describe (f);
	g_printerr ("detect:\n%sdetect-end\n", s);
	g_free (s);
	bnc_found_free (f);
}

static void
fe_probe_print (bnc_probe_result *r, gpointer d)
{
	if (r->kind)
		g_printerr ("probe: %s%s%s\n", r->kind, r->cert_untrusted ? " cert-untrusted: " : "",
					  r->cert_untrusted ? r->cert_why : "");
	else
		g_printerr ("probe-error: %s\n", r->error);
	bnc_probe_result_free (r);
}

void
fe_main (void)
{
	const char *shot = g_getenv ("NEXCHAT_SHOT");

	if (shot && *shot)
	{
		char *path = g_strdup (shot), *colon = strrchr (path, ':');
		int secs = 4;

		if (colon && colon[1] && g_ascii_isdigit (colon[1]))
		{
			*colon = 0;
			secs = atoi (colon + 1);
		}
		g_timeout_add_seconds (secs, fe_self_shot, path);
	}
	if (g_getenv ("NEXCHAT_SHOT_ON"))
		g_timeout_add (300, fe_shot_on, (gpointer) g_getenv ("NEXCHAT_SHOT_ON"));
	if (g_getenv ("NEXCHAT_TABS_DUMP"))
		g_timeout_add_seconds (1, fe_tabs_dump, NULL);
	/* tests: NEXCHAT_BNC_PROBE=host:port:tls:user:pass runs the Stay online
	   check and prints "probe: <kind>" or "probe-error: ..." */
	if (g_getenv ("NEXCHAT_BNC_PROBE"))
	{
		char **f = g_strsplit (g_getenv ("NEXCHAT_BNC_PROBE"), ":", 5);
		if (g_strv_length (f) == 5)
			bncsetup_probe (f[0], atoi (f[1]), atoi (f[2]), f[3], f[4], fe_probe_print, NULL);
		g_strfreev (f);
	}
	/* tests: NEXCHAT_BNC_SETUP=1 opens "Stay online" over the main window */
	if (g_getenv ("NEXCHAT_BNC_SETUP"))
		g_timeout_add_seconds (1, fe_bnc_setup_open, NULL);
	/* tests: NEXCHAT_BNC_WIN=1 (or a network's name) opens Server > Bouncer */
	if (g_getenv ("NEXCHAT_BNC_WIN"))
		g_timeout_add_seconds (1, fe_bnc_win_open, NULL);
	/* tests: NEXCHAT_BNC_DETECT=1 prints which bouncers this computer has */
	if (g_getenv ("NEXCHAT_BNC_DETECT"))
		bncdetect_run (fe_detect_print, NULL);

#ifdef __APPLE__
	/* Started from a terminal (not through Launch Services) the app and every
	   window it opened -- main window, Network List, or both -- would stay
	   behind the terminal: become a regular app and take the front, so the
	   last window opened (the Network List when it's shown) is key. Not
	   with --minimize, which asks for the opposite. */
	if (!arg_minimize)
	{
		GListModel *tl = gtk_window_get_toplevels ();
		guint i, n = g_list_model_get_n_items (tl);
		GtkWindow *front = NULL;

		macos_activate ();
		/* the list is newest first: the Network List over the main window */
		for (i = 0; i < n && !front; i++)
		{
			GtkWidget *w = g_list_model_get_item (tl, i);
			if (GTK_IS_WINDOW (w) && gtk_widget_get_visible (w))
				front = GTK_WINDOW (w);
			g_object_unref (w);
		}
		if (front)
			gtk_window_present (front);
	}
#endif

	if (g_getenv ("NEXCHAT_TIMING"))
	{
		GListModel *tl = gtk_window_get_toplevels ();
		GtkWidget *w = g_list_model_get_n_items (tl) ? g_list_model_get_item (tl, 0) : NULL;
		if (w)
		{
			gtk_widget_add_tick_callback (w, fe_timing_first_frame, NULL, NULL);
			g_object_unref (w);
		}
		g_idle_add_full (G_PRIORITY_LOW, fe_timing_idle, NULL, NULL);
	}

	if (g_getenv ("NEXCHAT_ZOOM"))
		g_timeout_add (2000, fe_zoom_start, g_strsplit (g_getenv ("NEXCHAT_ZOOM"), " ", -1));
	if (g_getenv ("NEXCHAT_KEYS"))
		g_timeout_add (500, fe_keys_step, g_strsplit (g_getenv ("NEXCHAT_KEYS"), ";", -1));
#ifdef USE_OPENSSL
	upload_test_init ();
#endif
	replybar_test_init ();
	if (g_getenv ("NEXCHAT_XDRAG"))
		g_timeout_add (30, fe_xdrag_step, g_strsplit (g_getenv ("NEXCHAT_XDRAG"), ";", -1));
	if (g_getenv ("NEXCHAT_XCHECK"))
		g_timeout_add (100, fe_xcheck_poll, (gpointer) g_getenv ("NEXCHAT_XCHECK"));

	nx_main ();

	/* sleep for 2 seconds so any QUIT messages are not lost. The  */
	/* GUI is closed at this point, so the user doesn't even know! */
	if (prefs.wait_on_exit)
		sleep (2);
}

void
fe_cleanup (void)
{
	/* it's saved when pressing OK in setup.c */
	/*palette_save ();*/
}

void
fe_exit (void)
{
	nx_main_quit ();
}

int
fe_timeout_add (int interval, void *callback, void *userdata)
{
	return g_timeout_add (interval, (GSourceFunc) callback, userdata);
}

int
fe_timeout_add_seconds (int interval, void *callback, void *userdata)
{
	return g_timeout_add_seconds (interval, (GSourceFunc) callback, userdata);
}

void
fe_timeout_remove (int tag)
{
	g_source_remove (tag);
}

#ifdef WIN32

static void
log_handler (const gchar   *log_domain,
		       GLogLevelFlags log_level,
		       const gchar   *message,
		       gpointer	      unused_data)
{
	session *sess;

	/* if (getenv ("HEXCHAT_WARNING_IGNORE")) this gets ignored sometimes, so simply just disable all warnings */
		return;

	sess = find_dialog (serv_list->data, "(warnings)");
	if (!sess)
		sess = new_ircwindow (serv_list->data, "(warnings)", SESS_DIALOG, 0);

	PrintTextf (sess, "%s\t%s\n", log_domain, message);
	if (getenv ("HEXCHAT_WARNING_ABORT"))
		abort ();
}

#endif

/* install tray stuff */

static int
fe_idle (gpointer data)
{
	session *sess = sess_list->data;

	plugin_add (sess, NULL, NULL, notification_plugin_init, notification_plugin_deinit, NULL, FALSE);

	plugin_add (sess, NULL, NULL, tray_plugin_init, tray_plugin_deinit, NULL, FALSE);

	if (arg_minimize == 1)
		nx_window_iconify (sess->gui->window);
	else if (arg_minimize == 2)
		tray_toggle_visibility (FALSE);

	return 0;
}

void
fe_new_window (session *sess, int focus)
{
	int tab = FALSE;

	if (sess->type == SESS_DIALOG)
	{
		if (prefs.hex_gui_tab_dialogs)
			tab = TRUE;
	} else
	{
		if (prefs.hex_gui_tab_chans)
			tab = TRUE;
	}

	mg_changui_new (sess, NULL, tab, focus);

#ifdef WIN32
	g_log_set_handler ("GLib", G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_WARNING, (GLogFunc)log_handler, 0);
	g_log_set_handler ("GLib-GObject", G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_WARNING, (GLogFunc)log_handler, 0);
	g_log_set_handler ("Gdk", G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_WARNING, (GLogFunc)log_handler, 0);
	g_log_set_handler ("Gtk", G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_WARNING, (GLogFunc)log_handler, 0);
#endif

	if (!sess_list->next)
		g_idle_add (fe_idle, NULL);

	sess->scrollback_replay_marklast = gtk_xtext_set_marker_last;
}

void
fe_new_server (struct server *serv)
{
	serv->gui = g_new0 (struct server_gui, 1);
}

static void
fe_message_response (GtkWidget *dialog, int response, gpointer unused)
{
	nx_widget_destroy (dialog);
}

void
fe_message (char *msg, int flags)
{
	GtkWidget *dialog;
	nx_msg_type type = NX_MSG_WARNING;

	if (flags & FE_MSG_ERROR)
		type = NX_MSG_ERROR;
	if (flags & FE_MSG_INFO)
		type = NX_MSG_INFO;

	dialog = nx_message_dialog_new (GTK_WINDOW (parent_window), (flags & FE_MSG_WAIT) != 0, type,
											  NX_BUTTONS_OK, (flags & FE_MSG_MARKUP) != 0, msg);
	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (fe_message_response), 0);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	nx_window_set_position_mouse (dialog);
	gtk_window_present (GTK_WINDOW (dialog));

	/* FE_MSG_WAIT: the caller goes on only once it's been read (it may be
	   about to exit). The main loop keeps running meanwhile, as GTK4 wants;
	   modal, so the rest of the app can't be used in between */
	if (flags & FE_MSG_WAIT)
	{
		g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
		while (dialog)
			g_main_context_iteration (NULL, TRUE);
	}
}

void
fe_idle_add (void *func, void *data)
{
	g_idle_add (func, data);
}

void
fe_input_remove (int tag)
{
	g_source_remove (tag);
}

int
fe_input_add (int sok, int flags, void *func, void *data)
{
	int tag, type = 0;
	GIOChannel *channel;

#ifdef WIN32
	if (flags & FIA_FD)
	{
		channel = g_io_channel_win32_new_fd (sok);
		/* read a byte at a time by waitline2 (the server child's pipe): raw
		   and unbuffered from the start. Switching later asserts once a read
		   has filled GLib's buffer. */
		g_io_channel_set_encoding (channel, NULL, NULL);
		g_io_channel_set_buffered (channel, FALSE);
	}
	else
		channel = g_io_channel_win32_new_socket (sok);
#else
	channel = g_io_channel_unix_new (sok);
#endif

	if (flags & FIA_READ)
		type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
	if (flags & FIA_WRITE)
		type |= G_IO_OUT | G_IO_ERR;
	if (flags & FIA_EX)
		type |= G_IO_PRI;

	tag = g_io_add_watch (channel, type, (GIOFunc) func, data);
	g_io_channel_unref (channel);

	return tag;
}

void
fe_set_topic (session *sess, char *topic, char *stripped_topic)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (prefs.hex_text_stripcolor_topic)
		{
			nx_entry_set_text (sess->gui->topic_entry, stripped_topic);
		}
		else
		{
			nx_entry_set_text (sess->gui->topic_entry, topic);
		}
		mg_set_topic_tip (sess);
	}
	else
	{
		g_free (sess->res->topic_text);

		if (prefs.hex_text_stripcolor_topic)
		{
			sess->res->topic_text = g_strdup (stripped_topic);
		}
		else
		{
			sess->res->topic_text = g_strdup (topic);
		}
	}
}

static void
fe_update_mode_entry (session *sess, GtkWidget *entry, char **text, char *new_text)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (sess->gui->flag_wid[0])	/* channel mode buttons enabled? */
			nx_entry_set_text (entry, new_text);
	} else
	{
		if (sess->gui->is_tab)
		{
			g_free (*text);
			*text = g_strdup (new_text);
		}
	}
}

void
fe_update_channel_key (struct session *sess)
{
	fe_update_mode_entry (sess, sess->gui->key_entry,
								 &sess->res->key_text, sess->channelkey);
	fe_set_title (sess);
}

void
fe_update_channel_limit (struct session *sess)
{
	char tmp[16];

	sprintf (tmp, "%d", sess->limit);
	fe_update_mode_entry (sess, sess->gui->limit_entry,
								 &sess->res->limit_text, tmp);
	fe_set_title (sess);
}

int
fe_is_chanwindow (struct server *serv)
{
	if (!serv->gui->chanlist_window)
		return 0;
	return 1;
}

void
fe_notify_update (char *name)
{
	if (!name)
		notify_gui_update ();
}

void
fe_text_clear (struct session *sess, int lines)
{
	gtk_xtext_clear (sess->res->buffer, lines);
}

void
fe_close_window (struct session *sess)
{
	if (sess->gui->is_tab)
		mg_tab_close (sess);
	else
		nx_widget_destroy (sess->gui->window);
}

void
fe_progressbar_start (session *sess)
{
	if (!sess->gui->is_tab || current_tab == sess)
	/* if it's the focused tab, create it for real! */
		mg_progressbar_create (sess->gui);
	else
	/* otherwise just remember to create on when it gets focused */
		sess->res->c_graph = TRUE;
}

void
fe_progressbar_end (server *serv)
{
	GSList *list = sess_list;
	session *sess;

	while (list)				  /* check all windows that use this server and  *
									   * remove the connecting graph, if it has one. */
	{
		sess = list->data;
		if (sess->server == serv)
		{
			if (sess->gui->bar)
				mg_progressbar_destroy (sess->gui);
			sess->res->c_graph = FALSE;
		}
		list = list->next;
	}
}

/* runs of joins, parts and quits as one line that grows:
     "→ alice, bob joined · ← carol left"
   A name that joins and leaves again within the run isn't listed twice:
   leaving after joining takes them off "joined" (they came and went). */
static void
jp_list (GString *s, GPtrArray *a, int max)
{
	guint i;

	for (i = 0; i < a->len && (int) i < max; i++)
	{
		if (i)
			g_string_append (s, ", ");
		g_string_append (s, g_ptr_array_index (a, i));
	}
	if ((int) a->len > max)
		g_string_append_printf (s, _(" and %d more"), (int) a->len - max);
}

static gboolean
jp_remove (GPtrArray *a, const char *nick)
{
	guint i;

	for (i = 0; i < a->len; i++)
		if (!g_ascii_strcasecmp (g_ptr_array_index (a, i), nick))
		{
			g_ptr_array_remove_index (a, i);
			return TRUE;
		}
	return FALSE;
}

gboolean
fe_print_joinpart (struct session *sess, int kind, const char *nick, time_t stamp)
{
	restore_gui *res = sess->res;
	xtext_buffer *buf = res->buffer;
	GString *s;

	/* only while our line is still the newest; anything else printed in
	   between starts a new group */
	/* the pointer alone could match a new line allocated where a trimmed
	   or cleared one was: the append count says no other line came since */
	if (!res->jp_line || buf->text_last != res->jp_line || buf->appended != res->jp_serial)
	{
		if (res->jp_joined)
			g_ptr_array_set_size (res->jp_joined, 0);
		else
			res->jp_joined = g_ptr_array_new_with_free_func (g_free);
		if (res->jp_left)
			g_ptr_array_set_size (res->jp_left, 0);
		else
			res->jp_left = g_ptr_array_new_with_free_func (g_free);
		/* the first of a run is shown as the event itself, full text */
		res->jp_line = NULL;
	}
	else
		gtk_xtext_drop_last (buf);

	if (kind == 0)
	{
		jp_remove (res->jp_left, nick);
		jp_remove (res->jp_joined, nick);
		g_ptr_array_add (res->jp_joined, g_strdup (nick));
	}
	else if (!jp_remove (res->jp_joined, nick))
	{
		jp_remove (res->jp_left, nick);
		g_ptr_array_add (res->jp_left, g_strdup (nick));
	}

	if (!res->jp_line && res->jp_joined->len + res->jp_left->len == 1)
	{
		/* one event: printed as usual (with its reason, host, colours);
		   fe_print_joinpart_done records its line */
		res->jp_pending = TRUE;
		return FALSE;
	}

	s = g_string_new ("*\t");
	if (res->jp_joined->len)
	{
		g_string_append (s, "\00303\342\206\222\017 ");
		jp_list (s, res->jp_joined, 12);
		g_string_append (s, _(" joined"));
	}
	if (res->jp_left->len)
	{
		if (res->jp_joined->len)
			g_string_append (s, "\302\240\00314\302\267\017  ");
		g_string_append (s, "\00305\342\206\220\017 ");
		jp_list (s, res->jp_left, 12);
		g_string_append (s, _(" left"));
	}
	if (!res->jp_joined->len && !res->jp_left->len)
		g_string_append_printf (s, _("\00314%s came and went"), nick);
	PrintTextRaw (buf, (unsigned char *) s->str, prefs.hex_text_indent, stamp);
	g_string_free (s, TRUE);
	res->jp_line = buf->text_last;
	res->jp_serial = buf->appended;
	return TRUE;
}

void
fe_print_text (struct session *sess, char *text, time_t stamp,
			   gboolean no_activity)
{
	PrintTextRaw (sess->res->buffer, (unsigned char *)text, prefs.hex_text_indent, stamp);
	/* the first join/part of a run: the next one replaces this line */
	if (sess->res->jp_pending)
	{
		sess->res->jp_pending = FALSE;
		sess->res->jp_line = ((xtext_buffer *) sess->res->buffer)->text_last;
		sess->res->jp_serial = ((xtext_buffer *) sess->res->buffer)->appended;
	}

	if (no_activity || !sess->gui->is_tab)
		return;

	if (sess == current_tab)
		fe_set_tab_color (sess, FE_COLOR_NONE);
	else if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
		fe_set_tab_color (sess, FE_COLOR_NEW_HILIGHT);
	else if (sess->tab_state & TAB_STATE_NEW_MSG)
		fe_set_tab_color (sess, FE_COLOR_NEW_MSG);
	else
		fe_set_tab_color (sess, FE_COLOR_NEW_DATA);
}

void
fe_beep (session *sess)
{
#ifdef WIN32
	/* Play the "Instant Message Notification" system sound
	 */
	if (!PlaySoundW (L"Notification.IM", NULL, SND_ALIAS | SND_ASYNC))
	{
		/* The user does not have the "Instant Message Notification" sound set. Fall back to system beep.
		 */
		Beep (1000, 50);
	}
#else
#ifdef USE_LIBCANBERRA
	if (ca_con == NULL)
	{
		ca_context_create (&ca_con);
		ca_context_change_props (ca_con,
										CA_PROP_APPLICATION_ID, "nexchat",
										CA_PROP_APPLICATION_NAME, DISPLAY_NAME,
										CA_PROP_APPLICATION_ICON_NAME, "io.github.nexchat", NULL);
	}

	if (ca_context_play (ca_con, 0, CA_PROP_EVENT_ID, "message-new-instant", NULL) != 0)
#endif
	gdk_display_beep (gdk_display_get_default ());
#endif
}

/* set lbuf up to search for sstr; FALSE (and said why) for a bad regex */
static gboolean
lastlog_prepare (xtext_buffer *lbuf, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags)
{
	GError *err = NULL;

	g_clear_pointer (&lbuf->search_re, g_regex_unref);
	g_clear_pointer (&lbuf->search_nee, g_free);
	g_clear_pointer (&lbuf->search_text, g_free);
	if (flags & regexp)
	{
		GRegexCompileFlags gcf = (flags & case_match)? 0: G_REGEX_CASELESS;

		lbuf->search_re = g_regex_new (sstr, gcf, 0, &err);
		if (err)
		{
			PrintText (lastlog_sess, _(err->message));
			g_error_free (err);
			return FALSE;
		}
	}
	else
	{
		if (flags & case_match)
		{
			lbuf->search_nee = g_strdup (sstr);
		}
		else
		{
			lbuf->search_nee = g_utf8_casefold (sstr, strlen (sstr));
		}
		lbuf->search_lnee = strlen (lbuf->search_nee);
	}
	lbuf->search_flags = flags & ~lastlog_all;
	lbuf->search_text = g_strdup (sstr);
	return TRUE;
}

void
fe_lastlog (session *sess, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags)
{
	xtext_buffer *buf, *lbuf;

	buf = sess->res->buffer;

	if (gtk_xtext_is_empty (buf))
	{
		PrintText (lastlog_sess, _("Search buffer is empty.\n"));
		return;
	}

	lbuf = lastlog_sess->res->buffer;
	if (!lastlog_prepare (lbuf, lastlog_sess, sstr, flags))
		return;
	gtk_xtext_lastlog (lbuf, buf);
}

int
fe_lastlog_append (session *sess, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags)
{
	xtext_buffer *buf = sess->res->buffer, *lbuf = lastlog_sess->res->buffer;
	int n;
	char *head;

	if (!lastlog_prepare (lbuf, lastlog_sess, sstr, flags))
		return -1;
	n = gtk_xtext_lastlog_count (lbuf, buf);
	if (n <= 0 || gtk_xtext_is_empty (buf))
		return 0;
	head = g_strdup_printf ("\002%s\002 (%s): %d\n", sess->channel,
									server_get_network (sess->server, TRUE), n);
	PrintText (lastlog_sess, head);
	g_free (head);
	return gtk_xtext_lastlog (lbuf, buf);
}

void
fe_set_lag (server *serv, long lag)
{
	GSList *list = sess_list;
	session *sess;
	gdouble per;
	char lagtext[64];
	char lagtip[128];
	unsigned long nowtim;

	if (lag == -1)
	{
		if (!serv->lag_sent)
			return;
		nowtim = make_ping_time ();
		lag = nowtim - serv->lag_sent;
	}

	/* if there is no pong for >30s report the lag as +30s */
	if (lag > 30000 && serv->lag_sent)
		lag=30000;

	per = ((double)lag) / 1000.0;
	if (per > 1.0)
		per = 1.0;

	g_snprintf (lagtext, sizeof (lagtext) - 1, "%s%ld.%lds",
			  serv->lag_sent ? "+" : "", lag / 1000, (lag/100) % 10);
	g_snprintf (lagtip, sizeof (lagtip) - 1, "Lag: %s%ld.%ld seconds",
				 serv->lag_sent ? "+" : "", lag / 1000, (lag/100) % 10);

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			g_free (sess->res->lag_tip);
			sess->res->lag_tip = g_strdup (lagtip);

			if (!sess->gui->is_tab || current_tab == sess)
			{
				if (sess->gui->lagometer)
				{
					gtk_progress_bar_set_fraction ((GtkProgressBar *) sess->gui->lagometer, per);
					gtk_widget_set_tooltip_text (gtk_widget_get_parent (sess->gui->lagometer), lagtip);
				}
				if (sess->gui->laginfo)
					gtk_label_set_text ((GtkLabel *) sess->gui->laginfo, lagtext);
			} else
			{
				sess->res->lag_value = per;
				g_free (sess->res->lag_text);
				sess->res->lag_text = g_strdup (lagtext);
			}
		}
		list = list->next;
	}
}

void
fe_set_throttle (server *serv)
{
	GSList *list = sess_list;
	struct session *sess;
	float per;
	char tbuf[96];
	char tip[160];

	per = (float) serv->sendq_len / 1024.0;
	if (per > 1.0)
		per = 1.0;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			g_snprintf (tbuf, sizeof (tbuf) - 1, _("%d bytes"), serv->sendq_len);
			g_snprintf (tip, sizeof (tip) - 1, _("Network send queue: %d bytes"), serv->sendq_len);

			g_free (sess->res->queue_tip);
			sess->res->queue_tip = g_strdup (tip);

			if (!sess->gui->is_tab || current_tab == sess)
			{
				if (sess->gui->throttlemeter)
				{
					gtk_progress_bar_set_fraction ((GtkProgressBar *) sess->gui->throttlemeter, per);
					gtk_widget_set_tooltip_text (gtk_widget_get_parent (sess->gui->throttlemeter), tip);
				}
				if (sess->gui->throttleinfo)
					gtk_label_set_text ((GtkLabel *) sess->gui->throttleinfo, tbuf);
			} else
			{
				sess->res->queue_value = per;
				g_free (sess->res->queue_text);
				sess->res->queue_text = g_strdup (tbuf);
			}
		}
		list = list->next;
	}
}

void
fe_ctrl_gui (session *sess, fe_gui_action action, int arg)
{
	switch (action)
	{
	case FE_GUI_HIDE:
		nx_widget_hide (sess->gui->window); break;
	case FE_GUI_SHOW:
		nx_widget_show (sess->gui->window);
		gtk_window_present (GTK_WINDOW (sess->gui->window));
		break;
	case FE_GUI_FOCUS:
		mg_bring_tofront_sess (sess); break;
	case FE_GUI_FLASH:
		fe_flash_window (sess); break;
	case FE_GUI_COLOR:
		fe_set_tab_color (sess, arg); break;
	case FE_GUI_ICONIFY:
		nx_window_iconify (sess->gui->window); break;
	case FE_GUI_MENU:
		menu_bar_toggle ();	/* toggle menubar on/off */
		break;
	case FE_GUI_ATTACH:
		mg_detach (sess, arg);	/* arg: 0=toggle 1=detach 2=attach */
		break;
	case FE_GUI_APPLY:
		setup_apply_real (TRUE, TRUE, TRUE, FALSE);
	}
}

static void
dcc_saveas_cb (struct DCC *dcc, char *file)
{
	if (is_dcc (dcc))
	{
		if (dcc->dccstat == STAT_QUEUED)
		{
			if (file)
				dcc_get_with_destfile (dcc, file);
			else if (dcc->resume_sent == 0)
				dcc_abort (dcc->serv->front_session, dcc);
		}
	}
}

void
fe_confirm (const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud)
{
	/* warning, assuming fe_confirm is used by DCC only! */
	struct DCC *dcc = ud;

	if (dcc->file)
	{
		char *filepath = g_build_filename (prefs.hex_dcc_dir, dcc->file, NULL);
		gtkutil_file_req (NULL, message, dcc_saveas_cb, ud, filepath, NULL,
								FRF_WRITE|FRF_NOASKOVERWRITE|FRF_FILTERISINITIAL);
		g_free (filepath);
	}
}

int
fe_gui_info (session *sess, int info_type)
{
	switch (info_type)
	{
	case 0:	/* window status */
		if (!gtk_widget_get_visible (GTK_WIDGET (sess->gui->window)))
		{
			return 2;	/* hidden (iconified or systray) */
		}

		if (gtk_window_is_active (GTK_WINDOW (sess->gui->window)))
		{
			return 1;	/* active/focused */
		}

		return 0;		/* normal (no keyboard focus or behind a window) */
	}

	return -1;
}

void *
fe_gui_info_ptr (session *sess, int info_type)
{
	switch (info_type)
	{
	case 0:	/* native window pointer (for plugins) */
#ifdef WIN32
		return gtkutil_window_hwnd (GTK_WINDOW (sess->gui->window));
#else
		return sess->gui->window;
#endif
		break;

	case 1:	/* GtkWindow * (for plugins) */
		return sess->gui->window;
	}
	return NULL;
}

char *
fe_get_inputbox_contents (session *sess)
{
	/* not the current tab */
	if (sess->res->input_text)
		return sess->res->input_text;

	/* current focused tab */
	return SPELL_ENTRY_GET_TEXT (sess->gui->input_box);
}

int
fe_get_inputbox_cursor (session *sess)
{
	/* not the current tab (we don't remember the cursor pos) */
	if (sess->res->input_text)
		return 0;

	/* current focused tab */
	return SPELL_ENTRY_GET_POS (sess->gui->input_box);
}

void
fe_set_inputbox_cursor (session *sess, int delta, int pos)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (delta)
			pos += SPELL_ENTRY_GET_POS (sess->gui->input_box);
		SPELL_ENTRY_SET_POS (sess->gui->input_box, pos);
	} else
	{
		/* we don't support changing non-front tabs yet */
	}
}

void
fe_set_inputbox_contents (session *sess, char *text)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		SPELL_ENTRY_SET_TEXT (sess->gui->input_box, text);
	} else
	{
		g_free (sess->res->input_text);
		sess->res->input_text = g_strdup (text);
	}
}

#ifdef __APPLE__
static char *
url_escape_hostname (const char *url)
{
    char *host_start, *host_end, *ret, *hostname;

    host_start = strstr (url, "://");
    if (host_start != NULL)
    {
        *host_start = '\0';
        host_start += 3;
        host_end = strchr (host_start, '/');

        if (host_end != NULL)
        {
            *host_end = '\0';
            host_end++;
        }

        hostname = g_hostname_to_ascii (host_start);
        if (host_end != NULL)
            ret = g_strdup_printf ("%s://%s/%s", url, hostname, host_end);
        else
            ret = g_strdup_printf ("%s://%s", url, hostname);

        g_free (hostname);
        return ret;
    }

    return g_strdup (url);
}

static void
osx_show_uri (const char *url)
{
    char *escaped_url, *encoded_url, *open, *cmd;

    escaped_url = url_escape_hostname (url);
    encoded_url = g_filename_from_utf8 (escaped_url, -1, NULL, NULL, NULL);
    if (encoded_url)
    {
        open = g_find_program_in_path ("open");
        cmd = g_strjoin (" ", open, encoded_url, NULL);

        hexchat_exec (cmd);

        g_free (encoded_url);
        g_free (cmd);
    }

    g_free (escaped_url);
}

#endif

#if !defined(WIN32) && !defined(__APPLE__)
static inline char *
escape_uri (const char *uri)
{
	return g_uri_escape_string(uri, G_URI_RESERVED_CHARS_GENERIC_DELIMITERS G_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS, FALSE);
}

static inline gboolean
uri_contains_forbidden_characters (const char *uri)
{
	while (*uri)
	{
		if (!g_ascii_isalnum (*uri) && !strchr ("-._~:/?#[]@!$&'()*+,;=", *uri))
			return TRUE;
		uri++;
	}

	return FALSE;
}

static char *
maybe_escape_uri (const char *uri)
{
	/* The only way to know if a string has already been escaped or not
	 * is by fulling parsing each segement but we can try some more simple heuristics. */

	/* If we find characters that should clearly be escaped. */
	if (uri_contains_forbidden_characters (uri))
		return escape_uri (uri);

	/* If it fails to be unescaped then it was not escaped. */
	char *unescaped = g_uri_unescape_string (uri, NULL);
	if (!unescaped)
		return escape_uri (uri);
	g_free (unescaped);

	/* At this point it is probably safe to pass through as-is. */
	return g_strdup (uri);
}
#endif

static void
fe_open_url_inner (const char *url)
{
#ifdef WIN32
	gunichar2 *url_utf16 = g_utf8_to_utf16 (url, -1, NULL, NULL, NULL);

	if (url_utf16 == NULL)
	{
		return;
	}

	ShellExecuteW (0, L"open", url_utf16, NULL, NULL, SW_SHOWNORMAL);

	g_free (url_utf16);
#elif defined(__APPLE__)
    osx_show_uri (url);
#else
	char *escaped_url = maybe_escape_uri (url);
	g_debug ("Opening URL \"%s\" (%s)", escaped_url, url);
	{
		/* gtk_show_uri's replacement; the portal on sandboxed systems */
		GtkUriLauncher *l = gtk_uri_launcher_new (escaped_url);
		gtk_uri_launcher_launch (l, NULL, NULL, NULL, NULL);
		g_object_unref (l);
	}
	g_free (escaped_url);
#endif
}

void
fe_open_url (const char *url)
{
	int url_type = url_check_word (url);
	char *uri;

	/* gvfs likes file:// */
	if (url_type == WORD_PATH)
	{
#ifndef WIN32
		uri = g_strconcat ("file://", url, NULL);
		fe_open_url_inner (uri);
		g_free (uri);
#else
		fe_open_url_inner (url);
#endif
	}
	/* IPv6 addr. Add http:// */
	else if (url_type == WORD_HOST6)
	{
		/* IPv6 addrs in urls should be enclosed in [ ] */
		if (*url != '[')
			uri = g_strdup_printf ("http://[%s]", url);
		else
			uri = g_strdup_printf ("http://%s", url);

		fe_open_url_inner (uri);
		g_free (uri);
	}
	/* the http:// part's missing, prepend it, otherwise it won't always work */
	else if (strchr (url, ':') == NULL)
	{
		uri = g_strdup_printf ("http://%s", url);
		fe_open_url_inner (uri);
		g_free (uri);
	}
	/* we have a sane URL, send it to the browser untouched */
	else
	{
		fe_open_url_inner (url);
	}
}

void
fe_server_event (server *serv, int type, int arg)
{
	GSList *list = sess_list;
	session *sess;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv && (current_tab == sess || !sess->gui->is_tab))
		{
			session_gui *gui = sess->gui;

			switch (type)
			{
			case FE_SE_CONNECTING:	/* connecting in progress */
			case FE_SE_RECONDELAY:	/* reconnect delay begun */
				/* enable Disconnect item */
				nx_item_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT], 1);
				break;

			case FE_SE_CONNECT:
				/* enable Disconnect and Away menu items */
				nx_item_set_sensitive (gui->menu_item[MENU_ID_AWAY], 1);
				nx_item_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT], 1);
				break;

			case FE_SE_LOGGEDIN:	/* end of MOTD */
				nx_item_set_sensitive (gui->menu_item[MENU_ID_JOIN], 1);
				/* if number of auto-join channels is zero, open joind */
				if (arg == 0)
					joind_open (serv);
				break;

			case FE_SE_DISCONNECT:
				/* disable Disconnect and Away menu items */
				nx_item_set_sensitive (gui->menu_item[MENU_ID_AWAY], 0);
				nx_item_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT], 0);
				nx_item_set_sensitive (gui->menu_item[MENU_ID_JOIN], 0);
				/* close the join-dialog, if one exists */
				joind_close (serv);
			}
			menu_debug_dump (gui->main_menu);
		}
		list = list->next;
	}
}

void
fe_get_file (const char *title, char *initial,
				 void (*callback) (void *userdata, char *file), void *userdata,
				 int flags)
				
{
	/* OK: Call callback once per file, then once more with file=NULL. */
	/* CANCEL: Call callback once with file=NULL. */
	gtkutil_file_req (NULL, title, callback, userdata, initial, NULL, flags | FRF_FILTERISINITIAL);
}

void
fe_open_chan_list (server *serv, char *filter, int do_refresh)
{
	chanlist_opengui (serv, do_refresh);
}

const char *
fe_get_default_font (void)
{
	/* Not looked up: listing every installed font (pango_font_map_list_families)
	   took ~47 ms of every start on macOS, before GTK was even up -- for a
	   font every system ships. Consolas has come with Windows since Vista,
	   Menlo with macOS since 10.6; if one were missing, Pango substitutes
	   another monospace font when the text box loads it. */
#ifdef WIN32
	return "Consolas 10";
#elif defined(__APPLE__)
	return "Menlo 13";
#else
	return NULL;
#endif
}
