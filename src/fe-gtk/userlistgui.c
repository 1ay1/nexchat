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
#include "../common/util.h"
#include "../common/userlist.h"
#include "../common/modes.h"
#include "../common/text.h"
#include "../common/notify.h"
#include "../common/hexchatc.h"
#include "../common/fe.h"
#include "gtkutil.h"
#include "palette.h"
#include "maingui.h"
#include "menu.h"
#include "pixmaps.h"
#include "userlistgui.h"
#include "fkeys.h"
#include "nxlist.h"

enum
{
	COL_PIX=0,		/* GdkPixbuf * */
	COL_NICK=1,		/* char * */
	COL_HOST=2,		/* char * */
	COL_USER=3,		/* struct User * */
	COL_GDKCOLOR=4	/* GdkRGBA * */
};


GdkPixbuf *
get_user_icon (server *serv, struct User *user)
{
	char *pre;
	int level;

	if (!user)
		return NULL;

	/* these ones are hardcoded */
	switch (user->prefix[0])
	{
		case 0: return NULL;
		case '+': return pix_ulist_voice;
		case '%': return pix_ulist_halfop;
		case '@': return pix_ulist_op;
	}

	/* find out how many levels above Op this user is */
	pre = strchr (serv->nick_prefixes, '@');
	if (pre && pre != serv->nick_prefixes)
	{
		pre--;
		level = 0;
		while (1)
		{
			if (pre[0] == user->prefix[0])
			{
				switch (level)
				{
					case 0: return pix_ulist_owner;		/* 1 level above op */
					case 1: return pix_ulist_founder;	/* 2 levels above op */
					case 2: return pix_ulist_netop;		/* 3 levels above op */
				}
				break;	/* 4+, no icons */
			}
			level++;
			if (pre == serv->nick_prefixes)
				break;
			pre--;
		}
	}

	return NULL;
}

void
fe_userlist_numbers (session *sess)
{
	char tbuf[256];

	if (sess == current_tab || !sess->gui->is_tab)
	{
		if (sess->total)
		{
			g_snprintf (tbuf, sizeof (tbuf), _("%d ops, %d total"), sess->ops, sess->total);
			tbuf[sizeof (tbuf) - 1] = 0;
			gtk_label_set_text (GTK_LABEL (sess->gui->namelistinfo), tbuf);
		} else
		{
			gtk_label_set_text (GTK_LABEL (sess->gui->namelistinfo), NULL);
		}

		if (sess->type == SESS_CHANNEL && prefs.hex_gui_win_ucount)
			fe_set_title (sess);
	}
}

/* the store the user list shows now; another tab's store isn't on screen */
static gboolean
is_front (session *sess)
{
	return nx_view_get_store (sess->gui->user_tree) == sess->res->user_model;
}

static void
scroll_to_row (nx_row *row, GtkWidget *view)
{
	nx_view_scroll_to (view, row, NX_SCROLL_CENTER);
}

/* select a row in the userlist by nick-name */

void
userlist_select (session *sess, char *name)
{
	GtkWidget *view = sess->gui->user_tree;
	nx_row *row;
	struct User *row_user;

	for (row = nx_store_first (nx_view_get_store (view)); row; row = nx_row_next (row))
	{
		row_user = nx_row_get_pointer (row, COL_USER);
		if (sess->server->p_cmp (row_user->nick, name) == 0)
		{
			if (nx_view_is_selected (view, row))
				nx_view_unselect (view, row);
			else
				nx_view_select (view, row);

			/* and make sure it's visible */
			scroll_to_row (row, view);
			return;
		}
	}
}

char **
userlist_selection_list (GtkWidget *widget, int *num_ret)
{
	GList *rows = nx_view_get_selected_rows (widget), *l;
	struct User *user;
	int i;
	char **nicks;

	*num_ret = 0;
	if (!rows)
		return NULL;

	nicks = g_new (char *, g_list_length (rows) + 1);
	i = 0;
	for (l = rows; l; l = l->next)
	{
		user = nx_row_get_pointer (l->data, COL_USER);
		nicks[i++] = g_strdup (user->nick);
	}
	nicks[i] = NULL;
	g_list_free (rows);

	*num_ret = i;
	return nicks;
}

