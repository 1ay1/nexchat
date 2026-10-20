/* X-Chat
 * Copyright (C) 1998-2007 Peter Zelezny.
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
#include <fcntl.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.h"


#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/ignore.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "../common/servlist.h"
#include "../common/notify.h"
#include "../common/util.h"
#include "../common/text.h"
#include "xtext.h"
#include "ascii.h"
#include "jumpto.h"
#include "banlist.h"
#include "chanlist.h"
#include "editlist.h"
#include "fkeys.h"
#include "gtkutil.h"
#include "maingui.h"
#include "notifygui.h"
#include "pixmaps.h"
#include "rawlog.h"
#include "palette.h"
#include "plugingui.h"
#include "search.h"
#include "textgui.h"
#include "urlgrab.h"
#include "mentions.h"
#include "replybar.h"
#include "../common/proto-irc.h"
#include "../common/batch.h"
#include "userlistgui.h"
#include "menu.h"
#include "bncsetup.h"
#include "bncwin.h"
#include "servlistgui.h"

static GSList *submenu_list;

enum
{
	M_MENUITEM,
	M_NEWMENU,
	M_END,
	M_SEP,
	M_MENUTOG,
	M_MENURADIO,
	M_MENUSTOCK,
	M_MENUPIX,
	M_MENUSUB
};

struct mymenu
{
	char *text;
	void *callback;
	char *image;
	unsigned char type;	/* M_XXX */
	unsigned char id;		/* MENU_ID_XXX (menu.h) */
	unsigned char state;	/* ticked or not? */
	unsigned char sensitive;	/* shaded out? */
	guint key;				/* GDK_KEY_x */
};

#define XCMENU_DOLIST 1
#define XCMENU_SHADED 1
#define XCMENU_MARKUP 2
#define XCMENU_MNEMONIC 4

/* execute a userlistbutton/popupmenu command */

static void
nick_command (session * sess, char *cmd)
{
	if (*cmd == '!')
		hexchat_exec (cmd + 1);
	else
		handle_command (sess, cmd, TRUE);
}

/* fill in the %a %s %n etc and execute the command */

void
nick_command_parse (session *sess, char *cmd, char *nick, char *allnick)
{
	char *buf;
	char *host = _("Host unknown");
	char *account = _("Account unknown");
	struct User *user;
	int len;

/*	if (sess->type == SESS_DIALOG)
	{
		buf = (char *)(GTK_ENTRY (sess->gui->topic_entry)->text);
		buf = strrchr (buf, '@');
		if (buf)
			host = buf + 1;
	} else*/
	{
		user = userlist_find (sess, nick);
		if (user)
		{
			if (user->hostname)
				host = strchr (user->hostname, '@') + 1;
			if (user->account)
				account = user->account;
		}
	}

	/* this can't overflow, since popup->cmd is only 256 */
	len = strlen (cmd) + strlen (nick) + strlen (allnick) + 512;
	buf = g_malloc (len);

	auto_insert (buf, len, cmd, 0, 0, allnick, sess->channel, "",
					 server_get_network (sess->server, TRUE), host,
					 sess->server->nick, nick, account);

	nick_command (sess, buf);

	g_free (buf);
}

/* userlist button has been clicked */

void
userlist_button_cb (GtkWidget * button, char *cmd)
{
	int i, num_sel, using_allnicks = FALSE;
	char **nicks, *allnicks;
	char *nick = NULL;
	session *sess;

	sess = current_sess;

	if (strstr (cmd, "%a"))
		using_allnicks = TRUE;

	if (sess->type == SESS_DIALOG)
	{
		/* fake a selection */
		nicks = g_new (char *, 2);
		nicks[0] = g_strdup (sess->channel);
		nicks[1] = NULL;
		num_sel = 1;
	}
	else
	{
		/* find number of selected rows */
		nicks = userlist_selection_list (sess->gui->user_tree, &num_sel);
		if (num_sel < 1)
		{
			nick_command_parse (sess, cmd, "", "");

			g_free (nicks);
			return;
		}
	}

	/* create "allnicks" string */
	allnicks = g_malloc (((NICKLEN + 1) * num_sel) + 1);
	*allnicks = 0;

	i = 0;
	while (nicks[i])
	{
		if (i > 0)
			strcat (allnicks, " ");
		strcat (allnicks, nicks[i]);

		if (!nick)
			nick = nicks[0];

		/* if not using "%a", execute the command once for each nickname */
		if (!using_allnicks)
			nick_command_parse (sess, cmd, nicks[i], "");

		i++;
	}

	if (using_allnicks)
	{
		if (!nick)
			nick = "";
		nick_command_parse (sess, cmd, nick, allnicks);
	}

	while (num_sel)
	{
		num_sel--;
		g_free (nicks[num_sel]);
	}

	g_free (nicks);
	g_free (allnicks);
}

/* a popup-menu-item has been selected */

static void
popup_menu_cb (nx_item *item, gpointer cmd)
{
	char *nick;

	/* the userdata is set in menu_quick_item() */
	nick = nx_item_get_data (item, "u");

	if (!nick)	/* userlist popup menu */
	{
		/* treat it just like a userlist button */
		userlist_button_cb (NULL, cmd);
		return;
	}

	if (!current_sess)	/* for url grabber window */
		nick_command_parse (sess_list->data, cmd, nick, nick);
	else
		nick_command_parse (current_sess, cmd, nick, nick);
}

nx_item *
menu_toggle_item (char *label, nx_menu *menu, nx_item_cb callback, void *userdata,
						int state)
{
	return nx_menu_add_check (menu, NX_MENU_APPEND, label, NX_ITEM_MNEMONIC, state,
									  callback, userdata);
}

/* label NULL: a separator. cmd: a user command run with userdata as the
   target nick (popup_menu_cb). icon: a file or stock name. */
static nx_item *
menu_quick_item_at (char *cmd, char *label, nx_menu *menu, int flags,
						 gpointer userdata, char *icon, int pos)
{
	nx_item *item;
	char *path = NULL;
	int nflags = 0;

	if (!label)
	{
		/* GTK2 drew an empty GtkMenuItem as a separator line; GTK3 draws an
		   empty gap. Use the real thing. */
		item = nx_menu_add_separator (menu, pos);
		nx_item_set_data (item, "u", userdata);
		/* (XCMENU_SHADED on a separator was a no-op too) */
		return item;
	}

	if (icon && access (icon, R_OK) != 0)
	{
		/* not a full path: try relative to <xdir>, else it's a stock name */
		path = g_build_filename (get_xdir (), icon, NULL);
		if (access (path, R_OK) == 0)
			icon = path;
	}
	/* an icon item's label is a mnemonic and never markup
	   (gtk_image_menu_item_new_with_mnemonic) */
	if (icon)
		nflags = NX_ITEM_MNEMONIC;
	else
	{
		if (flags & XCMENU_MARKUP)
			nflags |= NX_ITEM_MARKUP;
		if (flags & XCMENU_MNEMONIC)
			nflags |= NX_ITEM_MNEMONIC;
	}
	if (flags & XCMENU_SHADED)
		nflags |= NX_ITEM_INSENSITIVE;
	item = nx_menu_add_item (menu, pos, label, nflags, icon,
									 cmd ? popup_menu_cb : NULL, cmd);
	nx_item_set_data (item, "u", userdata);
	g_free (path);
	return item;
}

nx_item *
menu_quick_item (char *cmd, char *label, nx_menu *menu, int flags,
					  gpointer userdata, char *icon)
{
	return menu_quick_item_at (cmd, label, menu, flags, userdata, icon, NX_MENU_APPEND);
}

static void
menu_quick_item_with_callback (nx_item_cb callback, char *label, nx_menu *menu,
										 void *arg)
{
	nx_menu_add_item (menu, NX_MENU_APPEND, label, 0, NULL, callback, arg);
}

nx_menu *
menu_quick_sub (char *name, nx_menu *menu, nx_item **sub_item_ret, int flags, int pos)
{
	nx_menu *sub_menu;
	int nflags = 0;

	if (!name)
		return menu;

	/* markup submenu labels never took a mnemonic */
	if (flags & XCMENU_MARKUP)
		nflags = NX_ITEM_MARKUP;
	else if (flags & XCMENU_MNEMONIC)
		nflags = NX_ITEM_MNEMONIC;
	sub_menu = nx_menu_add_submenu (menu, pos, name, nflags, sub_item_ret);

	if (flags & XCMENU_DOLIST)
		/* We create a new element in the list */
		submenu_list = g_slist_prepend (submenu_list, sub_menu);
	return sub_menu;
}

static nx_menu *
menu_quick_endsub (void)
{
	/* Just delete the first element in the linked list pointed to by first */
	if (submenu_list)
		submenu_list = g_slist_remove (submenu_list, submenu_list->data);

	if (submenu_list)
		return (submenu_list->data);
	else
		return NULL;
}

static void
toggle_cb (nx_item *item, gpointer pref_name)
{
	char buf[256];

	if (nx_item_get_active (item))
		g_snprintf (buf, sizeof (buf), "set %s 1", (char *) pref_name);
	else
		g_snprintf (buf, sizeof (buf), "set %s 0", (char *) pref_name);

	handle_command (current_sess, buf, FALSE);
}

static int
is_in_path (char *cmd)
{
	char *orig = g_strdup (cmd + 1);	/* 1st char is "!" */
	char *prog = orig;
	char **argv;
	int argc;

	/* special-case these default entries. */
	/*                  123456789012345678 */
	if (strncmp (prog, "gnome-terminal -x ", 18) == 0)
	/* don't check for gnome-terminal, but the thing it's executing! */
		prog += 18;

	if (g_shell_parse_argv (prog, &argc, &argv, NULL))
	{
		char *path = g_find_program_in_path (argv[0]);
		g_strfreev (argv);
		if (path)
		{
			g_free (path);
			g_free (orig);
			return 1;
		}
	}

	g_free (orig);
	return 0;
}

/* syntax: "LABEL~ICON~STUFF~ADDED~LATER~" */

static void
menu_extract_icon (char *name, char **label, char **icon)
{
	char *p = name;
	char *start = NULL;
	char *end = NULL;

	while (*p)
	{
		if (*p == '~')
		{
			/* escape \~ */
			if (p == name || p[-1] != '\\')
			{
				if (!start)
					start = p + 1;
				else if (!end)
					end = p + 1;
			}
		}
		p++;
	}

	if (!end)
		end = p;

	if (start && start != end)
	{
		*label = g_strndup (name, (start - name) - 1);
		*icon = g_strndup (start, (end - start) - 1);
	}
	else
	{
		*label = g_strdup (name);
		*icon = NULL;
	}
}

/* append items to "menu" using the (struct popup*) list provided */

