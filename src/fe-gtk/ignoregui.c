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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/ignore.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "maingui.h"

enum
{
	MASK_COLUMN,
	CHAN_COLUMN,
	PRIV_COLUMN,
	NOTICE_COLUMN,
	CTCP_COLUMN,
	DCC_COLUMN,
	INVITE_COLUMN,
	UNIGNORE_COLUMN,
	N_COLUMNS
};

static GtkWidget *ignorewin = 0;

static GtkWidget *num_ctcp;
static GtkWidget *num_priv;
static GtkWidget *num_chan;
static GtkWidget *num_noti;
static GtkWidget *num_invi;

static GtkWidget *
get_view (void)
{
	return g_object_get_data (G_OBJECT (ignorewin), "view");
}

static int
ignore_get_flags (nx_row *row)
{
	int flags = 0;

	if (nx_row_get_bool (row, CHAN_COLUMN))
		flags |= IG_CHAN;
	if (nx_row_get_bool (row, PRIV_COLUMN))
		flags |= IG_PRIV;
	if (nx_row_get_bool (row, NOTICE_COLUMN))
		flags |= IG_NOTI;
	if (nx_row_get_bool (row, CTCP_COLUMN))
		flags |= IG_CTCP;
	if (nx_row_get_bool (row, DCC_COLUMN))
		flags |= IG_DCC;
	if (nx_row_get_bool (row, INVITE_COLUMN))
		flags |= IG_INVI;
	if (nx_row_get_bool (row, UNIGNORE_COLUMN))
		flags |= IG_UNIG;
	return flags;
}

static void
mask_edited (nx_row *row, const char *new, gpointer dat)
{
	char *old;
	int flags;

	nx_row_get (row, MASK_COLUMN, &old, -1);
	
	if (!strcmp (old, new))	/* no change */
		;
	else if (ignore_exists ((char *) new))	/* duplicate, ignore */
		fe_message (_("That mask already exists."), FE_MSG_ERROR);
	else
	{
		/* delete old mask, and add new one with original flags */
		ignore_del (old, NULL);
		flags = ignore_get_flags (row);
		ignore_add ((char *) new, flags, TRUE);

		/* update tree */
		nx_row_set (row, MASK_COLUMN, new, -1);
	}
	g_free (old);
	
}

static void
option_toggled (nx_row *row, gpointer data)
{
	int col_id = GPOINTER_TO_INT (data);
	char *mask;
	int flags;

	/* update model */
	nx_row_set (row, col_id, !nx_row_get_bool (row, col_id), -1);

	/* update ignore list */
	nx_row_get (row, MASK_COLUMN, &mask, -1);
	flags = ignore_get_flags (row);
	if (ignore_add (mask, flags, TRUE) != 2)
		g_warning ("ignore treeview is out of sync!\n");
	
	g_free (mask);
}

static GtkWidget *
ignore_treeview_new (GtkWidget *box)
{
	static const char *const titles[] = { N_("Mask"), N_("Channel"), N_("Private"), N_("Notice"),
	                                      N_("CTCP"), N_("DCC"), N_("Invite"), N_("Unignore") };
	nx_store *store;
	GtkWidget *view;
	nx_col *col;
	int col_id;

	store = nx_store_new (FALSE, N_COLUMNS, G_TYPE_STRING,
	                      G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
	                      G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
	                      G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
	                      G_TYPE_BOOLEAN);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);

	/* attach to signals and customise columns */
	for (col_id = 0; col_id < N_COLUMNS; col_id++)
	{
		if (col_id > 0)	/* it's a toggle button column */
		{
			col = nx_view_add_column (view, _(titles[col_id]), NX_COL_TOGGLE, col_id);
			nx_col_on_toggled (col, option_toggled, GINT_TO_POINTER (col_id));
		} else	/* mask column */
		{
			col = nx_view_add_column (view, _(titles[col_id]), NX_COL_TEXT, col_id);
			nx_col_set_editable (col, TRUE);
			nx_col_on_edited (col, mask_edited, NULL);
			/* make this column sortable */
			nx_col_set_sort_column (col, col_id);
			nx_col_set_min_width (col, 272);
			nx_col_set_expand (col, TRUE);
		}
		/* centre titles */
		nx_col_set_title_align (col, 0.5);
	}
	nx_view_set_search_column (view, MASK_COLUMN);
	
	nx_widget_show (view);
	return view;
}

