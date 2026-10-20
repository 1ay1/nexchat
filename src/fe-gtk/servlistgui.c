/* X-Chat
 * Copyright (C) 2004-2008 Peter Zelezny.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>


#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/servlist.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/util.h"

#include "fe-gtk.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "menu.h"
#include "pixmaps.h"
#include "fkeys.h"
#include "bncsetup.h"
#include "bncwin.h"

#define SERVLIST_X_PADDING 4			/* horizontal paddig in the network editor */
#define SERVLIST_Y_PADDING 0			/* vertical padding in the network editor */

#ifdef USE_OPENSSL
# define DEFAULT_SERVER "newserver/6697"
#else
# define DEFAULT_SERVER "newserver/6667"
#endif

/* servlistgui.c globals */
static GtkWidget *serverlist_win = NULL;
static GtkWidget *networks_tree;		/* network TreeView */
static GtkWidget *netfilter_entry;		/* "Find a network" */
static char *netfilter;					/* what's typed there */
static void servlist_network_row_cb (GtkWidget *view, gpointer user_data);
static void servlist_connect_cb (GtkWidget *button, gpointer userdata);
static void servlist_networks_populate (GtkWidget *treeview, GSList *netlist);

static int netlist_win_width = 0;		/* don't hardcode pixels, just use as much as needed by default, save if resized */
static int netlist_win_height = 0;
static int netedit_win_width = 0;
static int netedit_win_height = 0;

static int netedit_active_tab = 0;

/* global user info */
static GtkWidget *entry_nick1;
static GtkWidget *entry_nick2;
static GtkWidget *entry_nick3;
static GtkWidget *entry_guser;
/* static GtkWidget *entry_greal; */

enum {
		SERVER_TREE,
		CHANNEL_TREE,
		CMD_TREE,
		N_TREES,
};

/* edit area */
static GtkWidget *edit_win;
static GtkWidget *edit_entry_nick;
static GtkWidget *edit_entry_nick2;
static GtkWidget *edit_entry_user;
static GtkWidget *edit_entry_real;
static GtkWidget *edit_entry_pass;
static GtkWidget *edit_label_nick;
static GtkWidget *edit_label_nick2;
static GtkWidget *edit_label_real;
static GtkWidget *edit_label_user;
static GtkWidget *edit_trees[N_TREES];

static ircnet *selected_net = NULL;
static ircserver *selected_serv = NULL;
static commandentry *selected_cmd = NULL;
static favchannel *selected_chan = NULL;
static session *servlist_sess;

static void servlist_network_row_cb (GtkWidget *view, gpointer user_data);
static GtkWidget *servlist_open_edit (GtkWidget *parent, ircnet *net);


static const char *pages[]=
{
	IRC_DEFAULT_CHARSET,
	"CP1252 (Windows-1252)",
	"ISO-8859-15 (Western Europe)",
	"ISO-8859-2 (Central Europe)",
	"ISO-8859-7 (Greek)",
	"ISO-8859-8 (Hebrew)",
	"ISO-8859-9 (Turkish)",
	"ISO-2022-JP (Japanese)",
	"SJIS (Japanese)",
	"CP949 (Korean)",
	"KOI8-R (Cyrillic)",
	"CP1251 (Cyrillic)",
	"CP1256 (Arabic)",
	"CP1257 (Baltic)",
	"GB18030 (Chinese)",
	"TIS-620 (Thai)",
	NULL
};

/* This is our dictionary for authentication types. Keep these in sync with
 * login_types[]! This allows us to re-order the login type dropdown in the
 * network list without breaking config compatibility.
 *
 * Also make sure inbound_nickserv_login() won't break, i.e. if you add a new
 * type that is NickServ-based, add it there as well so that HexChat knows to
 * treat it as such.
 */
static int login_types_conf[] =
{
	LOGIN_DEFAULT,			/* default entry - we don't use this but it makes indexing consistent with login_types[] so it's nice */
	LOGIN_SASL,
#ifdef USE_OPENSSL
	LOGIN_SASLEXTERNAL,
	LOGIN_SASL_SCRAM_SHA_1,
	LOGIN_SASL_SCRAM_SHA_256,
	LOGIN_SASL_SCRAM_SHA_512,
#endif
	LOGIN_PASS,
	LOGIN_MSG_NICKSERV,
	LOGIN_NICKSERV,
#ifdef USE_OPENSSL
	LOGIN_CHALLENGEAUTH,
#endif
	LOGIN_CUSTOM
#if 0
	LOGIN_NS,
	LOGIN_MSG_NS,
	LOGIN_AUTH,
#endif
};

static const char *login_types[]=
{
	"Default",
	"SASL PLAIN (username + password)",
#ifdef USE_OPENSSL
	"SASL EXTERNAL (cert)",
	"SASL SCRAM-SHA-1",
	"SASL SCRAM-SHA-256",
	"SASL SCRAM-SHA-512",
#endif
	"Server password (/PASS password)",
	"NickServ (/MSG NickServ + password)",
	"NickServ (/NICKSERV + password)",
#ifdef USE_OPENSSL
	"Challenge Auth (username + password)",
#endif
	"Custom... (connect commands)",
#if 0
	"NickServ (/NS + password)",
	"NickServ (/MSG NS + password)",
	"AUTH (/AUTH nickname password)",
#endif
	NULL
};

/* poor man's IndexOf() - find the dropdown string index that belongs to the given config value */
static int
servlist_get_login_desc_index (int conf_value)
{
	int i;
	int length = sizeof (login_types_conf) / sizeof (login_types_conf[0]);		/* the number of elements in the conf array */

	for (i = 0; i < length; i++)
	{
		if (login_types_conf[i] == conf_value)
		{
			return i;
		}
	}

	return 0;	/* make the compiler happy */
}

static void
servlist_select_and_show (GtkWidget *view, nx_row *row)
{
	/* select this row and make sure it's visible */
	nx_view_select (view, row);
	nx_view_scroll_to (view, row, NX_SCROLL_CENTER);
	nx_view_set_cursor (view, row, NULL);
}

static void
servlist_channels_populate (ircnet *net, GtkWidget *treeview)
{
	nx_store *store;
	nx_row *row;
	int i;
	favchannel *favchan;
	GSList *list = net->favchanlist;

	store = nx_view_get_store (treeview);
	nx_store_clear (store);

	i = 0;
	while (list)
	{
		favchan = list->data;
		row = nx_store_append (store, NULL);
		nx_row_set (row, 0, favchan->name, 1, favchan->key, 2, TRUE, -1);

		if (net->selected == i)
		{
			/* select this server */
			servlist_select_and_show (treeview, row);
		}

		i++;
		list = list->next;
	}
}

static void
servlist_servers_populate (ircnet *net, GtkWidget *treeview)
{
	nx_store *store;
	nx_row *row;
	int i;
	ircserver *serv;
	GSList *list = net->servlist;

	store = nx_view_get_store (treeview);
	nx_store_clear (store);

	i = 0;
	while (list)
	{
		serv = list->data;
		row = nx_store_append (store, NULL);
		nx_row_set (row, 0, serv->hostname, 1, TRUE, -1);

		if (net->selected == i)
		{
			/* select this server */
			servlist_select_and_show (treeview, row);
		}

		i++;
		list = list->next;
	}
}

static void
servlist_commands_populate (ircnet *net, GtkWidget *treeview)
{
	nx_store *store;
	nx_row *row;
	int i;
	commandentry *entry;
	GSList *list = net->commandlist;

	store = nx_view_get_store (treeview);
	nx_store_clear (store);

	i = 0;
	while (list)
	{
		entry = list->data;
		row = nx_store_append (store, NULL);
		nx_row_set (row, 0, entry->command, 1, TRUE, -1);

		if (net->selected == i)
		{
			/* select this server */
			servlist_select_and_show (treeview, row);
		}

		i++;
		list = list->next;
	}
}

static void
servlist_networks_populate_ (GtkWidget *treeview, GSList *netlist, gboolean favorites)
{
	nx_store *store;
	nx_row *row;
	int i;
	ircnet *net;

	if (!netlist)
	{
		net = servlist_net_add (_("New Network"), "", FALSE);
		servlist_server_add (net, DEFAULT_SERVER);
		netlist = network_list;
	}
	store = nx_view_get_store (treeview);
	nx_store_clear (store);

	i = 0;
	{
	nx_row *first = NULL;
	gboolean chosen = FALSE;
	while (netlist)
	{
		net = netlist->data;
		if ((!favorites || (net->flags & FLAG_FAVORITE)) &&
			 (!netfilter || !*netfilter || nocasestrstr (net->name, netfilter)))
		{
			row = nx_store_append (store, NULL);
			if (!first)
				first = row;
			if (favorites)
				nx_row_set (row, 0, net->name, 1, TRUE, 2, 400, -1);
			else
				nx_row_set (row, 0, net->name, 1, TRUE, 2, (net->flags & FLAG_FAVORITE) ? 800 : 400, -1);
			if (i == prefs.hex_gui_slist_select && !(netfilter && *netfilter))
			{
				/* select this network */
				servlist_select_and_show (treeview, row);
				selected_net = net;
				chosen = TRUE;
			}
		}
		i++;
		netlist = netlist->next;
	}
	/* filtering: the best match, the first one, is the selected one */
	if (!chosen && first)
	{
		servlist_select_and_show (treeview, first);
		servlist_network_row_cb (treeview, NULL);
	}
	}
}