void
menu_create (nx_menu *menu, GSList *list, char *target, int check_path)
{
	struct popup *pop;
	nx_menu *tempmenu = menu;
	nx_item *subitem = NULL;
	int childcount = 0;

	submenu_list = g_slist_prepend (0, menu);
	while (list)
	{
		pop = (struct popup *) list->data;

		if (!g_ascii_strncasecmp (pop->name, "SUB", 3))
		{
			childcount = 0;
			tempmenu = menu_quick_sub (pop->cmd, tempmenu, &subitem, XCMENU_DOLIST|XCMENU_MNEMONIC, -1);

		} else if (!g_ascii_strncasecmp (pop->name, "TOGGLE", 6))
		{
			childcount++;
			menu_toggle_item (pop->name + 7, tempmenu, toggle_cb, pop->cmd,
									cfg_get_bool (pop->cmd));

		} else if (!g_ascii_strncasecmp (pop->name, "ENDSUB", 6))
		{
			/* empty sub menu due to no programs in PATH? */
			if (check_path && childcount < 1 && subitem)
				nx_item_remove (subitem);
			subitem = NULL;

			if (tempmenu != menu)
				tempmenu = menu_quick_endsub ();
			/* If we get here and tempmenu equals menu that means we havent got any submenus to exit from */

		} else if (!g_ascii_strncasecmp (pop->name, "SEP", 3))
		{
			menu_quick_item (0, 0, tempmenu, XCMENU_SHADED, 0, 0);

		} else
		{
			char *icon, *label;

			/* default command in hexchat.c */
			if (pop->cmd[0] == 'n' && !strcmp (pop->cmd, "notify -n ASK %s"))
			{
				/* don't create this item if already in notify list */
				if (!target || notify_is_in_list (current_sess->server, target))
				{
					list = list->next;
					continue;
				}
			}

			menu_extract_icon (pop->name, &label, &icon);

			if (!check_path || pop->cmd[0] != '!')
			{
				menu_quick_item (pop->cmd, label, tempmenu, 0, target, icon);
			/* check if the program is in path, if not, leave it out! */
			} else if (is_in_path (pop->cmd))
			{
				childcount++;
				menu_quick_item (pop->cmd, label, tempmenu, 0, target, icon);
			}

			g_free (label);
			g_free (icon);
		}

		list = list->next;
	}

	/* Let's clean up the linked list from mem */
	while (submenu_list)
		submenu_list = g_slist_remove (submenu_list, submenu_list->data);
}

static char *str_copy = NULL;		/* for all pop-up menus */
static nx_menu *nick_submenu = NULL;	/* user info submenu */

/* a popup closed (and was freed) */
static void
menu_destroy (gpointer objtounref)
{
	if (objtounref)
		g_object_unref (G_OBJECT (objtounref));
	nick_submenu = NULL;
}

/* pop up at the pointer of the event being handled, over the widget it
   came to */
static void
menu_popup (nx_menu *menu, GdkEvent *event, gpointer objtounref)
{
	GtkWidget *w = nx_current_event_widget ();

	nx_menu_popup (menu, w ? w : parent_window, event, menu_destroy, objtounref);
}

/* the nick info submenu opens: /WHOIS (output hidden) to fill in what's
   missing; fe_userlist_update then rebuilds it */
static void
menu_nickinfo_cb (nx_menu *menu, gpointer sess)
{
	char buf[512];

	if (!is_session (sess))
		return;

	/* issue a /WHOIS */
	g_snprintf (buf, sizeof (buf), "WHOIS %s %s", str_copy, str_copy);
	handle_command (sess, buf, FALSE);
	/* and hide the output */
	((session *) sess)->server->skip_next_whois = 1;
}

static void
copy_to_clipboard_cb (nx_item *item, gpointer url)
{
	GtkWidget *w = nx_item_get_widget (item);

	gtkutil_copy_to_clipboard (w ? w : parent_window, url);
}

/* an info line, "Label: value" as plain text (the macOS menu bar and
   NSMenu show labels as-is, so markup came out as <tt><b> tags; and a
   value with < or & in it broke markup elsewhere). Choosing it copies
   `copy` (NULL: it does nothing). */
static void
menu_info_item (nx_menu *submenu, const char *label, const char *value, char *copy)
{
	char *text = g_strdup_printf ("%s %s", label, value ? value : _("unknown"));

	nx_menu_add_item (submenu, NX_MENU_APPEND, text, 0, NULL,
							copy ? copy_to_clipboard_cb : NULL, copy);
	g_free (text);
}

/* "just now", "4 minutes ago", "2 hours ago", "3 days ago" */
static char *
menu_ago (time_t when)
{
	long s = (long) (time (NULL) - when);

	if (s < 60)
		return g_strdup (_("just now"));
	if (s < 3600)
		return g_strdup_printf (g_dngettext (NULL, "%ld minute ago", "%ld minutes ago", s / 60), s / 60);
	if (s < 86400)
		return g_strdup_printf (g_dngettext (NULL, "%ld hour ago", "%ld hours ago", s / 3600), s / 3600);
	return g_strdup_printf (g_dngettext (NULL, "%ld day ago", "%ld days ago", s / 86400), s / 86400);
}

/* returns boolean: Some data is missing */

static gboolean
menu_create_nickinfo_menu (struct User *user, nx_menu *submenu)
{
	char *real, *users_country, *ago;
	struct away_msg *away;
	gboolean missing = FALSE;

	real = user->realname ? strip_color (user->realname, -1, STRIP_ALL) : NULL;
	menu_info_item (submenu, _("Real name:"), real, user->realname);
	g_free (real);

	menu_info_item (submenu, _("User:"), user->hostname, user->hostname);
	menu_info_item (submenu, _("Account:"), user->account, user->account);

	users_country = country (user->hostname);
	if (users_country)
		menu_info_item (submenu, _("Country:"), users_country, users_country);

	menu_info_item (submenu, _("Server:"), user->servername, user->servername);

	ago = user->lasttalk ? menu_ago (user->lasttalk) : NULL;
	menu_info_item (submenu, _("Last message:"), ago, NULL);
	g_free (ago);

	if (user->away)
	{
		away = server_away_find_message (current_sess->server, user->nick);
		if (away)
		{
			char *msg = away->message ? strip_color (away->message, -1, STRIP_ALL) : NULL;
			menu_info_item (submenu, _("Away:"), msg, away->message);
			g_free (msg);
		}
		else
			missing = TRUE;
	}

	return missing;
}

void
fe_userlist_update (session *sess, struct User *user)
{
	if (!nick_submenu || !str_copy)
		return;

	/* not the same nick as the menu? */
	if (sess->server->p_cmp (user->nick, str_copy))
		return;

	/* get rid of the "show" signal */
	nx_menu_on_show_disconnect (nick_submenu);

	/* destroy all the old items and re-create them with new info */
	nx_menu_clear (nick_submenu);
	menu_create_nickinfo_menu (user, nick_submenu);
}

static void
menu_reply_cb (nx_item *item, gpointer nick)
{
	const char *msgid;

	if (!current_sess || !nick)
		return;
	msgid = reply_last_from (current_sess, nick);
	if (msgid)
		replybar_set (current_sess, msgid);
}

void
menu_nickmenu (session *sess, GdkEvent *event, char *nick, int num_sel)
{
	char buf[512];
	struct User *user;
	nx_menu *submenu, *menu = nx_menu_new ();

	g_free (str_copy);
	str_copy = g_strdup (nick);

	submenu_list = 0;	/* first time through, might not be 0 */

	/* more than 1 nick selected? */
	if (num_sel > 1)
	{
		g_snprintf (buf, sizeof (buf), _("%d nicks selected."), num_sel);
		menu_quick_item (0, buf, menu, 0, 0, 0);
		menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);
	} else
	{
		user = userlist_find (sess, nick);	/* lasttalk is channel specific */
		if (!user)
			user = userlist_find_global (current_sess->server, nick);
		if (user)
		{
			nick_submenu = submenu = menu_quick_sub (nick, menu, NULL, XCMENU_DOLIST, -1);

			if (menu_create_nickinfo_menu (user, submenu) ||
				 !user->hostname || !user->realname || !user->servername)
			{
				nx_menu_on_show (submenu, menu_nickinfo_cb, sess);
			}

			menu_quick_endsub ();
			/* answer what they said last here (shown over the input box) */
			if ((sess->type == SESS_CHANNEL || sess->type == SESS_DIALOG) &&
				 reply_last_from (sess, nick))
				menu_quick_item_with_callback (menu_reply_cb, _("Reply"), menu, str_copy);
			menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);
		}
	}

	if (num_sel > 1)
		menu_create (menu, popup_list, NULL, FALSE);
	else
		menu_create (menu, popup_list, str_copy, FALSE);

	if (num_sel == 0)	/* xtext click */
		menu_add_plugin_items (menu, "\x5$NICK", str_copy);
	else	/* userlist treeview click */
		menu_add_plugin_items (menu, "\x5$NICK", NULL);

	menu_popup (menu, event, NULL);
}

/* stuff for the View menu */

static void
menu_showhide_cb (session *sess)
{
	if (prefs.hex_gui_hide_menu)
		nx_widget_hide (sess->gui->menu);
	else
		nx_widget_show (sess->gui->menu);
}

static void
menu_topic_showhide_cb (session *sess)
{
	if (prefs.hex_gui_topicbar)
		nx_widget_show (sess->gui->topic_bar);
	else
		nx_widget_hide (sess->gui->topic_bar);
}

static void
menu_userlist_showhide_cb (session *sess)
{
	mg_decide_userlist (sess, TRUE);
}

static void
menu_ulbuttons_showhide_cb (session *sess)
{
	if (prefs.hex_gui_ulist_buttons)
		nx_widget_show (sess->gui->button_box);
	else
		nx_widget_hide (sess->gui->button_box);
}

static void
menu_cmbuttons_showhide_cb (session *sess)
{
	switch (sess->type)
	{
	case SESS_CHANNEL:
		if (prefs.hex_gui_mode_buttons)
			nx_widget_show (sess->gui->topicbutton_box);
		else
			nx_widget_hide (sess->gui->topicbutton_box);
		break;
	default:
		nx_widget_hide (sess->gui->topicbutton_box);
	}
}

/* GTK2 code assigned GtkCheckMenuItem->active directly: the state changes,
   no handler runs (nx_item_set_active_quiet keeps that) */
static void
menu_check_item_set_active_quiet (nx_item *item, gboolean active)
{
	nx_item_set_active_quiet (item, active);
}

static void
menu_setting_foreach (void (*callback) (session *), int id, guint state)
{
	session *sess;
	GSList *list;
	int maindone = FALSE;	/* do it only once for EVERY tab */

	list = sess_list;
	while (list)
	{
		sess = list->data;

		if (!sess->gui->is_tab || !maindone)
		{
			if (sess->gui->is_tab)
				maindone = TRUE;
			if (id != -1)
				menu_check_item_set_active_quiet (sess->gui->menu_item[id], state);
			if (callback)
				callback (sess);
		}

		list = list->next;
	}
}

void
menu_bar_toggle (void)
{
	prefs.hex_gui_hide_menu = !prefs.hex_gui_hide_menu;
	menu_setting_foreach (menu_showhide_cb, MENU_ID_MENUBAR, !prefs.hex_gui_hide_menu);
}

static void
menu_bar_toggle_cb (void)
{
	menu_bar_toggle ();
	if (prefs.hex_gui_hide_menu)
		fe_message (_("The Menubar is now hidden. You can show it again"
						  " by pressing Control+F9 or right-clicking in a blank part of"
						  " the main text area."), FE_MSG_INFO);
}

static void
menu_topicbar_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_topicbar = !prefs.hex_gui_topicbar;
	menu_setting_foreach (menu_topic_showhide_cb, MENU_ID_TOPICBAR,
								 prefs.hex_gui_topicbar);
}

static void
menu_userlist_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_ulist_hide = !prefs.hex_gui_ulist_hide;
	menu_setting_foreach (menu_userlist_showhide_cb, MENU_ID_USERLIST,
								 !prefs.hex_gui_ulist_hide);
}

