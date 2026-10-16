/* X-Chat
 * Copyright (C) 2006-2007 Peter Zelezny.
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

#include <string.h>
#include "../common/hexchat-plugin.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/inbound.h"
#include "../common/server.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/outbound.h"
#include "fe-gtk.h"
#include "pixmaps.h"
#include "maingui.h"
#include "menu.h"
#include "gtkutil.h"
#include "tray-backend.h"

#ifndef WIN32
#include <unistd.h>
#endif

typedef enum	/* current icon status */
{
	TS_NONE,
	TS_MESSAGE,
	TS_HIGHLIGHT,
	TS_FILEOFFER,
	TS_CUSTOM /* plugin */
} TrayStatus;

typedef enum
{
	WS_FOCUSED,
	WS_NORMAL,
	WS_HIDDEN
} WinStatus;

typedef GdkPixbuf* TrayIcon;
#define tray_icon_from_file(f) gdk_pixbuf_new_from_file(f,NULL)
#define tray_icon_free(i) g_object_unref(i)

#define ICON_NORMAL pix_tray_normal
#define ICON_MSG pix_tray_message
#define ICON_HILIGHT pix_tray_highlight
#define ICON_FILE pix_tray_fileoffer
#define TIMEOUT 500

static tray_backend *sticon;
static gint flash_tag;
static TrayStatus tray_status;
#ifdef WIN32
#endif
static hexchat_plugin *ph;

static TrayIcon custom_icon1;
static TrayIcon custom_icon2;

static int tray_priv_count = 0;
static int tray_pub_count = 0;
static int tray_hilight_count = 0;
static int tray_file_count = 0;
static int tray_restore_timer = 0;


void tray_apply_setup (void);
static gboolean tray_menu_try_restore (void);
static void tray_cleanup (void);
static void tray_init (void);


static WinStatus
tray_get_window_status (void)
{
	const char *st;

	st = hexchat_get_info (ph, "win_status");

	if (!st)
		return WS_HIDDEN;

	if (!strcmp (st, "active"))
		return WS_FOCUSED;

	if (!strcmp (st, "hidden"))
		return WS_HIDDEN;

	return WS_NORMAL;
}

static int
tray_count_channels (void)
{
	int cons = 0;
	GSList *list;
	session *sess;

	for (list = sess_list; list; list = list->next)
	{
		sess = list->data;
		if (sess->server->connected && sess->channel[0] &&
			 sess->type == SESS_CHANNEL)
			cons++;
	}
	return cons;
}

static int
tray_count_networks (void)
{
	int cons = 0;
	GSList *list;

	for (list = serv_list; list; list = list->next)
	{
		if (((server *)list->data)->connected)
			cons++;
	}
	return cons;
}

void
fe_tray_set_tooltip (const char *text)
{
	if (sticon)
		tray_backend_set_tooltip (sticon, text);
}

static void
tray_set_tipf (const char *format, ...)
{
	va_list args;
	char *buf;

	va_start (args, format);
	buf = g_strdup_vprintf (format, args);
	va_end (args);

	fe_tray_set_tooltip (buf);
	g_free (buf);
}

static void
tray_stop_flash (void)
{
	int nets, chans;

	if (flash_tag)
	{
		g_source_remove (flash_tag);
		flash_tag = 0;
	}

	if (sticon)
	{
		tray_backend_set_icon (sticon, ICON_NORMAL);
		nets = tray_count_networks ();
		chans = tray_count_channels ();
		if (nets)
			tray_set_tipf (_("Connected to %u networks and %u channels - %s"),
								nets, chans, _(DISPLAY_NAME));
		else
			tray_set_tipf ("%s - %s", _("Not connected."), _(DISPLAY_NAME));
	}

	if (custom_icon1)
	{
		tray_icon_free (custom_icon1);
		custom_icon1 = NULL;
	}

	if (custom_icon2)
	{
		tray_icon_free (custom_icon2);
		custom_icon2 = NULL;
	}

	tray_status = TS_NONE;
}

static void
tray_reset_counts (void)
{
	tray_priv_count = 0;
	tray_pub_count = 0;
	tray_hilight_count = 0;
	tray_file_count = 0;
}

