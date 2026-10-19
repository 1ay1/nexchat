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

/* abstract channel view: tabs or tree or anything you like */

#include <stdlib.h>
#include <string.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "fe-gtk.h"
#include "maingui.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "chanview.h"

/* store columns */
#define COL_NAME 0		/* (char *) */
#define COL_CHAN 1		/* (chan *) */
#define COL_ATTR 2		/* (PangoAttrList *) */
#define COL_PIXBUF 3		/* (GdkPixbuf *) */

struct _chanview
{
	/* impl scratch area */
	char implscratch[sizeof (void *) * 8];

	nx_store *store;
	int size;			/* number of channels in view */

	GtkWidget *box;	/* the box we destroy when changing implementations */
	gboolean use_text_font;	/* tree uses the text box font (via CSS) */
	chan *focused;		/* currently focused channel */
	int trunc_len;

	/* callbacks */
	void (*cb_focus) (chanview *, chan *, int tag, void *userdata);
	void (*cb_xbutton) (chanview *, chan *, int tag, void *userdata);
	gboolean (*cb_contextmenu) (chanview *, chan *, int tag, void *userdata, guint button, int n_press);
	int (*cb_compare) (void *a, void *b);

	/* impl */
	void (*func_init) (chanview *);
	void (*func_postinit) (chanview *);
	void *(*func_add) (chanview *, chan *, char *, nx_row *parent);
	void (*func_move_focus) (chanview *, gboolean, int);
	void (*func_change_orientation) (chanview *);
	void (*func_remove) (chan *);
	void (*func_move) (chan *, int delta);
	void (*func_move_family) (chan *, int delta);
	void (*func_focus) (chan *);
	void (*func_set_color) (chan *, PangoAttrList *);
	void (*func_rename) (chan *, char *);
	gboolean (*func_is_collapsed) (chan *);
	chan *(*func_get_parent) (chan *);
	void (*func_cleanup) (chanview *);

	unsigned int sorted:1;
	unsigned int vertical:1;
	unsigned int use_icons:1;
};

struct _chan
{
	chanview *cv;	/* our owner */
	nx_row *row;	/* our row in cv->store */
	void *userdata;	/* session * */
	void *family;		/* server * or null */
	void *impl;	/* togglebutton or null */
	GdkPixbuf *icon;
	short allow_closure;	/* allow it to be closed when it still has children? */
	short tag;
};

static chan *cv_find_chan_by_number (chanview *cv, int num);
static int cv_find_number_of_chan (chanview *cv, chan *find_ch);


/* ======= TABS ======= */

#include "chanview-tabs.c"


/* ======= TREE ======= */

#include "chanview-tree.c"


/* ==== ABSTRACT CHANVIEW ==== */

static char *
truncate_tab_name (char *name, int max)
{
	char *buf;

	if (max > 2 && g_utf8_strlen (name, -1) > max)
	{
		/* truncate long channel names */
		buf = g_malloc (strlen (name) + 4);
		g_utf8_strncpy (buf, name, max);
		strcat (buf, "..");
		return buf;
	}

	return name;
}

/* iterate through the store, into 1 depth of children */

static void
store_foreach_1 (nx_store *store, void (*func)(void *, nx_row *), void *userdata)
{
	nx_row *row, *inner;

	for (row = nx_store_first (store); row; row = nx_row_next (row))
	{
		func (userdata, row);
		for (inner = nx_store_nth (store, row, 0); inner; inner = nx_row_next (inner))
			func (userdata, inner);
	}
}

static void
chanview_pop_cb (chanview *cv, nx_row *row)
{
	chan *ch = nx_row_get_pointer (row, COL_CHAN);
	PangoAttrList *attr = nx_row_get_pointer (row, COL_ATTR);

	ch->impl = cv->func_add (cv, ch, (char *) nx_row_peek_string (row, COL_NAME), NULL);
	if (attr)
		cv->func_set_color (ch, attr);
}

static void
chanview_populate (chanview *cv)
{
	store_foreach_1 (cv->store, (void *)chanview_pop_cb, cv);
}