static void
menu_ulbuttons_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_ulist_buttons = !prefs.hex_gui_ulist_buttons;
	menu_setting_foreach (menu_ulbuttons_showhide_cb, MENU_ID_ULBUTTONS,
								 prefs.hex_gui_ulist_buttons);
}

static void
menu_cmbuttons_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_mode_buttons = !prefs.hex_gui_mode_buttons;
	menu_setting_foreach (menu_cmbuttons_showhide_cb, MENU_ID_MODEBUTTONS,
								 prefs.hex_gui_mode_buttons);
}

static void
menu_fullscreen_toggle (GtkWidget *wid, gpointer ud)
{
	if (!prefs.hex_gui_win_fullscreen)
		gtk_window_fullscreen (GTK_WINDOW(parent_window));
	else
	{
		gtk_window_unfullscreen (GTK_WINDOW(parent_window));

#ifdef WIN32
		if (!prefs.hex_gui_win_state) /* not maximized */
		{
			/* other window managers seem to handle this */
			gtk_window_set_default_size (GTK_WINDOW (parent_window),
				prefs.hex_gui_win_width, prefs.hex_gui_win_height);
			gtkutil_window_move (GTK_WINDOW (parent_window),
				prefs.hex_gui_win_left, prefs.hex_gui_win_top);
		}
#endif
	}
}

void
menu_middlemenu (session *sess, GdkEvent *event)
{
	nx_menu *menu;

	/* the main menu as a popup: shortcuts shown, not active */
	menu = menu_create_main (NULL, FALSE, sess->server->is_away, !sess->gui->is_tab, NULL);
	menu_popup (menu, event, NULL);
}

static void savebuffer_req_done (session *sess, char *file);

static void
textmenu_copy_cb (nx_item *item, gpointer xtext)
{
	gtk_xtext_copy_selection (GTK_XTEXT (xtext));
}

static void
textmenu_select_all_cb (nx_item *item, gpointer xtext)
{
	gtk_xtext_select_all (GTK_XTEXT (xtext));
}

static void
textmenu_search_cb (nx_item *item, gpointer sess)
{
	if (is_session (sess))
		mg_search_toggle (sess);
}

static void
textmenu_clear_cb (nx_item *item, gpointer sess)
{
	if (is_session (sess))
		fe_text_clear (sess, 0);
}

static void
textmenu_save_cb (nx_item *item, gpointer sess)
{
	if (is_session (sess))
		gtkutil_file_req (NULL, _("Select an output filename"), savebuffer_req_done,
								sess, NULL, NULL, FRF_WRITE);
}

/* right click on plain text: copy/select/search on top, then the main
   menus HexChat showed here */
void
menu_textmenu (session *sess, GdkEvent *event, int x, int y)
{
	GtkXText *xtext = GTK_XTEXT (sess->gui->xtext);
	nx_menu *menu;
	nx_item *item;
	int pos = 0;

	menu = menu_create_main (NULL, FALSE, sess->server->is_away, !sess->gui->is_tab, NULL);

	item = nx_menu_add_item (menu, pos++, _("_Copy"), NX_ITEM_MNEMONIC, NX_STOCK_COPY,
									 textmenu_copy_cb, xtext);
	nx_item_set_accel (item, GDK_KEY_C, STATE_CTRL | STATE_SHIFT);
	if (!gtk_xtext_has_selection (xtext))
		nx_item_set_sensitive (item, FALSE);

	g_free (str_copy);
	str_copy = gtk_xtext_line_text_at (xtext, x, y);
	item = nx_menu_add_item (menu, pos++, _("Copy _Line"), NX_ITEM_MNEMONIC, NULL,
									 copy_to_clipboard_cb, str_copy);
	if (!str_copy)
		nx_item_set_sensitive (item, FALSE);

	nx_menu_add_item (menu, pos++, _("Select _All"), NX_ITEM_MNEMONIC, NULL,
							textmenu_select_all_cb, xtext);
	nx_menu_add_separator (menu, pos++);

	item = nx_menu_add_item (menu, pos++, _("_Search Text" ELLIPSIS), NX_ITEM_MNEMONIC,
									 NX_STOCK_FIND, textmenu_search_cb, sess);
	nx_item_set_accel (item, GDK_KEY_f, STATE_CTRL);
	nx_menu_add_item (menu, pos++, _("C_lear Text"), NX_ITEM_MNEMONIC, NX_STOCK_CLEAR,
							textmenu_clear_cb, sess);
	nx_menu_add_item (menu, pos++, _("Save Text" ELLIPSIS), NX_ITEM_MNEMONIC, NX_STOCK_SAVE,
							textmenu_save_cb, sess);
	nx_menu_add_separator (menu, pos++);

	menu_popup (menu, event, NULL);
}

static void
open_url_cb (nx_item *item, gpointer url)
{
	char buf[512];

	/* pass this to /URL so it can handle irc:// */
	g_snprintf (buf, sizeof (buf), "URL %s", (char *) url);
	handle_command (current_sess, buf, FALSE);
}

void
menu_urlmenu (GdkEvent *event, char *url)
{
	nx_menu *menu;
	char *tmp, *chop;

	g_free (str_copy);
	str_copy = g_strdup (url);

	menu = nx_menu_new ();
	/* more than 51 chars? Chop it */
	if (g_utf8_strlen (str_copy, -1) >= 52)
	{
		tmp = g_strdup (str_copy);
		chop = g_utf8_offset_to_pointer (tmp, 48);
		chop[0] = chop[1] = chop[2] = '.';
		chop[3] = 0;
		menu_quick_item (0, tmp, menu, XCMENU_SHADED, 0, 0);
		g_free (tmp);
	} else
	{
		menu_quick_item (0, str_copy, menu, XCMENU_SHADED, 0, 0);
	}
	menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);

	/* Two hardcoded entries */
	if (strncmp (str_copy, "irc://", 6) == 0 ||
	    strncmp (str_copy, "ircs://",7) == 0)
		menu_quick_item_with_callback (open_url_cb, _("Connect"), menu, str_copy);
	else
		menu_quick_item_with_callback (open_url_cb, _("Open Link in Browser"), menu, str_copy);
	menu_quick_item_with_callback (copy_to_clipboard_cb, _("Copy Selected Link"), menu, str_copy);
	/* custom ones from urlhandlers.conf */
	menu_create (menu, urlhandler_list, str_copy, TRUE);
	menu_add_plugin_items (menu, "\x4$URL", str_copy);
	menu_popup (menu, event, NULL);
}

