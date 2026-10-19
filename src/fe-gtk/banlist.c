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

#ifndef WIN32
#include <unistd.h>
#endif

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/modes.h"
#include "../common/outbound.h"
#include "../common/hexchatc.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "maingui.h"
#include "banlist.h"

/*
 * These supports_* routines set capable, readable, writable bits */
static void supports_bans (banlist_info *, int);
static void supports_exempt (banlist_info *, int);
static void supports_invite (banlist_info *, int);
static void supports_quiet (banlist_info *, int);

static mode_info modes[MODE_CT] = {
	{
		N_("Bans"),
		N_("Ban"),
		'b',
		RPL_BANLIST,
		RPL_ENDOFBANLIST,
		1<<MODE_BAN,
		supports_bans
	}
	,{
		N_("Exempts"),
		N_("Exempt"),
		'e',
		RPL_EXCEPTLIST,
		RPL_ENDOFEXCEPTLIST,
		1<<MODE_EXEMPT,
		supports_exempt
	}
	,{
		N_("Invites"),
		N_("Invite"),
		'I',
		RPL_INVITELIST,
		RPL_ENDOFINVITELIST,
		1<<MODE_INVITE,
		supports_invite
	}
	,{
		N_("Quiets"),
		N_("Quiet"),
		'q',
		RPL_QUIETLIST,
		RPL_ENDOFQUIETLIST,
		1<<MODE_QUIET,
		supports_quiet
	}
};

/* model for the banlist tree */
enum
{
	TYPE_COLUMN,
	MASK_COLUMN,
	FROM_COLUMN,
	DATE_COLUMN,
	N_COLUMNS
};

static GtkWidget *
get_view (struct session *sess)
{
	return sess->res->banlist->treeview;
}

static nx_store *
get_store (struct session *sess)
{
	return nx_view_get_store (get_view (sess));
}

static void
supports_bans (banlist_info *banl, int i)
{
	int bit = 1<<i;

	banl->capable |= bit;
	banl->readable |= bit;
	banl->writeable |= bit;
	return;
}

static void
supports_exempt (banlist_info *banl, int i)
{
	server *serv = banl->sess->server;
	char *cm = serv->chanmodes;
	int bit = 1<<i;

	if (serv->have_except)
		goto yes;

	if (!cm)
		return;

	while (*cm)
	{
		if (*cm == ',')
			break;
		if (*cm == 'e')
			goto yes;
		cm++;
	}
	return;

yes:
	banl->capable |= bit;
	banl->writeable |= bit;
}

static void
supports_invite (banlist_info *banl, int i)
{
	server *serv = banl->sess->server;
	char *cm = serv->chanmodes;
	int bit = 1<<i;

	if (serv->have_invite)
		goto yes;

	if (!cm)
		return;

	while (*cm)
	{
		if (*cm == ',')
			break;
		if (*cm == 'I')
			goto yes;
		cm++;
	}
	return;

yes:
	banl->capable |= bit;
	banl->writeable |= bit;
}

static void
supports_quiet (banlist_info *banl, int i)
{
	server *serv = banl->sess->server;
	char *cm = serv->chanmodes;
	int bit = 1<<i;

	if (!cm)
		return;

	while (*cm)
	{
		if (*cm == ',')
			break;
		if (*cm == modes[i].letter)
			goto yes;
		cm++;
	}
	return;

yes:
	banl->capable |= bit;
	banl->readable |= bit;
	banl->writeable |= bit;
}

/* fe_add_ban_list() and fe_ban_list_end() return TRUE if consumed, FALSE otherwise */
gboolean
fe_add_ban_list (struct session *sess, char *mask, char *who, char *when, int rplcode)
{
	banlist_info *banl = sess->res->banlist;
	int i;

	if (!banl)
		return FALSE;

	for (i = 0; i < MODE_CT; i++)
		if (modes[i].code == rplcode)
			break;
	if (i == MODE_CT)
	{
		/* printf ("Unexpected value in fe_add_ban_list:  %d\n", rplcode); */
		return FALSE;
	}
	if (banl->pending & 1<<i)
	{
		nx_row_set (nx_store_append (get_store (sess), NULL), TYPE_COLUMN, _(modes[i].type),
		            MASK_COLUMN, mask, FROM_COLUMN, who, DATE_COLUMN, when, -1);

		banl->line_ct++;
		return TRUE;
	}
	else return FALSE;
}