void
chanview_set_impl (chanview *cv, int type)
{
	/* cleanup the old one */
	if (cv->func_cleanup)
		cv->func_cleanup (cv);

	switch (type)
	{
	case 0:
		cv->func_init = cv_tabs_init;
		cv->func_postinit = cv_tabs_postinit;
		cv->func_add = cv_tabs_add;
		cv->func_move_focus = cv_tabs_move_focus;
		cv->func_change_orientation = cv_tabs_change_orientation;
		cv->func_remove = cv_tabs_remove;
		cv->func_move = cv_tabs_move;
		cv->func_move_family = cv_tabs_move_family;
		cv->func_focus = cv_tabs_focus;
		cv->func_set_color = cv_tabs_set_color;
		cv->func_rename = cv_tabs_rename;
		cv->func_is_collapsed = cv_tabs_is_collapsed;
		cv->func_get_parent = cv_tabs_get_parent;
		cv->func_cleanup = cv_tabs_cleanup;
		break;

	default:
		cv->func_init = cv_tree_init;
		cv->func_postinit = cv_tree_postinit;
		cv->func_add = cv_tree_add;
		cv->func_move_focus = cv_tree_move_focus;
		cv->func_change_orientation = cv_tree_change_orientation;
		cv->func_remove = cv_tree_remove;
		cv->func_move = cv_tree_move;
		cv->func_move_family = cv_tree_move_family;
		cv->func_focus = cv_tree_focus;
		cv->func_set_color = cv_tree_set_color;
		cv->func_rename = cv_tree_rename;
		cv->func_is_collapsed = cv_tree_is_collapsed;
		cv->func_get_parent = cv_tree_get_parent;
		cv->func_cleanup = cv_tree_cleanup;
		break;
	}

	/* now rebuild a new tabbar or tree */
	cv->func_init (cv);

	chanview_populate (cv);

	cv->func_postinit (cv);

	/* force re-focus */
	if (cv->focused)
		cv->func_focus (cv->focused);
}

static void
chanview_free_ch (chanview *cv, nx_row *row)
{
	g_free (nx_row_get_pointer (row, COL_CHAN));
}

static void
chanview_destroy_store (chanview *cv)	/* free every (chan *) in the store */
{
	store_foreach_1 (cv->store, (void *)chanview_free_ch, cv);
	g_object_unref (cv->store);
}

static void
chanview_destroy (chanview *cv)
{
	if (cv->func_cleanup)
		cv->func_cleanup (cv);

	if (cv->box)
		nx_widget_destroy (cv->box);

	chanview_destroy_store (cv);
	g_free (cv);
}

static void
chanview_box_destroy_cb (GtkWidget *box, chanview *cv)
{
	cv->box = NULL;
	chanview_destroy (cv);
}

chanview *
chanview_new (int type, int trunc_len, gboolean sort, gboolean use_icons,
				  gboolean use_text_font)
{
	chanview *cv;

	cv = g_new0 (chanview, 1);
	cv->store = nx_store_new (TRUE, 4, G_TYPE_STRING, G_TYPE_POINTER,
									  PANGO_TYPE_ATTR_LIST, GDK_TYPE_PIXBUF);
	cv->use_text_font = use_text_font;
	cv->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	cv->trunc_len = trunc_len;
	cv->sorted = sort;
	cv->use_icons = use_icons;
	nx_widget_show (cv->box);
	chanview_set_impl (cv, type);

	g_signal_connect (G_OBJECT (cv->box), "destroy",
							G_CALLBACK (chanview_box_destroy_cb), cv);

	return cv;
}

/* too lazy for signals */

void
chanview_set_callbacks (chanview *cv,
	void (*cb_focus) (chanview *, chan *, int tag, void *userdata),
	void (*cb_xbutton) (chanview *, chan *, int tag, void *userdata),
	gboolean (*cb_contextmenu) (chanview *, chan *, int tag, void *userdata, guint button, int n_press),
	int (*cb_compare) (void *a, void *b))
{
	cv->cb_focus = cb_focus;
	cv->cb_xbutton = cb_xbutton;
	cv->cb_contextmenu = cb_contextmenu;
	cv->cb_compare = cb_compare;
}

/* a new row under parent, placed by the compare function */