static void
ignore_delete_entry_clicked (GtkWidget * wid, struct session *sess)
{
	GtkWidget *view = get_view ();
	nx_row *row = nx_view_get_selected (view), *next;
	char *mask = NULL;

	if (row)
	{
		nx_row_get (row, MASK_COLUMN, &mask, -1);
		/* delete this row, select next one */
		next = nx_row_next (row);
		nx_row_remove (row);
		if (next)
		{
			nx_view_scroll_to (view, next, NX_SCROLL_BOTTOM);
			nx_view_set_cursor (view, next, NULL);
		}

		ignore_del (mask, NULL);
		g_free (mask);
	}
}

static void
ignore_store_new (int cancel, char *mask, gpointer data)
{
	GtkWidget *view = get_view ();
	nx_row *row;
	int flags = IG_CHAN | IG_PRIV | IG_NOTI | IG_CTCP | IG_DCC | IG_INVI;

	if (cancel)
		return;
	/* check if it already exists */
	if (ignore_exists (mask))
	{
		fe_message (_("That mask already exists."), FE_MSG_ERROR);
		return;
	}

	ignore_add (mask, flags, TRUE);

	row = nx_store_append (nx_view_get_store (view), NULL);
	/* ignore everything by default */
	nx_row_set (row, 0, mask, 1, TRUE, 2, TRUE, 3, TRUE,
	            4, TRUE, 5, TRUE, 6, TRUE, 7, FALSE, -1);
	/* make sure the new row is visible and selected */
	nx_view_scroll_to (view, row, NX_SCROLL_BOTTOM);
	nx_view_set_cursor (view, row, NULL);
}

static void
ignore_clear_cb (GtkWidget *dialog, gint response)
{
	nx_store *store = nx_view_get_store (get_view ());
	nx_row *row;
	char *mask;

	nx_widget_destroy (GTK_WIDGET (dialog));

	if ((row = nx_store_first (store)) && response == GTK_RESPONSE_OK)
	{
		/* remove from ignore_list */
		do
		{
			mask = NULL;
			nx_row_get (row, MASK_COLUMN, &mask, -1);
			ignore_del (mask, NULL);
			g_free (mask);
		}
		while ((row = nx_row_next (row)));

		/* remove from GUI */
		nx_store_clear (store);
	}
}

static void
ignore_clear_entry_clicked (GtkWidget * wid)
{
	GtkWidget *dialog;

	dialog = nx_message_dialog_new (NULL, FALSE, NX_MSG_QUESTION, NX_BUTTONS_OK_CANCEL, FALSE,
					_("Are you sure you want to remove all ignores?"));
	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (ignore_clear_cb), NULL);
	nx_window_set_position_mouse (dialog);
	nx_widget_show (dialog);
}

static void
ignore_new_entry_clicked (GtkWidget * wid, struct session *sess)
{
	fe_get_str (_("Enter mask to ignore:"), "nick!userid@host.com",
	            ignore_store_new, NULL);

}

static void
close_ignore_gui_callback (void)
{
	ignore_save ();
	ignorewin = 0;
}

static GtkWidget *
ignore_stats_entry (GtkWidget * box, char *label, int value)
{
	GtkWidget *wid;
	char buf[16];

	sprintf (buf, "%d", value);
	gtkutil_label_new (label, box);
	wid = gtkutil_entry_new (16, box, 0, 0);
	gtk_widget_set_size_request (wid, 30, -1);
	/* GTK3 entries also *want* width-chars (default ~20 chars) on top of the
	   size request; GTK2 used the request as the natural width. Without this
	   the five stat boxes make the window ~1100px wide. */
	nx_entry_set_width_chars (wid, 4);
	gtk_editable_set_editable (GTK_EDITABLE (wid), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (wid), FALSE);
	nx_entry_set_text (wid, buf);

	return wid;
}