/* Sensitize checkboxes and buttons as appropriate for the moment  */
static void
banlist_sensitize (banlist_info *banl)
{
	int checkable, i;
	gboolean is_op = FALSE;

	if (banl->sess->me == NULL)
		return;

	/* FIXME: More access levels than these can unban */
	if (banl->sess->me->op || banl->sess->me->hop)
		is_op = TRUE;

	/* CHECKBOXES -- */
	checkable = is_op? banl->writeable: banl->readable;
	for (i = 0; i < MODE_CT; i++)
	{
		if (banl->checkboxes[i] == NULL)
			continue;
		if ((checkable & 1<<i) == 0)
		/* Checkbox is not checkable.  Grey it and uncheck it. */
		{
			gtk_widget_set_sensitive (banl->checkboxes[i], FALSE);
			nx_toggle_set_active ((GtkWidget *) (banl->checkboxes[i]), FALSE);
		}
		else
		/* Checkbox is checkable.  Be sure it's sensitive. */
		{
			gtk_widget_set_sensitive (banl->checkboxes[i], TRUE);
		}
	}

	/* BUTTONS --- */
	if (!is_op || banl->line_ct == 0)
	{
		/* If user is not op or list is empty, buttons should be all greyed */
		gtk_widget_set_sensitive (banl->but_clear, FALSE);
		gtk_widget_set_sensitive (banl->but_crop, FALSE);
		gtk_widget_set_sensitive (banl->but_remove, FALSE);
	}
	else
	{
		/* If no lines are selected, only the CLEAR button should be sensitive */
		if (banl->select_ct == 0)
		{
			gtk_widget_set_sensitive (banl->but_clear, TRUE);
			gtk_widget_set_sensitive (banl->but_crop, FALSE);
			gtk_widget_set_sensitive (banl->but_remove, FALSE);
		}
		/* If any lines are selected, only the REMOVE and CROP buttons should be sensitive */
		else
		{
			gtk_widget_set_sensitive (banl->but_clear, FALSE);
			gtk_widget_set_sensitive (banl->but_crop, banl->line_ct == banl->select_ct? FALSE: TRUE);
			gtk_widget_set_sensitive (banl->but_remove, TRUE);
		}
	}

	/* Set "Refresh" sensitvity */
	gtk_widget_set_sensitive (banl->but_refresh, banl->pending? FALSE: banl->checked? TRUE: FALSE);
}
/* fe_ban_list_end() returns TRUE if consumed, FALSE otherwise */
gboolean
fe_ban_list_end (struct session *sess, int rplcode)
{
	banlist_info *banl = sess->res->banlist;
	int i;

	if (!banl)
		return FALSE;

	for (i = 0; i < MODE_CT; i++)
		if (modes[i].endcode == rplcode)
			break;
	if (i == MODE_CT)
	{
		/* printf ("Unexpected rplcode value in fe_ban_list_end:  %d\n", rplcode); */
		return FALSE;
	}
	if (banl->pending & modes[i].bit)
	{
		banl->pending &= ~modes[i].bit;
		if (!banl->pending)
		{
			gtk_widget_set_sensitive (banl->but_refresh, TRUE);
			banlist_sensitize (banl);
		}
		return TRUE;
	}
	else return FALSE;
}

/* the row right-clicked last, for the popup's Copy items */
static void
banlist_copy (GtkWidget *view, gboolean mask_only)
{
	nx_row *row = g_object_get_data (G_OBJECT (view), "nx-clicked-row");
	char *mask, *from, *date;
	char *str;

	if (!row || nx_row_index (row) < 0)
		return;
	nx_row_get (row, MASK_COLUMN, &mask, FROM_COLUMN, &from, DATE_COLUMN, &date, -1);

	if (mask_only)
		str = g_strdup (mask);
	else
		str = g_strdup_printf (_("%s on %s by %s"), mask, date, from);

	if (str[0] != 0)
		gtkutil_copy_to_clipboard (view, str);

	g_free (mask);
	g_free (from);
	g_free (date);
	g_free (str);
}

static void
banlist_copymask (nx_item *item, gpointer view)
{
	banlist_copy (view, TRUE);
}

static void
banlist_copyentry (nx_item *item, gpointer view)
{
	banlist_copy (view, FALSE);
}

