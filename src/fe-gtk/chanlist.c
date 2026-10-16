/* X-Chat
 * Copyright (C) 1998-2006 Peter Zelezny.
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

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.h"


#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/util.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "maingui.h"
#include "menu.h"

enum
{
	COL_CHANNEL,
	COL_USERS,
	COL_TOPIC,
	COL_ROW,		/* chanlistrow *, for sorting */
	N_COLUMNS
};

typedef struct
{
	char *topic;
	char *collation_key;
	guint32	pos;
	guint32 users;
	/* channel string lives beyond "users" */
#define GET_CHAN(row) (((char *)row)+sizeof(chanlistrow))
}
chanlistrow;

#define GET_STORE(xserv) (nx_view_get_store (xserv->gui->chanlist_list))

/* fast as possible compares, as custom-list.c's were */
#define TOSML(c) (((c) >= 'A' && (c) <= 'Z') ? (c) - 'A' + 'a' : (c))

static int
fast_ascii_stricmp (const char *s1, const char *s2)
{
	int c1, c2;

	while (*s1 && *s2)
	{
		c1 = (int) (unsigned char) TOSML (*s1);
		c2 = (int) (unsigned char) TOSML (*s2);
		if (c1 != c2)
			return (c1 - c2);
		s1++;
		s2++;
	}

	return (((int) (unsigned char) *s1) - ((int) (unsigned char) *s2));
}

static int
chanlist_cmp_channel (nx_row *a, nx_row *b, gpointer data)
{
	return strcmp (((chanlistrow *) nx_row_get_pointer (a, COL_ROW))->collation_key,
						((chanlistrow *) nx_row_get_pointer (b, COL_ROW))->collation_key);
}

static int
chanlist_cmp_users (nx_row *a, nx_row *b, gpointer data)
{
	return (int) ((chanlistrow *) nx_row_get_pointer (a, COL_ROW))->users -
			 (int) ((chanlistrow *) nx_row_get_pointer (b, COL_ROW))->users;
}

static int
chanlist_cmp_topic (nx_row *a, nx_row *b, gpointer data)
{
	return fast_ascii_stricmp (((chanlistrow *) nx_row_get_pointer (a, COL_ROW))->topic,
										((chanlistrow *) nx_row_get_pointer (b, COL_ROW))->topic);
}

static void
chanlist_store_append (server *serv, chanlistrow *row)
{
	nx_row_set (nx_store_append (GET_STORE (serv), NULL), COL_CHANNEL, GET_CHAN (row),
					COL_USERS, (int) row->users, COL_TOPIC, row->topic, COL_ROW, row, -1);
}


static gboolean
chanlist_match (server *serv, const char *str)
{
	switch (serv->gui->chanlist_search_type)
	{
	case 1:
		return match (nx_entry_get_text (serv->gui->chanlist_wild), str);
	case 2:
		if (!serv->gui->have_regex)
			return 0;

		return g_regex_match (serv->gui->chanlist_match_regex, str, 0, NULL);
	default:	/* case 0: */
		return nocasestrstr (str, nx_entry_get_text (serv->gui->chanlist_wild)) ? 1 : 0;
	}
}

/**
 * Updates the caption to reflect the number of users and channels
 */
static void
chanlist_update_caption (server *serv)
{
	gchar tbuf[256];

	g_snprintf (tbuf, sizeof tbuf,
				 _("Displaying %d/%d users on %d/%d channels."),
				 serv->gui->chanlist_users_shown_count,
				 serv->gui->chanlist_users_found_count,
				 serv->gui->chanlist_channels_shown_count,
				 serv->gui->chanlist_channels_found_count);

	gtk_label_set_text (GTK_LABEL (serv->gui->chanlist_label), tbuf);
	serv->gui->chanlist_caption_is_stale = FALSE;
}

static void
chanlist_update_buttons (server *serv)
{
	if (serv->gui->chanlist_channels_shown_count)
	{
		gtk_widget_set_sensitive (serv->gui->chanlist_join, TRUE);
		gtk_widget_set_sensitive (serv->gui->chanlist_savelist, TRUE);
	}
	else
	{
		gtk_widget_set_sensitive (serv->gui->chanlist_join, FALSE);
		gtk_widget_set_sensitive (serv->gui->chanlist_savelist, FALSE);
	}
}