static void
menu_chan_cycle (nx_item *item, gpointer chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "CYCLE %s", (char *) chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
menu_chan_part (nx_item *item, gpointer chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "part %s", (char *) chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
menu_chan_focus (nx_item *item, gpointer chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "doat %s gui focus", (char *) chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

static void
menu_chan_join (nx_item *item, gpointer chan)
{
	char tbuf[256];

	if (current_sess)
	{
		g_snprintf (tbuf, sizeof tbuf, "join %s", (char *) chan);
		handle_command (current_sess, tbuf, FALSE);
	}
}

void
menu_chanmenu (struct session *sess, GdkEvent *event, char *chan)
{
	nx_menu *menu;
	int is_joined = FALSE;
	session * chan_session;

	chan_session = find_channel (sess->server, chan);

	if (chan_session)
		is_joined = TRUE;

	g_free (str_copy);
	str_copy = g_strdup (chan);

	menu = nx_menu_new ();

	menu_quick_item (0, chan, menu, XCMENU_SHADED, str_copy, 0);
	menu_quick_item (0, 0, menu, XCMENU_SHADED, str_copy, 0);

	if (!is_joined)
		menu_quick_item_with_callback (menu_chan_join, _("Join Channel"), menu,
												 str_copy);
	else
	{
		if (chan_session != current_sess)
			menu_quick_item_with_callback (menu_chan_focus, _("Focus Channel"), menu,
													 str_copy);
		menu_quick_item_with_callback (menu_chan_part, _("Part Channel"), menu,
												 str_copy);
		menu_quick_item_with_callback (menu_chan_cycle, _("Cycle Channel"), menu,
												 str_copy);
	}

	menu_addfavoritemenu (sess->server, menu, str_copy, FALSE);

	menu_add_plugin_items (menu, "\x5$CHAN", str_copy);
	menu_popup (menu, event, NULL);
}

static void
menu_delfav_cb (nx_item *item, gpointer data)
{
	server *serv = data;

	servlist_autojoinedit (serv->network, str_copy, FALSE);
}

static void
menu_addfav_cb (nx_item *item, gpointer data)
{
	server *serv = data;

	servlist_autojoinedit (serv->network, str_copy, TRUE);
}

void
menu_addfavoritemenu (server *serv, nx_menu *menu, char *channel, gboolean istree)
{
	char *str;
	
	if (!serv->network)
		return;

	if (channel != str_copy)
	{
		g_free (str_copy);
		str_copy = g_strdup (channel);
	}
	
	if (istree)
		str = _("_Autojoin");
	else
		str = _("Autojoin Channel");

	if (joinlist_is_in_list (serv, channel))
	{
		menu_toggle_item (str, menu, menu_delfav_cb, serv, TRUE);
	}
	else
	{
		menu_toggle_item (str, menu, menu_addfav_cb, serv, FALSE);
	}
}

static void
menu_delautoconn_cb (nx_item *item, gpointer data)
{
	server *serv = data;

	if (!serv->network)
		return;
	((ircnet*)serv->network)->flags &= ~FLAG_AUTO_CONNECT;
	servlist_save ();
}

static void
menu_addautoconn_cb (nx_item *item, gpointer data)
{
	server *serv = data;

	if (!serv->network)
		return;
	((ircnet*)serv->network)->flags |= FLAG_AUTO_CONNECT;
	servlist_save ();
}

void
menu_addconnectmenu (server *serv, nx_menu *menu)
{
	if (!serv->network)
		return;

	if (((ircnet*)serv->network)->flags & FLAG_AUTO_CONNECT)
	{
		menu_toggle_item (_("_Auto-Connect"), menu, menu_delautoconn_cb, serv, TRUE);
	}
	else
	{
		menu_toggle_item (_("_Auto-Connect"), menu, menu_addautoconn_cb, serv, FALSE);
	}
}

static void
menu_open_server_list (GtkWidget *wid, gpointer none)
{
	fe_serverlist_open (current_sess);
}

/* the network of this tab is where it starts */
static void
menu_stayonline (GtkWidget *wid, gpointer none)
{
	bncwin_open (current_sess && current_sess->gui ? GTK_WINDOW (current_sess->gui->window) : NULL,
					 current_sess && current_sess->server ? current_sess->server->network : NULL);
}

static void
menu_settings (GtkWidget * wid, gpointer none)
{
	extern void setup_open (void);
	setup_open ();
}

static void
menu_usermenu (nx_item *item, gpointer unused)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("User menu - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, usermenu_list, buf, "usermenu", "usermenu.conf", 0);
}

static void
usermenu_create (nx_menu *menu)
{
	menu_create (menu, usermenu_list, "", FALSE);
	menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);	/* sep */
	menu_quick_item_with_callback (menu_usermenu, _("Edit This Menu" ELLIPSIS), menu, 0);
}

static void
usermenu_destroy (nx_menu *menu)
{
	nx_menu_clear (menu);
}

void
usermenu_update (void)
{
	int done_main = FALSE;
	GSList *list = sess_list;
	session *sess;
	nx_menu *menu;

	while (list)
	{
		sess = list->data;
		menu = sess->gui->menu_item[MENU_ID_USERMENU] ?
			nx_item_get_submenu (sess->gui->menu_item[MENU_ID_USERMENU]) : NULL;
		if (sess->gui->is_tab)
		{
			if (!done_main && menu)
			{
				usermenu_destroy (menu);
				usermenu_create (menu);
				done_main = TRUE;
			}
		} else if (menu)
		{
			usermenu_destroy (menu);
			usermenu_create (menu);
		}
		list = list->next;
	}
}

static void
menu_newserver_window (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 0;
	new_ircwindow (NULL, NULL, SESS_SERVER, 0);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_newchannel_window (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 0;
	new_ircwindow (current_sess->server, NULL, SESS_CHANNEL, 0);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_newserver_tab (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;
	int oldf = prefs.hex_gui_tab_newtofront;

	prefs.hex_gui_tab_chans = 1;
	/* force focus if setting is "only requested tabs" */
	if (prefs.hex_gui_tab_newtofront == 2)
		prefs.hex_gui_tab_newtofront = 1;
	new_ircwindow (NULL, NULL, SESS_SERVER, 0);
	prefs.hex_gui_tab_chans = old;
	prefs.hex_gui_tab_newtofront = oldf;
}

static void
menu_newchannel_tab (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 1;
	new_ircwindow (current_sess->server, NULL, SESS_CHANNEL, 0);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_rawlog (GtkWidget * wid, gpointer none)
{
	open_rawlog (current_sess->server);
}

static void
menu_detach (GtkWidget * wid, gpointer none)
{
	mg_detach (current_sess, 0);
}

static void
menu_close (GtkWidget * wid, gpointer none)
{
	mg_close_sess (current_sess);
}

static void
menu_quit (GtkWidget * wid, gpointer none)
{
	mg_open_quit_dialog (FALSE);
}

static void
menu_search (void)
{
	mg_search_toggle (current_sess);
}

static void
menu_search_next (GtkWidget *wid)
{
	mg_search_handle_next (NULL, current_sess);
}

static void
menu_search_prev (GtkWidget *wid)
{
	mg_search_handle_previous (NULL, current_sess);
}

static void
menu_resetmarker (GtkWidget * wid, gpointer none)
{
	gtk_xtext_reset_marker_pos (GTK_XTEXT (current_sess->gui->xtext));
}

static void
menu_movetomarker (GtkWidget *wid, gpointer none)
{
	marker_reset_reason reason;
	char *str;

	if (!prefs.hex_text_show_marker)
		PrintText (current_sess, _("Marker line disabled."));
	else
	{
		reason = gtk_xtext_moveto_marker_pos (GTK_XTEXT (current_sess->gui->xtext));
		switch (reason) {
		case MARKER_WAS_NEVER_SET:
			str = _("Marker line never set."); break;
		case MARKER_IS_SET:
			str = ""; break;
		case MARKER_RESET_MANUALLY:
			str = _("Marker line reset manually."); break;
		case MARKER_RESET_BY_KILL:
			str = _("Marker line reset because exceeded scrollback limit."); break;
		case MARKER_RESET_BY_CLEAR:
			str = _("Marker line reset by CLEAR command."); break;
		default:
			str = _("Marker line state unknown."); break;
		}
		if (str[0])
			PrintText (current_sess, str);
	}
}

static void
menu_copy_selection (GtkWidget * wid, gpointer none)
{
	gtk_xtext_copy_selection (GTK_XTEXT (current_sess->gui->xtext));
}

static void
menu_flushbuffer (GtkWidget * wid, gpointer none)
{
	fe_text_clear (current_sess, 0);
}

static void
savebuffer_req_done (session *sess, char *file)
{
	int fh;

	if (!file)
		return;

	fh = g_open (file, O_TRUNC | O_WRONLY | O_CREAT, 0600);
	if (fh != -1)
	{
		gtk_xtext_save (GTK_XTEXT (sess->gui->xtext), fh);
		close (fh);
	}
}

static void
menu_savebuffer (GtkWidget * wid, gpointer none)
{
	gtkutil_file_req (NULL, _("Select an output filename"), savebuffer_req_done,
							current_sess, NULL, NULL, FRF_WRITE);
}

static void
menu_disconnect (GtkWidget * wid, gpointer none)
{
	handle_command (current_sess, "DISCON", FALSE);
}

static void
menu_reconnect (GtkWidget * wid, gpointer none)
{
	if (current_sess->server->hostname[0])
		handle_command (current_sess, "RECONNECT", FALSE);
	else
		fe_serverlist_open (current_sess);
}

static void
menu_join_cb (GtkWidget *dialog, gint response, GtkEntry *entry)
{
	switch (response)
	{
	case NX_RESPONSE_ACCEPT:
		menu_chan_join (NULL, (char *) nx_entry_get_text (entry));
		break;

	case NX_RESPONSE_HELP:
		chanlist_opengui (current_sess->server, TRUE);
		break;
	}

	nx_widget_destroy (dialog);
}

static void
menu_join_entry_cb (GtkWidget *entry, GtkWidget *dialog)
{
	nx_dialog_response (NX_DIALOG_CAST (dialog), NX_RESPONSE_ACCEPT);
}

static void
menu_join (GtkWidget * wid, gpointer none)
{
	GtkWidget *hbox, *dialog, *entry, *label;

	dialog = nx_dialog_new (_("Join Channel"), GTK_WINDOW (parent_window), FALSE);
	nx_dialog_add_button (NX_DIALOG_CAST (dialog), _("Retrieve channel list"), NX_RESPONSE_HELP);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_CANCEL, NX_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_OK, NX_RESPONSE_ACCEPT);
	gtk_box_set_homogeneous (GTK_BOX (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog))), TRUE);
	nx_window_set_position_mouse (dialog);
	hbox = gtkutil_box_new (GTK_ORIENTATION_HORIZONTAL, TRUE, 0);

	entry = gtk_entry_new ();
	gtk_editable_set_editable (GTK_EDITABLE (entry), FALSE);	/* avoid auto-selection */
	nx_entry_set_text (entry, "#");
	g_signal_connect (G_OBJECT (entry), "activate",
						 	G_CALLBACK (menu_join_entry_cb), dialog);
	nx_box_pack_end (hbox, entry, 0, 0, 0);

	label = gtk_label_new (_("Enter Channel to Join:"));
	nx_box_pack_end (hbox, label, 0, 0, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (menu_join_cb), entry);

	gtkutil_container_add (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog)), hbox);

	nx_widget_show_all (dialog);

	gtk_editable_set_editable (GTK_EDITABLE (entry), TRUE);
	gtk_editable_set_position (GTK_EDITABLE (entry), 1);
}

static void
menu_away (nx_item *item, gpointer none)
{
	handle_command (current_sess, nx_item_get_active (item) ? "away" : "back", FALSE);
}

static void
menu_chanlist (GtkWidget * wid, gpointer none)
{
	chanlist_opengui (current_sess->server, FALSE);
}

static void
menu_banlist (GtkWidget * wid, gpointer none)
{
	banlist_opengui (current_sess);
}

#ifdef USE_PLUGIN

static void
menu_loadplugin (void)
{
	plugingui_load ();
}

static void
menu_pluginlist (void)
{
	plugingui_open ();
}

#else

static void
menu_noplugin_info (void)
{
	fe_message (_(DISPLAY_NAME " has been build without plugin support."), FE_MSG_INFO);
}

#define menu_loadplugin menu_noplugin_info
#define menu_pluginlist menu_noplugin_info

#endif

#define usercommands_help  _("User Commands - Special codes:\n\n"\
                           "%c  =  current channel\n"\
									"%e  =  current network name\n"\
									"%m  =  machine info\n"\
                           "%n  =  your nick\n"\
									"%t  =  time/date\n"\
                           "%v  =  HexChat version\n"\
                           "%2  =  word 2\n"\
                           "%3  =  word 3\n"\
                           "&2  =  word 2 to the end of line\n"\
                           "&3  =  word 3 to the end of line\n\n"\
                           "eg:\n"\
                           "/cmd john hello\n\n"\
                           "%2 would be \042john\042\n"\
                           "&2 would be \042john hello\042.")

#define ulbutton_help       _("Userlist Buttons - Special codes:\n\n"\
							"%a  =  all selected nicks\n"\
							"%c  =  current channel\n"\
							"%e  =  current network name\n"\
							"%h  =  selected nick's hostname\n"\
							"%m  =  machine info\n"\
							"%n  =  your nick\n"\
							"%s  =  selected nick\n"\
							"%t  =  time/date\n"\
							"%u  =  selected users account")

#define dlgbutton_help      _("Dialog Buttons - Special codes:\n\n"\
							"%a  =  all selected nicks\n"\
							"%c  =  current channel\n"\
							"%e  =  current network name\n"\
							"%h  =  selected nick's hostname\n"\
							"%m  =  machine info\n"\
							"%n  =  your nick\n"\
							"%s  =  selected nick\n"\
							"%t  =  time/date\n"\
							"%u  =  selected users account")

#define ctcp_help          _("CTCP Replies - Special codes:\n\n"\
                           "%d  =  data (the whole ctcp)\n"\
									"%e  =  current network name\n"\
									"%m  =  machine info\n"\
                           "%s  =  nick who sent the ctcp\n"\
                           "%t  =  time/date\n"\
                           "%2  =  word 2\n"\
                           "%3  =  word 3\n"\
                           "&2  =  word 2 to the end of line\n"\
                           "&3  =  word 3 to the end of line\n\n")

#define url_help           _("URL Handlers - Special codes:\n\n"\
                           "%s  =  the URL string\n\n"\
                           "Putting a ! in front of the command\n"\
                           "indicates it should be sent to a\n"\
                           "shell instead of HexChat")

static void
menu_usercommands (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("User Defined Commands - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, command_list, buf, "commands", "commands.conf",
							usercommands_help);
}

static void
menu_ulpopup (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Userlist Popup menu -  %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, popup_list, buf, "popup", "popup.conf", ulbutton_help);
}

static void
menu_rpopup (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Replace - %s"), _(DISPLAY_NAME));
	editlist_gui_open (_("Text"), _("Replace with"), replace_list, buf, "replace", "replace.conf", 0);
}

static void
menu_urlhandlers (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("URL Handlers - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, urlhandler_list, buf, "urlhandlers", "urlhandlers.conf", url_help);
}

static void
menu_evtpopup (void)
{
	pevent_dialog_show ();
}

static void
menu_keypopup (void)
{
	key_dialog_show ();
}

static void
menu_ulbuttons (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Userlist buttons - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, button_list, buf, "buttons", "buttons.conf", ulbutton_help);
}

static void
menu_dlgbuttons (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("Dialog buttons - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, dlgbutton_list, buf, "dlgbuttons", "dlgbuttons.conf",
							 dlgbutton_help);
}

static void
menu_ctcpguiopen (void)
{
	char buf[128];
	g_snprintf(buf, sizeof(buf), _("CTCP Replies - %s"), _(DISPLAY_NAME));
	editlist_gui_open (NULL, NULL, ctcp_list, buf, "ctcpreply", "ctcpreply.conf", ctcp_help);
}

