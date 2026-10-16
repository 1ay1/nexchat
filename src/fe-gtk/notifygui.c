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
#include <string.h>
#include <fcntl.h>
#include <time.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/notify.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "../common/util.h"
#include "../common/userlist.h"
#include "../common/outbound.h"
#include "gtkutil.h"
#include "maingui.h"
#include "palette.h"
#include "notifygui.h"
#include "nxlist.h"


/* model for the notify treeview */
enum
{
	USER_COLUMN,
	STATUS_COLUMN,
	SERVER_COLUMN,
	SEEN_COLUMN,
	COLOUR_COLUMN,
	NPS_COLUMN, 	/* struct notify_per_server * */
	N_COLUMNS
};


static GtkWidget *notify_window = 0;
static GtkWidget *notify_button_opendialog;
static GtkWidget *notify_button_remove;


static void
notify_closegui (void)
{
	notify_window = 0;
	notify_save ();
}

static void
notify_row_cb (GtkWidget *view, gpointer data)
{
	nx_row *row = nx_view_get_selected (view);
	struct notify_per_server *servnot;

	if (row)
	{
		servnot = nx_row_get_pointer (row, NPS_COLUMN);
		gtk_widget_set_sensitive (notify_button_opendialog, servnot ? servnot->ison : 0);
		gtk_widget_set_sensitive (notify_button_remove, TRUE);
		return;
	}

	gtk_widget_set_sensitive (notify_button_opendialog, FALSE);
	gtk_widget_set_sensitive (notify_button_remove, FALSE);
}

static GtkWidget *
notify_treeview_new (GtkWidget *box)
{
	nx_store *store;
	GtkWidget *view;
	nx_col *col;
	static const char *const titles[] = { N_("Name"), N_("Status"), N_("Network"), N_("Last Seen") };
	int i;

	store = nx_store_new (FALSE, N_COLUMNS,
	                      G_TYPE_STRING,
	                      G_TYPE_STRING,
	                      G_TYPE_STRING,
	                      G_TYPE_STRING,
	                      GDK_TYPE_RGBA,
	                      G_TYPE_POINTER);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);

	for (i = 0; i < 4; i++)
	{
		col = nx_view_add_column (view, _(titles[i]), NX_COL_TEXT, i);
		/* each row's colour: online or offline */
		nx_col_bind (col, NX_BIND_FOREGROUND, COLOUR_COLUMN);
		nx_col_set_title_align (col, 0.5);
		if (i == 0)
			nx_col_set_expand (col, TRUE);
	}
	nx_view_set_search_column (view, USER_COLUMN);
	nx_view_on_select (view, notify_row_cb, NULL);

	nx_widget_show (view);
	return view;
}