static int
tray_timeout_cb (TrayIcon icon)
{
	if (custom_icon1)
	{
		if (tray_backend_get_icon (sticon) == custom_icon1)
		{
			if (custom_icon2)
				tray_backend_set_icon (sticon, custom_icon2);
			else
				tray_backend_set_icon (sticon, ICON_NORMAL);
		}
		else
		{
			tray_backend_set_icon (sticon, custom_icon1);
		}
	}
	else
	{
		if (tray_backend_get_icon (sticon) == ICON_NORMAL)
			tray_backend_set_icon (sticon, icon);
		else
			tray_backend_set_icon (sticon, ICON_NORMAL);
	}
	return 1;
}

static void
tray_set_flash (TrayIcon icon)
{
	if (!sticon)
		return;

	/* already flashing the same icon */
	if (flash_tag && tray_backend_get_icon (sticon) == icon)
		return;

	/* no flashing if window is focused */
	if (tray_get_window_status () == WS_FOCUSED)
		return;

	tray_stop_flash ();

	tray_backend_set_icon (sticon, icon);
	if (prefs.hex_gui_tray_blink)
		flash_tag = g_timeout_add (TIMEOUT, (GSourceFunc) tray_timeout_cb, icon);
}

void
fe_tray_set_flash (const char *filename1, const char *filename2, int tout)
{
	tray_apply_setup ();
	if (!sticon)
		return;

	tray_stop_flash ();

	if (tout == -1)
		tout = TIMEOUT;

	custom_icon1 = tray_icon_from_file (filename1);
	if (filename2)
		custom_icon2 = tray_icon_from_file (filename2);

	tray_backend_set_icon (sticon, custom_icon1);
	flash_tag = g_timeout_add (tout, (GSourceFunc) tray_timeout_cb, NULL);
	tray_status = TS_CUSTOM;
}

void
fe_tray_set_icon (feicon icon)
{
	tray_apply_setup ();
	if (!sticon)
		return;

	tray_stop_flash ();

	switch (icon)
	{
	case FE_ICON_NORMAL:
		break;
	case FE_ICON_MESSAGE:
	case FE_ICON_PRIVMSG:
		tray_set_flash (ICON_MSG);
		break;
	case FE_ICON_HIGHLIGHT:
		tray_set_flash (ICON_HILIGHT);
		break;
	case FE_ICON_FILEOFFER:
		tray_set_flash (ICON_FILE);
	}
}

void
fe_tray_set_file (const char *filename)
{
	tray_apply_setup ();
	if (!sticon)
		return;

	tray_stop_flash ();

	if (filename)
	{
		custom_icon1 = tray_icon_from_file (filename);
		tray_backend_set_icon (sticon, custom_icon1);
		tray_status = TS_CUSTOM;
	}
}

gboolean
tray_toggle_visibility (gboolean force_hide)
{
	static int x, y;
#if !GTK_CHECK_VERSION(4, 0, 0)
	static GdkScreen *screen;
#endif
	static int maximized;
	static int fullscreen;
	GtkWindow *win;

	if (!sticon)
		return FALSE;

	/* ph may have an invalid context now */
	hexchat_set_context (ph, hexchat_find_context (ph, NULL, NULL));

	win = GTK_WINDOW (hexchat_get_info (ph, "gtkwin_ptr"));

	tray_stop_flash ();
	tray_reset_counts ();

	if (!win)
		return FALSE;

	if (force_hide || gtk_widget_get_visible (GTK_WIDGET (win)))
	{
		if (prefs.hex_gui_tray_away)
			hexchat_command (ph, "ALLSERV AWAY");
		gtkutil_window_get_position (win, &x, &y);
#if !GTK_CHECK_VERSION(4, 0, 0)
		screen = gtk_window_get_screen (win);
#endif
		maximized = prefs.hex_gui_win_state;
		fullscreen = prefs.hex_gui_win_fullscreen;
		nx_widget_hide (GTK_WIDGET (win));
	}
	else
	{
		if (prefs.hex_gui_tray_away)
			hexchat_command (ph, "ALLSERV BACK");
#if !GTK_CHECK_VERSION(4, 0, 0)
		/* (GTK4: one display per window, nothing to restore) */
		gtk_window_set_screen (win, screen);
#endif
		gtkutil_window_move (win, x, y);
		if (maximized)
			gtk_window_maximize (win);
		if (fullscreen)
			gtk_window_fullscreen (win);
		nx_widget_show (GTK_WIDGET (win));
		nx_window_deiconify (win);
		gtk_window_present (win);
	}

	return TRUE;
}