static void
menu_docs (GtkWidget *wid, gpointer none)
{
	fe_open_url ("http://hexchat.readthedocs.org");
}

/*static void
menu_webpage (GtkWidget *wid, gpointer none)
{
	fe_open_url ("http://xchat.org");
}*/

static void
menu_dcc_win (GtkWidget *wid, gpointer none)
{
	fe_dcc_open_recv_win (FALSE);
	fe_dcc_open_send_win (FALSE);
}

static void
menu_dcc_chat_win (GtkWidget *wid, gpointer none)
{
	fe_dcc_open_chat_win (FALSE);
}

void
menu_change_layout (void)
{
	if (prefs.hex_gui_tab_layout == 0)
	{
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TABS, 1);
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TREE, 0);
		mg_change_layout (0);
	} else
	{
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TABS, 0);
		menu_setting_foreach (NULL, MENU_ID_LAYOUT_TREE, 1);
		mg_change_layout (2);
	}
}

static void
menu_layout_cb (nx_item *item, gpointer none)
{
	prefs.hex_gui_tab_layout = 2;
	if (nx_item_get_active (item))
		prefs.hex_gui_tab_layout = 0;

	menu_change_layout ();
}

static void
menu_apply_metres_cb (session *sess)
{
	mg_update_meters (sess->gui);
}

static void
menu_metres_off (nx_item *item, gpointer none)
{
	if (nx_item_get_active (item))
	{
		prefs.hex_gui_lagometer = 0;
		prefs.hex_gui_throttlemeter = 0;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_text (nx_item *item, gpointer none)
{
	if (nx_item_get_active (item))
	{
		prefs.hex_gui_lagometer = 2;
		prefs.hex_gui_throttlemeter = 2;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_graph (nx_item *item, gpointer none)
{
	if (nx_item_get_active (item))
	{
		prefs.hex_gui_lagometer = 1;
		prefs.hex_gui_throttlemeter = 1;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_both (nx_item *item, gpointer none)
{
	if (nx_item_get_active (item))
	{
		prefs.hex_gui_lagometer = 3;
		prefs.hex_gui_throttlemeter = 3;
		hexchat_reinit_timers ();
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

/* GTK4's GtkAboutDialog puts the pages in a header bar switcher; this is
   GTK3's layout: logo, name, version, comments, link, copyright, and
   Credits/License toggles at the bottom left with Close on the right. */
static void
about_license_toggled (GtkToggleButton *b, GtkWidget *stack)
{
	gtk_stack_set_visible_child_name (GTK_STACK (stack),
		gtk_toggle_button_get_active (b) ? "license" : "about");
}

static void
about_response (NxDialog *d, int response, gpointer data)
{
	gtk_window_destroy (GTK_WINDOW (d));
}

static gboolean
about_link (GtkLabel *label, char *uri, gpointer data)
{
	fe_open_url (uri);
	return TRUE;
}

static GtkWidget *
about_label (const char *markup, const char *css)
{
	GtkWidget *l = gtk_label_new (NULL);

	gtk_label_set_markup (GTK_LABEL (l), markup);
	gtk_label_set_justify (GTK_LABEL (l), GTK_JUSTIFY_CENTER);
	gtk_label_set_selectable (GTK_LABEL (l), TRUE);
	gtk_widget_set_focusable (l, FALSE);
	if (css)
		gtk_widget_add_css_class (l, css);
	return l;
}

static void
about_dialog_show (GtkWindow *parent, const char *comment, const char *license,
						 const char *copyright)
{
	NxDialog *d = NX_DIALOG (nx_dialog_new (NULL, parent, FALSE));
	GtkWidget *content = nx_dialog_get_content_area (d);
	GtkWidget *box, *stack, *page, *l, *img, *sw, *tv, *b, *credits;
	GdkTexture *logo;
	char *s;

	s = g_strdup_printf (_("About %s"), _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (d), s);
	g_free (s);
	gtk_window_set_resizable (GTK_WINDOW (d), FALSE);
	gtk_widget_add_css_class (GTK_WIDGET (d), "aboutdialog");

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (box, 5);
	gtk_widget_set_margin_end (box, 5);
	gtk_widget_set_margin_top (box, 5);
	gtk_box_append (GTK_BOX (content), box);

	logo = nx_texture_new_for_pixbuf (pix_hexchat);
	img = gtk_image_new_from_paintable (GDK_PAINTABLE (logo));
	g_object_unref (logo);
	gtk_image_set_pixel_size (GTK_IMAGE (img), gdk_pixbuf_get_width (pix_hexchat));
	gtk_box_append (GTK_BOX (box), img);

	s = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", _(DISPLAY_NAME));
	gtk_box_append (GTK_BOX (box), about_label (s, NULL));
	g_free (s);

	stack = gtk_stack_new ();
	/* GTK3 sizes the dialog for its biggest page, the license */
	gtk_widget_set_margin_top (stack, 5);
	gtk_box_append (GTK_BOX (box), stack);

	page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_append (GTK_BOX (page), about_label (PACKAGE_VERSION, NULL));
	s = g_markup_escape_text (comment, -1);
	l = about_label (s, NULL);
	g_free (s);
	gtk_label_set_wrap (GTK_LABEL (l), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (l), 60);
	gtk_box_append (GTK_BOX (page), l);
	l = about_label ("<a href=\"https://github.com/1ay1/nexchat\">Website</a>", NULL);
	g_signal_connect (l, "activate-link", G_CALLBACK (about_link), NULL);
	gtk_box_append (GTK_BOX (page), l);
	s = g_markup_printf_escaped ("<span size=\"small\">%s</span>", copyright);
	gtk_box_append (GTK_BOX (page), about_label (s, NULL));
	g_free (s);
	gtk_stack_add_named (GTK_STACK (stack), page, "about");

	/* license: a framed, scrolled text view, as GTK3 */
	sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (sw, TRUE);
	/* GTK3's license area; a text view reports no width before layout */
	gtk_widget_set_size_request (sw, 410, 116);
	tv = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD);
	gtk_widget_add_css_class (tv, "nx-about-license");
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (tv), 8);
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (tv), 8);
	gtk_text_view_set_top_margin (GTK_TEXT_VIEW (tv), 4);
	gtk_text_view_set_bottom_margin (GTK_TEXT_VIEW (tv), 4);
	gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv)), license, -1);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), tv);
	gtk_stack_add_named (GTK_STACK (stack), sw, "license");
	gtk_stack_set_visible_child_name (GTK_STACK (stack), "about");

	/* Credits (no authors are set, so it does nothing, as in GTK3) and
	   License at the start of the button row, in a group */
	credits = gtk_toggle_button_new_with_mnemonic (g_dgettext ("gtk30", "C_redits"));
	b = gtk_toggle_button_new_with_mnemonic (g_dgettext ("gtk30", "_License"));
	gtk_widget_set_size_request (credits, 85, -1);
	gtk_widget_set_size_request (b, 85, -1);
	g_signal_connect (b, "toggled", G_CALLBACK (about_license_toggled), stack);
	{
		GtkWidget *action = nx_dialog_get_action_area (d);
		gtk_widget_set_halign (action, GTK_ALIGN_FILL);
		gtk_box_append (GTK_BOX (action), credits);
		gtk_box_append (GTK_BOX (action), b);
		l = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_hexpand (l, TRUE);
		gtk_box_append (GTK_BOX (action), l);
	}
	b = nx_dialog_add_button (d, g_dgettext ("gtk30", "_Close"), NX_RESPONSE_CLOSE);
	nx_dialog_set_default_response (d, NX_RESPONSE_CLOSE);
	g_signal_connect (d, "response", G_CALLBACK (about_response), NULL);

	gtk_window_present (GTK_WINDOW (d));
	gtk_widget_grab_focus (b);
}

static void
menu_about (GtkWidget *wid, gpointer sess)
{
	char comment[512];
	char *license = "This program is free software; you can redistribute it and/or modify\n" \
					"it under the terms of the GNU General Public License as published by\n" \
					"the Free Software Foundation; version 2.\n\n" \
					"This program is distributed in the hope that it will be useful,\n" \
					"but WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
					"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n" \
					"GNU General Public License for more details.\n\n" \
					"You should have received a copy of the GNU General Public License\n" \
					"along with this program. If not, see <http://www.gnu.org/licenses/>";

	g_snprintf  (comment, sizeof(comment), ""
#ifdef WIN32
				"Portable Mode: %s\n"
				"Build Type: x%d\n"
#endif
				"OS: %s",
#ifdef WIN32
				(portable_mode () ? "Yes" : "No"),
				get_cpu_arch (),
#endif
				get_sys_str (0));

	about_dialog_show (GTK_WINDOW (parent_window), comment, license,
		"\302\251 1998-2010 Peter \305\275elezn\303\275\n\302\251 2009-2014 Berke Viktor\nBased on HexChat");
}

static struct mymenu mymenu[] = {
	{N_("ne_xchat"), 0, 0, M_NEWMENU, MENU_ID_HEXCHAT, 0, 1},
	{N_("Network Li_st"), menu_open_server_list, (char *)&pix_book, M_MENUPIX, 0, 0, 1, GDK_KEY_s},
	{0, 0, 0, M_SEP, 0, 0, 0},

	{N_("_New"), 0, NX_STOCK_NEW, M_MENUSUB, 0, 0, 1},
		{N_("Server Tab"), menu_newserver_tab, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_t},
		{N_("Channel Tab"), menu_newchannel_tab, 0, M_MENUITEM, 0, 0, 1},
		{N_("Server Window"), menu_newserver_window, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_n},
		{N_("Channel Window"), menu_newchannel_window, 0, M_MENUITEM, 0, 0, 1},
		{0, 0, 0, M_END, 0, 0, 0},
	{0, 0, 0, M_SEP, 0, 0, 0},

	{N_("_Load Plugin or Script" ELLIPSIS), menu_loadplugin, NX_STOCK_REVERT_TO_SAVED, M_MENUSTOCK, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},	/* 11 */
#define DETACH_OFFSET (12)
	{0, menu_detach, NX_STOCK_REDO, M_MENUSTOCK, 0, 0, 1},	/* 12 */
#define CLOSE_OFFSET (13)
	{0, menu_close, NX_STOCK_CLOSE, M_MENUSTOCK, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("_Quit"), menu_quit, NX_STOCK_QUIT, M_MENUSTOCK, 0, 0, 1, GDK_KEY_q},	/* 15 */