/* the "Find a network" box */
static void
servlist_filter_changed (GtkSearchEntry *e, gpointer d)
{
	g_free (netfilter);
	netfilter = g_strdup (gtk_editable_get_text (GTK_EDITABLE (e)));
	servlist_networks_populate (networks_tree, network_list);
}

static void
servlist_filter_activate (GtkSearchEntry *e, gpointer d)
{
	/* only what's shown and selected: nothing matched, nothing happens */
	if (selected_net && nx_view_get_selected (networks_tree))
		servlist_connect_cb (NULL, NULL);
}

/* Up/Down (Ctrl+G) from the box move through what's listed */
static void
servlist_filter_move (GtkSearchEntry *e, gpointer d)
{
	nx_row *row = nx_view_get_selected (networks_tree);
	nx_row *to = row ? (GPOINTER_TO_INT (d) > 0 ? nx_row_next (row) : nx_row_prev (row)) : NULL;

	if (to)
	{
		servlist_select_and_show (networks_tree, to);
		servlist_network_row_cb (networks_tree, NULL);
	}
}

static void
servlist_networks_populate (GtkWidget *treeview, GSList *netlist)
{
	servlist_networks_populate_ (treeview, netlist, prefs.hex_gui_slist_fav);
}

static void
servlist_server_row_cb (GtkWidget *view, gpointer user_data)
{
	nx_row *row;
	ircserver *serv;
	int pos;

	if (!selected_net)
		return;

	row = nx_view_get_selected (view);
	if (row)
	{
		serv = servlist_server_find (selected_net, (char *) nx_row_peek_string (row, 0), &pos);
		if (serv)
			selected_net->selected = pos;
		selected_serv = serv;
	}
}

static void
servlist_command_row_cb (GtkWidget *view, gpointer user_data)
{
	nx_row *row;
	commandentry *cmd;
	int pos;

	if (!selected_net)
		return;

	row = nx_view_get_selected (view);
	if (row)
	{
		cmd = servlist_command_find (selected_net, (char *) nx_row_peek_string (row, 0), &pos);
		if (cmd)
			selected_net->selected = pos;
		selected_cmd = cmd;
	}
}

static void
servlist_channel_row_cb (GtkWidget *view, gpointer user_data)
{
	nx_row *row;
	favchannel *channel;
	int pos;

	if (!selected_net)
		return;

	row = nx_view_get_selected (view);
	if (row)
	{
		channel = servlist_favchan_find (selected_net, (char *) nx_row_peek_string (row, 0), &pos);
		if (channel)
			selected_net->selected = pos;
		selected_chan = channel;
	}
}

static void
servlist_start_editing (GtkWidget *view)
{
	nx_row *row = nx_view_get_selected (view);

	if (row)
		nx_view_set_cursor (view, row, nx_view_get_column (view, 0));
}

static void
servlist_addserver (void)
{
	GtkWidget *view = edit_trees[SERVER_TREE];
	nx_row *row;

	if (!selected_net)
		return;

	servlist_server_add (selected_net, DEFAULT_SERVER);

	row = nx_store_append (nx_view_get_store (view), NULL);
	nx_row_set (row, 0, DEFAULT_SERVER, 1, TRUE, -1);

	/* select this server */
	servlist_select_and_show (view, row);
	servlist_start_editing (view);

	servlist_server_row_cb (view, NULL);
}

static void
servlist_addcommand (void)
{
	GtkWidget *view = edit_trees[CMD_TREE];
	nx_row *row;

	if (!selected_net)
		return;

	servlist_command_add (selected_net, "ECHO hello");

	row = nx_store_append (nx_view_get_store (view), NULL);
	nx_row_set (row, 0, "ECHO hello", 1, TRUE, -1);

	servlist_select_and_show (view, row);
	servlist_start_editing (view);

	servlist_command_row_cb (view, NULL);
}

static void
servlist_addchannel (void)
{
	GtkWidget *view = edit_trees[CHANNEL_TREE];
	nx_row *row;

	if (!selected_net)
		return;

	servlist_favchan_add (selected_net, "#channel");

	row = nx_store_append (nx_view_get_store (view), NULL);
	nx_row_set (row, 0, "#channel", 1, "", 2, TRUE, -1);

	/* select this server */
	servlist_select_and_show (view, row);
	servlist_start_editing (view);

	servlist_channel_row_cb (view, NULL);
}

static void
servlist_addnet_cb (GtkWidget *item, gpointer userdata)
{
	nx_row *row;
	ircnet *net;

	net = servlist_net_add (_("New Network"), "", TRUE);
	net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
	servlist_server_add (net, DEFAULT_SERVER);

	row = nx_store_prepend (nx_view_get_store (networks_tree), NULL);
	nx_row_set (row, 0, net->name, 1, TRUE, 2, 400, -1);

	/* select this network */
	servlist_select_and_show (networks_tree, row);
	servlist_start_editing (networks_tree);

	servlist_network_row_cb (networks_tree, NULL);
}

static void
servlist_deletenetwork (ircnet *net)
{
	nx_store *store = nx_view_get_store (networks_tree);
	nx_row *row;

	/* remove from GUI */
	row = nx_view_get_selected (networks_tree);
	if (row)
		nx_row_remove (row);

	/* remove from list */
	servlist_net_remove (net);

	/* force something to be selected */
	row = nx_store_first (store);
	if (row)
		servlist_select_and_show (networks_tree, row);
	servlist_network_row_cb (networks_tree, NULL);
}

static void
servlist_deletenetdialog_cb (GtkWidget *dialog, gint arg1, ircnet *net)
{
	nx_widget_destroy (GTK_WIDGET (dialog));
	if (arg1 == NX_RESPONSE_OK)
		servlist_deletenetwork (net);
}

static GSList *
servlist_move_item (GtkWidget *view, GSList *list, gpointer item, int delta)
{
	nx_row *row, *other;
	int pos;

	/* keep the view in sync with the list */
	row = nx_view_get_selected (view);
	if (!row)
		return list;
	other = delta == 1 ? nx_row_next (row) : nx_row_prev (row);

	pos = g_slist_index (list, item);
	if (pos >= 0)
	{
		pos += delta;
		if (pos >= 0)
		{
			list = g_slist_remove (list, item);
			list = g_slist_insert (list, item, pos);

			if (other)
			{
				nx_row_swap (row, other);
				/* the moved row stays selected, so it can be moved on */
				nx_view_set_cursor (view, row, NULL);
			}
		}
	}
	
	return list;
}

static gboolean
servlist_net_keypress_cb (GtkWidget *wid, guint keyval, GdkModifierType state, gpointer tree)
{
	gboolean handled = FALSE;
	
	if (!selected_net || prefs.hex_gui_slist_fav)
		return FALSE;

	if (state & STATE_SHIFT)
	{
		if (keyval == GDK_KEY_Up)
		{
			handled = TRUE;
			network_list = servlist_move_item (tree, network_list, selected_net, -1);
		}
		else if (keyval == GDK_KEY_Down)
		{
			handled = TRUE;
			network_list = servlist_move_item (tree, network_list, selected_net, +1);
		}
	}

	return handled;
}

static gint
servlist_compare (ircnet *net1, ircnet *net2)
{
	gchar *net1_casefolded, *net2_casefolded;
	int result=0;

	net1_casefolded=g_utf8_casefold(net1->name,-1),
	net2_casefolded=g_utf8_casefold(net2->name,-1),

	result=g_utf8_collate(net1_casefolded,net2_casefolded);

	g_free(net1_casefolded);
	g_free(net2_casefolded);

	return result;

}

static void
servlist_sort (GtkWidget *button, gpointer none)
{
	network_list=g_slist_sort(network_list,(GCompareFunc)servlist_compare);
	servlist_networks_populate (networks_tree, network_list);
}

static gboolean
servlist_has_selection (GtkWidget *view)
{
	/* make sure something is selected */
	return nx_view_get_selected (view) != NULL;
}