void
notify_gui_update (void)
{
	struct notify *notify;
	struct notify_per_server *servnot;
	GSList *list = notify_list;
	GSList *slist;
	gchar *name, *status, *server, *seen;
	int online, servcount, lastseenminutes;
	time_t lastseen;
	char agobuf[128];

	nx_store *store;
	GtkWidget *view;
	nx_row *row;	/* the next row to reuse; NULL: append */

	if (!notify_window)
		return;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	store = nx_view_get_store (view);
	row = nx_store_first (store);

	while (list)
	{
		notify = (struct notify *) list->data;
		name = notify->name;
		status = _("Offline");
		server = "";

		online = FALSE;
		lastseen = 0;
		/* First see if they're online on any servers */
		slist = notify->server_list;
		while (slist)
		{
			servnot = (struct notify_per_server *) slist->data;
			if (servnot->ison)
				online = TRUE;
			if (servnot->lastseen > lastseen)
				lastseen = servnot->lastseen;
			slist = slist->next;
		}

		if (!online)				  /* Offline on all servers */
		{
			if (!lastseen)
				seen = _("Never");
			else
			{
				lastseenminutes = (int)(time (0) - lastseen) / 60;
				if (lastseenminutes < 60) 
					g_snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), lastseenminutes);
				else if (lastseenminutes < 120)
					g_snprintf (agobuf, sizeof (agobuf), _("An hour ago"));
				else
					g_snprintf (agobuf, sizeof (agobuf), _("%d hours ago"), lastseenminutes / 60);
				seen = agobuf;
			}
			nx_row_set (row ? row : nx_store_append (store, NULL), 0, name, 1, status,
			            2, server, 3, seen, 4, &colors[4], 5, NULL, -1);
			if (row)
				row = nx_row_next (row);

		} else
		{
			/* Online - add one line per server */
			servcount = 0;
			slist = notify->server_list;
			status = _("Online");
			while (slist)
			{
				servnot = (struct notify_per_server *) slist->data;
				if (servnot->ison)
				{
					if (servcount > 0)
						name = "";
					server = server_get_network (servnot->server, TRUE);

					g_snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), (int)(time (0) - lastseen) / 60);
					seen = agobuf;

					nx_row_set (row ? row : nx_store_append (store, NULL), 0, name, 1, status,
					            2, server, 3, seen, 4, &colors[3], 5, servnot, -1);
					if (row)
						row = nx_row_next (row);

					servcount++;
				}
				slist = slist->next;
			}
		}
		
		list = list->next;
	}

	while (row)
	{
		nx_row *old = row;
		row = nx_row_next (row);
		nx_row_remove (old);
	}

	notify_row_cb (view, NULL);
}

static void
notify_opendialog_clicked (GtkWidget * igad)
{
	GtkWidget *view;
	nx_row *row;
	struct notify_per_server *servnot;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	row = nx_view_get_selected (view);
	if (row)
	{
		servnot = nx_row_get_pointer (row, NPS_COLUMN);
		if (servnot)
			open_query (servnot->server, servnot->notify->name, TRUE);
	}
}

static void
notify_remove_clicked (GtkWidget * igad)
{
	GtkWidget *view;
	nx_row *row;
	gboolean found = FALSE;
	char *name;

	view = g_object_get_data (G_OBJECT (notify_window), "view");
	row = nx_view_get_selected (view);
	if (row)
	{
		nx_row_get (row, USER_COLUMN, &name, -1);
		found = (*name != 0);
		while (!found)	/* the real nick is some previous node */
		{
			g_free (name); /* it's useless to us */
			row = nx_row_prev (row);
			if (!row)	/* arrgh! no previous node! */
			{
				g_warning ("notify list state is invalid\n");
				break;
			}
			nx_row_get (row, USER_COLUMN, &name, -1);
			found = (*name != 0);
		}
		if (!found)
			return;

		/* ok, now we can remove it */
		nx_row_remove (row);
		notify_deluser (name);
		g_free (name);
	}
}

static void
notifygui_add_cb (GtkWidget *dialog, gint response, gpointer entry)
{
	char *networks;
	char *text;

	text = (char *)nx_entry_get_text (entry);
	if (text[0] && response == GTK_RESPONSE_ACCEPT)
	{
		networks = (char*)nx_entry_get_text (g_object_get_data (G_OBJECT (entry), "net"));
		if (g_ascii_strcasecmp (networks, "ALL") == 0 || networks[0] == 0)
			notify_adduser (text, NULL);
		else
			notify_adduser (text, networks);
	}

	nx_widget_destroy (GTK_WIDGET (dialog));
}

static void
notifygui_add_enter (GtkWidget *entry, GtkWidget *dialog)
{
	nx_dialog_response (NX_DIALOG_CAST (dialog), GTK_RESPONSE_ACCEPT);
}