	{N_("_View"), 0, 0, M_NEWMENU, 0, 0, 1},
#define MENUBAR_OFFSET (17)
	{N_("_Menu Bar"), menu_bar_toggle_cb, 0, M_MENUTOG, MENU_ID_MENUBAR, 0, 1, GDK_KEY_F9},
	{N_("_Topic Bar"), menu_topicbar_toggle, 0, M_MENUTOG, MENU_ID_TOPICBAR, 0, 1},
	{N_("_User List"), menu_userlist_toggle, 0, M_MENUTOG, MENU_ID_USERLIST, 0, 1, GDK_KEY_F7},
	{N_("U_ser List Buttons"), menu_ulbuttons_toggle, 0, M_MENUTOG, MENU_ID_ULBUTTONS, 0, 1},
	{N_("M_ode Buttons"), menu_cmbuttons_toggle, 0, M_MENUTOG, MENU_ID_MODEBUTTONS, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("_Channel Switcher"), 0, 0, M_MENUSUB, 0, 0, 1},	/* 23 */
#define TABS_OFFSET (24)
		{N_("_Tabs"), menu_layout_cb, 0, M_MENURADIO, MENU_ID_LAYOUT_TABS, 0, 1},
		{N_("T_ree"), 0, 0, M_MENURADIO, MENU_ID_LAYOUT_TREE, 0, 1},
		{0, 0, 0, M_END, 0, 0, 0},
	{N_("_Network Meters"), 0, 0, M_MENUSUB, 0, 0, 1},	/* 27 */
#define METRE_OFFSET (28)
		{N_("Off"), menu_metres_off, 0, M_MENURADIO, 0, 0, 1},
		{N_("Graph"), menu_metres_graph, 0, M_MENURADIO, 0, 0, 1},
		{N_("Text"), menu_metres_text, 0, M_MENURADIO, 0, 0, 1},
		{N_("Both"), menu_metres_both, 0, M_MENURADIO, 0, 0, 1},
		{0, 0, 0, M_END, 0, 0, 0},	/* 32 */
	{ 0, 0, 0, M_SEP, 0, 0, 0 },
	{N_ ("_Fullscreen"), menu_fullscreen_toggle, 0, M_MENUTOG, MENU_ID_FULLSCREEN, 0, 1, GDK_KEY_F11},