static gboolean
banlist_button_pressed (GtkWidget *wid, guint button, int n_press, double x, double y,
								GdkModifierType state, gpointer userdata)
{
	nx_row *row;
	nx_menu *menu;

	/* Check for right click */
	if (n_press == 1 && button == 3)
	{
		row = nx_view_row_at (wid, x, y);
		if (row)
		{
			/* Must set the row active for use in callback */
			nx_view_unselect_all (wid);
			nx_view_set_cursor (wid, row, NULL);
			g_object_set_data (G_OBJECT (wid), "nx-clicked-row", row);
			
			menu = nx_menu_new ();
			nx_menu_add_item (menu, NX_MENU_APPEND, _("Copy mask"), 0, NULL, banlist_copymask, wid);
			nx_menu_add_item (menu, NX_MENU_APPEND, _("Copy entry"), 0, NULL, banlist_copyentry, wid);
			nx_menu_popup (menu, wid, nx_current_event (), NULL, NULL);
		}
		
		return TRUE;
	}
	
	return FALSE;
}

static void
banlist_select_changed (GtkWidget *view, gpointer data)
{
	banlist_info *banl = data;

	if (banl->line_ct == 0)
		banl->select_ct = 0;
	else
		banl->select_ct = nx_view_count_selected (view);
	banlist_sensitize (banl);
}

/**
 *  * Performs the actual refresh operations.
 *  */
static void
banlist_do_refresh (banlist_info *banl)
{
	session *sess = banl->sess;
	char tbuf[256];
	int i;

	banlist_sensitize (banl);

	if (sess->server->connected)
	{
		g_snprintf (tbuf, sizeof tbuf, "Ban List (%s, %s) - %s",
						sess->channel, sess->server->servername, _(DISPLAY_NAME));
		mg_set_title (banl->window, tbuf);

		g_object_set_data (G_OBJECT (get_view (sess)), "nx-clicked-row", NULL);
		nx_store_clear (get_store (sess));
		banl->line_ct = 0;
		banl->pending = banl->checked;
		if (banl->pending)
		{
			for (i = 0; i < MODE_CT; i++)
				if (banl->pending & 1<<i)
				{
					g_snprintf (tbuf, sizeof tbuf, "quote mode %s +%c", sess->channel, modes[i].letter);
					handle_command (sess, tbuf, FALSE);
				}
		}
	}
	else
	{
		fe_message (_("Not connected."), FE_MSG_ERROR);
	}
}

static void
banlist_refresh (GtkWidget * wid, banlist_info *banl)
{
	/* JG NOTE: Didn't see actual use of wid here, so just forwarding
	   *          * this to chanlist_do_refresh because I use it without any widget
	   *          * param in chanlist_build_gui_list when the user presses enter
	   *          * or apply for the first time if the list has not yet been
	   *          * received.
	   *          */
	banlist_do_refresh (banl);
}

static int
banlist_unban_inner (gpointer none, banlist_info *banl, int mode_num)
{
	session *sess = banl->sess;
	GList *rows, *l;
	char tbuf[2048];
	char **masks;
	const char *type;
	int num_sel, i;


	/* grab the list of selected items */
	rows = nx_view_get_selected_rows (get_view (sess));
	if (!rows)
		return 0;

	masks = g_new (char *, g_list_length (rows));
	num_sel = 0;
	for (l = rows; l; l = l->next)
	{
		type = nx_row_peek_string (l->data, TYPE_COLUMN);

		/* If it's the wrong type of mask, just continue */
		if (!type || strcmp (_(modes[mode_num].type), type) != 0)
			continue;

		/* Otherwise add it to our array of mask pointers */
		nx_row_get (l->data, MASK_COLUMN, &masks[num_sel++], -1);
	}
	g_list_free (rows);

	/* and send to server */
	if (num_sel)
		send_channel_modes (sess, tbuf, masks, 0, num_sel, '-', modes[mode_num].letter, 0);

	/* now free everything */
	for (i=0; i < num_sel; i++)
		g_free (masks[i]);
	g_free (masks);

	return num_sel;
}

static void
banlist_unban (GtkWidget * wid, banlist_info *banl)
{
	int i, num = 0;

	for (i = 0; i < MODE_CT; i++)
		num += banlist_unban_inner (wid, banl, i);

	/* This really should not occur with the redesign */
	if (num < 1)
	{
		fe_message (_("You must select some bans."), FE_MSG_ERROR);
		return;
	}

	banlist_do_refresh (banl);
}

static void
banlist_clear_cb (GtkWidget *dialog, gint response, gpointer data)
{
	banlist_info *banl = data;

	nx_widget_destroy (GTK_WIDGET (dialog));

	if (response == NX_RESPONSE_OK)
	{
		nx_view_select_all (get_view (banl->sess));
		banlist_unban (NULL, banl);
	}
}