static nx_row *
chanview_insert_sorted (chanview *cv, nx_row *parent, void *ud)
{
	nx_row *row;
	chan *ch;

	if (cv->sorted)
	{
		for (row = nx_store_nth (cv->store, parent, 0); row; row = nx_row_next (row))
		{
			ch = nx_row_get_pointer (row, COL_CHAN);
			if (ch->tag == 0 && cv->cb_compare (ch->userdata, ud) > 0)
				return nx_store_insert_before (cv->store, parent, row);
		}
	}

	return nx_store_append (cv->store, parent);
}

/* find a parent node with the same "family" pointer (i.e. the Server tab) */

static nx_row *
chanview_find_parent (chanview *cv, void *family, chan *avoid)
{
	nx_row *row;
	chan *search_ch;

	for (row = nx_store_first (cv->store); row; row = nx_row_next (row))
	{
		search_ch = nx_row_get_pointer (row, COL_CHAN);
		if (family == search_ch->family && search_ch != avoid)
			return row;
	}

	return NULL;
}

static chan *
chanview_add_real (chanview *cv, char *name, void *family, void *userdata,
						 gboolean allow_closure, int tag, GdkPixbuf *icon,
						 chan *ch, chan *avoid)
{
	nx_row *parent = chanview_find_parent (cv, family, avoid);
	nx_row *row;

	if (parent)
		row = chanview_insert_sorted (cv, parent, userdata);
	else
		row = nx_store_append (cv->store, NULL);

	if (!ch)
	{
		ch = g_new0 (chan, 1);
		ch->userdata = userdata;
		ch->family = family;
		ch->cv = cv;
		ch->allow_closure = allow_closure;
		ch->tag = tag;
		ch->icon = icon;
	}
	ch->row = row;

	nx_row_set (row, COL_NAME, name, COL_CHAN, ch, COL_PIXBUF, icon, -1);

	cv->size++;
	ch->impl = cv->func_add (cv, ch, name, parent);

	return ch;
}

chan *
chanview_add (chanview *cv, char *name, void *family, void *userdata, gboolean allow_closure, int tag, GdkPixbuf *icon)
{
	char *new_name;
	chan *ret;

	new_name = truncate_tab_name (name, cv->trunc_len);

	ret = chanview_add_real (cv, new_name, family, userdata, allow_closure, tag, icon, NULL, NULL);

	if (new_name != name)
		g_free (new_name);

	return ret;
}

int
chanview_get_size (chanview *cv)
{
	return cv->size;
}

GtkWidget *
chanview_get_box (chanview *cv)
{
	return cv->box;
}

void
chanview_move_focus (chanview *cv, gboolean relative, int num)
{
	cv->func_move_focus (cv, relative, num);
}