void
fe_notify_ask (char *nick, char *networks)
{
	GtkWidget *dialog;
	GtkWidget *entry;
	GtkWidget *label;
	GtkWidget *wid;
	GtkWidget *table;
	char *msg = _("Enter nickname to add:");
	char buf[256];

	dialog = nx_dialog_new (msg, NULL, FALSE);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_OK, GTK_RESPONSE_ACCEPT);
	if (parent_window)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));
	nx_window_set_position_mouse (dialog);

	table = gtk_grid_new ();
	nx_set_border_width (table, 12);
	gtk_grid_set_row_spacing (GTK_GRID (table), 3);
	gtk_grid_set_column_spacing (GTK_GRID (table), 8);
	gtkutil_container_add (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog)), table);

	label = gtk_label_new (msg);
	gtkutil_grid_attach (table, label, 0, 1, 0, 1, GU_EXPAND | GU_FILL, GU_EXPAND | GU_FILL, 0, 0);

	entry = gtk_entry_new ();
	nx_entry_set_text (entry, nick);
	g_signal_connect (G_OBJECT (entry), "activate",
						 	G_CALLBACK (notifygui_add_enter), dialog);
	gtkutil_grid_attach (table, entry, 1, 2, 0, 1, GU_EXPAND | GU_FILL, GU_EXPAND | GU_FILL, 0, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (notifygui_add_cb), entry);

	label = gtk_label_new (_("Notify on these networks:"));
	gtkutil_grid_attach (table, label, 0, 1, 2, 3, GU_EXPAND | GU_FILL, GU_EXPAND | GU_FILL, 0, 0);

	wid = gtk_entry_new ();
	g_object_set_data (G_OBJECT (entry), "net", wid);
	g_signal_connect (G_OBJECT (wid), "activate",
						 	G_CALLBACK (notifygui_add_enter), dialog);
	nx_entry_set_text (wid, networks ? networks : "ALL");
	gtkutil_grid_attach (table, wid, 1, 2, 2, 3, GU_EXPAND | GU_FILL, GU_EXPAND | GU_FILL, 0, 0);

	label = gtk_label_new (NULL);
	g_snprintf (buf, sizeof (buf), "<i><span size=\"smaller\">%s</span></i>", _("Comma separated list of networks is accepted."));
	gtk_label_set_markup (GTK_LABEL (label), buf);
	gtkutil_grid_attach (table, label, 1, 2, 3, 4, GU_EXPAND | GU_FILL, GU_EXPAND | GU_FILL, 0, 0);

	nx_widget_show_all (dialog);
}

static void
notify_add_clicked (GtkWidget * igad)
{
	fe_notify_ask ("", NULL);
}

void
notify_opengui (void)
{
	GtkWidget *vbox, *bbox;
	GtkWidget *view;
	char buf[128];

	if (notify_window)
	{
		mg_bring_tofront (notify_window);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("Friends List - %s"), _(DISPLAY_NAME));
	notify_window =
		mg_create_generic_tab ("Notify", buf, FALSE, TRUE, notify_closegui, NULL, 400,
								250, &vbox, 0);
	gtkutil_destroy_on_esc (notify_window);

	view = notify_treeview_new (vbox);
	g_object_set_data (G_OBJECT (notify_window), "view", view);
  
	bbox = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		nx_set_border_width (bbox, 5);
	nx_box_pack_end (vbox, bbox, 0, 0, 0);
	nx_widget_show (bbox);

	gtkutil_button (bbox, NX_STOCK_NEW, 0, notify_add_clicked, 0,
	                _("Add..."));

	notify_button_remove =
	gtkutil_button (bbox, NX_STOCK_DELETE, 0, notify_remove_clicked, 0,
	                _("Remove"));

	notify_button_opendialog =
	gtkutil_button (bbox, NULL, 0, notify_opendialog_clicked, 0,
	                _("Open Dialog"));

	gtk_widget_set_sensitive (notify_button_opendialog, FALSE);
	gtk_widget_set_sensitive (notify_button_remove, FALSE);

	notify_gui_update ();

	nx_widget_show (notify_window);
}