static void
banlist_clear (GtkWidget * wid, banlist_info *banl)
{
	GtkWidget *dialog;
	char *q = g_strdup_printf (_("Are you sure you want to remove all listed items in %s?"),
									  banl->sess->channel);

	dialog = nx_message_dialog_new (NULL, FALSE, NX_MSG_QUESTION, NX_BUTTONS_OK_CANCEL, FALSE, q);
	g_free (q);

	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (banlist_clear_cb), banl);
	nx_window_set_position_mouse (dialog);
	nx_widget_show (dialog);
}

static void
banlist_crop (GtkWidget * wid, banlist_info *banl)
{
	GtkWidget *view = get_view (banl->sess);
	GList *list, *node;

	/* remember which bans are selected */
	list = nx_view_get_selected_rows (view);

	/* select all, then unselect those that we remembered */
	if (list)
	{
		nx_view_select_all (view);

		for (node = list; node; node = node->next)
			nx_view_unselect (view, node->data);

		g_list_free (list);

		banlist_unban (NULL, banl);
	} else
		fe_message (_("You must select some bans."), FE_MSG_ERROR);
}

static void
banlist_toggle (GtkWidget *item, gpointer data)
{
	banlist_info *banl = data;
	int i, bit = 0;

	for (i = 0; i < MODE_CT; i++)
		if (banl->checkboxes[i] == item)
		{
			bit = 1<<i;
			break;
		}

	if (bit)		/* Should be gassert() */
	{
		banl->checked &= ~bit;
		banl->checked |= (nx_toggle_get_active ((GtkWidget *) (item)))? bit: 0;
		banlist_do_refresh (banl);
	}
}

/* NOTICE:  The official strptime() is not available on all platforms so
 * I've implemented a special version here.  The official version is
 * vastly more general than this:  it uses locales for weekday and month
 * names and its second arg is a format character-string.  This special
 * version depends on the format returned by ctime(3) whose manpage
 * says it returns:
 *     "a null-terminated string of the form "Wed Jun 30 21:49:08 1993\n"
 *
 * If the real strpftime() comes available, use this format string:
 *		#define DATE_FORMAT "%a %b %d %T %Y"
 */
static void
banlist_strptime (char *ti, struct tm *tm)
{
	/* Expect something like "Sat Mar 16 21:24:27 2013" */
	static char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
								  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };
	int M = -1, d = -1, h = -1, m = -1, s = -1, y = -1;

	if (*ti == 0)
	{
		memset (tm, 0, sizeof *tm);
		return;
	}
	/* No need to supply tm->tm_wday; mktime() doesn't read it */
	ti += 4;
	while ((mon[++M]))
		if (strncmp (ti, mon[M], 3) == 0)
			break;
	ti += 4;

	d = strtol (ti, &ti, 10);
	h = strtol (++ti, &ti, 10);
	m = strtol (++ti, &ti, 10);
	s = strtol (++ti, &ti, 10);
	y = strtol (++ti, NULL, 10) - 1900;

	tm->tm_sec = s;
	tm->tm_min = m;
	tm->tm_hour = h;
	tm->tm_mday = d;
	tm->tm_mon = M;
	tm->tm_year = y;
}

static int
banlist_date_sort (nx_row *a, nx_row *b, gpointer user_data)
{
	struct tm tm1, tm2;
	time_t t1, t2;

	banlist_strptime ((char *) nx_row_peek_string (a, DATE_COLUMN), &tm1);
	banlist_strptime ((char *) nx_row_peek_string (b, DATE_COLUMN), &tm2);
	t1 = mktime (&tm1);
	t2 = mktime (&tm2);

	if (t1 < t2) return 1;
	if (t1 == t2) return 0;
	return -1;
}