static void
servlist_favor (GtkWidget *button, gpointer none)
{
	nx_row *row;

	if (!selected_net)
		return;

	row = nx_view_get_selected (networks_tree);
	if (row)
	{
		if (selected_net->flags & FLAG_FAVORITE)
		{
			nx_row_set (row, 2, 400, -1);
			selected_net->flags &= ~FLAG_FAVORITE;
		}
		else
		{
			nx_row_set (row, 2, 800, -1);
			selected_net->flags |= FLAG_FAVORITE;
		}
	}
}

static void
servlist_update_from_entry (char **str, GtkWidget *entry)
{
	g_free (*str);

	if (nx_entry_get_text (entry)[0] == 0)
		*str = NULL;
	else
		*str = g_strdup (nx_entry_get_text (entry));
}

static void
servlist_edit_update (ircnet *net)
{
	servlist_update_from_entry (&net->nick, edit_entry_nick);
	servlist_update_from_entry (&net->nick2, edit_entry_nick2);
	servlist_update_from_entry (&net->user, edit_entry_user);
	servlist_update_from_entry (&net->real, edit_entry_real);
	servlist_update_from_entry (&net->pass, edit_entry_pass);
}

static void
servlist_edit_close_cb (GtkWidget *button, gpointer userdata)
{
	if (selected_net)
		servlist_edit_update (selected_net);

	nx_widget_destroy (edit_win);
	edit_win = NULL;
}

static gboolean
servlist_editwin_delete_cb (GtkWidget *win, gpointer none)
{
	servlist_edit_close_cb (NULL, NULL);
	return FALSE;
}

static void
servlist_configure_cb (GtkWindow *win, gpointer none)
{
	/* remember the window size */
	nx_window_get_size (win, &netlist_win_width, &netlist_win_height);
}

static void
servlist_edit_configure_cb (GtkWindow *win, gpointer none)
{
	/* remember the window size */
	nx_window_get_size (win, &netedit_win_width, &netedit_win_height);
}

static void
servlist_edit_cb (GtkWidget *but, gpointer none)
{
	if (!servlist_has_selection (networks_tree))
		return;

	edit_win = servlist_open_edit (serverlist_win, selected_net);
	gtkutil_set_icon (edit_win);
	servlist_servers_populate (selected_net, edit_trees[SERVER_TREE]);
	servlist_channels_populate (selected_net, edit_trees[CHANNEL_TREE]);
	servlist_commands_populate (selected_net, edit_trees[CMD_TREE]);
	nx_on_close_request (edit_win, servlist_editwin_delete_cb, 0);
	nx_on_window_configure (edit_win, servlist_edit_configure_cb, 0);
	nx_widget_show (edit_win);
}

static void
servlist_deletenet_cb (GtkWidget *item, ircnet *net)
{
	GtkWidget *dialog;

	if (!servlist_has_selection (networks_tree))
		return;

	net = selected_net;
	if (!net)
		return;
	{
		char *q = g_strdup_printf (_("Really remove network \"%s\" and all its servers?"), net->name);
		dialog = nx_message_dialog_new (GTK_WINDOW (serverlist_win), TRUE, NX_MSG_QUESTION,
												  NX_BUTTONS_OK_CANCEL, FALSE, q);
		g_free (q);
	}
	g_signal_connect (dialog, "response",
							G_CALLBACK (servlist_deletenetdialog_cb), net);
	nx_window_set_position_mouse (dialog);
	nx_widget_show (dialog);
}

static void
servlist_deleteserver (ircserver *serv)
{
	nx_row *row;

	/* don't remove the last server */
	if (selected_net && g_slist_length (selected_net->servlist) < 2)
		return;

	/* remove from GUI */
	row = nx_view_get_selected (edit_trees[SERVER_TREE]);
	if (row)
		nx_row_remove (row);

	/* remove from list */
	if (selected_net)
		servlist_server_remove (selected_net, serv);
}

static void
servlist_editbutton_cb (GtkWidget *item, GtkNotebook *notebook)
{
	servlist_start_editing (edit_trees[gtk_notebook_get_current_page(notebook)]);
}

static void
servlist_deleteserver_cb (void)
{
	nx_row *row;
	ircserver *serv;
	int pos;

	/* find the selected item in the GUI */
	row = nx_view_get_selected (edit_trees[SERVER_TREE]);
	if (row)
	{
		serv = servlist_server_find (selected_net, (char *) nx_row_peek_string (row, 0), &pos);
		if (serv)
		{
			servlist_deleteserver (serv);
		}
	}
}

static void
servlist_deletecommand (commandentry *entry)
{
	nx_row *row;

	/* remove from GUI */
	row = nx_view_get_selected (edit_trees[CMD_TREE]);
	if (row)
	{
		nx_row_remove (row);
	}

	/* remove from list */
	if (selected_net)
	{
		servlist_command_remove (selected_net, entry);
	}
}

static void
servlist_deletecommand_cb (void)
{
	nx_row *row;
	commandentry *entry;
	int pos;

	/* find the selected item in the GUI */
	row = nx_view_get_selected (edit_trees[CMD_TREE]);
	if (row)
	{
		entry = servlist_command_find (selected_net, (char *) nx_row_peek_string (row, 0), &pos);
		if (entry)
		{
			servlist_deletecommand (entry);
		}
	}
}

static void
servlist_deletechannel (favchannel *favchan)
{
	nx_row *row;

	/* remove from GUI */
	row = nx_view_get_selected (edit_trees[CHANNEL_TREE]);
	if (row)
	{
		nx_row_remove (row);
	}

	/* remove from list */
	if (selected_net)
	{
		servlist_favchan_remove (selected_net, favchan);
	}
}

static void
servlist_deletechannel_cb (void)
{
	nx_row *row;
	favchannel *favchan;
	int pos;

	/* find the selected item in the GUI */
	row = nx_view_get_selected (edit_trees[CHANNEL_TREE]);
	if (row)
	{
		favchan = servlist_favchan_find (selected_net, (char *) nx_row_peek_string (row, 0), &pos);
		if (favchan)
		{
			servlist_deletechannel (favchan);
		}
	}
}

static ircnet *
servlist_find_selected_net (GtkWidget *view)
{
	nx_row *row;
	int pos;
	ircnet *net = NULL;

	row = nx_view_get_selected (view);
	if (row)
	{
		net = servlist_net_find ((char *) nx_row_peek_string (row, 0), &pos, strcmp);
		if (net)
			prefs.hex_gui_slist_select = pos;
	}

	return net;
}

static void
servlist_network_row_cb (GtkWidget *view, gpointer user_data)
{
	ircnet *net;

	selected_net = NULL;

	net = servlist_find_selected_net (view);
	if (net)
		selected_net = net;
}

static int
servlist_savegui (void)
{
	char *sp;
	const char *nick1, *nick2;

	/* check for blank username, ircd will not allow this */
	if (nx_entry_get_text (entry_guser)[0] == 0)
		return 1;

	/* if (nx_entry_get_text (entry_greal)[0] == 0)
		return 1; */

	nick1 = nx_entry_get_text (entry_nick1);
	nick2 = nx_entry_get_text (entry_nick2);

	/* ensure unique nicknames */
	if (!rfc_casecmp (nick1, nick2))
		return 2;

	safe_strcpy (prefs.hex_irc_nick1, nick1, sizeof(prefs.hex_irc_nick1));
	safe_strcpy (prefs.hex_irc_nick2, nick2, sizeof(prefs.hex_irc_nick2));
	safe_strcpy (prefs.hex_irc_nick3, nx_entry_get_text (entry_nick3), sizeof(prefs.hex_irc_nick3));
	safe_strcpy (prefs.hex_irc_user_name, nx_entry_get_text (entry_guser), sizeof(prefs.hex_irc_user_name));
	sp = strchr (prefs.hex_irc_user_name, ' ');
	if (sp)
		sp[0] = 0;	/* spaces will break the login */
	/* strcpy (prefs.hex_irc_real_name, nx_entry_get_text (entry_greal)); */
	servlist_save ();
	save_config (); /* For nicks stored in hexchat.conf */

	return 0;
}

static void
servlist_addbutton_cb (GtkWidget *item, GtkNotebook *notebook)
{
		switch (gtk_notebook_get_current_page (notebook))
		{
				case SERVER_TREE:
						servlist_addserver ();
						break;
				case CHANNEL_TREE:
						servlist_addchannel ();
						break;
				case CMD_TREE:
						servlist_addcommand ();
						break;
				default:
						break;
		}
}

static void
servlist_deletebutton_cb (GtkWidget *item, GtkNotebook *notebook)
{
		switch (gtk_notebook_get_current_page (notebook))
		{
				case SERVER_TREE:
						servlist_deleteserver_cb ();
						break;
				case CHANNEL_TREE:
						servlist_deletechannel_cb ();
						break;
				case CMD_TREE:
						servlist_deletecommand_cb ();
						break;
				default:
						break;
		}
}