static void
chanlist_reset_counters (server *serv)
{
	serv->gui->chanlist_users_found_count = 0;
	serv->gui->chanlist_users_shown_count = 0;
	serv->gui->chanlist_channels_found_count = 0;
	serv->gui->chanlist_channels_shown_count = 0;

	chanlist_update_caption (serv);
	chanlist_update_buttons (serv);
}

/* free up our entire linked list and all the nodes */

static void
chanlist_data_free (server *serv)
{
	GSList *rows;
	chanlistrow *data;

	if (serv->gui->chanlist_data_stored_rows)
	{
		for (rows = serv->gui->chanlist_data_stored_rows; rows != NULL;
			  rows = rows->next)
		{
			data = rows->data;
			g_free (data->topic);
			g_free (data->collation_key);
			g_free (data);
		}

		g_slist_free (serv->gui->chanlist_data_stored_rows);
		serv->gui->chanlist_data_stored_rows = NULL;
	}

	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;
}

/* add any rows we received from the server in the last 0.25s to the GUI */

static void
chanlist_flush_pending (server *serv)
{
	GSList *list = serv->gui->chanlist_pending_rows;
	chanlistrow *row;

	if (!list)
	{
		if (serv->gui->chanlist_caption_is_stale)
			chanlist_update_caption (serv);
		return;
	}

	nx_store_freeze (GET_STORE (serv));
	while (list)
	{
		row = list->data;
		chanlist_store_append (serv, row);
		list = list->next;
	}
	nx_store_thaw (GET_STORE (serv));

	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;
	chanlist_update_caption (serv);
}

static gboolean
chanlist_timeout (server *serv)
{
	chanlist_flush_pending (serv);
	return TRUE;
}

/**
 * Places a data row into the channel list, if and only if the row matches
 * the user and regex/search requirements.
 */
static void
chanlist_place_row_in_gui (server *serv, chanlistrow *next_row, gboolean force)
{
	/* First, update the 'found' counter values */
	serv->gui->chanlist_users_found_count += next_row->users;
	serv->gui->chanlist_channels_found_count++;

	if (serv->gui->chanlist_channels_shown_count == 1)
		/* join & save buttons become live */
		chanlist_update_buttons (serv);

	if (next_row->users < serv->gui->chanlist_minusers)
	{
		serv->gui->chanlist_caption_is_stale = TRUE;
		return;
	}

	if (next_row->users > serv->gui->chanlist_maxusers
		 && serv->gui->chanlist_maxusers > 0)
	{
		serv->gui->chanlist_caption_is_stale = TRUE;
		return;
	}

	if (nx_entry_get_text (serv->gui->chanlist_wild)[0])
	{
		/* Check what the user wants to match. If both buttons or _neither_
		 * button is checked, look for match in both by default. 
		 */
		if (serv->gui->chanlist_match_wants_channel ==
			 serv->gui->chanlist_match_wants_topic)
		{
			if (!chanlist_match (serv, GET_CHAN (next_row))
				 && !chanlist_match (serv, next_row->topic))
			{
				serv->gui->chanlist_caption_is_stale = TRUE;
				return;
			}
		}

		else if (serv->gui->chanlist_match_wants_channel)
		{
			if (!chanlist_match (serv, GET_CHAN (next_row)))
			{
				serv->gui->chanlist_caption_is_stale = TRUE;
				return;
			}
		}

		else if (serv->gui->chanlist_match_wants_topic)
		{
			if (!chanlist_match (serv, next_row->topic))
			{
				serv->gui->chanlist_caption_is_stale = TRUE;
				return;
			}
		}
	}

	if (force || serv->gui->chanlist_channels_shown_count < 20)
	{
		/* makes it appear fast :) */
		chanlist_store_append (serv, next_row);
		chanlist_update_caption (serv);
	}
	else
		/* add it to GUI at the next update interval */
		serv->gui->chanlist_pending_rows = g_slist_prepend (serv->gui->chanlist_pending_rows, next_row);

	/* Update the 'shown' counter values */
	serv->gui->chanlist_users_shown_count += next_row->users;
	serv->gui->chanlist_channels_shown_count++;
}

/* Performs the LIST download from the IRC server. */