GtkOrientation
chanview_get_orientation (chanview *cv)
{
	return (cv->vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
}

void
chanview_set_orientation (chanview *cv, gboolean vertical)
{
	if (vertical != cv->vertical)
	{
		cv->vertical = vertical;
		cv->func_change_orientation (cv);
	}
}

int
chan_get_tag (chan *ch)
{
	return ch->tag;
}

void *
chan_get_userdata (chan *ch)
{
	return ch->userdata;
}

void
chan_focus (chan *ch)
{
	if (ch->cv->focused == ch)
		return;

	ch->cv->func_focus (ch);
}

void
chan_move (chan *ch, int delta)
{
	ch->cv->func_move (ch, delta);
}

void
chan_move_family (chan *ch, int delta)
{
	ch->cv->func_move_family (ch, delta);
}

void
chan_set_color (chan *ch, PangoAttrList *list)
{
	nx_row_set (ch->row, COL_ATTR, list, -1);
	ch->cv->func_set_color (ch, list);
}

void
chan_rename (chan *ch, char *name, int trunc_len)
{
	char *new_name;

	new_name = truncate_tab_name (name, trunc_len);

	nx_row_set (ch->row, COL_NAME, new_name, -1);
	ch->cv->func_rename (ch, new_name);
	ch->cv->trunc_len = trunc_len;

	if (new_name != name)
		g_free (new_name);
}

char *
chan_get_name (chan *ch)
{
	char *name = NULL;

	nx_row_get (ch->row, COL_NAME, &name, -1);
	return name;
}

void
chan_click (chan *ch)
{
	/* a tab's button: what a left click does; the tree: select it */
	if (ch->impl && GTK_IS_BUTTON (ch->impl))
		g_signal_emit_by_name (ch->impl, "clicked");
	else
		chan_focus (ch);
}

/* this thing is overly complicated */

static int
cv_find_number_of_chan (chanview *cv, chan *find_ch)
{
	nx_row *row, *inner;
	int i = 0;

	for (row = nx_store_first (cv->store); row; row = nx_row_next (row))
	{
		if (nx_row_get_pointer (row, COL_CHAN) == find_ch)
			return i;
		i++;

		for (inner = nx_store_nth (cv->store, row, 0); inner; inner = nx_row_next (inner))
		{
			if (nx_row_get_pointer (inner, COL_CHAN) == find_ch)
				return i;
			i++;
		}
	}

	return 0;	/* WARNING */
}

/* this thing is overly complicated too */

static chan *
cv_find_chan_by_number (chanview *cv, int num)
{
	nx_row *row, *inner;
	int i = 0;

	for (row = nx_store_first (cv->store); row; row = nx_row_next (row))
	{
		if (i == num)
			return nx_row_get_pointer (row, COL_CHAN);
		i++;

		for (inner = nx_store_nth (cv->store, row, 0); inner; inner = nx_row_next (inner))
		{
			if (i == num)
				return nx_row_get_pointer (inner, COL_CHAN);
			i++;
		}
	}

	return NULL;
}

static void
chan_emancipate_children (chan *ch)
{
	char *name;
	chan *childch;
	nx_row *child;
	PangoAttrList *attr;

	while ((child = nx_store_nth (ch->cv->store, ch->row, 0)))
	{
		/* remove and re-add all the children, but avoid using "ch" as parent */
		nx_row_get (child, COL_NAME, &name, COL_ATTR, &attr, -1);
		childch = nx_row_get_pointer (child, COL_CHAN);
		ch->cv->func_remove (childch);
		nx_row_remove (child);
		ch->cv->size--;
		chanview_add_real (childch->cv, name, childch->family, childch->userdata, childch->allow_closure, childch->tag, childch->icon, childch, ch);
		if (attr)
		{
			nx_row_set (childch->row, COL_ATTR, attr, -1);
			childch->cv->func_set_color (childch, attr);
			pango_attr_list_unref (attr);
		}
		g_free (name);
	}
}

gboolean
chan_remove (chan *ch, gboolean force)
{
	chan *new_ch;
	int i, num;
	extern int hexchat_is_quitting;

	if (hexchat_is_quitting)	/* avoid lots of looping on exit */
		return TRUE;

	/* is this ch allowed to be closed while still having children? */
	if (!force &&
		 nx_store_n_rows (ch->cv->store, ch->row) > 0 &&
		 !ch->allow_closure)
		return FALSE;

	chan_emancipate_children (ch);
	ch->cv->func_remove (ch);

	/* is it the focused one? */
	if (ch->cv->focused == ch)
	{
		ch->cv->focused = NULL;

		/* try to move the focus to some other valid channel */
		num = cv_find_number_of_chan (ch->cv, ch);
		/* move to the one left of the closing tab */
		new_ch = cv_find_chan_by_number (ch->cv, num - 1);
		if (new_ch && new_ch != ch)
		{
			chan_focus (new_ch);	/* this'll will set ch->cv->focused for us too */
		} else
		{
			/* if it fails, try focus from tab 0 and up */
			for (i = 0; i < ch->cv->size; i++)
			{
				new_ch = cv_find_chan_by_number (ch->cv, i);
				if (new_ch && new_ch != ch)
				{
					chan_focus (new_ch);	/* this'll will set ch->cv->focused for us too */
					break;
				}
			}
		}
	}

	ch->cv->size--;
	nx_row_remove (ch->row);
	g_free (ch);
	return TRUE;
}

gboolean
chan_is_collapsed (chan *ch)
{
	return ch->cv->func_is_collapsed (ch);
}

chan *
chan_get_parent (chan *ch)
{
	return ch->cv->func_get_parent (ch);
}