static GtkWidget *
banlist_treeview_new (GtkWidget *box, banlist_info *banl)
{
	nx_store *store;
	GtkWidget *view;
	nx_col *col;

	store = nx_store_new (FALSE, N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
	                      G_TYPE_STRING, G_TYPE_STRING);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);
	nx_on_click (view, banlist_button_pressed, NULL);

	col = nx_view_add_column (view, _("Type"), NX_COL_TEXT, TYPE_COLUMN);
	nx_col_set_title_align (col, 0.5);
	nx_col_set_sort_column (col, TYPE_COLUMN);

	col = nx_view_add_column (view, _("Mask"), NX_COL_TEXT, MASK_COLUMN);
	nx_col_set_title_align (col, 0.5);
	nx_col_set_min_width (col, 100);
	nx_col_set_sort_column (col, MASK_COLUMN);
	nx_col_set_resizable (col, TRUE);

	col = nx_view_add_column (view, _("From"), NX_COL_TEXT, FROM_COLUMN);
	nx_col_set_title_align (col, 0.5);
	nx_col_set_sort_column (col, FROM_COLUMN);
	nx_col_set_resizable (col, TRUE);

	col = nx_view_add_column (view, _("Date"), NX_COL_TEXT, DATE_COLUMN);
	nx_col_set_title_align (col, 0.5);
	nx_col_set_sort_func (col, banlist_date_sort, NULL);
	nx_col_set_resizable (col, TRUE);

	nx_view_set_select_mode (view, GTK_SELECTION_MULTIPLE);
	nx_view_on_select (view, banlist_select_changed, banl);
	nx_view_set_search_column (view, TYPE_COLUMN);

	nx_widget_show (view);
	return view;
}

static void
banlist_closegui (GtkWidget *wid, banlist_info *banl)
{
	session *sess = banl->sess;

	if (sess->res->banlist == banl)
	{
		g_free (banl);
		sess->res->banlist = NULL;
	}
}

void
banlist_opengui (struct session *sess)
{
	banlist_info *banl;
	int i;
	GtkWidget *table, *vbox, *bbox;
	char tbuf[256];

	if (sess->type != SESS_CHANNEL || sess->channel[0] == 0)
	{
		fe_message (_("You can only open the Ban List window while in a channel tab."), FE_MSG_ERROR);
		return;
	}

	if (sess->res->banlist == NULL)
	{
		sess->res->banlist = g_new0 (banlist_info, 1);
	}
	banl = sess->res->banlist;
	if (banl->window)
	{
		mg_bring_tofront (banl->window);
		return;
	}

	/* New banlist for this session -- Initialize it */
	banl->sess = sess;
	/* For each mode set its bit in capable/readable/writeable */
	for (i = 0; i < MODE_CT; i++)
		modes[i].tester (banl, i);
	/* Force on the checkmark in the "Bans" box */
	banl->checked = 1<<MODE_BAN;

	g_snprintf (tbuf, sizeof tbuf, _("Ban List (%s) - %s"),
					sess->server->servername, _(DISPLAY_NAME));

	banl->window = mg_create_generic_tab ("BanList", tbuf, FALSE,
					TRUE, banlist_closegui, banl, 700, 300, &vbox, sess->server);
	gtkutil_destroy_on_esc (banl->window);

	nx_set_border_width (banl->window, 3);
	gtk_box_set_spacing (GTK_BOX (vbox), 3);

	/* create banlist view */
	banl->treeview = banlist_treeview_new (vbox, banl);

	table = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (table), 16);
	nx_box_pack_start (vbox, table, 0, 0, 0);

	for (i = 0; i < MODE_CT; i++)
	{
		if (!(banl->capable & 1<<i))
			continue;
		banl->checkboxes[i] = gtk_check_button_new_with_label (_(modes[i].name));
		nx_toggle_set_active ((GtkWidget *) (banl->checkboxes[i]), (banl->checked & 1<<i? TRUE: FALSE));
		g_signal_connect (G_OBJECT (banl->checkboxes[i]), "toggled",
								G_CALLBACK (banlist_toggle), banl);
		gtkutil_grid_attach (table, banl->checkboxes[i], i+1, i+2, 0, 1, GU_FILL, GU_FILL, 0, 0);
	}

	bbox = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		nx_set_border_width (bbox, 5);
	nx_box_pack_end (vbox, bbox, 0, 0, 0);
	nx_widget_show (bbox);

	banl->but_remove = gtkutil_button (bbox, NX_STOCK_REMOVE, 0, banlist_unban, banl,
	                _("Remove"));
	banl->but_crop = gtkutil_button (bbox, NX_STOCK_REMOVE, 0, banlist_crop, banl,
	                _("Crop"));
	banl->but_clear = gtkutil_button (bbox, NX_STOCK_CLEAR, 0, banlist_clear, banl,
	                _("Clear"));

	banl->but_refresh = gtkutil_button (bbox, NX_STOCK_REFRESH, 0, banlist_refresh, banl, _("Refresh"));

	banlist_do_refresh (banl);

	nx_widget_show_all (banl->window);
}