void
fe_userlist_set_selected (struct session *sess)
{
	GtkWidget *view = sess->gui->user_tree;
	nx_row *row;
	struct User *user;

	/* if it's not front-most tab it doesn't own the view! */
	if (!is_front (sess))
		return;

	for (row = nx_store_first (sess->res->user_model); row; row = nx_row_next (row))
	{
		user = nx_row_get_pointer (row, COL_USER);
		user->selected = nx_view_is_selected (view, row) ? 1 : 0;
	}
}

static nx_row *
find_row (session *sess, struct User *user, int *selected)
{
	nx_row *row = nx_store_find_pointer (sess->res->user_model, COL_USER, user);

	*selected = row && is_front (sess) && nx_view_is_selected (sess->gui->user_tree, row);
	return row;
}

void
userlist_set_value (GtkWidget *treeview, gfloat val)
{
	nx_view_set_scroll (treeview, val);
}

gfloat
userlist_get_value (GtkWidget *treeview)
{
	return nx_view_get_scroll (treeview);
}

int
fe_userlist_remove (session *sess, struct User *user)
{
	nx_row *row;
	int sel;

	row = find_row (sess, user, &sel);
	if (!row)
		return 0;

	nx_row_remove (row);

	return sel;
}

void
fe_userlist_rehash (session *sess, struct User *user)
{
	nx_row *row;
	int sel;
	int nick_color = 0;

	row = find_row (sess, user, &sel);
	if (!row)
		return;

	if (prefs.hex_away_track && user->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(user->nick);

	nx_row_set (row,
					COL_HOST, user->hostname,
					COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
					-1);
}

void
fe_userlist_insert (session *sess, struct User *newuser, gboolean sel)
{
	GdkPixbuf *pix = get_user_icon (sess->server, newuser);
	nx_row *row;
	char *nick;
	int nick_color = 0;

	if (prefs.hex_away_track && newuser->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(newuser->nick);

	nick = newuser->nick;
	if (!prefs.hex_gui_ulist_icons)
	{
		nick = g_malloc (strlen (newuser->nick) + 2);
		nick[0] = newuser->prefix[0];
		if (nick[0] == '\0' || nick[0] == ' ')
			strcpy (nick, newuser->nick);
		else
			strcpy (nick + 1, newuser->nick);
		pix = NULL;
	}

	/* sorted stores put it in its place; unsorted, at the top as before.
	   Sorted: append, so a burst of joins (or a big NAMES) goes in as one
	   sorted batch at the next idle instead of one view update each */
	row = prefs.hex_gui_ulist_sort <= 3
		? nx_store_append (sess->res->user_model, NULL)
		: nx_store_prepend (sess->res->user_model, NULL);
	nx_row_set (row,
					COL_PIX, pix,
					COL_NICK, nick,
					COL_HOST, newuser->hostname,
					COL_USER, newuser,
					COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
					-1);

	if (!prefs.hex_gui_ulist_icons)
	{
		g_free (nick);
	}

	/* is it me? */
	if (newuser->me && sess->gui->nick_box)
	{
		if (!sess->gui->is_tab || sess == current_tab)
			mg_set_access_icon (sess->gui, pix, sess->server->is_away);
	}

	/* is it the front-most tab? */
	if (sel && is_front (sess))
		nx_view_select (sess->gui->user_tree, row);
}

void
fe_userlist_clear (session *sess)
{
	nx_store_clear (sess->res->user_model);
}

static void
userlist_dnd_drop (GtkWidget *w, double x, double y, const char *uris, gpointer userdata)
{
	nx_row *row = nx_view_row_at (w, x, y);
	struct User *user;

	if (!row)
		return;
	user = nx_row_get_pointer (row, COL_USER);

	mg_dnd_drop_file (current_sess, user->nick, (char *) uris);
}

/* any drag over the list selects the nick under it (the file's target) */
static void
userlist_dnd_motion (GtkWidget *w, double x, double y, gpointer tree)
{
	nx_row *row;

	if (!tree)
		return;

	if ((row = nx_view_row_at (w, x, y)))
	{
		nx_view_unselect_all (w);
		nx_view_select (w, row);
	}
}

static void
userlist_dnd_leave (GtkWidget *widget, gpointer unused)
{
	nx_view_unselect_all (widget);
}

static int
userlist_alpha_cmp (nx_row *a, nx_row *b, gpointer userdata)
{
	return nick_cmp_alpha (nx_row_get_pointer (a, COL_USER), nx_row_get_pointer (b, COL_USER),
								  ((session*)userdata)->server);
}

static int
userlist_ops_cmp (nx_row *a, nx_row *b, gpointer userdata)
{
	return nick_cmp_az_ops (((session*)userdata)->server, nx_row_get_pointer (a, COL_USER),
									nx_row_get_pointer (b, COL_USER));
}

nx_store *
userlist_create_model (session *sess)
{
	nx_store *store;
	nx_row_compare_fn cmp_func;
	GtkSortType sort_type;

	store = nx_store_new (FALSE, 5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
								 G_TYPE_POINTER, GDK_TYPE_RGBA);

	switch (prefs.hex_gui_ulist_sort)
	{
	case 0:
		cmp_func = userlist_ops_cmp;
		sort_type = GTK_SORT_ASCENDING;
		break;
	case 1:
		cmp_func = userlist_alpha_cmp;
		sort_type = GTK_SORT_ASCENDING;
		break;
	case 2:
		cmp_func = userlist_ops_cmp;
		sort_type = GTK_SORT_DESCENDING;
		break;
	case 3:
		cmp_func = userlist_alpha_cmp;
		sort_type = GTK_SORT_DESCENDING;
		break;
	default:
		/* No sorting */
		return store;
	}

	nx_store_set_sort_func (store, cmp_func, sess, sort_type);

	return store;
}

static void
userlist_add_columns (GtkWidget *view)
{
	nx_col *col;

	/* icon column */
	nx_col_set_expand (nx_view_add_column (view, NULL, NX_COL_ICON, COL_PIX), FALSE);

	/* nick column */
	col = nx_view_add_column (view, NULL, NX_COL_TEXT, COL_NICK);
	nx_col_bind (col, NX_BIND_FOREGROUND, COL_GDKCOLOR);

	if (prefs.hex_gui_ulist_show_hosts)
	{
		/* hostname column */
		nx_view_add_column (view, NULL, NX_COL_TEXT, COL_HOST);
	}
}

static gint
userlist_click_cb (GtkWidget *widget, guint button, int n_press, double x, double y,
						 GdkModifierType state, gpointer userdata)
{
	char **nicks;
	int i;
	nx_row *row;
	/* the nick menu positions its popup from the event */
	GdkEvent *event = nx_current_event ();

	if (!(state & STATE_CTRL) &&
		n_press == 2 && prefs.hex_gui_ulist_doubleclick[0])
	{
		nicks = userlist_selection_list (widget, &i);
		if (nicks)
		{
			nick_command_parse (current_sess, prefs.hex_gui_ulist_doubleclick, nicks[0],
									  nicks[0]);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
		}
		return TRUE;
	}

	if (button == 3)
	{
		/* do we have a multi-selection? */
		nicks = userlist_selection_list (widget, &i);
		if (nicks && i > 1)
		{
			menu_nickmenu (current_sess, event, nicks[0], i);
			while (i)
			{
				i--;
				g_free (nicks[i]);
			}
			g_free (nicks);
			return TRUE;
		}
		if (nicks)
		{
			g_free (nicks[0]);
			g_free (nicks);
		}

		if ((row = nx_view_row_at (widget, x, y)))
		{
			nx_view_unselect_all (widget);
			nx_view_select (widget, row);
			nicks = userlist_selection_list (widget, &i);
			if (nicks)
			{
				menu_nickmenu (current_sess, event, nicks[0], i);
				while (i)
				{
					i--;
					g_free (nicks[i]);
				}
				g_free (nicks);
			}
		} else
		{
			nx_view_unselect_all (widget);
		}

		return TRUE;
	}

	return FALSE;
}

static gboolean
userlist_key_cb (GtkWidget *wid, guint keyval, GdkModifierType state, gpointer userdata)
{
	if (keyval >= GDK_KEY_asterisk && keyval <= GDK_KEY_z)
	{
		/* dirty trick to avoid auto-selection */
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
		gtk_widget_grab_focus (current_sess->gui->input_box);
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
		/* and the key goes to the input box (GTK4: the box takes the
		   keyval itself, see nx_widget_forward_key) */
		nx_widget_forward_key (current_sess->gui->input_box, nx_current_event ());
		return TRUE;
	}

	return FALSE;
}

GtkWidget *
userlist_create (GtkWidget *box)
{
	GtkWidget *sw, *treeview;
	nx_store *empty;

	sw = nx_scrolled_window_new ();
	nx_scrolled_window_set_shadow_etched (sw);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
											  prefs.hex_gui_ulist_show_hosts ?
												GTK_POLICY_AUTOMATIC :
												GTK_POLICY_NEVER,
											  GTK_POLICY_AUTOMATIC);
	nx_box_pack_start (box, mg_drop_overlay (sw), TRUE, TRUE, 0);
	nx_widget_show (sw);

	/* each tab has its own store (userlist_create_model); this one is
	   only shown until a tab's is */
	empty = nx_store_new (FALSE, 5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
								 G_TYPE_POINTER, GDK_TYPE_RGBA);
	treeview = nx_view_new (empty);
	g_object_unref (empty);
	gtk_widget_set_name (treeview, "hexchat-userlist");
	nx_view_set_headers_visible (treeview, FALSE);
	nx_view_set_select_mode (treeview, GTK_SELECTION_MULTIPLE);
	if (prefs.hex_gui_compact)
		nx_view_set_compact (treeview, TRUE);

	/* file DND (for DCC), then layout drags of the channel tree; the user
	   list itself can be dragged around */
	nx_dnd_file_dest (treeview, userlist_dnd_drop, userlist_dnd_motion,
							userlist_dnd_leave, treeview);
	mg_layout_drop_dest (treeview, NX_DND_CHANVIEW);
	nx_dnd_layout_source (treeview, NX_DND_USERLIST);

	nx_on_click (treeview, userlist_click_cb, 0);
	nx_on_key (treeview, userlist_key_cb, 0);

	userlist_add_columns (treeview);

	gtkutil_container_add (GTK_WIDGET (sw), treeview);
	nx_widget_show (treeview);

	return treeview;
}

void
userlist_show (session *sess)
{
	nx_view_set_store (sess->gui->user_tree, sess->res->user_model);
}

void
fe_uselect (session *sess, char *word[], int do_clear, int scroll_to)
{
	int thisname;
	char *name;
	GtkWidget *view = sess->gui->user_tree;
	nx_row *row = nx_store_first (nx_view_get_store (view));
	struct User *row_user;

	if (!row)
		return;

	if (do_clear)
		nx_view_unselect_all (view);

	for (; row; row = nx_row_next (row))
	{
		if (*word[0])
		{
			row_user = nx_row_get_pointer (row, COL_USER);
			thisname = 0;
			while ( *(name = word[thisname++]) )
			{
				if (sess->server->p_cmp (row_user->nick, name) == 0)
				{
					nx_view_select (view, row);
					if (scroll_to)
						scroll_to_row (row, view);
					break;
				}
			}
		}
	}
}
