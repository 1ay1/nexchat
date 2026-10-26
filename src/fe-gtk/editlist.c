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
#include <sys/types.h>
#include <sys/stat.h>


#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/cfgfiles.h"
#include "../common/hexchatc.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "menu.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "maingui.h"
#include "editlist.h"

enum
{
	NAME_COLUMN,
	CMD_COLUMN,
	N_COLUMNS
};

static GtkWidget *editlist_win = NULL;
static GSList *editlist_list = NULL;

static GtkWidget *
get_view (void)
{
	return g_object_get_data (G_OBJECT (editlist_win), "view");
}

static void
editlist_save (GtkWidget *igad, gchar *file)
{
	nx_row *row;
	char buf[512];
	char *name, *cmd;
	int fh;

	fh = hexchat_open_file (file, O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		for (row = nx_store_first (nx_view_get_store (get_view ())); row; row = nx_row_next (row))
		{
			name = NULL;
			cmd = NULL;
			nx_row_get (row, NAME_COLUMN, &name, CMD_COLUMN, &cmd, -1);
			g_snprintf (buf, sizeof (buf), "NAME %s\nCMD %s\n\n", name, cmd);
			write_all (fh, buf, strlen (buf));
			g_free (name);
			g_free (cmd);
		}

		close (fh);
		nx_widget_destroy (editlist_win);
		if (editlist_list == replace_list)
		{
			list_free (&replace_list);
			list_loadconf (file, &replace_list, 0);
		} else if (editlist_list == popup_list)
		{
			list_free (&popup_list);
			list_loadconf (file, &popup_list, 0);
		} else if (editlist_list == button_list)
		{
			GSList *list = sess_list;
			struct session *sess;
			list_free (&button_list);
			list_loadconf (file, &button_list, 0);
			while (list)
			{
				sess = (struct session *) list->data;
				fe_buttons_update (sess);
				list = list->next;
			}
		} else if (editlist_list == dlgbutton_list)
		{
			GSList *list = sess_list;
			struct session *sess;
			list_free (&dlgbutton_list);
			list_loadconf (file, &dlgbutton_list, 0);
			while (list)
			{
				sess = (struct session *) list->data;
				fe_dlgbuttons_update (sess);
				list = list->next;
			}
		} else if (editlist_list == ctcp_list)
		{
			list_free (&ctcp_list);
			list_loadconf (file, &ctcp_list, 0);
		} else if (editlist_list == command_list)
		{
			list_free (&command_list);
			list_loadconf (file, &command_list, 0);
		} else if (editlist_list == usermenu_list)
		{
			list_free (&usermenu_list);
			list_loadconf (file, &usermenu_list, 0);
			usermenu_update ();
		} else
		{
			list_free (&urlhandler_list);
			list_loadconf (file, &urlhandler_list, 0);
		}
	}
}

static void
editlist_load (nx_store *store, GSList *list)
{
	struct popup *pop;

	while (list)
	{
		pop = (struct popup *) list->data;

		nx_row_set (nx_store_append (store, NULL),
		            NAME_COLUMN, pop->name, CMD_COLUMN, pop->cmd, -1);

		list = list->next;
	}
}

static void
editlist_delete (GtkWidget *wid, gpointer userdata)
{
	GtkWidget *view = get_view ();
	nx_row *row = nx_view_get_selected (view), *next;

	if (row)
	{
		/* delete this row, select next one */
		next = nx_row_next (row);
		nx_row_remove (row);
		if (next)
		{
			nx_view_scroll_to (view, next, NX_SCROLL_BOTTOM);
			nx_view_set_cursor (view, next, NULL);
		}
	}
}

static void
editlist_add (GtkWidget *wid, gpointer userdata)
{
	GtkWidget *view = get_view ();
	nx_row *row = nx_store_append (nx_view_get_store (view), NULL);

	/* make sure the new row is visible and selected */
	nx_view_scroll_to (view, row, NX_SCROLL_MIN);
	nx_view_set_cursor (view, row, nx_view_get_column (view, NAME_COLUMN));
}