static void
chanlist_do_refresh (server *serv)
{
	if (serv->gui->chanlist_flash_tag)
	{
		g_source_remove (serv->gui->chanlist_flash_tag);
		serv->gui->chanlist_flash_tag = 0;
	}

	if (!serv->connected)
	{
		fe_message (_("Not connected."), FE_MSG_ERROR);
		return;
	}

	nx_store_clear (GET_STORE (serv));
	gtk_widget_set_sensitive (serv->gui->chanlist_refresh, FALSE);

	chanlist_data_free (serv);
	chanlist_reset_counters (serv);

	/* can we request a list with minusers arg? */
	if (serv->use_listargs)
	{
		/* yes - it will download faster */
		serv->p_list_channels (serv, "", serv->gui->chanlist_minusers);
		/* don't allow the spin button below this value from now on */
		serv->gui->chanlist_minusers_downloaded = serv->gui->chanlist_minusers;
	}
	else
	{
		/* download all, filter minusers locally only */
		serv->p_list_channels (serv, "", 1);
		serv->gui->chanlist_minusers_downloaded = 1;
	}

/*	gtk_spin_button_set_range ((GtkSpinButton *)serv->gui->chanlist_min_spin,
										serv->gui->chanlist_minusers_downloaded, 999999);*/
}

static void
chanlist_refresh (GtkWidget * wid, server *serv)
{
	chanlist_do_refresh (serv);
}

/**
 * Fills the channel list with stored items from the GSList.
 */
static void
chanlist_build_gui_list (server *serv)
{
	GSList *rows;

	/* first check if the list is present */
	if (serv->gui->chanlist_data_stored_rows == NULL)
	{
		/* start a download */
		chanlist_do_refresh (serv);
		return;
	}

	nx_store_clear (GET_STORE (serv));

	/* discard pending rows FIXME: free the structs? */
	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;

	/* Reset the counters */
	chanlist_reset_counters (serv);

	/* Refill the list */
	nx_store_freeze (GET_STORE (serv));
	for (rows = serv->gui->chanlist_data_stored_rows; rows != NULL;
		  rows = rows->next)
	{
		chanlist_place_row_in_gui (serv, rows->data, TRUE);
	}
	nx_store_thaw (GET_STORE (serv));
}

/**
 * Accepts incoming channel data from inbound.c, allocates new space for a
 * chanlistrow, adds it to our linked list and calls chanlist_place_row_in_gui.
 */
void
fe_add_chan_list (server *serv, char *chan, char *users, char *topic)
{
	chanlistrow *next_row;
	int len = strlen (chan) + 1;

	/* we allocate the struct and channel string in one go */
	next_row = g_malloc (sizeof (chanlistrow) + len);
	memcpy (((char *)next_row) + sizeof (chanlistrow), chan, len);
	next_row->topic = strip_color (topic, -1, STRIP_ALL);
	next_row->collation_key = g_utf8_collate_key (chan, len-1);
	if (!(next_row->collation_key))
		next_row->collation_key = g_strdup (chan);
	next_row->users = atoi (users);

	/* add this row to the data */
	serv->gui->chanlist_data_stored_rows =
		g_slist_prepend (serv->gui->chanlist_data_stored_rows, next_row);

	/* _possibly_ add the row to the gui */
	chanlist_place_row_in_gui (serv, next_row, FALSE);
}

void
fe_chan_list_end (server *serv)
{
	/* download complete */
	chanlist_flush_pending (serv);
	gtk_widget_set_sensitive (serv->gui->chanlist_refresh, TRUE);
}

static void
chanlist_search_pressed (GtkButton * button, server *serv)
{
	chanlist_build_gui_list (serv);
}

static void
chanlist_find_cb (GtkWidget * wid, server *serv)
{
	const char *pattern = nx_entry_get_text (wid);

	/* recompile the regular expression. */
	if (serv->gui->have_regex)
	{
		serv->gui->have_regex = 0;
		g_regex_unref (serv->gui->chanlist_match_regex);
	}

	serv->gui->chanlist_match_regex = g_regex_new (pattern, G_REGEX_CASELESS | G_REGEX_EXTENDED,
												G_REGEX_MATCH_NOTBOL, NULL);

	if (serv->gui->chanlist_match_regex)
		serv->gui->have_regex = 1;
}

static void
chanlist_match_channel_button_toggled (GtkWidget * wid, server *serv)
{
	serv->gui->chanlist_match_wants_channel = nx_toggle_get_active ((GtkWidget *) (wid));
}