static void
tray_menu_restore_cb (GtkWidget *item, gpointer userdata)
{
	tray_toggle_visibility (FALSE);
}

static void
tray_menu_restore_item (nx_item *item, gpointer userdata)
{
	tray_toggle_visibility (FALSE);
}

static void
tray_menu_notify_cb (tray_backend *tb, gboolean embedded, gpointer user_data)
{
	if (sticon)
	{
		if (!embedded)
		{
			tray_restore_timer = g_timeout_add(500, (GSourceFunc)tray_menu_try_restore, NULL);
		}
		else
		{
			if (tray_restore_timer)
			{
				g_source_remove (tray_restore_timer);
				tray_restore_timer = 0;
			}
		}
	}
}

static gboolean
tray_menu_try_restore (void)
{
	tray_cleanup();
	tray_init();
	return TRUE;
}

static void
tray_menu_quit_cb (GtkWidget *item, gpointer userdata)
{
	mg_open_quit_dialog (FALSE);
}

/* returns 0-mixed 1-away 2-back */

static int
tray_find_away_status (void)
{
	GSList *list;
	server *serv;
	int away = 0;
	int back = 0;

	for (list = serv_list; list; list = list->next)
	{
		serv = list->data;

		if (serv->is_away || serv->reconnect_away)
			away++;
		else
			back++;
	}

	if (away && back)
		return 0;

	if (away)
		return 1;

	return 2;
}

static void
tray_foreach_server (nx_item *item, gpointer cmd)
{
	GSList *list;
	server *serv;

	for (list = serv_list; list; list = list->next)
	{
		serv = list->data;
		if (serv->connected)
			handle_command (serv->server_session, cmd, FALSE);
	}
}

/* label NULL: a separator */
static nx_item *
tray_make_item (nx_menu *menu, char *label, nx_item_cb callback, void *userdata)
{
	if (!label)
		return nx_menu_add_separator (menu, NX_MENU_APPEND);
	return nx_menu_add_item (menu, NX_MENU_APPEND, label, NX_ITEM_MNEMONIC, NULL, callback, userdata);
}

#ifndef WIN32
static void
tray_toggle_cb (nx_item *item, gpointer setting)
{
	*(unsigned int *) setting = nx_item_get_active (item);
}

static void
blink_item (unsigned int *setting, nx_menu *menu, char *label)
{
	menu_toggle_item (label, menu, tray_toggle_cb, setting, *setting);
}
#endif


static void
tray_menu_settings (nx_item *item, gpointer none)
{
	extern void setup_open (void);
	setup_open ();
}

static void
tray_menu_quit_item (nx_item *item, gpointer none)
{
	tray_menu_quit_cb (NULL, NULL);
}

static void
tray_menu_cb (tray_backend *tb, guint button, guint32 time, gpointer userdata)
{
	nx_menu *menu;
	nx_menu *submenu;
	nx_item *item;
	int away_status;

	/* ph may have an invalid context now */
	hexchat_set_context (ph, hexchat_find_context (ph, NULL, NULL));

	menu = nx_menu_new ();

	if (tray_get_window_status () == WS_HIDDEN)
		tray_make_item (menu, _("_Restore Window"), tray_menu_restore_item, NULL);
	else
		tray_make_item (menu, _("_Hide Window"), tray_menu_restore_item, NULL);
	tray_make_item (menu, NULL, NULL, NULL);

#ifndef WIN32 /* submenus are buggy on win32 */
	submenu = mg_submenu (menu, _("_Blink on"));
	blink_item (&prefs.hex_input_tray_chans, submenu, _("Channel Message"));
	blink_item (&prefs.hex_input_tray_priv, submenu, _("Private Message"));
	blink_item (&prefs.hex_input_tray_hilight, submenu, _("Highlighted Message"));
	/*blink_item (BIT_FILEOFFER, submenu, _("File Offer"));*/

	submenu = mg_submenu (menu, _("_Change status"));
#else /* so show away/back in main tray menu */
	submenu = menu;
#endif

	away_status = tray_find_away_status ();
	item = tray_make_item (submenu, _("_Away"), tray_foreach_server, "away");
	if (away_status == 1)
		nx_item_set_sensitive (item, FALSE);
	item = tray_make_item (submenu, _("_Back"), tray_foreach_server, "back");
	if (away_status == 2)
		nx_item_set_sensitive (item, FALSE);

	menu_add_plugin_items (menu, "\x5$TRAY", NULL);

	tray_make_item (menu, NULL, NULL, NULL);
	mg_create_icon_item (_("_Preferences"), NX_STOCK_PREFERENCES, menu, tray_menu_settings, NULL);
	tray_make_item (menu, NULL, NULL, NULL);
	mg_create_icon_item (_("_Quit"), NX_STOCK_QUIT, menu, tray_menu_quit_item, NULL);

	/* NEXCHAT_TRAY_MENU_DUMP=path: for tests/ux/tray.sh (headless servers
	   have no tray to click): write the menu, don't show it */
	if (g_getenv ("NEXCHAT_TRAY_MENU_DUMP"))
	{
		menu_debug_dump_to (menu, g_getenv ("NEXCHAT_TRAY_MENU_DUMP"));
#if GTK_CHECK_VERSION(4, 0, 0)
		nx_menu_free (menu);
#else
		g_object_ref_sink (menu);
		gtk_widget_destroy (NX_MENU_WIDGET (menu));
		g_object_unref (menu);
#endif
		return;
	}

	/* the backend shows it and frees it (closing any menu still open) */
	tray_backend_show_menu (tb, menu, button, time);
}

