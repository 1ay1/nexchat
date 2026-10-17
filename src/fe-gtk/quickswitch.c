/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Quick switcher (Ctrl+J): type part of a channel, query or network name,
   Enter jumps there. Tabs with highlights or unread messages come first. */

#include <string.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/util.h"
#include "../common/server.h"
#include "gtkutil.h"
#include "gtkevents.h"
#include "nxlist.h"
#include "maingui.h"
#include "palette.h"

enum
{
	QS_COL_TEXT,		/* markup */
	QS_COL_SESS,
	QS_N_COLS
};

static GtkWidget *qs_win;
static GtkWidget *qs_entry;
static GtkWidget *qs_view;
static nx_store *qs_store;

/* how well needle matches hay, case-insensitive; -1: not at all.
   prefix > word start > substring > letters in order */
static int
qs_score (const char *hay, const char *needle)
{
	char *h, *n, *p;
	int score = -1;
	const char *hp, *np;

	if (!*needle)
		return 0;
	h = g_utf8_casefold (hay, -1);
	n = g_utf8_casefold (needle, -1);

	if (g_str_has_prefix (h, n))
		score = 1000;
	else if ((p = strstr (h, n)))
	{
		/* after a separator counts as a word start */
		score = (p > h && strchr ("#&-_. /", p[-1])) ? 800 : 600;
		score -= MIN (100, (int) (p - h));
	}
	else
	{
		/* fuzzy: all letters in order */
		for (hp = h, np = n; *hp && *np; hp++)
			if (*hp == *np)
				np++;
		if (!*np)
			score = 200 - MIN (150, (int) strlen (h));
	}
	g_free (h);
	g_free (n);
	return score;
}

static const char *
qs_name (session *sess)
{
	if (sess->type == SESS_SERVER)
		return server_get_network (sess->server, TRUE);
	return sess->channel;
}

typedef struct
{
	session *sess;
	int score;
} qs_hit;

static int
qs_hit_cmp (gconstpointer a, gconstpointer b)
{
	const qs_hit *x = a, *y = b;
	return y->score - x->score;
}

static void
qs_refill (void)
{
	const char *needle = nx_entry_get_text (qs_entry);
	GArray *hits = g_array_new (FALSE, FALSE, sizeof (qs_hit));
	GSList *list;
	guint i;

	for (list = sess_list; list; list = list->next)
	{
		session *sess = list->data;
		const char *name = qs_name (sess);
		qs_hit hit;
		int s;

		if (!name || !*name || sess->type == SESS_NOTICES || sess->type == SESS_SNOTICES)
			continue;
		s = qs_score (name, needle);
		if (s < 0)
		{
			/* the network name finds its channels too */
			s = qs_score (server_get_network (sess->server, TRUE), needle);
			if (s < 0)
				continue;
			s /= 4;
		}
		/* what needs attention first, the tab we're on last */
		if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
			s += 300;
		else if (sess->tab_state & TAB_STATE_NEW_MSG)
			s += 150;
		else if (sess->tab_state & TAB_STATE_NEW_DATA)
			s += 20;
		if (sess == current_sess)
			s -= 500;
		hit.sess = sess;
		hit.score = s;
		g_array_append_val (hits, hit);
	}
	g_array_sort (hits, qs_hit_cmp);

	nx_store_clear (qs_store);
	for (i = 0; i < hits->len && i < 50; i++)
	{
		session *sess = g_array_index (hits, qs_hit, i).sess;
		const char *net = server_get_network (sess->server, TRUE);
		const char *col = NULL;
		char *name, *text;

		if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
			col = "hilight";
		else if (sess->tab_state & TAB_STATE_NEW_MSG)
			col = "msg";

		name = g_markup_escape_text (qs_name (sess), -1);
		if (sess->type == SESS_SERVER)
			text = g_strdup_printf ("<b>%s</b>  <small><span alpha=\"60%%\">%s</span></small>",
											name, _("server"));
		else
		{
			char *n = g_markup_escape_text (net ? net : "", -1);
			text = g_strdup_printf ("%s%s%s  <small><span alpha=\"60%%\">%s%s</span></small>",
											col ? "<b>" : "", name, col ? "</b>" : "",
											n, sess->type == SESS_DIALOG ? _(" · query") : "");
			g_free (n);
		}
		nx_row_set (nx_store_append (qs_store, NULL), QS_COL_TEXT, text, QS_COL_SESS, sess, -1);
		g_free (name);
		g_free (text);
	}
	g_array_free (hits, TRUE);

	if (nx_store_n_rows (qs_store, NULL) > 0)
		nx_view_set_cursor (qs_view, nx_store_nth (qs_store, NULL, 0), NULL);
}

static void
qs_close (void)
{
	if (qs_win)
		nx_widget_destroy (qs_win);
}

static void
qs_go (nx_row *row)
{
	session *sess;

	if (!row)
		return;
	sess = nx_row_get_pointer (row, QS_COL_SESS);
	qs_close ();
	if (is_session (sess))
		mg_bring_tofront_sess (sess);
}

static void
qs_activate_cb (GtkWidget *view, nx_row *row, gpointer unused)
{
	qs_go (row);
}