static void
chanlist_match_topic_button_toggled (GtkWidget * wid, server *serv)
{
	serv->gui->chanlist_match_wants_topic = nx_toggle_get_active ((GtkWidget *) (wid));
}

static char *
chanlist_get_selected (server *serv, gboolean get_topic)
{
	char *chan = NULL;
	nx_row *row = nx_view_get_selected (serv->gui->chanlist_list);

	if (!row)
		return NULL;
	nx_row_get (row, get_topic ? COL_TOPIC : COL_CHANNEL, &chan, -1);
	return chan;
}

static void
chanlist_join (GtkWidget * wid, server *serv)
{
	char tbuf[CHANLEN + 6];
	char *chan = chanlist_get_selected (serv, FALSE);
	if (chan)
	{
		if (serv->connected && (strcmp (chan, "*") != 0))
		{
			g_snprintf (tbuf, sizeof (tbuf), "join %s", chan);
			handle_command (serv->server_session, tbuf, FALSE);
		} else
			gdk_display_beep (gdk_display_get_default ());
		g_free (chan);
	}
}

static void
chanlist_filereq_done (server *serv, char *file)
{
	time_t t = time (0);
	int fh, users;
	char *chan, *topic;
	char buf[1024];
	nx_row *row;

	if (!file)
		return;

	fh = hexchat_open_file (file, O_TRUNC | O_WRONLY | O_CREAT, 0600,
								 XOF_DOMODE | XOF_FULLPATH);
	if (fh == -1)
		return;

	g_snprintf (buf, sizeof buf, "HexChat Channel List: %s - %s\n",
				 serv->servername, ctime (&t));
	write (fh, buf, strlen (buf));

	for (row = nx_store_first (GET_STORE (serv)); row; row = nx_row_next (row))
	{
		nx_row_get (row,
						COL_CHANNEL, &chan,
						COL_USERS, &users,
						COL_TOPIC, &topic, -1);
		g_snprintf (buf, sizeof buf, "%-16s %-5d%s\n", chan, users, topic);
		g_free (chan);
		g_free (topic);
		write (fh, buf, strlen (buf));
	}

	close (fh);
}

static void
chanlist_save (GtkWidget * wid, server *serv)
{
	if (nx_store_first (GET_STORE (serv)))
		gtkutil_file_req (NULL, _("Select an output filename"), chanlist_filereq_done,
								serv, NULL, NULL, FRF_WRITE);
}

static gboolean
chanlist_flash (server *serv)
{
	if (!(gtk_widget_get_state_flags (serv->gui->chanlist_refresh) & GTK_STATE_FLAG_ACTIVE))
		gtk_widget_set_state_flags (serv->gui->chanlist_refresh, GTK_STATE_FLAG_ACTIVE, TRUE);
	else
		gtk_widget_set_state_flags (serv->gui->chanlist_refresh, GTK_STATE_FLAG_PRELIGHT, TRUE);

	return TRUE;
}

static void
chanlist_minusers (GtkSpinButton *wid, server *serv)
{
	serv->gui->chanlist_minusers = gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_minusers = serv->gui->chanlist_minusers;
	save_config();

	if (serv->gui->chanlist_minusers < serv->gui->chanlist_minusers_downloaded)
	{
		if (serv->gui->chanlist_flash_tag == 0)
			serv->gui->chanlist_flash_tag = g_timeout_add (500, (GSourceFunc)chanlist_flash, serv);
	}
	else
	{
		if (serv->gui->chanlist_flash_tag)
		{
			g_source_remove (serv->gui->chanlist_flash_tag);
			serv->gui->chanlist_flash_tag = 0;
		}
	}
}

static void
chanlist_maxusers (GtkSpinButton *wid, server *serv)
{
	serv->gui->chanlist_maxusers = gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_maxusers = serv->gui->chanlist_maxusers;
	save_config();
}

static void
chanlist_dclick_cb (GtkWidget *view, nx_row *row, gpointer data)
{
	chanlist_join (0, (server *) data);	/* double clicked a row */
}

static void
chanlist_join_item (nx_item *item, gpointer serv)
{
	chanlist_join (NULL, serv);
}

static void
chanlist_copychannel (nx_item *item, gpointer serv)
{
	char *chan = chanlist_get_selected (serv, FALSE);
	if (chan)
	{
		gtkutil_copy_to_clipboard (((server *) serv)->gui->chanlist_window, chan);
		g_free (chan);
	}
}