static void
editlist_close (GtkWidget *wid, gpointer userdata)
{
	nx_widget_destroy (editlist_win);
	editlist_win = NULL;
}

static void
editlist_edited (nx_row *row, const char *new_text, gpointer data)
{
	nx_row_set (row, GPOINTER_TO_INT (data), new_text, -1);
}

static gboolean
editlist_keypress (GtkWidget *wid, guint keyval, GdkModifierType state, gpointer userdata)
{
	GtkWidget *view = get_view ();
	nx_row *row, *other;
	gboolean handled = FALSE;
	int delta;

	if (state & GDK_SHIFT_MASK)
	{
		if (keyval == GDK_KEY_Up)
		{
			handled = TRUE;
			delta = -1;
		}
		else if (keyval == GDK_KEY_Down)
		{
			handled = TRUE;
			delta = 1;
		}
	}

	if (handled)
	{
		row = nx_view_get_selected (view);
		other = row ? (delta == 1 ? nx_row_next (row) : nx_row_prev (row)) : NULL;
		if (other)
		{
			nx_row_swap (row, other);
			/* the moved row stays selected, so it can be moved on */
			nx_view_set_cursor (view, row, NULL);
		}
	}

	return handled;
}

static GtkWidget *
editlist_treeview_new (GtkWidget *box, char *title1, char *title2)
{
	nx_store *store;
	GtkWidget *view;
	nx_col *col;

	store = nx_store_new (FALSE, N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);
	nx_scrolled_window_set_shadow_in (gtk_widget_get_parent (view), TRUE);
	nx_view_set_reorderable (view, TRUE);
	
	nx_on_key (view, editlist_keypress, NULL);

	col = nx_view_add_column (view, title1, NX_COL_TEXT, NAME_COLUMN);
	nx_col_set_editable (col, TRUE);
	nx_col_on_edited (col, editlist_edited, GINT_TO_POINTER (NAME_COLUMN));
	nx_col_set_resizable (col, TRUE);
	nx_col_set_min_width (col, 100);

	col = nx_view_add_column (view, title2, NX_COL_TEXT, CMD_COLUMN);
	nx_col_set_editable (col, TRUE);
	nx_col_on_edited (col, editlist_edited, GINT_TO_POINTER (CMD_COLUMN));

	nx_widget_show_all (box);

	return view;
}


void
editlist_gui_open (char *title1, char *title2, GSList *list, char *title, char *wmclass,
					char *file, char *help)
{
	GtkWidget *vbox, *box;
	GtkWidget *view;

	if (editlist_win)
	{
		mg_bring_tofront (editlist_win);
		return;
	}

	editlist_win = mg_create_generic_tab (wmclass, title, TRUE, FALSE,
												editlist_close, NULL, 450, 250, &vbox, 0);

	editlist_list = list;

	view = editlist_treeview_new (vbox, title1, title2);
	g_object_set_data (G_OBJECT (editlist_win), "view", view);

	if (help)
		gtk_widget_set_tooltip_text (view, help);

	box = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		nx_box_pack_start (vbox, box, FALSE, FALSE, 2);
	nx_set_border_width (box, 5);
	nx_widget_show (box);

	gtkutil_button (box, NX_STOCK_NEW, 0, editlist_add,
					NULL, _("Add"));
	gtkutil_button (box, NX_STOCK_DELETE, 0, editlist_delete,
					NULL, _("Delete"));
	gtkutil_button (box, NX_STOCK_CANCEL, 0, editlist_close,
					NULL, _("Cancel"));
	gtkutil_button (box, NX_STOCK_SAVE, 0, editlist_save,
					file, _("Save"));

	editlist_load (nx_view_get_store (view), list);

	nx_widget_show (editlist_win);
}