/* /TRAYMENU-DUMP: build the tray menu as a right-click would (tests) */
static int
tray_menu_dump_cmd (char *word[], char *word_eol[], void *userdata)
{
	tray_menu_cb (sticon, 3, 0, NULL);
	return HEXCHAT_EAT_ALL;
}

static void
tray_activate_cb (tray_backend *tb, gpointer userdata)
{
	tray_menu_restore_cb (NULL, NULL);
}

static void
tray_init (void)
{
	static const tray_backend_callbacks cb = {
		tray_activate_cb, tray_menu_cb, tray_menu_notify_cb
	};

	flash_tag = 0;
	tray_status = TS_NONE;
	custom_icon1 = NULL;
	custom_icon2 = NULL;

	sticon = tray_backend_new (ICON_NORMAL, &cb, NULL);
}

static int
tray_hilight_cb (char *word[], void *userdata)
{
	/*if (tray_status == TS_HIGHLIGHT)
		return HEXCHAT_EAT_NONE;*/

	if (prefs.hex_input_tray_hilight)
	{
		tray_set_flash (ICON_HILIGHT);

		/* FIXME: hides any previous private messages */
		tray_hilight_count++;
		if (tray_hilight_count == 1)
			tray_set_tipf (_("Highlighted message from: %s (%s) - %s"),
								word[1], hexchat_get_info (ph, "channel"), _(DISPLAY_NAME));
		else
			tray_set_tipf (_("%u highlighted messages, latest from: %s (%s) - %s"),
								tray_hilight_count, word[1], hexchat_get_info (ph, "channel"),
								_(DISPLAY_NAME));
	}

	return HEXCHAT_EAT_NONE;
}

static int
tray_message_cb (char *word[], void *userdata)
{
	if (/*tray_status == TS_MESSAGE ||*/ tray_status == TS_HIGHLIGHT)
		return HEXCHAT_EAT_NONE;
		
	if (prefs.hex_input_tray_chans)
	{
		tray_set_flash (ICON_MSG);

		tray_pub_count++;
		if (tray_pub_count == 1)
			tray_set_tipf (_("Channel message from: %s (%s) - %s"),
								word[1], hexchat_get_info (ph, "channel"), _(DISPLAY_NAME));
		else
			tray_set_tipf (_("%u channel messages. - %s"), tray_pub_count, _(DISPLAY_NAME));
	}

	return HEXCHAT_EAT_NONE;
}

static void
tray_priv (char *from, char *text)
{
	const char *network;

	if (alert_match_word (from, prefs.hex_irc_no_hilight))
		return;

	network = hexchat_get_info (ph, "network");
	if (!network)
		network = hexchat_get_info (ph, "server");

	if (prefs.hex_input_tray_priv)
	{
		tray_set_flash (ICON_MSG);

		tray_priv_count++;
		if (tray_priv_count == 1)
			tray_set_tipf (_("Private message from: %s (%s) - %s"), from,
								network, _(DISPLAY_NAME));
		else
			tray_set_tipf (_("%u private messages, latest from: %s (%s) - %s"),
								tray_priv_count, from, network, _(DISPLAY_NAME));
	}
}