static void
chanlist_copytopic (nx_item *item, gpointer serv)
{
	char *topic = chanlist_get_selected (serv, TRUE);
	if (topic)
	{
		gtkutil_copy_to_clipboard (((server *) serv)->gui->chanlist_window, topic);
		g_free (topic);
	}
}

static gboolean
chanlist_button_cb (GtkWidget *w, guint button, int n_press, double x, double y,
						  GdkModifierType state, gpointer userdata)
{
	server *serv = userdata;
	nx_menu *menu;
	nx_row *row;
	char *chan;

	if (button != 3)
		return FALSE;

	if (!(row = nx_view_row_at (w, x, y)))
		return FALSE;

	/* select what they right-clicked on */
	nx_view_unselect_all (w);
	nx_view_select (w, row);

	menu = nx_menu_new ();
	mg_create_icon_item (_("_Join Channel"), NX_STOCK_JUMP_TO, menu,
								chanlist_join_item, serv);
	mg_create_icon_item (_("_Copy Channel Name"), NX_STOCK_COPY, menu,
								chanlist_copychannel, serv);
	mg_create_icon_item (_("Copy _Topic Text"), NX_STOCK_COPY, menu,
								chanlist_copytopic, serv);

	chan = chanlist_get_selected (serv, FALSE);
	menu_addfavoritemenu (serv, menu, chan, FALSE);
	g_free (chan);

	/* (on the clicked widget's screen: the popup takes the event's) */
	nx_menu_popup (menu, w, nx_current_event (), NULL, NULL);

	return TRUE;
}

static void
chanlist_destroy_widget (GtkWidget *wid, server *serv)
{
	nx_store_clear (GET_STORE (serv));
	chanlist_data_free (serv);

	if (serv->gui->chanlist_flash_tag)
	{
		g_source_remove (serv->gui->chanlist_flash_tag);
		serv->gui->chanlist_flash_tag = 0;
	}

	if (serv->gui->chanlist_tag)
	{
		g_source_remove (serv->gui->chanlist_tag);
		serv->gui->chanlist_tag = 0;
	}

	if (serv->gui->have_regex)
	{
		g_regex_unref (serv->gui->chanlist_match_regex);
		serv->gui->have_regex = 0;
	}
}

static void
chanlist_closegui (GtkWidget *wid, server *serv)
{
	if (is_server (serv))
		serv->gui->chanlist_window = NULL;
}

static nx_col *
chanlist_add_column (GtkWidget *view, int textcol, int size, char *title, gboolean right_justified,
							nx_row_compare_fn cmp)
{
	nx_col *col = nx_view_add_column (view, title, NX_COL_TEXT, textcol);

	if (right_justified)
		nx_col_set_xalign (col, 1.0);
	nx_col_set_sort_func (col, cmp, NULL);
	nx_col_set_resizable (col, TRUE);
	if (textcol == COL_CHANNEL)
		nx_col_set_fixed_width (col, size);
	else if (textcol == COL_USERS)
		nx_col_set_resizable (col, FALSE);
	return col;
}

static void
chanlist_combo_cb (GtkWidget *combo, gpointer data)
{
	server *serv = data;
	serv->gui->chanlist_search_type = nx_combo_get_active (combo);
}