static gboolean
servlist_keypress_cb (GtkWidget *wid, guint keyval, GdkModifierType state, gpointer userdata)
{
	GtkNotebook *notebook = userdata;
	gboolean handled = FALSE;
	int delta = 0;
	
	if (!selected_net)
		return FALSE;

	if (state & STATE_SHIFT)
	{
		if (keyval == GDK_KEY_Up)
		{
			handled = TRUE;
			delta = -1;
		}
		else if (keyval == GDK_KEY_Down)
		{
			handled = TRUE;
			delta = +1;
		}
	}
	
	if (handled)
	{
		switch (gtk_notebook_get_current_page (notebook))
		{
			case SERVER_TREE:
				if (selected_serv)
					selected_net->servlist = servlist_move_item (edit_trees[SERVER_TREE], 
																selected_net->servlist, selected_serv, delta);
				break;
			case CHANNEL_TREE:
				if (selected_chan)
					selected_net->favchanlist = servlist_move_item (edit_trees[CHANNEL_TREE], 
																	selected_net->favchanlist, selected_chan, delta);
				break;
			case CMD_TREE:
				if (selected_cmd)
					selected_net->commandlist = servlist_move_item (edit_trees[CMD_TREE], 
																	selected_net->commandlist, selected_cmd, delta);
				break;
		}
	}
	
	return handled;
}

void
servlist_autojoinedit (ircnet *net, char *channel, gboolean add)
{
	favchannel *fav;

	if (add)
	{
		servlist_favchan_add (net, channel);
		servlist_save ();
	}
	else
	{
		fav = servlist_favchan_find (net, channel, NULL);
		if (fav)
		{
			servlist_favchan_remove (net, fav);
			servlist_save ();
		}
	}
}

static void
servlist_toggle_global_user (gboolean sensitive)
{
	gtk_widget_set_sensitive (edit_entry_nick, sensitive);
	gtk_widget_set_sensitive (edit_label_nick, sensitive);

	gtk_widget_set_sensitive (edit_entry_nick2, sensitive);
	gtk_widget_set_sensitive (edit_label_nick2, sensitive);

	gtk_widget_set_sensitive (edit_entry_user, sensitive);
	gtk_widget_set_sensitive (edit_label_user, sensitive);

	gtk_widget_set_sensitive (edit_entry_real, sensitive);
	gtk_widget_set_sensitive (edit_label_real, sensitive);
}

static void
servlist_connect_cb (GtkWidget *button, gpointer userdata)
{
	int servlist_err;

	if (!selected_net)
		return;

	servlist_err = servlist_savegui ();
	if (servlist_err == 1)
	{
		fe_message (_("User name cannot be left blank."), FE_MSG_ERROR);
		return;
	}

 	if (!is_session (servlist_sess))
		servlist_sess = NULL;	/* open a new one */

	{
		GSList *list;
		session *sess;
		session *chosen = servlist_sess;

		servlist_sess = NULL;	/* open a new one */

		for (list = sess_list; list; list = list->next)
		{
			sess = list->data;
			if (sess->server->network == selected_net)
			{
				servlist_sess = sess;
				if (sess->server->connected)
					servlist_sess = NULL;	/* open a new one */
				break;
			}
		}

		/* use the chosen one, if it's empty */
		if (!servlist_sess &&
			  chosen &&
			 !chosen->server->connected &&
			  chosen->server->server_session->channel[0] == 0)
		{
			servlist_sess = chosen;
		}
	}

	servlist_connect (servlist_sess, selected_net, TRUE);

	nx_widget_destroy (serverlist_win);
	serverlist_win = NULL;
	selected_net = NULL;
}

static void
servlist_stayonline_cb (GtkWidget *button, gpointer userdata)
{
	/* the selected network's address and login are the starting point */
	if (selected_net)
		servlist_savegui ();
	bncwin_open (GTK_WINDOW (serverlist_win), selected_net);
}

static void
servlist_celledit_cb (nx_row *row, const char *arg2, gpointer user_data)
{
	char *netname;
	ircnet *net;

	if (!arg2)
		return;

	net = servlist_net_find ((char *) nx_row_peek_string (row, 0), NULL, strcmp);
	if (net)
	{
		/* delete empty item */
		if (arg2[0] == 0)
		{
			servlist_deletenetwork (net);
			return;
		}

		netname = net->name;
		net->name = g_strdup (arg2);
		nx_row_set (row, 0, net->name, -1);
		g_free (netname);
	}
}

static void
servlist_check_cb (GtkWidget *but, gpointer num_p)
{
	int num = GPOINTER_TO_INT (num_p);

	if (!selected_net)
		return;

	if ((1 << num) == FLAG_CYCLE || (1 << num) == FLAG_USE_PROXY)
	{
		/* these ones are reversed, so it's compat with 2.0.x */
		if (nx_toggle_get_active ((GtkWidget *) (but)))
			selected_net->flags &= ~(1 << num);
		else
			selected_net->flags |= (1 << num);
	} else
	{
		if (nx_toggle_get_active ((GtkWidget *) (but)))
			selected_net->flags |= (1 << num);
		else
			selected_net->flags &= ~(1 << num);
	}

	if ((1 << num) == FLAG_USE_GLOBAL)
	{
		servlist_toggle_global_user (!nx_toggle_get_active ((GtkWidget *) (but)));
	}
}

static GtkWidget *
servlist_create_check (int num, int state, GtkWidget *table, int row, int col, char *labeltext)
{
	GtkWidget *but;

	but = gtk_check_button_new_with_label (labeltext);
	nx_toggle_set_active ((GtkWidget *) (but), state);
	g_signal_connect (G_OBJECT (but), "toggled",
							G_CALLBACK (servlist_check_cb), GINT_TO_POINTER (num));
	gtkutil_grid_attach (table, but, col, col+2, row, row+1, GU_FILL|GU_EXPAND, 0, SERVLIST_X_PADDING, SERVLIST_Y_PADDING);
	nx_widget_show (but);

	return but;
}

static GtkWidget *
servlist_create_entry (GtkWidget *table, char *labeltext, int row,
							  char *def, GtkWidget **label_ret, char *tip)
{
	GtkWidget *label, *entry;

	label = gtk_label_new_with_mnemonic (labeltext);
	if (label_ret)
		*label_ret = label;
	nx_widget_show (label);
	gtkutil_grid_attach (table, label, 0, 1, row, row+1, GU_FILL, 0, SERVLIST_X_PADDING, SERVLIST_Y_PADDING);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	entry = gtk_entry_new ();
	gtk_widget_set_tooltip_text (entry, tip);
	nx_widget_show (entry);
	nx_entry_set_text (entry, def ? def : "");
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);

	gtkutil_grid_attach (table, entry, 1, 2, row, row+1, GU_FILL|GU_EXPAND, 0, SERVLIST_X_PADDING, SERVLIST_Y_PADDING);

	return entry;
}

static gboolean
servlist_delete_cb (GtkWidget *win, gpointer userdata)
{
	servlist_savegui ();
	serverlist_win = NULL;
	selected_net = NULL;

	if (sess_list == NULL)
		hexchat_exit ();

	return FALSE;
}

static void
servlist_close_cb (GtkWidget *button, gpointer userdata)
{
	servlist_savegui ();
	nx_widget_destroy (serverlist_win);
	serverlist_win = NULL;
	selected_net = NULL;

	if (sess_list == NULL)
		hexchat_exit ();
}

/* convert "host:port" format to "host/port" */

static char *
servlist_sanitize_hostname (char *host)
{
	char *ret, *c, *e;

	ret = g_strdup (host);

	c = strchr  (ret, ':');
	e = strrchr (ret, ':');

	/* if only one colon exists it's probably not IPv6 */
	if (c && c == e)
		*c = '/';

	return g_strstrip(ret);
}

/* remove leading slash */
static char *
servlist_sanitize_command (char *cmd)
{
	if (cmd[0] == '/')
	{
		return (g_strdup (cmd + 1));
	}
	else
	{
		return (g_strdup (cmd));
	}
}

static void
servlist_editserver_cb (nx_row *row, const char *newval, gpointer user_data)
{
	char *servname;
	ircserver *serv;

	if (!selected_net)
	{
		return;
	}

	serv = servlist_server_find (selected_net, (char *) nx_row_peek_string (row, 0), NULL);

	if (serv)
	{
		/* delete empty item */
		if (newval[0] == 0)
		{
			servlist_deleteserver (serv);
			return;
		}

		servname = serv->hostname;
		serv->hostname = servlist_sanitize_hostname ((char *) newval);
		nx_row_set (row, 0, serv->hostname, -1);
		g_free (servname);
	}
}

