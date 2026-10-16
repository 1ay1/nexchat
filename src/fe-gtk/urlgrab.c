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

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/url.h"
#include "../common/tree.h"
#include "gtkutil.h"
#include "menu.h"
#include "maingui.h"
#include "urlgrab.h"
#include "nxlist.h"

/* model for the URL treeview */
enum
{
	URL_COLUMN,
	N_COLUMNS
};

static GtkWidget *urlgrabberwindow = 0;


static gboolean
url_treeview_url_clicked_cb (GtkWidget *view, guint button, int n_press,
                             double x, double y, GdkModifierType state, gpointer data)
{
	gchar *url;
	nx_row *row = nx_view_row_at (view, x, y);

	if (!row)
		return FALSE;

	/* select what they right-clicked on */
	nx_view_unselect_all (view);
	nx_view_select (view, row);

	nx_row_get (row, URL_COLUMN, &url, -1);
	
	switch (button)
	{
		case 1:
			if (n_press == 2)
				fe_open_url (url);
			break;
		case 3:
			menu_urlmenu (nx_current_event (), url);
			break;
		default:
			break;
	}
	g_free (url);

	return FALSE;
}

static GtkWidget *
url_treeview_new (GtkWidget *box)
{
	nx_store *store;
	GtkWidget *view;

	store = nx_store_new (FALSE, N_COLUMNS, G_TYPE_STRING);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);
	nx_view_add_column (view, _("URL"), NX_COL_TEXT, URL_COLUMN);
	nx_view_set_search_column (view, URL_COLUMN);
	nx_on_click (view, url_treeview_url_clicked_cb, NULL);
	/* don't want column headers */
	nx_view_set_headers_visible (view, FALSE);
	nx_widget_show (view);
	return view;
}

static void
url_closegui (GtkWidget *wid, gpointer userdata)
{
	urlgrabberwindow = 0;
}

static void
url_button_clear (void)
{
	url_clear ();
	nx_store_clear (g_object_get_data (G_OBJECT (urlgrabberwindow), "model"));
}

static void
url_button_copy (GtkWidget *widget, gpointer data)
{
	GtkWidget *view = data;
	nx_row *row = nx_view_get_selected (view);
	gchar *url = NULL;

	if (row)
	{
		nx_row_get (row, URL_COLUMN, &url, -1);
		gtkutil_copy_to_clipboard (view, url);
		g_free (url);
	}
}

static void
url_save_callback (void *arg1, char *file)
{
	if (file)
	{
		url_save_tree (file, "w", TRUE);
	}
}

static void
url_button_save (void)
{
	gtkutil_file_req (NULL, _("Select an output filename"),
							url_save_callback, NULL, NULL, NULL, FRF_WRITE);
}

void
fe_url_add (const char *urltext)
{
	nx_store *store;
	nx_row *row;
	
	if (urlgrabberwindow)
	{
		store = g_object_get_data (G_OBJECT (urlgrabberwindow), "model");
		nx_row_set (nx_store_prepend (store, NULL), URL_COLUMN, urltext, -1);

		/* remove any overflow */
		if (prefs.hex_url_grabber_limit > 0)
		{
			while ((row = nx_store_nth (store, NULL, prefs.hex_url_grabber_limit)))
				nx_row_remove (row);
		}
	}
}

static int
populate_cb (char *urltext, gpointer userdata)
{
	fe_url_add (urltext);
	return TRUE;
}

void
url_opengui ()
{
	GtkWidget *vbox, *hbox, *view;
	char buf[128];

	if (urlgrabberwindow)
	{
		mg_bring_tofront (urlgrabberwindow);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("URL Grabber - %s"), _(DISPLAY_NAME));
	urlgrabberwindow =
		mg_create_generic_tab ("UrlGrabber", buf, FALSE, TRUE, url_closegui, NULL,
								 400, 256, &vbox, 0);
	gtkutil_destroy_on_esc (urlgrabberwindow);
	view = url_treeview_new (vbox);
	g_object_set_data (G_OBJECT (urlgrabberwindow), "model", nx_view_get_store (view));

	hbox = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		nx_set_border_width (hbox, 5);
	nx_box_pack_end (vbox, hbox, 0, 0, 0);
	nx_widget_show (hbox);

	gtkutil_button (hbox, NX_STOCK_CLEAR,
						 _("Clear list"), url_button_clear, 0, _("Clear"));
	gtkutil_button (hbox, NX_STOCK_COPY,
						 _("Copy selected URL"), url_button_copy, view, _("Copy"));
	gtkutil_button (hbox, NX_STOCK_SAVE_AS,
						 _("Save list to a file"), url_button_save, 0, _("Save As..."));

	nx_widget_show (urlgrabberwindow);

	if (prefs.hex_url_grabber)
		tree_foreach (url_tree, (tree_traverse_func *)populate_cb, NULL);
	else
	{
		nx_store_clear (nx_view_get_store (view));
		fe_url_add ("URL Grabber is disabled.");
	}
}