static void
qs_entry_activate (GtkWidget *entry, gpointer unused)
{
	qs_go (nx_view_get_selected (qs_view));
}

static void
qs_changed (GtkWidget *entry, gpointer unused)
{
	qs_refill ();
}

/* move the selection from the entry, so typing never leaves it */
static void
qs_move (int delta)
{
	nx_row *row = nx_view_get_selected (qs_view), *to;

	if (!row)
		to = nx_store_nth (qs_store, NULL, 0);
	else
		to = delta > 0 ? nx_row_next (row) : nx_row_prev (row);
	if (to)
	{
		nx_view_set_cursor (qs_view, to, NULL);
		nx_view_scroll_to (qs_view, to, NX_SCROLL_MIN);
	}
}

static gboolean
qs_key (GtkWidget *w, guint keyval, GdkModifierType state, gpointer unused)
{
	switch (keyval)
	{
	case GDK_KEY_Escape:
		qs_close ();
		return TRUE;
	case GDK_KEY_Down:
	case GDK_KEY_Tab:
		qs_move (1);
		return TRUE;
	case GDK_KEY_Up:
	case GDK_KEY_ISO_Left_Tab:
		qs_move (-1);
		return TRUE;
	case GDK_KEY_n:
	case GDK_KEY_p:
		if (state & GDK_CONTROL_MASK)
		{
			qs_move (keyval == GDK_KEY_n ? 1 : -1);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static void
qs_destroyed (GtkWidget *w, gpointer unused)
{
	qs_win = NULL;
	qs_entry = NULL;
	qs_view = NULL;
	if (qs_store)
	{
		g_object_unref (qs_store);
		qs_store = NULL;
	}
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void
qs_active_changed (GObject *win, GParamSpec *pspec, gpointer unused)
{
	/* clicked somewhere else: go away like a popup */
	if (!gtk_window_is_active (GTK_WINDOW (win)))
		qs_close ();
}
#else
static gboolean
qs_focus_out (GtkWidget *win, GdkEvent *ev, gpointer unused)
{
	qs_close ();
	return FALSE;
}
#endif

void
quickswitch_open (void)
{
	GtkWidget *box, *parent, *hint;

	if (qs_win)
	{
		gtk_window_present (GTK_WINDOW (qs_win));
		return;
	}
	parent = current_sess && current_sess->gui ? current_sess->gui->window : NULL;

	qs_win = nx_window_new ();
	gtk_widget_set_name (qs_win, "nexchat-quickswitch");
	gtk_window_set_title (GTK_WINDOW (qs_win), _("Switch to"));
	gtk_window_set_decorated (GTK_WINDOW (qs_win), FALSE);
	gtk_window_set_resizable (GTK_WINDOW (qs_win), FALSE);
	gtk_window_set_default_size (GTK_WINDOW (qs_win), 420, 340);
	if (parent)
	{
		gtk_window_set_transient_for (GTK_WINDOW (qs_win), GTK_WINDOW (parent));
		gtk_window_set_modal (GTK_WINDOW (qs_win), TRUE);
	}
	nx_window_set_position_on_parent (qs_win);
	g_signal_connect (qs_win, "destroy", G_CALLBACK (qs_destroyed), NULL);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	nx_set_border_width (box, 10);
	gtkutil_container_add (qs_win, box);

	qs_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (qs_entry), _("Jump to a channel, query or network\342\200\246"));
	nx_box_pack_start (box, qs_entry, FALSE, FALSE, 0);
	g_signal_connect (qs_entry, "changed", G_CALLBACK (qs_changed), NULL);
	g_signal_connect (qs_entry, "activate", G_CALLBACK (qs_entry_activate), NULL);
	nx_on_key (qs_entry, qs_key, NULL);

	qs_store = nx_store_new (FALSE, QS_N_COLS, G_TYPE_STRING, G_TYPE_POINTER);
	qs_view = nx_view_new_scrolled (box, qs_store);
	nx_view_set_headers_visible (qs_view, FALSE);
	nx_view_set_select_mode (qs_view, GTK_SELECTION_BROWSE);
	nx_view_add_column (qs_view, NULL, NX_COL_MARKUP, QS_COL_TEXT);
	nx_view_on_activate (qs_view, qs_activate_cb, NULL);
	nx_view_set_search_column (qs_view, -1);
	gtk_widget_set_can_focus (qs_view, FALSE);

	hint = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (hint),
								 _("<small>\342\206\221\342\206\223 to move   Enter to switch   Esc to close</small>"));
	gtk_widget_set_opacity (hint, 0.6);
	nx_box_pack_start (box, hint, FALSE, FALSE, 0);

#if GTK_CHECK_VERSION(4, 0, 0)
	g_signal_connect (qs_win, "notify::is-active", G_CALLBACK (qs_active_changed), NULL);
#else
	g_signal_connect (qs_win, "focus-out-event", G_CALLBACK (qs_focus_out), NULL);
#endif

	qs_refill ();
	nx_widget_show_all (qs_win);
	gtk_widget_grab_focus (qs_entry);
}