static void
servlist_editcommand_cb (nx_row *row, const char *newval, gpointer user_data)
{
	char *cmd;
	commandentry *entry;

	if (!selected_net)
	{
		return;
	}

	entry = servlist_command_find (selected_net, (char *) nx_row_peek_string (row, 0), NULL);

	if (entry)
	{
		/* delete empty item */
		if (newval[0] == 0)
		{
			servlist_deletecommand (entry);
			return;
		}

		cmd = entry->command;
		entry->command = servlist_sanitize_command ((char *) newval);
		nx_row_set (row, 0, entry->command, -1);
		g_free (cmd);
	}
}

static void
servlist_editchannel_cb (nx_row *row, const char *newval, gpointer user_data)
{
	char *chan;
	favchannel *favchan;

	if (!selected_net)
	{
		return;
	}

	favchan = servlist_favchan_find (selected_net, (char *) nx_row_peek_string (row, 0), NULL);

	if (favchan)
	{
		/* delete empty item */
		if (newval[0] == 0)
		{
			servlist_deletechannel (favchan);
			return;
		}

		chan = favchan->name;
		favchan->name = g_strdup (newval);
		nx_row_set (row, 0, favchan->name, -1);
		g_free (chan);
	}
}

static void
servlist_editkey_cb (nx_row *row, const char *newval, gpointer user_data)
{
	char *key;
	favchannel *favchan;

	if (!selected_net)
	{
		return;
	}

	favchan = servlist_favchan_find (selected_net, (char *) nx_row_peek_string (row, 0), NULL);

	if (favchan)
	{
		key = favchan->key;

		if (strlen (newval))	/* check key length, the field can be empty in order to delete the key! */
		{
			favchan->key = g_strdup (newval);
		}
		else					/* if key's empty, make sure we actually remove the key */
		{
			favchan->key = NULL;
		}

		nx_row_set (row, 1, favchan->key, -1);
		g_free (key);
	}
}

static gboolean
servlist_edit_tabswitch_cb (GtkNotebook *nb, gpointer *newtab, guint newindex, gpointer user_data)
{
	/* remember the active tab */
	netedit_active_tab = newindex;

	return FALSE;
}

static void
servlist_combo_cb (GtkEntry *entry, gpointer userdata)
{
	if (!selected_net)
		return;

	g_free (selected_net->encoding);
	selected_net->encoding = g_strdup (nx_entry_get_text (entry));
}

/* Fills up the network's authentication type so that it's guaranteed to be either NULL or a valid value. */
static void
servlist_logintypecombo_cb (GtkWidget *cb, gpointer userdata)
{
	int index;

	if (!selected_net)
	{
		return;
	}

	index = nx_combo_get_active (cb);	/* starts at 0, returns -1 for invalid selections */

	if (index == -1)
		return; /* Invalid */

	/* The selection is valid. It can be 0, which is the default type, but we need to allow
	 * that so that you can revert from other types. servlist_save() will dump 0 anyway.
	 */
	selected_net->logintype = login_types_conf[index];

	if (login_types_conf[index] == LOGIN_CUSTOM)
	{
		gtk_notebook_set_current_page (GTK_NOTEBOOK (userdata), 2);		/* FIXME avoid hardcoding? */
	}
	
	/* EXTERNAL uses a cert, not a pass */
	if (login_types_conf[index] == LOGIN_SASLEXTERNAL)
		gtk_widget_set_sensitive (edit_entry_pass, FALSE);
	else
		gtk_widget_set_sensitive (edit_entry_pass, TRUE);
}

static void
servlist_username_changed_cb (GtkEntry *entry, gpointer userdata)
{
	GtkWidget *connect_btn = GTK_WIDGET (userdata);

	if (nx_entry_get_text (entry)[0] == 0)
	{
		gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NX_STOCK_DIALOG_ERROR));
		gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY,
										_("User name cannot be left blank."));
		gtk_widget_set_sensitive (connect_btn, FALSE);
	}
	else
	{
		gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NULL));
		gtk_widget_set_sensitive (connect_btn, TRUE);
	}
}

static void
servlist_nick_changed_cb (GtkEntry *entry, gpointer userdata)
{
	GtkWidget *connect_btn = GTK_WIDGET (userdata);
	const gchar *nick1 = nx_entry_get_text (entry_nick1);
	const gchar *nick2 = nx_entry_get_text (entry_nick2);

	if (!nick1[0] || !nick2[0])
	{
		entry = GTK_ENTRY(!nick1[0] ? entry_nick1 : entry_nick2);
		gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NX_STOCK_DIALOG_ERROR));
		gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY,
		                                 _("You cannot have an empty nick name."));
		gtk_widget_set_sensitive (connect_btn, FALSE);
	}
	else if (!rfc_casecmp (nick1, nick2))
	{
		gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NX_STOCK_DIALOG_ERROR));
		gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY,
										_("You must have two unique nick names."));
		gtk_widget_set_sensitive (connect_btn, FALSE);
	}
	else
	{
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY(entry_nick1), GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NULL));
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY(entry_nick2), GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NULL));
		gtk_widget_set_sensitive (connect_btn, TRUE);
	}
}

/* a GtkComboBoxText with entry is deprecated and GTK4 has no editable
   drop-down: an entry, linked to a button that drops the same list down */
static void
servlist_charset_pick (GObject *dd, GParamSpec *ps, GtkWidget *entry)
{
	guint i = gtk_drop_down_get_selected (GTK_DROP_DOWN (dd));

	if (i != GTK_INVALID_LIST_POSITION)
		nx_entry_set_text (entry, pages[i]);
}

static GtkWidget *
servlist_create_charsetcombo (void)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *entry = gtk_entry_new ();
	GtkWidget *dd = gtk_drop_down_new_from_strings (pages);

	gtk_widget_add_css_class (box, "linked");
	gtk_widget_set_hexpand (entry, TRUE);
	nx_entry_set_text (entry, selected_net->encoding ? selected_net->encoding : pages[0]);
	gtk_drop_down_set_selected (GTK_DROP_DOWN (dd), GTK_INVALID_LIST_POSITION);
	/* only the arrow shows: the entry holds the text */
	gtk_widget_add_css_class (dd, "nx-combo-arrow");
	gtk_box_append (GTK_BOX (box), entry);
	gtk_box_append (GTK_BOX (box), dd);
	g_signal_connect (dd, "notify::selected", G_CALLBACK (servlist_charset_pick), entry);
	g_signal_connect (G_OBJECT (entry), "changed", G_CALLBACK (servlist_combo_cb), NULL);
	return box;
}

static GtkWidget *
servlist_create_logintypecombo (GtkWidget *data)
{
	GtkWidget *cb;

	cb = nx_combo_new ((const char * const *) login_types);
	nx_combo_set_active (cb, servlist_get_login_desc_index (selected_net->logintype));

	gtk_widget_set_tooltip_text (cb, _("The way you identify yourself to the server. For custom login methods use connect commands."));
	nx_combo_on_changed (cb, servlist_logintypecombo_cb, data);

	return cb;
}

static void
no_servlist (GtkWidget * igad, gpointer serv)
{
	if (nx_toggle_get_active ((GtkWidget *) (igad)))
		prefs.hex_gui_slist_skip = TRUE;
	else
		prefs.hex_gui_slist_skip = FALSE;
}

static void
fav_servlist (GtkWidget * igad, gpointer serv)
{
	if (nx_toggle_get_active ((GtkWidget *) (igad)))
		prefs.hex_gui_slist_fav = TRUE;
	else
		prefs.hex_gui_slist_fav = FALSE;

	servlist_networks_populate (networks_tree, network_list);
}

static GtkWidget *
bold_label (char *text)
{
	char buf[128];
	GtkWidget *label;

	g_snprintf (buf, sizeof (buf), "<b>%s</b>", text);
	label = gtk_label_new (buf);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	nx_widget_show (label);

	return label;
}