static int
tray_priv_cb (char *word[], void *userdata)
{
	tray_priv (word[1], word[2]);

	return HEXCHAT_EAT_NONE;
}

static int
tray_invited_cb (char *word[], void *userdata)
{
	if (!prefs.hex_away_omit_alerts || tray_find_away_status () != 1)
		tray_priv (word[2], "Invited");

	return HEXCHAT_EAT_NONE;
}

static int
tray_dcc_cb (char *word[], void *userdata)
{
	const char *network;

/*	if (tray_status == TS_FILEOFFER)
		return HEXCHAT_EAT_NONE;*/

	network = hexchat_get_info (ph, "network");
	if (!network)
		network = hexchat_get_info (ph, "server");

	if (prefs.hex_input_tray_priv && (!prefs.hex_away_omit_alerts || tray_find_away_status () != 1))
	{
		tray_set_flash (ICON_FILE);

		tray_file_count++;
		if (tray_file_count == 1)
			tray_set_tipf (_("File offer from: %s (%s) - %s"), word[1], network,
								_(DISPLAY_NAME));
		else
			tray_set_tipf (_("%u file offers, latest from: %s (%s) - %s"),
								tray_file_count, word[1], network, _(DISPLAY_NAME));
	}

	return HEXCHAT_EAT_NONE;
}

static int
tray_focus_cb (char *word[], void *userdata)
{
	tray_stop_flash ();
	tray_reset_counts ();
	return HEXCHAT_EAT_NONE;
}

static void
tray_cleanup (void)
{
	tray_stop_flash ();

	if (sticon)
	{
		tray_backend_free (sticon);
		sticon = NULL;
	}
}

void
tray_apply_setup (void)
{
	if (sticon)
	{
		if (!prefs.hex_gui_tray)
			tray_cleanup ();
	}
	else
	{
		GtkWindow *window = GTK_WINDOW(hexchat_get_info (ph, "gtkwin_ptr"));
		if (prefs.hex_gui_tray && gtkutil_tray_icon_supported (window))
			tray_init ();
	}
}

int
tray_plugin_init (hexchat_plugin *plugin_handle, char **plugin_name,
				char **plugin_desc, char **plugin_version, char *arg)
{
	/* we need to save this for use with any hexchat_* functions */
	ph = plugin_handle;

	*plugin_name = "";
	*plugin_desc = "";
	*plugin_version = "";

	hexchat_hook_print (ph, "Channel Msg Hilight", -1, tray_hilight_cb, NULL);
	hexchat_hook_print (ph, "Channel Action Hilight", -1, tray_hilight_cb, NULL);

	hexchat_hook_print (ph, "Channel Message", -1, tray_message_cb, NULL);
	hexchat_hook_print (ph, "Channel Action", -1, tray_message_cb, NULL);
	hexchat_hook_print (ph, "Channel Notice", -1, tray_message_cb, NULL);

	hexchat_hook_print (ph, "Private Message", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Message to Dialog", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Action", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Private Action to Dialog", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Notice", -1, tray_priv_cb, NULL);
	hexchat_hook_print (ph, "Invited", -1, tray_invited_cb, NULL);

	hexchat_hook_print (ph, "DCC Offer", -1, tray_dcc_cb, NULL);

	hexchat_hook_print (ph, "Focus Window", -1, tray_focus_cb, NULL);

	/* tests only: never there for users */
	if (g_getenv ("NEXCHAT_TRAY_MENU_DUMP"))
		hexchat_hook_command (ph, "TRAYMENU-DUMP", HEXCHAT_PRI_NORM, tray_menu_dump_cmd, NULL, NULL);

	GtkWindow *window = GTK_WINDOW(hexchat_get_info (ph, "gtkwin_ptr"));
	if (prefs.hex_gui_tray && gtkutil_tray_icon_supported (window))
		tray_init ();

	return 1;       /* return 1 for success */
}

int
tray_plugin_deinit (hexchat_plugin *plugin_handle)
{
#ifdef WIN32
	tray_cleanup ();
#endif
	return 1;
}