void
ignore_gui_open (void)
{
	GtkWidget *vbox, *box, *stat_box, *frame;
	GtkWidget *view;
	nx_store *store;
	GSList *temp = ignore_list;
	char *mask;
	gboolean private, chan, notice, ctcp, dcc, invite, unignore;
	char buf[128];

	if (ignorewin)
	{
		mg_bring_tofront (ignorewin);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("Ignore list - %s"), _(DISPLAY_NAME));
	ignorewin =
			  mg_create_generic_tab ("IgnoreList", buf, FALSE, TRUE,
											close_ignore_gui_callback,
											NULL, 700, 300, &vbox, 0);
	gtkutil_destroy_on_esc (ignorewin);

	view = ignore_treeview_new (vbox);
	g_object_set_data (G_OBJECT (ignorewin), "view", view);
	
#if GTK_CHECK_VERSION(4, 0, 0)
	frame = nx_labelled_frame_new (_("Ignore Stats:"));
#else
	frame = gtk_frame_new (_("Ignore Stats:"));
#endif
	nx_widget_show (frame);

	stat_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	nx_set_border_width (stat_box, 6);
#if GTK_CHECK_VERSION(4, 0, 0)
	/* below the label's lower half */
	gtk_widget_set_margin_top (stat_box, 6 + GPOINTER_TO_INT (g_object_get_data (G_OBJECT (frame), "nx-label-h")));
#endif
	gtkutil_container_add (GTK_WIDGET (frame), stat_box);
	nx_widget_show (stat_box);

	num_chan = ignore_stats_entry (stat_box, _("Channel:"), ignored_chan);
	num_priv = ignore_stats_entry (stat_box, _("Private:"), ignored_priv);
	num_noti = ignore_stats_entry (stat_box, _("Notice:"), ignored_noti);
	num_ctcp = ignore_stats_entry (stat_box, _("CTCP:"), ignored_ctcp);
	num_invi = ignore_stats_entry (stat_box, _("Invite:"), ignored_invi);

#if GTK_CHECK_VERSION(4, 0, 0)
	nx_box_pack_start (vbox, gtk_widget_get_parent (frame), 0, 0, 5);
#else
	nx_box_pack_start (vbox, frame, 0, 0, 5);
#endif

	box = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		nx_box_pack_start (vbox, box, FALSE, FALSE, 2);
	nx_set_border_width (box, 5);
	nx_widget_show (box);

	gtkutil_button (box, NX_STOCK_NEW, 0, ignore_new_entry_clicked, 0,
						 _("Add..."));
	gtkutil_button (box, NX_STOCK_DELETE, 0, ignore_delete_entry_clicked,
						 0, _("Delete"));
	gtkutil_button (box, NX_STOCK_CLEAR, 0, ignore_clear_entry_clicked,
						 0, _("Clear"));

	store = nx_view_get_store (view);

	while (temp)
	{
		struct ignore *ignore = temp->data;

		mask = ignore->mask;
		chan = (ignore->type & IG_CHAN);
		private = (ignore->type & IG_PRIV);
		notice = (ignore->type & IG_NOTI);
		ctcp = (ignore->type & IG_CTCP);
		dcc = (ignore->type & IG_DCC);
		invite = (ignore->type & IG_INVI);
		unignore = (ignore->type & IG_UNIG);

		nx_row_set (nx_store_append (store, NULL),
		                    MASK_COLUMN, mask,
		                    CHAN_COLUMN, chan,
		                    PRIV_COLUMN, private,
		                    NOTICE_COLUMN, notice,
		                    CTCP_COLUMN, ctcp,
		                    DCC_COLUMN, dcc,
		                    INVITE_COLUMN, invite,
		                    UNIGNORE_COLUMN, unignore,
		                    -1);
		
		temp = temp->next;
	}
	nx_widget_show (ignorewin);
}

void
fe_ignore_update (int level)
{
	/* some ignores have changed via /ignore, we should update
	   the gui now */
	/* level 1 = the list only. */
	/* level 2 = the numbers only. */
	/* for now, ignore level 1, since the ignore GUI isn't realtime,
	   only saved when you click OK */
	char buf[16];

	if (level == 2 && ignorewin)
	{
		sprintf (buf, "%d", ignored_ctcp);
		nx_entry_set_text (num_ctcp, buf);

		sprintf (buf, "%d", ignored_noti);
		nx_entry_set_text (num_noti, buf);

		sprintf (buf, "%d", ignored_chan);
		nx_entry_set_text (num_chan, buf);

		sprintf (buf, "%d", ignored_invi);
		nx_entry_set_text (num_invi, buf);

		sprintf (buf, "%d", ignored_priv);
		nx_entry_set_text (num_priv, buf);
	}
}