static GtkWidget *
servlist_open_edit (GtkWidget *parent, ircnet *net)
{
	GtkWidget *editwindow;
	GtkWidget *vbox5;
	GtkWidget *table3;
	GtkWidget *label34;
	GtkWidget *label_logintype;
	GtkWidget *comboboxentry_charset;
	GtkWidget *combobox_logintypes;
	GtkWidget *hbox1;
	GtkWidget *scrolledwindow2;
	GtkWidget *scrolledwindow4;
	GtkWidget *scrolledwindow5;
	GtkWidget *treeview_servers;
	GtkWidget *treeview_channels;
	GtkWidget *treeview_commands;
	GtkWidget *vbuttonbox1;
	GtkWidget *buttonadd;
	GtkWidget *buttonremove;
	GtkWidget *buttonedit;
	GtkWidget *hseparator2;
	GtkWidget *hbuttonbox4;
	GtkWidget *button10;
	GtkWidget *check;
	GtkWidget *notebook;
	nx_store *store;
	nx_col *col;
	char buf[128];

	editwindow = nx_window_new ();
	nx_set_border_width (editwindow, 4);
	g_snprintf (buf, sizeof (buf), _("Edit %s - %s"), net->name, _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (editwindow), buf);
	gtk_window_set_default_size (GTK_WINDOW (editwindow), netedit_win_width, netedit_win_height);
	gtk_window_set_transient_for (GTK_WINDOW (editwindow), GTK_WINDOW (parent));
	gtk_window_set_modal (GTK_WINDOW (editwindow), TRUE);
	nx_window_set_dialog_hint (editwindow);
	nx_window_set_role (editwindow, "editserv");

	vbox5 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtkutil_container_add (GTK_WIDGET (editwindow), vbox5);


	/* Tabs and buttons */
	hbox1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (vbox5, hbox1, TRUE, TRUE, 4);

	scrolledwindow2 = nx_scrolled_window_new ();
	scrolledwindow4 = nx_scrolled_window_new ();
	scrolledwindow5 = nx_scrolled_window_new ();

	notebook = gtk_notebook_new ();
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolledwindow2, gtk_label_new (_("Servers")));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolledwindow4, gtk_label_new (_("Autojoin channels")));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolledwindow5, gtk_label_new (_("Connect commands")));
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_BOTTOM);
	nx_box_pack_start (hbox1, notebook, TRUE, TRUE, SERVLIST_X_PADDING);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow2), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	nx_scrolled_window_set_shadow_in (scrolledwindow2, TRUE);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow4), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	nx_scrolled_window_set_shadow_in (scrolledwindow4, TRUE);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow5), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	nx_scrolled_window_set_shadow_in (scrolledwindow5, TRUE);
	gtk_widget_set_tooltip_text (scrolledwindow5, _("%n=Nick name\n%p=Password\n%r=Real name\n%u=User name"));


	/* Server Tree */
	store = nx_store_new (FALSE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);

	edit_trees[SERVER_TREE] = treeview_servers = nx_view_new (store);
	g_object_unref (store);
	nx_view_set_search_column (treeview_servers, 0);
	nx_on_key (treeview_servers, servlist_keypress_cb, notebook);
	nx_view_on_select (treeview_servers, servlist_server_row_cb, NULL);
	gtkutil_container_add (GTK_WIDGET (scrolledwindow2), treeview_servers);
	gtk_widget_set_size_request (treeview_servers, -1, 80);
	nx_view_set_headers_visible (treeview_servers, FALSE);

	col = nx_view_add_column (treeview_servers, NULL, NX_COL_TEXT, 0);
	nx_col_bind (col, NX_BIND_EDITABLE, 1);
	nx_col_on_edited (col, servlist_editserver_cb, NULL);

	/* Channel Tree */
	store = nx_store_new (FALSE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

	edit_trees[CHANNEL_TREE] = treeview_channels = nx_view_new (store);
	g_object_unref (store);
	nx_view_set_search_column (treeview_channels, 0);
	nx_on_key (treeview_channels, servlist_keypress_cb, notebook);
	nx_view_on_select (treeview_channels, servlist_channel_row_cb, NULL);
	gtkutil_container_add (GTK_WIDGET (scrolledwindow4), treeview_channels);
	nx_view_set_headers_visible (treeview_channels, TRUE);

	col = nx_view_add_column (treeview_channels, _("Channel"), NX_COL_TEXT, 0);
	nx_col_bind (col, NX_BIND_EDITABLE, 2);
	nx_col_on_edited (col, servlist_editchannel_cb, NULL);
	nx_col_set_expand (col, TRUE);

	col = nx_view_add_column (treeview_channels, _("Key (Password)"), NX_COL_TEXT, 1);
	nx_col_bind (col, NX_BIND_EDITABLE, 2);
	nx_col_on_edited (col, servlist_editkey_cb, NULL);
	nx_col_set_expand (col, TRUE);


	/* Command Tree */
	store = nx_store_new (FALSE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);

	edit_trees[CMD_TREE] = treeview_commands = nx_view_new (store);
	g_object_unref (store);
	nx_view_set_search_column (treeview_commands, 0);
	nx_on_key (treeview_commands, servlist_keypress_cb, notebook);
	nx_view_on_select (treeview_commands, servlist_command_row_cb, NULL);
	gtkutil_container_add (GTK_WIDGET (scrolledwindow5), treeview_commands);
	nx_view_set_headers_visible (treeview_commands, FALSE);

	col = nx_view_add_column (treeview_commands, NULL, NX_COL_TEXT, 0);
	nx_col_bind (col, NX_BIND_EDITABLE, 1);
	nx_col_on_edited (col, servlist_editcommand_cb, NULL);


	/* Button Box */
	vbuttonbox1 = nx_button_box_new (GTK_ORIENTATION_VERTICAL, NX_BUTTONBOX_START);
	gtk_box_set_spacing (GTK_BOX (vbuttonbox1), 3);
	nx_box_pack_start (hbox1, vbuttonbox1, FALSE, FALSE, 3);

	buttonadd = gtkutil_button_new_from_stock ("gtk-add");
	g_signal_connect (G_OBJECT (buttonadd), "clicked",
							G_CALLBACK (servlist_addbutton_cb), notebook);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox1), buttonadd);

	buttonremove = gtkutil_button_new_from_stock ("gtk-remove");
	g_signal_connect (G_OBJECT (buttonremove), "clicked",
							G_CALLBACK (servlist_deletebutton_cb), notebook);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox1), buttonremove);

	buttonedit = gtk_button_new_with_mnemonic (_("_Edit"));
	g_signal_connect (G_OBJECT (buttonedit), "clicked",
							G_CALLBACK (servlist_editbutton_cb), notebook);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox1), buttonedit);


	/* Checkboxes and entries */
	table3 = gtk_grid_new ();
	nx_box_pack_start (vbox5, table3, FALSE, FALSE, 0);
	gtk_grid_set_row_spacing (GTK_GRID (table3), 2);
	gtk_grid_set_column_spacing (GTK_GRID (table3), 8);

	check = servlist_create_check (0, !(net->flags & FLAG_CYCLE), table3, 0, 0, _("Connect to selected server only"));
	gtk_widget_set_tooltip_text (check, _("Don't cycle through all the servers when the connection fails."));
	servlist_create_check (3, net->flags & FLAG_AUTO_CONNECT, table3, 1, 0, _("Connect to this network automatically"));
	servlist_create_check (4, !(net->flags & FLAG_USE_PROXY), table3, 2, 0, _("Bypass proxy server"));
	check = servlist_create_check (2, net->flags & FLAG_USE_SSL, table3, 3, 0, _("Use SSL for all the servers on this network"));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (check, FALSE);
#endif
	check = servlist_create_check (5, net->flags & FLAG_ALLOW_INVALID, table3, 4, 0, _("Accept invalid SSL certificates"));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (check, FALSE);
#endif
	servlist_create_check (1, net->flags & FLAG_USE_GLOBAL, table3, 5, 0, _("Use global user information"));

	edit_entry_nick = servlist_create_entry (table3, _("_Nick name:"), 6, net->nick, &edit_label_nick, 0);
	edit_entry_nick2 = servlist_create_entry (table3, _("Second choice:"), 7, net->nick2, &edit_label_nick2, 0);
	edit_entry_real = servlist_create_entry (table3, _("Rea_l name:"), 8, net->real, &edit_label_real, 0);
	edit_entry_user = servlist_create_entry (table3, _("_User name:"), 9, net->user, &edit_label_user, 0);

	label_logintype = gtk_label_new (_("Login method:"));
	gtkutil_grid_attach (table3, label_logintype, 0, 1, 10, 11, (GU_FILL), (0), SERVLIST_X_PADDING, SERVLIST_Y_PADDING);
	gtk_label_set_xalign (GTK_LABEL (label_logintype), 0); gtk_label_set_yalign (GTK_LABEL (label_logintype), 0.5);
	combobox_logintypes = servlist_create_logintypecombo (notebook);
	gtkutil_grid_attach (table3, combobox_logintypes, 1, 2, 10, 11, (GU_FILL), (GU_FILL), 4, 2);

	edit_entry_pass = servlist_create_entry (table3, _("Password:"), 11, net->pass, 0, _("Password used for login. If in doubt, leave blank."));
	gtk_entry_set_visibility (GTK_ENTRY (edit_entry_pass), FALSE);
	if (selected_net && selected_net->logintype == LOGIN_SASLEXTERNAL)
		gtk_widget_set_sensitive (edit_entry_pass, FALSE);

	label34 = gtk_label_new (_("Character set:"));
	gtkutil_grid_attach (table3, label34, 0, 1, 12, 13, (GU_FILL), (0), SERVLIST_X_PADDING, SERVLIST_Y_PADDING);
	gtk_label_set_xalign (GTK_LABEL (label34), 0); gtk_label_set_yalign (GTK_LABEL (label34), 0.5);
	comboboxentry_charset = servlist_create_charsetcombo ();
	gtkutil_grid_attach (table3, comboboxentry_charset, 1, 2, 12, 13, (GU_FILL), (GU_FILL), 4, 2);


	/* Rule and Close button */
	hseparator2 = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	nx_box_pack_start (vbox5, hseparator2, FALSE, FALSE, 8);

	hbuttonbox4 = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_END);
	nx_box_pack_start (vbox5, hbuttonbox4, FALSE, FALSE, 0);

	button10 = gtkutil_button_new_from_stock ("gtk-close");
	g_signal_connect (G_OBJECT (button10), "clicked",
							G_CALLBACK (servlist_edit_close_cb), 0);
	gtkutil_container_add (GTK_WIDGET (hbuttonbox4), button10);

	if (net->flags & FLAG_USE_GLOBAL)
	{
		servlist_toggle_global_user (FALSE);
	}

	gtk_widget_grab_focus (button10);
	nx_widget_make_default (button10);

	nx_widget_show_all (editwindow);

	/* We can't set the active tab without child elements being shown, so this must be *after* gtk_widget_show()s! */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), netedit_active_tab);

	/* We need to connect this *after* setting the active tab so that the value doesn't get overriden. */
	g_signal_connect (G_OBJECT (notebook), "switch-page", G_CALLBACK (servlist_edit_tabswitch_cb), notebook);

	return editwindow;
}