void
chanlist_opengui (server *serv, int do_refresh)
{
	GtkWidget *vbox, *hbox, *table, *wid, *view;
	char tbuf[256];
	nx_store *store;
	nx_col *chan_col;

	if (serv->gui->chanlist_window)
	{
		mg_bring_tofront (serv->gui->chanlist_window);
		return;
	}

	g_snprintf (tbuf, sizeof tbuf, _("Channel List (%s) - %s"),
				 server_get_network (serv, TRUE), _(DISPLAY_NAME));

	serv->gui->chanlist_pending_rows = NULL;
	serv->gui->chanlist_tag = 0;
	serv->gui->chanlist_flash_tag = 0;
	serv->gui->chanlist_data_stored_rows = NULL;

	if (!serv->gui->chanlist_minusers)
	{
		if (prefs.hex_gui_chanlist_minusers < 1 || prefs.hex_gui_chanlist_minusers > 999999)
		{
			prefs.hex_gui_chanlist_minusers = 5;
			save_config();
		}

		serv->gui->chanlist_minusers = prefs.hex_gui_chanlist_minusers;
	}

	if (!serv->gui->chanlist_maxusers)
	{
		if (prefs.hex_gui_chanlist_maxusers < 1 || prefs.hex_gui_chanlist_maxusers > 999999)
		{
			prefs.hex_gui_chanlist_maxusers = 9999;
			save_config();
		}

		serv->gui->chanlist_maxusers = prefs.hex_gui_chanlist_maxusers;
	}

	serv->gui->chanlist_window =
		mg_create_generic_tab ("ChanList", tbuf, FALSE, TRUE, chanlist_closegui,
								serv, 640, 480, &vbox, serv);
	gtkutil_destroy_on_esc (serv->gui->chanlist_window);

	nx_set_border_width (vbox, 6);
	gtk_box_set_spacing (GTK_BOX (vbox), 12);

	/* make a label to store the user/channel info */
	wid = gtk_label_new (NULL);
	nx_box_pack_start (vbox, wid, 0, 0, 0);
	nx_widget_show (wid);
	serv->gui->chanlist_label = wid;

	/* ============================================================= */

	store = nx_store_new (FALSE, N_COLUMNS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_POINTER);
	view = nx_view_new_scrolled (vbox, store);
	g_object_unref (store);
	nx_scrolled_window_set_shadow_in (gtk_widget_get_parent (view), TRUE);
	serv->gui->chanlist_list = view;

	nx_view_on_activate (view, chanlist_dclick_cb, serv);
	nx_on_click (view, chanlist_button_cb, serv);

	chan_col = chanlist_add_column (view, COL_CHANNEL, 96, _("Channel"), FALSE, chanlist_cmp_channel);
	chanlist_add_column (view, COL_USERS,   50, _("Users"),   TRUE, chanlist_cmp_users);
	chanlist_add_column (view, COL_TOPIC,   50, _("Topic"),   FALSE, chanlist_cmp_topic);
	/* rows without padding, as GTK3's ypad 0 */
	nx_view_set_compact (view, TRUE);
	nx_view_set_search_column (view, COL_CHANNEL);
	nx_view_sort_by (view, chan_col, GTK_SORT_ASCENDING);
	nx_widget_show (view);

	/* ============================================================= */

	table = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (table), 12);
	gtk_grid_set_row_spacing (GTK_GRID (table), 3);
	nx_box_pack_start (vbox, table, 0, 1, 0);
	nx_widget_show (table);

	wid = gtkutil_button (NULL, NX_STOCK_FIND, 0, chanlist_search_pressed, serv,
								 _("_Search"));
	serv->gui->chanlist_search = wid;
	gtkutil_grid_attach (table, wid, 3, 4, 3, 4,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);

	wid = gtkutil_button (NULL, NX_STOCK_REFRESH, 0, chanlist_refresh, serv,
								 _("_Download List"));
	serv->gui->chanlist_refresh = wid;
	gtkutil_grid_attach (table, wid, 3, 4, 2, 3,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);

	wid = gtkutil_button (NULL, NX_STOCK_SAVE_AS, 0, chanlist_save, serv,
								 _("Save _List..."));
	serv->gui->chanlist_savelist = wid;
	gtkutil_grid_attach (table, wid, 3, 4, 1, 2,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);

	wid = gtkutil_button (NULL, NX_STOCK_JUMP_TO, 0, chanlist_join, serv,
						 _("_Join Channel"));
	serv->gui->chanlist_join = wid;
	gtkutil_grid_attach (table, wid, 3, 4, 0, 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);

	/* ============================================================= */

	wid = gtk_label_new (_("Show only:"));
	gtk_label_set_xalign (GTK_LABEL (wid), 0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 0, 1, 3, 4,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	nx_widget_show (wid);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_spacing (GTK_BOX (hbox), 9);
	gtkutil_grid_attach (table, hbox, 1, 2, 3, 4,
							GU_FILL, GU_FILL, 0, 0);
	nx_widget_show (hbox);

	wid = gtk_label_new (_("channels with"));
	nx_box_pack_start (hbox, wid, 0, 0, 0);
	nx_widget_show (wid);

	wid = gtk_spin_button_new_with_range (1, 999999, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (wid),
										serv->gui->chanlist_minusers);
	g_signal_connect (G_OBJECT (wid), "value_changed",
							G_CALLBACK (chanlist_minusers), serv);
	nx_box_pack_start (hbox, wid, 0, 0, 0);
	nx_widget_show (wid);
	serv->gui->chanlist_min_spin = wid;

	wid = gtk_label_new (_("to"));
	nx_box_pack_start (hbox, wid, 0, 0, 0);
	nx_widget_show (wid);

	wid = gtk_spin_button_new_with_range (1, 999999, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (wid),
										serv->gui->chanlist_maxusers);
	g_signal_connect (G_OBJECT (wid), "value_changed",
							G_CALLBACK (chanlist_maxusers), serv);
	nx_box_pack_start (hbox, wid, 0, 0, 0);
	nx_widget_show (wid);

	wid = gtk_label_new (_("users."));
	nx_box_pack_start (hbox, wid, 0, 0, 0);
	nx_widget_show (wid);

	/* ============================================================= */

	wid = gtk_label_new (_("Look in:"));
	gtk_label_set_xalign (GTK_LABEL (wid), 0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 0, 1, 2, 3,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	nx_widget_show (wid);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_spacing (GTK_BOX (hbox), 12);
	gtkutil_grid_attach (table, hbox, 1, 2, 2, 3,
							GU_FILL, GU_FILL, 0, 0);
	nx_widget_show (hbox);

	wid = gtk_check_button_new_with_label (_("Channel name"));
	nx_toggle_set_active ((GtkWidget *) (wid), TRUE);
	g_signal_connect (G_OBJECT (wid), "toggled",
							  G_CALLBACK(chanlist_match_channel_button_toggled), serv);
	nx_box_pack_start (hbox, wid, 0, 0, 0);
	nx_widget_show (wid);

	wid = gtk_check_button_new_with_label (_("Topic"));
	nx_toggle_set_active ((GtkWidget *) (wid), TRUE);
	g_signal_connect (G_OBJECT (wid), "toggled",
							  G_CALLBACK (chanlist_match_topic_button_toggled),
							  serv);
	nx_box_pack_start (hbox, wid, 0, 0, 0);
	nx_widget_show (wid);

	serv->gui->chanlist_match_wants_channel = 1;
	serv->gui->chanlist_match_wants_topic = 1;

	/* ============================================================= */

	wid = gtk_label_new (_("Search type:"));
	gtk_label_set_xalign (GTK_LABEL (wid), 0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 0, 1, 1, 2,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	nx_widget_show (wid);

	{
		const char *types[] = { _("Simple Search"), _("Pattern Match (Wildcards)"),
										_("Regular Expression"), NULL };
		wid = nx_combo_new (types);
	}
	nx_combo_set_active (wid, serv->gui->chanlist_search_type);
	gtkutil_grid_attach (table, wid, 1, 2, 1, 2,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	nx_combo_on_changed (wid, chanlist_combo_cb, serv);
	nx_widget_show (wid);

	/* ============================================================= */

	wid = gtk_label_new (_("Find:"));
	gtk_label_set_xalign (GTK_LABEL (wid), 0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 0, 1, 0, 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	nx_widget_show (wid);

	wid = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY(wid), 255);
	g_signal_connect (G_OBJECT (wid), "changed",
							  G_CALLBACK (chanlist_find_cb), serv);
	g_signal_connect (G_OBJECT (wid), "activate",
							  G_CALLBACK (chanlist_search_pressed),
							  (gpointer) serv);
	gtkutil_grid_attach (table, wid, 1, 2, 0, 1,
							GU_EXPAND | GU_FILL, 0, 0, 0);
	nx_widget_show (wid);
	serv->gui->chanlist_wild = wid;

	chanlist_find_cb (wid, serv);

	/* ============================================================= */

	wid = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
	gtkutil_grid_attach (table, wid, 2, 3, 0, 5,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	nx_widget_show (wid);

	g_signal_connect (G_OBJECT (serv->gui->chanlist_window), "destroy",
							G_CALLBACK (chanlist_destroy_widget), serv);

	/* reset the counters. */
	chanlist_reset_counters (serv);

	serv->gui->chanlist_tag = g_timeout_add (250, (GSourceFunc)chanlist_timeout, serv);

	if (do_refresh)
		chanlist_do_refresh (serv);

	chanlist_update_buttons (serv);
	nx_widget_show (serv->gui->chanlist_window);
	gtk_widget_grab_focus (serv->gui->chanlist_refresh);
}