	{N_("_Server"), 0, 0, M_NEWMENU, 0, 0, 1},
	{N_("_Disconnect"), menu_disconnect, NX_STOCK_DISCONNECT, M_MENUSTOCK, MENU_ID_DISCONNECT, 0, 1},
	{N_("_Reconnect"), menu_reconnect, NX_STOCK_CONNECT, M_MENUSTOCK, MENU_ID_RECONNECT, 0, 1},
	{N_("_Join a Channel" ELLIPSIS), menu_join, NX_STOCK_JUMP_TO, M_MENUSTOCK, MENU_ID_JOIN, 0, 1},
	{N_("Channel _List"), menu_chanlist, NX_STOCK_INDEX, M_MENUITEM, 0, 0, 1},
	{N_("_Bouncer" ELLIPSIS), menu_stayonline, 0, M_MENUITEM, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
#define AWAY_OFFSET (42)
	{N_("Marked _Away"), menu_away, 0, M_MENUTOG, MENU_ID_AWAY, 0, 1, GDK_KEY_a},

	{N_("_Usermenu"), 0, 0, M_NEWMENU, MENU_ID_USERMENU, 0, 1},	/* 40 */

	{N_("S_ettings"), 0, 0, M_NEWMENU, 0, 0, 1},
	{N_("_Preferences"), menu_settings, NX_STOCK_PREFERENCES, M_MENUSTOCK, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("Auto Replace"), menu_rpopup, 0, M_MENUITEM, 0, 0, 1},
	{N_("CTCP Replies"), menu_ctcpguiopen, 0, M_MENUITEM, 0, 0, 1},
	{N_("Dialog Buttons"), menu_dlgbuttons, 0, M_MENUITEM, 0, 0, 1},
	{N_("Keyboard Shortcuts"), menu_keypopup, 0, M_MENUITEM, 0, 0, 1},
	{N_("Text Events"), menu_evtpopup, 0, M_MENUITEM, 0, 0, 1},
	{N_("URL Handlers"), menu_urlhandlers, 0, M_MENUITEM, 0, 0, 1},
	{N_("User Commands"), menu_usercommands, 0, M_MENUITEM, 0, 0, 1},
	{N_("User List Buttons"), menu_ulbuttons, 0, M_MENUITEM, 0, 0, 1},
	{N_("User List Popup"), menu_ulpopup, 0, M_MENUITEM, 0, 0, 1},	/* 52 */

	{N_("_Window"), 0, 0, M_NEWMENU, 0, 0, 1},
	{N_("_Ban List"), menu_banlist, 0, M_MENUITEM, 0, 0, 1},
	{N_("Character Chart"), ascii_open, 0, M_MENUITEM, 0, 0, 1},
	{N_("Direct Chat"), menu_dcc_chat_win, 0, M_MENUITEM, 0, 0, 1},
	{N_("File _Transfers"), menu_dcc_win, 0, M_MENUITEM, 0, 0, 1},
	{N_("Friends List"), notify_opengui, 0, M_MENUITEM, 0, 0, 1},
	{N_("Ignore List"), ignore_gui_open, 0, M_MENUITEM, 0, 0, 1},
	{N_("_Mentions"), mentions_open, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_I},
	{N_("_Plugins and Scripts"), menu_pluginlist, 0, M_MENUITEM, 0, 0, 1},
	{N_("_Raw Log"), menu_rawlog, 0, M_MENUITEM, 0, 0, 1},	/* 61 */
	{N_("_URL Grabber"), url_opengui, 0, M_MENUITEM, 0, 0, 1},
	{0, 0, 0, M_SEP, 0, 0, 0},
	{N_("Reset Marker Line"), menu_resetmarker, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_m},
	{N_("Move to Marker Line"), menu_movetomarker, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_M},
	{N_("_Copy Selection"), menu_copy_selection, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_C},
	{N_("C_lear Text"), menu_flushbuffer, NX_STOCK_CLEAR, M_MENUSTOCK, 0, 0, 1},
	{N_("Save Text" ELLIPSIS), menu_savebuffer, NX_STOCK_SAVE, M_MENUSTOCK, 0, 0, 1},
#define SEARCH_OFFSET (73)
	{N_("Search"), 0, NX_STOCK_JUSTIFY_LEFT, M_MENUSUB, 0, 0, 1},
		{N_("Search Text" ELLIPSIS), menu_search, NX_STOCK_FIND, M_MENUSTOCK, 0, 0, 1, GDK_KEY_f},
		{N_("Search Next"   ), menu_search_next, NX_STOCK_FIND, M_MENUSTOCK, 0, 0, 1, GDK_KEY_g},
		{N_("Search Previous"   ), menu_search_prev, NX_STOCK_FIND, M_MENUSTOCK, 0, 0, 1, GDK_KEY_G},
		{0, 0, 0, M_END, 0, 0, 0},
	{N_("_Jump To" ELLIPSIS), jumpto_open, 0, M_MENUITEM, 0, 0, 1, GDK_KEY_j},

	{N_("_Help"), 0, 0, M_NEWMENU, 0, 0, 1},	/* 75 */
	{N_("_Contents"), menu_docs, NX_STOCK_HELP, M_MENUSTOCK, 0, 0, 1, GDK_KEY_F1},
	{N_("_About"), menu_about, NX_STOCK_ABOUT, M_MENUSTOCK, 0, 0, 1},

	{0, 0, 0, M_END, 0, 0, 0},
};

/* the state follows the server / window, without running the callback */
void
menu_set_away (session_gui *gui, int away)
{
	nx_item_set_active_quiet (gui->menu_item[MENU_ID_AWAY], away);
}

void
menu_set_fullscreen (session_gui *gui, int full)
{
	nx_item_set_active_quiet (gui->menu_item[MENU_ID_FULLSCREEN], full);
}

/* === STUFF FOR /MENU === */

/* name NULL: the first separator */
static nx_item *
menu_find_item (nx_menu *menu, char *name)
{
	int i, n = nx_menu_get_n_items (menu);
	nx_item *item;
	const char *labeltext;

	for (i = 0; i < n; i++)
	{
		item = nx_menu_get_item (menu, i);
		if (!nx_item_is_separator (item))	/* separators arn't labels, skip them */
		{
			labeltext = nx_item_get_data (item, "name");
			if (!labeltext)
				labeltext = nx_item_get_text (item);
			if (labeltext && !menu_streq (labeltext, name, 1))
				return item;
		} else if (name == NULL)
			return item;
	}

	return NULL;
}

static nx_menu *
menu_find_path (nx_menu *menu, char *path)
{
	nx_item *item;
	char *s;
	char name[128];
	int len;

	/* grab the next part of the path */
	s = strchr (path, '/');
	len = s - path;
	if (!s)
		len = strlen (path);
	len = MIN (len, sizeof (name) - 1);
	memcpy (name, path, len);
	name[len] = 0;

	item = menu_find_item (menu, name);
	if (!item)
		return NULL;

	menu = nx_item_get_submenu (item);
	if (!menu)
		return NULL;

	path += len;
	if (*path == 0)
		return menu;

	return menu_find_path (menu, path + 1);
}

static nx_item *
menu_find (nx_menu *menu, char *path, char *label)
{
	nx_item *item = NULL;

	if (path[0] != 0)
		menu = menu_find_path (menu, path);
	if (menu)
		item = menu_find_item (menu, label);
	return item;
}

static void
menu_foreach_gui (menu_entry *me, void (*callback) (nx_menu *, menu_entry *, char *))
{
	GSList *list = sess_list;
	int tabdone = FALSE;
	session *sess;

	if (!me->is_main)
		return;	/* not main menu */

	while (list)
	{
		sess = list->data;
		/* do it only once for tab sessions, since they share a GUI */
		if (sess->gui->main_menu && (!sess->gui->is_tab || !tabdone))
		{
			callback (sess->gui->main_menu, me, NULL);
			if (sess->gui->is_tab)
				tabdone = TRUE;
		}
		list = list->next;
	}
}

static void
menu_update_cb (nx_menu *menu, menu_entry *me, char *target)
{
	nx_item *item;

	item = menu_find (menu, me->path, me->label);
	if (item)
	{
		nx_item_set_sensitive (item, me->enable);
		/* must do it without triggering the callback (plain items: no-op) */
		nx_item_set_active_quiet (item, me->state);
	}
}

/* radio state changed via mouse click */
static void
menu_radio_cb (nx_item *item, gpointer data)
{
	menu_entry *me = data;

	me->state = 0;
	if (nx_item_get_active (item))
		me->state = 1;

	/* update the state, incase this was changed via right-click. */
	/* This will update all other windows and menu bars */
	menu_foreach_gui (me, menu_update_cb);

	if (me->state && me->cmd)
		handle_command (current_sess, me->cmd, FALSE);
}

/* toggle state changed via mouse click */
static void
menu_toggle_cb (nx_item *item, gpointer data)
{
	menu_entry *me = data;

	me->state = 0;
	if (nx_item_get_active (item))
		me->state = 1;

	/* update the state, incase this was changed via right-click. */
	/* This will update all other windows and menu bars */
	menu_foreach_gui (me, menu_update_cb);

	if (me->state)
		handle_command (current_sess, me->cmd, FALSE);
	else
		handle_command (current_sess, me->ucmd, FALSE);
}

/* where /MENU -p<pos> puts a new item: 0xffff (outbound.c's default) at the
   end, negative counts from the end, as the old append-then-reorder did */
static int
menu_insert_pos (nx_menu *menu, int pos)
{
	if (pos == 0xffff)
		return NX_MENU_APPEND;
	if (pos < 0)
	{
		pos += nx_menu_get_n_items (menu);
		if (pos < 0)
			return NX_MENU_APPEND;	/* GTK's "negative: to the end" */
	}
	return pos;
}

static nx_item *
menu_add_radio (nx_menu *menu, menu_entry *me)
{
	nx_item *item = NULL;
	char *path = me->path + me->root_offset;

	if (path[0] != 0)
		menu = menu_find_path (menu, path);
	if (menu)
		/* the group is the item called me->group in the same menu */
		item = nx_menu_add_radio (menu, menu_insert_pos (menu, me->pos), me->label, 0,
										  menu_find_item (menu, me->group), me->state,
										  menu_radio_cb, me);
	return item;
}

static nx_item *
menu_add_toggle (nx_menu *menu, menu_entry *me)
{
	nx_item *item = NULL;
	char *path = me->path + me->root_offset;

	if (path[0] != 0)
		menu = menu_find_path (menu, path);
	if (menu)
		item = nx_menu_add_check (menu, menu_insert_pos (menu, me->pos), me->label,
										  NX_ITEM_MNEMONIC, me->state, menu_toggle_cb, me);
	return item;
}

static nx_item *
menu_add_item (nx_menu *menu, menu_entry *me, char *target)
{
	nx_item *item = NULL;
	char *path = me->path + me->root_offset;

	if (path[0] != 0)
		menu = menu_find_path (menu, path);
	if (menu)
		item = menu_quick_item_at (me->cmd, me->label, menu,
											me->markup ? XCMENU_MARKUP|XCMENU_MNEMONIC : XCMENU_MNEMONIC,
											target, me->icon, menu_insert_pos (menu, me->pos));
	return item;
}

static nx_item *
menu_add_sub (nx_menu *menu, menu_entry *me)
{
	nx_item *item = NULL;
	char *path = me->path + me->root_offset;
	int pos;

	if (path[0] != 0)
		menu = menu_find_path (menu, path);
	if (menu)
	{
		pos = me->pos;
		if (pos < 0)	/* position offset from end/bottom */
			pos = nx_menu_get_n_items (menu) + pos;
		menu_quick_sub (me->label, menu, &item, me->markup ? XCMENU_MARKUP|XCMENU_MNEMONIC : XCMENU_MNEMONIC, pos);
	}
	return item;
}

static void
menu_del_cb (nx_menu *menu, menu_entry *me, char *target)
{
	nx_item *item = menu_find (menu, me->path + me->root_offset, me->label);
	if (item)
		nx_item_remove (item);
}

static void
menu_add_cb (nx_menu *menu, menu_entry *me, char *target)
{
	nx_item *item;

	if (me->group)	/* have a group name? Must be a radio item */
		item = menu_add_radio (menu, me);
	else if (me->ucmd)	/* have unselect-cmd? Must be a toggle item */
		item = menu_add_toggle (menu, me);
	else if (me->cmd || !me->label)	/* label=NULL for separators */
		item = menu_add_item (menu, me, target);
	else
		item = menu_add_sub (menu, me);

	if (item)
	{
		nx_item_set_sensitive (item, me->enable);
		/* only the menu bar has shortcuts (popups ignore them) */
		if (me->key)
			nx_item_set_accel (item, me->key, me->modifier);
	}
}

static void menu_debug_redump (void);

char *
fe_menu_add (menu_entry *me)
{
	char *text;

	menu_foreach_gui (me, menu_add_cb);
	menu_debug_redump ();

	if (!me->markup)
		return NULL;

	if (!pango_parse_markup (me->label, -1, 0, NULL, &text, NULL, NULL))
		return NULL;

	/* return the label with markup stripped */
	return text;
}

/* tests: refresh the dump of the current main menu after /MENU changes */
static void
menu_debug_redump (void)
{
	if (!g_getenv ("NEXCHAT_MENU_DUMP"))
		return;
	if (current_sess && current_sess->gui && current_sess->gui->main_menu)
		menu_debug_dump (current_sess->gui->main_menu);
}

void
fe_menu_del (menu_entry *me)
{
	menu_foreach_gui (me, menu_del_cb);
	menu_debug_redump ();
}

void
fe_menu_update (menu_entry *me)
{
	menu_foreach_gui (me, menu_update_cb);
	menu_debug_redump ();
}

/* used to add custom menus to the right-click menu */

static void
menu_add_plugin_mainmenu_items (nx_menu *menu)
{
	GSList *list;
	menu_entry *me;

	list = menu_list;	/* outbound.c */
	while (list)
	{
		me = list->data;
		if (me->is_main)
			menu_add_cb (menu, me, NULL);
		list = list->next;
	}
}

void
menu_add_plugin_items (nx_menu *menu, char *root, char *target)
{
	GSList *list;
	menu_entry *me;

	list = menu_list;	/* outbound.c */
	while (list)
	{
		me = list->data;
		if (!me->is_main && !strncmp (me->path, root + 1, root[0]))
			menu_add_cb (menu, me, target);
		list = list->next;
	}
}

/* === END STUFF FOR /MENU === */

/* ---- menu tree dump for tests/ux/menus.sh ----
   One line per item: indent, kind, label as given (mnemonic underscore
   kept), [accel], state. Walks the nx_menu tree, so it records what the
   user gets on either GTK. */

static void
menu_dump_level (FILE *f, nx_menu *menu, int depth)
{
	int i, n = nx_menu_get_n_items (menu);

	for (i = 0; i < n; i++)
	{
		nx_item *item = nx_menu_get_item (menu, i);
		nx_item_kind k = nx_item_get_kind (item);
		const char *kind = "item", *text;
		char accel[64] = "";
		guint key;
		GdkModifierType mods;
		nx_menu *sub;

		if (k == NX_KIND_SEPARATOR)
		{
			fprintf (f, "%*s---\n", depth * 2, "");
			continue;
		}
		if (k == NX_KIND_RADIO)
			kind = "radio";
		else if (k == NX_KIND_CHECK)
			kind = "check";

		text = nx_item_get_label (item);
		nx_item_get_accel (item, &key, &mods);
		if (key)
		{
			/* not gtk_accelerator_name: GTK3 and GTK4 spell it differently */
			const GdkModifierType alt = GDK_ALT_MASK;
			g_snprintf (accel, sizeof accel, "%s%s%s%s",
							(mods & GDK_CONTROL_MASK) ? "<Primary>" : "",
							(mods & GDK_SHIFT_MASK) ? "<Shift>" : "",
							(mods & alt) ? "<Alt>" : "",
							gdk_keyval_name (gdk_keyval_to_lower (key)));
		}

		fprintf (f, "%*s%s \"%s\"%s%s%s%s\n", depth * 2, "", kind, text ? text : "",
					accel[0] ? " [" : "", accel, accel[0] ? "]" : "",
					!nx_item_get_sensitive (item) ? " insensitive" : "");
		if (k == NX_KIND_CHECK || k == NX_KIND_RADIO)
			fprintf (f, "%*s  = %s\n", depth * 2, "",
						nx_item_get_active (item) ? "on" : "off");

		sub = nx_item_get_submenu (item);
		if (sub)
			menu_dump_level (f, sub, depth + 1);
	}
}

void
menu_debug_dump (nx_menu *menu_bar)
{
	menu_debug_dump_to (menu_bar, g_getenv ("NEXCHAT_MENU_DUMP"));
}

/* tests only: NEXCHAT_MENU_ACTIVATE="Window/Friends List" chooses that
   item once the main window is up, as a click would (screenshot tests
   used to type the menu's keys, which landed differently per build) */
static gboolean
menu_debug_activate_idle (gpointer data)
{
	nx_menu *m = data;
	char **parts = g_strsplit (g_getenv ("NEXCHAT_MENU_ACTIVATE"), "/", -1);
	int i, j;

	for (i = 0; parts[i] && m; i++)
	{
		nx_item *found = NULL;
		for (j = 0; j < nx_menu_get_n_items (m); j++)
		{
			nx_item *it = nx_menu_get_item (m, j);
			const char *t = nx_item_get_text (it);
			char *plain = NULL;
			if (t)
			{
				/* match "Network List" against "Network Li_st", "Join a Channel"
				   against "_Join a Channel..." */
				GString *g = g_string_new (NULL);
				const char *c;
				for (c = t; *c; c++)
					if (*c != '_')
						g_string_append_c (g, *c);
				while (g->len && (g->str[g->len - 1] == '.' || (guchar) g->str[g->len - 1] >= 0x80))
					g_string_truncate (g, g->len - 1);
				plain = g_string_free (g, FALSE);
			}
			if (t && (!strcmp (t, parts[i]) || !g_ascii_strcasecmp (plain, parts[i])))
			{
				g_free (plain);
				found = it;
				break;
			}
			g_free (plain);
		}
		if (!found)
			break;
		if (parts[i + 1])
			m = nx_item_get_submenu (found);
		else
			nx_item_activate (found);
	}
	g_strfreev (parts);
	return G_SOURCE_REMOVE;
}

void
menu_debug_activate (nx_menu *menu_bar)
{
	static gboolean done;
	const char *path = g_getenv ("NEXCHAT_MENU_ACTIVATE");

	if (done || !path || !*path || !menu_bar)
		return;
	done = TRUE;
	g_timeout_add (1500, menu_debug_activate_idle, menu_bar);
}

void
menu_debug_dump_to (nx_menu *menu_bar, const char *path)
{
	FILE *f;

	if (!path || !*path || !menu_bar)
		return;
	f = fopen (path, "w");
	if (!f)
		return;
	menu_dump_level (f, menu_bar, 0);
	fclose (f);
}

/* the table's callbacks: void (*) (GtkWidget-or-item, gpointer); items
   with no use for the first argument get the nx_item, which they ignore */
#define TABLE_CB(i) ((nx_item_cb) mymenu[i].callback)

/* ---- shortcuts that aren't menu items ---- */

static int
sess_is_tab (session *s)
{
	return s->gui && s->gui->is_tab;
}

/* the next tab with something to read, most important first (a private
   message that highlighted you, then a highlight, then messages) */
static gboolean
win_next_unread (GtkWidget *w, GVariant *args, gpointer d)
{
	session *s = lastact_getfirst (sess_is_tab);

	(void) w; (void) args; (void) d;
	if (s && s != current_sess)
		mg_bring_tofront_sess (s);
	return TRUE;
}

static gboolean
win_tab_n (GtkWidget *w, GVariant *args, gpointer d)
{
	(void) w; (void) d;
	mg_switch_page (FALSE, g_variant_get_int32 (args));
	return TRUE;
}

static gboolean
win_tab_step (GtkWidget *w, GVariant *args, gpointer d)
{
	(void) w; (void) d;
	mg_switch_page (TRUE, g_variant_get_int32 (args));
	return TRUE;
}

static gboolean
win_jump (GtkWidget *w, GVariant *args, gpointer d)
{
	(void) w; (void) args; (void) d;
	jumpto_open ();
	return TRUE;
}

static gboolean
win_emoji (GtkWidget *w, GVariant *args, gpointer d)
{
	(void) w; (void) args; (void) d;
	if (current_sess && current_sess->gui && current_sess->gui->input_box)
	{
		GtkWidget *e = current_sess->gui->input_box;
		GtkWidget *t = GTK_IS_EDITABLE (e) ? GTK_WIDGET (gtk_editable_get_delegate (GTK_EDITABLE (e))) : NULL;

		gtk_widget_grab_focus (e);
		/* the action is the GtkText's, inside the entry */
		g_signal_emit_by_name (t ? t : e, "insert-emoji");
		if (g_getenv ("NEXCHAT_KEYS"))
		{
			GtkWidget *ch = g_object_get_data (G_OBJECT (t ? t : e), "gtk-emoji-chooser");
			g_printerr ("emoji chooser: %s\n", ch && gtk_widget_get_visible (ch) ? "open" : "not open");
		}
	}
	return TRUE;
}

static void
win_shortcut (GtkShortcutController *c, guint key, GdkModifierType mods,
				  GtkShortcutFunc f, GVariant *args)
{
	GtkShortcut *s = gtk_shortcut_new (gtk_keyval_trigger_new (key, mods),
												  gtk_callback_action_new (f, NULL, NULL));
	if (args)
		gtk_shortcut_set_arguments (s, args);
	gtk_shortcut_controller_add_shortcut (c, s);
}

static void
menu_window_shortcuts (GtkWidget *window)
{
	GtkEventController *c = gtk_shortcut_controller_new ();
	GtkShortcutController *sc = GTK_SHORTCUT_CONTROLLER (c);

	gtk_shortcut_controller_set_scope (sc, GTK_SHORTCUT_SCOPE_MANAGED);
	gtk_event_controller_set_propagation_phase (c, GTK_PHASE_CAPTURE);

	/* next unread: Alt+A is Marked Away, and Option+letters type accents on
	   a Mac, so Ctrl+Space here and Cmd+Shift+A below */
#ifndef __APPLE__
	win_shortcut (sc, GDK_KEY_space, GDK_CONTROL_MASK, win_next_unread, NULL);
#endif
	/* the emoji chooser on the input box: Cmd+Ctrl+Space as in every Mac
	   app (and Ctrl+. elsewhere, GTK's own key, wherever the focus is) */
#ifdef __APPLE__
	win_shortcut (sc, GDK_KEY_space, GDK_META_MASK | GDK_CONTROL_MASK, win_emoji, NULL);
#else
	win_shortcut (sc, GDK_KEY_period, GDK_CONTROL_MASK, win_emoji, NULL);
#endif
#ifdef __APPLE__
	{
		int i;
		/* what every Mac app with tabs does: Cmd+1..9 a tab (9 = the last
		   one in Safari; here simply the ninth), Cmd+Shift+[ ] and
		   Cmd+Option+Left/Right the previous/next tab */
		for (i = 1; i <= 9; i++)
			win_shortcut (sc, GDK_KEY_0 + i, GDK_META_MASK, win_tab_n, g_variant_new_int32 (i - 1));
		win_shortcut (sc, GDK_KEY_braceleft, GDK_META_MASK | GDK_SHIFT_MASK, win_tab_step, g_variant_new_int32 (-1));
		win_shortcut (sc, GDK_KEY_braceright, GDK_META_MASK | GDK_SHIFT_MASK, win_tab_step, g_variant_new_int32 (1));
		win_shortcut (sc, GDK_KEY_bracketleft, GDK_META_MASK | GDK_SHIFT_MASK, win_tab_step, g_variant_new_int32 (-1));
		win_shortcut (sc, GDK_KEY_bracketright, GDK_META_MASK | GDK_SHIFT_MASK, win_tab_step, g_variant_new_int32 (1));
		win_shortcut (sc, GDK_KEY_Left, GDK_META_MASK | GDK_ALT_MASK, win_tab_step, g_variant_new_int32 (-1));
		win_shortcut (sc, GDK_KEY_Right, GDK_META_MASK | GDK_ALT_MASK, win_tab_step, g_variant_new_int32 (1));
		/* Cmd+K stays "insert colour" (IRC people know it); Xcode's
		   Open Quickly and VS Code's Cmd+P for jump to */
		win_shortcut (sc, GDK_KEY_O, GDK_META_MASK | GDK_SHIFT_MASK, win_jump, NULL);
		win_shortcut (sc, GDK_KEY_o, GDK_META_MASK | GDK_SHIFT_MASK, win_jump, NULL);
		win_shortcut (sc, GDK_KEY_p, GDK_META_MASK, win_jump, NULL);
		win_shortcut (sc, GDK_KEY_A, GDK_META_MASK | GDK_SHIFT_MASK, win_next_unread, NULL);
		win_shortcut (sc, GDK_KEY_a, GDK_META_MASK | GDK_SHIFT_MASK, win_next_unread, NULL);
	}
#endif
	gtk_widget_add_controller (window, c);
}

/* bar: the window's menu bar, its shortcuts working in `window`. !bar: a
   popup copy (middle-click), shortcuts only shown. */
nx_menu *
menu_create_main (GtkWidget *window, int bar, int away, int toplevel,
						nx_item **menu_widgets)
{
	int i = 0;
	nx_item *item;
	nx_item *menu_item = NULL;
	nx_menu *menu = NULL;
	nx_menu *menu_bar;
	nx_menu *usermenu = NULL;
	nx_menu *submenu = NULL;
	nx_item *group = NULL;
	int close_mask = STATE_CTRL;
	int away_mask = STATE_ALT;
	char *key_theme = NULL;
	GtkSettings *settings;

	menu_bar = bar ? nx_menu_bar_new () : nx_menu_new ();
	/* the bar's shortcuts work in the window; the popup copy only shows them
	   (its accel group was never attached to a window) */
	nx_menu_set_accel_window (menu_bar, bar ? window : NULL);
	if (bar && window)
		menu_window_shortcuts (window);

	/* set the initial state of toggles */
	mymenu[MENUBAR_OFFSET].state = !prefs.hex_gui_hide_menu;
	mymenu[MENUBAR_OFFSET+1].state = prefs.hex_gui_topicbar;
	mymenu[MENUBAR_OFFSET+2].state = !prefs.hex_gui_ulist_hide;
	mymenu[MENUBAR_OFFSET+3].state = prefs.hex_gui_ulist_buttons;
	mymenu[MENUBAR_OFFSET+4].state = prefs.hex_gui_mode_buttons;

	mymenu[AWAY_OFFSET].state = away;

	switch (prefs.hex_gui_tab_layout)
	{
	case 0:
		mymenu[TABS_OFFSET].state = 1;
		mymenu[TABS_OFFSET+1].state = 0;
		break;
	default:
		mymenu[TABS_OFFSET].state = 0;
		mymenu[TABS_OFFSET+1].state = 1;
	}

	mymenu[METRE_OFFSET].state = 0;
	mymenu[METRE_OFFSET+1].state = 0;
	mymenu[METRE_OFFSET+2].state = 0;
	mymenu[METRE_OFFSET+3].state = 0;
	switch (prefs.hex_gui_lagometer)
	{
	case 0:
		mymenu[METRE_OFFSET].state = 1;
		break;
	case 1:
		mymenu[METRE_OFFSET+1].state = 1;
		break;
	case 2:
		mymenu[METRE_OFFSET+2].state = 1;
		break;
	default:
		mymenu[METRE_OFFSET+3].state = 1;
	}

	/* change Close binding to ctrl-shift-w when using emacs keys (GTK4 has
	   no key themes) */
	settings = gtk_settings_get_default ();
	(void) settings; (void) key_theme;

	/* Away binding to ctrl-alt-a if the _Help menu conflicts (FR/PT/IT) */
	{
		char *help = _("_Help");
		char *under = strchr (help, '_');
		if (under && (under[1] == 'a' || under[1] == 'A'))
			away_mask = STATE_ALT | STATE_CTRL;
	}

	if (!toplevel)
	{
		mymenu[DETACH_OFFSET].text = N_("_Detach");
		mymenu[CLOSE_OFFSET].text = N_("_Close");
	}
	else
	{
		mymenu[DETACH_OFFSET].text = N_("_Attach");
		mymenu[CLOSE_OFFSET].text = N_("_Close");
	}

	while (1)
	{
		nx_menu *target = submenu ? submenu : menu;

		item = NULL;
		if (mymenu[i].id == MENU_ID_USERMENU && !prefs.hex_gui_usermenu)
		{
			i++;
			continue;
		}

		switch (mymenu[i].type)
		{
		case M_NEWMENU:
			menu = nx_menu_add_submenu (menu_bar, NX_MENU_APPEND, _(mymenu[i].text),
												 NX_ITEM_MNEMONIC, &menu_item);
			/* record the English name for /menu */
			nx_item_set_data (menu_item, "name", mymenu[i].text);
			if (mymenu[i].id == MENU_ID_USERMENU)
				usermenu = menu;
			item = menu_item;	/* usermenu_update finds the submenu from it */
			break;

		case M_MENUPIX:
			item = nx_menu_add_item_pixbuf (target, NX_MENU_APPEND, _(mymenu[i].text),
													  NX_ITEM_MNEMONIC, *(GdkPixbuf **) mymenu[i].image,
													  TABLE_CB (i), NULL);
			goto normalitem;

		case M_MENUSTOCK:
			item = nx_menu_add_item (target, NX_MENU_APPEND, _(mymenu[i].text), NX_ITEM_MNEMONIC,
											 mymenu[i].image, TABLE_CB (i), NULL);
			goto normalitem;

		case M_MENUITEM:
			item = nx_menu_add_item (target, NX_MENU_APPEND, _(mymenu[i].text), NX_ITEM_MNEMONIC,
											 NULL, TABLE_CB (i), NULL);
normalitem:
			if (mymenu[i].key != 0)
				nx_item_set_accel (item, mymenu[i].key,
										 mymenu[i].key == GDK_KEY_F1 ? 0 :
										 mymenu[i].key == GDK_KEY_w ? close_mask :
										 (g_ascii_isupper (mymenu[i].key)) ?
											STATE_SHIFT | STATE_CTRL :
											STATE_CTRL);
			break;

		case M_MENUTOG:
			/* the state is set without running the callback, as before */
			item = nx_menu_add_check (target, NX_MENU_APPEND, _(mymenu[i].text), NX_ITEM_MNEMONIC,
											  mymenu[i].state, TABLE_CB (i), NULL);
			goto togitem;

		case M_MENURADIO:
			item = nx_menu_add_radio (target, NX_MENU_APPEND, _(mymenu[i].text), NX_ITEM_MNEMONIC,
											  group, mymenu[i].state, TABLE_CB (i), NULL);
			if (!group)
				group = item;
togitem:
			if (mymenu[i].key != 0)
				nx_item_set_accel (item, mymenu[i].key,
										 mymenu[i].id == MENU_ID_FULLSCREEN ? 0 :
										 mymenu[i].id == MENU_ID_AWAY ? away_mask :
										 STATE_CTRL);
			nx_item_set_sensitive (item, mymenu[i].sensitive);
			break;

		case M_SEP:
			item = nx_menu_add_separator (menu, NX_MENU_APPEND);
			break;

		case M_MENUSUB:
			group = NULL;
			/* laid out as an icon item, as GTK2's image item was (an empty
			   icon column for the image-less ones) */
			submenu = nx_menu_add_submenu_icon (menu, NX_MENU_APPEND, _(mymenu[i].text),
															mymenu[i].image, &item);
			/* record the English name for /menu */
			nx_item_set_data (item, "name", mymenu[i].text);
			break;

		/*case M_END:*/ default:
			if (!submenu)
			{
				if (menu)
					menu_add_plugin_mainmenu_items (menu_bar);
				if (usermenu)
					usermenu_create (usermenu);
				return (menu_bar);
			}
			submenu = NULL;
		}

		/* record this item so its state might be changed later */
		if (mymenu[i].id != 0 && menu_widgets)
			/* this ends up in sess->gui->menu_item[MENU_ID_XXX] */
			menu_widgets[mymenu[i].id] = item;

		i++;
	}
}