static GtkWidget *
servlist_open_networks (void)
{
	GtkWidget *servlist;
	GtkWidget *vbox1;
	GtkWidget *label2;
	GtkWidget *table1;
	GtkWidget *label3;
	GtkWidget *label4;
	GtkWidget *label5;
	GtkWidget *label6;
	/* GtkWidget *label7; */
	GtkWidget *entry1;
	GtkWidget *entry2;
	GtkWidget *entry3;
	GtkWidget *entry4;
	/* GtkWidget *entry5; */
	GtkWidget *vbox2;
	GtkWidget *label1;
	GtkWidget *table4;
	GtkWidget *scrolledwindow3;
	GtkWidget *treeview_networks;
	GtkWidget *checkbutton_skip;
	GtkWidget *checkbutton_fav;
	GtkWidget *hbox;
	GtkWidget *vbuttonbox2;
	GtkWidget *button_add;
	GtkWidget *button_remove;
	GtkWidget *button_edit;
	GtkWidget *button_sort;
	GtkWidget *hseparator1;
	GtkWidget *hbuttonbox1;
	GtkWidget *button_connect;
	GtkWidget *button_close;
	nx_store *store;
	nx_col *col;
	char buf[128];

	servlist = nx_window_new ();
	nx_set_border_width (servlist, 4);
	g_snprintf(buf, sizeof(buf), _("Network List - %s"), _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (servlist), buf);
	gtk_window_set_default_size (GTK_WINDOW (servlist), netlist_win_width, netlist_win_height);
	nx_window_set_role (servlist, "servlist");
	nx_window_set_dialog_hint (servlist);
	if (current_sess)
		gtk_window_set_transient_for (GTK_WINDOW (servlist), GTK_WINDOW (current_sess->gui->window));

	vbox1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	nx_widget_show (vbox1);
	gtkutil_container_add (GTK_WIDGET (servlist), vbox1);

	label2 = bold_label (_("User Information"));
	nx_box_pack_start (vbox1, label2, FALSE, FALSE, 0);

	table1 = gtk_grid_new ();
	nx_widget_show (table1);
	nx_box_pack_start (vbox1, table1, FALSE, FALSE, 0);
	nx_set_border_width (table1, 8);
	gtk_grid_set_row_spacing (GTK_GRID (table1), 2);
	gtk_grid_set_column_spacing (GTK_GRID (table1), 4);

	label3 = gtk_label_new_with_mnemonic (_("_Nick name:"));
	nx_widget_show (label3);
	gtkutil_grid_attach (table1, label3, 0, 1, 0, 1,
							(GU_FILL),
							(0), 0, 0);
	gtk_label_set_xalign (GTK_LABEL (label3), 0); gtk_label_set_yalign (GTK_LABEL (label3), 0.5);

	label4 = gtk_label_new (_("Second choice:"));
	nx_widget_show (label4);
	gtkutil_grid_attach (table1, label4, 0, 1, 1, 2,
							(GU_FILL),
							(0), 0, 0);
	gtk_label_set_xalign (GTK_LABEL (label4), 0); gtk_label_set_yalign (GTK_LABEL (label4), 0.5);

	label5 = gtk_label_new (_("Third choice:"));
	nx_widget_show (label5);
	gtkutil_grid_attach (table1, label5, 0, 1, 2, 3,
							(GU_FILL),
							(0), 0, 0);
	gtk_label_set_xalign (GTK_LABEL (label5), 0); gtk_label_set_yalign (GTK_LABEL (label5), 0.5);

	label6 = gtk_label_new_with_mnemonic (_("_User name:"));
	nx_widget_show (label6);
	gtkutil_grid_attach (table1, label6, 0, 1, 3, 4,
							(GU_FILL),
							(0), 0, 0);
	gtk_label_set_xalign (GTK_LABEL (label6), 0); gtk_label_set_yalign (GTK_LABEL (label6), 0.5);

	/* label7 = gtk_label_new_with_mnemonic (_("Rea_l name:"));
	nx_widget_show (label7);
	gtkutil_grid_attach (table1, label7, 0, 1, 4, 5,
							(GU_FILL),
							(0), 0, 0);
	gtk_label_set_xalign (GTK_LABEL (label7), 0); gtk_label_set_yalign (GTK_LABEL (label7), 0.5);*/

	entry_nick1 = entry1 = gtk_entry_new ();
	nx_entry_set_text (entry1, prefs.hex_irc_nick1);
	nx_widget_show (entry1);
	gtkutil_grid_attach (table1, entry1, 1, 2, 0, 1,
							(GU_EXPAND | GU_FILL),
							(0), 0, 0);

	entry_nick2 = entry2 = gtk_entry_new ();
	nx_entry_set_text (entry2, prefs.hex_irc_nick2);
	nx_widget_show (entry2);
	gtkutil_grid_attach (table1, entry2, 1, 2, 1, 2,
							(GU_EXPAND | GU_FILL),
							(0), 0, 0);

	entry_nick3 = entry3 = gtk_entry_new ();
	nx_entry_set_text (entry3, prefs.hex_irc_nick3);
	nx_widget_show (entry3);
	gtkutil_grid_attach (table1, entry3, 1, 2, 2, 3,
							(GU_EXPAND | GU_FILL),
							(0), 0, 0);

	entry_guser = entry4 = gtk_entry_new ();
	nx_entry_set_text (entry4, prefs.hex_irc_user_name);
	nx_widget_show (entry4);
	gtkutil_grid_attach (table1, entry4, 1, 2, 3, 4,
							(GU_EXPAND | GU_FILL),
							(0), 0, 0);

	/* entry_greal = entry5 = gtk_entry_new ();
	nx_entry_set_text (entry5, prefs.hex_irc_real_name);
	nx_widget_show (entry5);
	gtkutil_grid_attach (table1, entry5, 1, 2, 4, 5,
							(GU_EXPAND | GU_FILL),
							(0), 0, 0); */

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	nx_widget_show (vbox2);
	nx_box_pack_start (vbox1, vbox2, TRUE, TRUE, 0);

	label1 = bold_label (_("Networks"));
	nx_box_pack_start (vbox2, label1, FALSE, FALSE, 0);

	g_clear_pointer (&netfilter, g_free);

	/* type part of a name to narrow the list (there are dozens); Enter
	   connects to the one selected */
	netfilter_entry = gtk_search_entry_new ();
	gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (netfilter_entry), _("Find a network"));
	gtk_widget_set_margin_start (netfilter_entry, 8);
	gtk_widget_set_margin_end (netfilter_entry, 8);
	gtk_widget_set_margin_top (netfilter_entry, 4);
	nx_box_pack_start (vbox2, netfilter_entry, FALSE, FALSE, 0);
	g_signal_connect (netfilter_entry, "search-changed", G_CALLBACK (servlist_filter_changed), NULL);
	g_signal_connect (netfilter_entry, "activate", G_CALLBACK (servlist_filter_activate), NULL);
	g_signal_connect (netfilter_entry, "next-match", G_CALLBACK (servlist_filter_move), GINT_TO_POINTER (1));
	g_signal_connect (netfilter_entry, "previous-match", G_CALLBACK (servlist_filter_move), GINT_TO_POINTER (-1));
	nx_widget_show (netfilter_entry);

	table4 = gtk_grid_new ();
	nx_widget_show (table4);
	nx_box_pack_start (vbox2, table4, TRUE, TRUE, 0);
	nx_set_border_width (table4, 8);
	gtk_grid_set_row_spacing (GTK_GRID (table4), 2);
	gtk_grid_set_column_spacing (GTK_GRID (table4), 3);

	scrolledwindow3 = nx_scrolled_window_new ();
	nx_widget_show (scrolledwindow3);
	gtkutil_grid_attach (table4, scrolledwindow3, 0, 1, 0, 1,
							(GU_EXPAND | GU_FILL),
							(GU_EXPAND | GU_FILL), 0, 0);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow3),
											  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	nx_scrolled_window_set_shadow_in (scrolledwindow3, TRUE);

	store = nx_store_new (FALSE, 3, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INT);

	networks_tree = treeview_networks = nx_view_new (store);
	g_object_unref (store);
	nx_view_set_search_column (treeview_networks, 0);
	nx_widget_show (treeview_networks);
	gtkutil_container_add (GTK_WIDGET (scrolledwindow3), treeview_networks);
	nx_view_set_headers_visible (treeview_networks, FALSE);

	col = nx_view_add_column (treeview_networks, NULL, NX_COL_TEXT, 0);
	nx_col_bind (col, NX_BIND_EDITABLE, 1);
	nx_col_bind (col, NX_BIND_WEIGHT, 2);
	nx_col_on_edited (col, servlist_celledit_cb, NULL);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtkutil_grid_attach (table4, hbox, 0, 2, 1, 2,
							(GU_FILL),
							(0), 0, 0);
	nx_widget_show (hbox);

	checkbutton_skip =
		gtk_check_button_new_with_mnemonic (_("Skip network list on startup"));
	nx_toggle_set_active ((GtkWidget *) (checkbutton_skip),
											prefs.hex_gui_slist_skip);
	gtkutil_container_add (GTK_WIDGET (hbox), checkbutton_skip);
	g_signal_connect (G_OBJECT (checkbutton_skip), "toggled",
							G_CALLBACK (no_servlist), 0);
	nx_widget_show (checkbutton_skip);

	checkbutton_fav =
		gtk_check_button_new_with_mnemonic (_("Show favorites only"));
	nx_toggle_set_active ((GtkWidget *) (checkbutton_fav),
											prefs.hex_gui_slist_fav);
	gtkutil_container_add (GTK_WIDGET (hbox), checkbutton_fav);
	g_signal_connect (G_OBJECT (checkbutton_fav), "toggled",
							G_CALLBACK (fav_servlist), 0);
	nx_widget_show (checkbutton_fav);

	vbuttonbox2 = nx_button_box_new (GTK_ORIENTATION_VERTICAL, NX_BUTTONBOX_START);
	gtk_box_set_spacing (GTK_BOX (vbuttonbox2), 3);
	nx_widget_show (vbuttonbox2);
	gtkutil_grid_attach (table4, vbuttonbox2, 1, 2, 0, 1,
							(GU_FILL),
							(GU_FILL), 0, 0);

	button_add = gtkutil_button_new_from_stock ("gtk-add");
	g_signal_connect (G_OBJECT (button_add), "clicked",
							G_CALLBACK (servlist_addnet_cb), networks_tree);
	nx_widget_show (button_add);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox2), button_add);

	button_remove = gtkutil_button_new_from_stock ("gtk-remove");
	g_signal_connect (G_OBJECT (button_remove), "clicked",
							G_CALLBACK (servlist_deletenet_cb), 0);
	nx_widget_show (button_remove);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox2), button_remove);

	button_edit = gtk_button_new_with_mnemonic (_("_Edit..."));
	g_signal_connect (G_OBJECT (button_edit), "clicked",
							G_CALLBACK (servlist_edit_cb), 0);
	nx_widget_show (button_edit);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox2), button_edit);

	button_sort = gtk_button_new_with_mnemonic (_("_Sort"));
	gtk_widget_set_tooltip_text (button_sort, _("Sorts the network list in alphabetical order. "
				"Use Shift+Up and Shift+Down keys to move a row."));
	g_signal_connect (G_OBJECT (button_sort), "clicked",
							G_CALLBACK (servlist_sort), 0);
	nx_widget_show (button_sort);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox2), button_sort);

	button_sort = gtk_button_new_with_mnemonic (_("_Favor"));
	gtk_widget_set_tooltip_text (button_sort, _("Mark or unmark this network as a favorite."));
	g_signal_connect (G_OBJECT (button_sort), "clicked",
							G_CALLBACK (servlist_favor), 0);
	nx_widget_show (button_sort);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox2), button_sort);

	button_sort = gtk_button_new_with_mnemonic (_("_Bouncer..."));
	gtk_widget_set_tooltip_text (button_sort, _("Stay in your channels when nexchat is closed, "
				"with a bouncer (soju, ZNC) or a network that can keep you connected."));
	g_signal_connect (G_OBJECT (button_sort), "clicked",
							G_CALLBACK (servlist_stayonline_cb), 0);
	nx_widget_show (button_sort);
	gtkutil_container_add (GTK_WIDGET (vbuttonbox2), button_sort);

	hseparator1 = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	nx_widget_show (hseparator1);
	nx_box_pack_start (vbox1, hseparator1, FALSE, TRUE, 4);

	/* GtkButtonBox's default layout is EDGE: Close left, Connect right */
	hbuttonbox1 = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_EDGE);
	nx_widget_show (hbuttonbox1);
	nx_box_pack_start (vbox1, hbuttonbox1, FALSE, TRUE, 0);
	nx_set_border_width (hbuttonbox1, 8);

	button_close = gtkutil_button_new_from_stock ("gtk-close");
	nx_widget_show (button_close);
	g_signal_connect (G_OBJECT (button_close), "clicked",
							G_CALLBACK (servlist_close_cb), 0);
	gtkutil_container_add (GTK_WIDGET (hbuttonbox1), button_close);

	button_connect = gtkutil_button (hbuttonbox1, NX_STOCK_CONNECT, NULL,
												servlist_connect_cb, NULL, _("C_onnect"));

	g_signal_connect (G_OBJECT (entry_guser), "changed", 
					G_CALLBACK(servlist_username_changed_cb), button_connect);
	g_signal_connect (G_OBJECT (entry_nick1), "changed",
					G_CALLBACK(servlist_nick_changed_cb), button_connect);
	g_signal_connect (G_OBJECT (entry_nick2), "changed",
					G_CALLBACK(servlist_nick_changed_cb), button_connect);

	/* Run validity checks now */
	servlist_nick_changed_cb (GTK_ENTRY(entry_nick2), button_connect);
	servlist_username_changed_cb (GTK_ENTRY(entry_guser), button_connect);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label3), entry1);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label6), entry4);
	/* gtk_label_set_mnemonic_widget (GTK_LABEL (label7), entry5); */

	gtk_widget_grab_focus (netfilter_entry);
	/* tests: NEXCHAT_NETFIND=text types it in the find box */
	if (g_getenv ("NEXCHAT_NETFIND"))
	{
		nx_row *r;
		gtk_editable_set_text (GTK_EDITABLE (netfilter_entry), g_getenv ("NEXCHAT_NETFIND"));
		servlist_filter_changed (GTK_SEARCH_ENTRY (netfilter_entry), NULL);
		r = nx_view_get_selected (networks_tree);
		g_printerr ("netfind: %d listed, selected %s\n",
						nx_store_n_rows (nx_view_get_store (networks_tree), NULL),
						r ? nx_row_peek_string (r, 0) : "none");
	}
	nx_widget_make_default (button_close);
	return servlist;
}

void
fe_serverlist_open (session *sess)
{
	if (serverlist_win)
	{
		gtk_window_present (GTK_WINDOW (serverlist_win));
		return;
	}

	servlist_sess = sess;

	serverlist_win = servlist_open_networks ();
	gtkutil_set_icon (serverlist_win);

	servlist_networks_populate (networks_tree, network_list);

	nx_on_close_request (serverlist_win, servlist_delete_cb, 0);
	nx_on_window_configure (serverlist_win, servlist_configure_cb, 0);
	nx_view_on_select (networks_tree, servlist_network_row_cb, NULL);
	nx_on_key (networks_tree, servlist_net_keypress_cb, networks_tree);

	nx_widget_show (serverlist_win);
}

void
servlist_bouncer_changed (server *serv)
{
	/* new or gone entries for the bouncer's networks */
	if (serverlist_win && networks_tree && !edit_win)
		servlist_networks_populate (networks_tree, network_list);
}
