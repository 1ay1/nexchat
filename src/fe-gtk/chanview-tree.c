/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

/* file included in chanview.c */

typedef struct
{
	GtkWidget *tree;		/* nxlist view */
	GtkWidget *scrollw;	/* scrolledWindow */
} treeview;

#include <gdk/gdk.h>

static void 	/* row-activated, when a row is double clicked */
cv_tree_activated_cb (GtkWidget *view, nx_row *row, gpointer data)
{
	if (nx_view_is_expanded (view, row))
		nx_view_collapse (view, row);
	else
		nx_view_expand (view, row);
}

static void		/* row selected callback */
cv_tree_sel_cb (GtkWidget *view, gpointer data)
{
	chanview *cv = data;
	nx_row *row = nx_view_get_selected (view);
	chan *ch;

	if (row)
	{
		ch = nx_row_get_pointer (row, COL_CHAN);

		cv->focused = ch;
		cv->cb_focus (cv, ch, ch->tag, ch->userdata);
	}
}

static gboolean
cv_tree_click_cb (GtkWidget *w, guint button, int n_press, double x, double y,
						GdkModifierType state, gpointer userdata)
{
	chanview *cv = userdata;
	nx_row *row = nx_view_row_at (w, x, y);
	chan *ch;

	if (!row)
		return FALSE;
	ch = nx_row_get_pointer (row, COL_CHAN);
	return cv->cb_contextmenu (cv, ch, ch->tag, ch->userdata, button, n_press);
}

static gboolean
cv_tree_scroll_event_cb (GtkWidget *widget, GdkScrollDirection dir, GdkModifierType state, gpointer user_data)
{
	if (prefs.hex_gui_tab_scrollchans)
	{
		if (dir == GDK_SCROLL_DOWN)
			mg_switch_page (1, 1);
		else if (dir == GDK_SCROLL_UP)
			mg_switch_page (1, -1);

		return TRUE;
	}

	return FALSE;
}

static void
cv_tree_init (chanview *cv)
{
	GtkWidget *view, *win;
	nx_col *col;

	win = nx_scrolled_window_new ();
	/*nx_set_border_width (win, 1);*/
	nx_scrolled_window_set_shadow_in (win, TRUE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (win),
											  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtkutil_container_add (cv->box, mg_drop_overlay (win));
	nx_widget_show (win);

	view = nx_view_new (cv->store);
	gtk_widget_set_name (view, "hexchat-tree");	/* font/colours come from fe_apply_css() */
	gtk_widget_set_can_focus (view, FALSE);
	nx_view_set_headers_visible (view, FALSE);
	nx_view_set_select_mode (view, GTK_SELECTION_BROWSE);
	if (prefs.hex_gui_compact)
		nx_view_set_compact (view, TRUE);

	if (prefs.hex_gui_tab_dots)
		nx_view_set_tree_lines (view, TRUE);

	/* Indented channels with no server looks silly, but we still want expanders */
	if (!prefs.hex_gui_tab_server)
		nx_view_set_unindented (view, TRUE);

	gtkutil_container_add (GTK_WIDGET (win), view);

	/* the name, with the icon before it */
	col = nx_view_add_column (view, NULL, NX_COL_TEXT, COL_NAME);
	if (cv->use_icons)
		nx_col_add_icon (col, COL_PIXBUF);
	nx_col_bind (col, NX_BIND_ATTRS, COL_ATTR);

	nx_view_on_select (view, cv_tree_sel_cb, cv);
	nx_on_click (view, cv_tree_click_cb, cv);
	nx_view_on_activate (view, cv_tree_activated_cb, NULL);
	nx_on_scroll (view, cv_tree_scroll_event_cb, NULL);

	/* takes the user list's layout drags, and can be dragged itself */
	mg_layout_drop_dest (view, NX_DND_USERLIST);
	nx_dnd_layout_source (view, NX_DND_CHANVIEW);

	((treeview *)cv)->tree = view;
	((treeview *)cv)->scrollw = win;
	nx_widget_show (view);
}

static void
cv_tree_postinit (chanview *cv)
{
	nx_view_expand_all (((treeview *)cv)->tree);
}

static void *
cv_tree_add (chanview *cv, chan *ch, char *name, nx_row *parent)
{
	/* expand the parent node */
	if (parent)
		nx_view_expand (((treeview *)cv)->tree, parent);

	return NULL;
}

static void
cv_tree_change_orientation (chanview *cv)
{
}

static void
cv_tree_focus (chan *ch)
{
	GtkWidget *tree = ((treeview *)ch->cv)->tree;
	nx_row *parent = nx_row_parent (ch->row);

	/* expand the parent node */
	if (parent)
		nx_view_expand (tree, parent);

	/* scroll only if it's out of sight (then centre it), and make it the cursor */
	nx_view_scroll_to (tree, ch->row, NX_SCROLL_IF_HIDDEN_CENTER);
	nx_view_set_cursor (tree, ch->row, NULL);
}

static void
cv_tree_move_focus (chanview *cv, gboolean relative, int num)
{
	chan *ch;

	if (relative)
	{
		num += cv_find_number_of_chan (cv, cv->focused);
		num %= cv->size;
		/* make it wrap around at both ends */
		if (num < 0)
			num = cv->size - 1;
	}

	ch = cv_find_chan_by_number (cv, num);
	if (ch)
		cv_tree_focus (ch);
}

static void
cv_tree_remove (chan *ch)
{
}

static void
move_row (chan *ch, int delta)
{
	nx_row *row = ch->row, *dest;
	int n = nx_store_n_rows (ch->cv->store, nx_row_parent (row));

	if (delta < 0) /* down */
	{
		if ((dest = nx_row_next (row)))
			nx_row_swap (row, dest);
		else	/* move to top */
			nx_row_move (row, 0);
	} else
	{
		if ((dest = nx_row_prev (row)))
			nx_row_swap (row, dest);
		else	/* move to bottom */
			nx_row_move (row, n - 1);
	}
}

static void
cv_tree_move (chan *ch, int delta)
{
	/* do nothing if this is a server row */
	if (nx_row_parent (ch->row))
		move_row (ch, delta);
}

static void
cv_tree_move_family (chan *ch, int delta)
{
	move_row (ch, delta);
}

static void
cv_tree_cleanup (chanview *cv)
{
	if (cv->box)
	{
		/* kill the scrolled window (and its drop overlay, GTK4) */
		GtkWidget *sw = ((treeview *)cv)->scrollw;
		GtkWidget *p = gtk_widget_get_parent (sw);
		nx_widget_destroy (p && p != cv->box ? p : sw);
	}
}

static void
cv_tree_set_color (chan *ch, PangoAttrList *list)
{
	/* nothing to do, it's already set in the store */
}

static void
cv_tree_rename (chan *ch, char *name)
{
	/* nothing to do, it's already renamed in the store */
}

static chan *
cv_tree_get_parent (chan *ch)
{
	nx_row *parent = nx_row_parent (ch->row);

	return parent ? nx_row_get_pointer (parent, COL_CHAN) : NULL;
}

static gboolean
cv_tree_is_collapsed (chan *ch)
{
	chan *parent = cv_tree_get_parent (ch);

	if (parent == NULL)
		return FALSE;

	return !nx_view_is_expanded (((treeview *)parent->cv)->tree, parent->row);
}
