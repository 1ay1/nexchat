/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Window > Jump To... (Ctrl+J): type part of a channel, query or network
   name, Enter goes there. A small classic window with an entry over a
   list. Matches are the name containing what's typed, letters in order
   if nothing contains it; the most recently active window first. */

#include <string.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/server.h"
#include "gtkutil.h"
#include "gtkcompat.h"
#include "maingui.h"
#include "nxlist.h"
#include "jumpto.h"

enum { COL_NAME, COL_NET, COL_SESS, N_COLS };

static GtkWidget *jump_win;
static GtkWidget *jump_entry;
static GtkWidget *jump_view;
static nx_store *jump_store;

static const char *
sess_network (session *sess)
{
	return sess->server->network ? server_get_network (sess->server, TRUE)
		: sess->server->servername;
}

static const char *
sess_name (session *sess)
{
	if (sess->type == SESS_SERVER)
		return sess_network (sess);
	return sess->channel;
}

/* 0: no match; higher is better. A substring beats letters in order, and
   an earlier substring beats a later one. */
int
jumpto_score (const char *name, const char *typed)
{
	char *n, *t, *hit;
	const char *p, *q;
	int score = 0;

	if (!*typed)
		return 1;
	n = g_utf8_casefold (name, -1);
	t = g_utf8_casefold (typed, -1);
	hit = strstr (n, t);
	if (hit)
		score = 1000 - MIN ((int) (hit - n), 999);
	else
	{
		/* each typed char somewhere after the previous one */
		for (p = n, q = t; *p && *q; p++)
			if (*p == *q)
				q++;
		if (!*q)
			score = 1;
	}
	g_free (n);
	g_free (t);
	return score;
}

/* the switchable windows: everything in sess_list except the one we're in */
static void
jump_fill (void)
{
	const char *typed = nx_entry_get_text (jump_entry);
	GSList *l;
	GPtrArray *hits = g_ptr_array_new ();
	GArray *scores = g_array_new (FALSE, FALSE, sizeof (int));
	guint i, j;

	for (l = sess_list; l; l = l->next)
	{
		session *sess = l->data;
		int s1, s2;

		if (sess == current_sess)
			continue;
		s1 = jumpto_score (sess_name (sess), typed);
		/* the network name counts too, a bit less */
		s2 = jumpto_score (sess_network (sess), typed) / 2;
		s1 = MAX (s1, s2);
		if (!s1)
			continue;
		/* stable insertion by score; sess_list order is the tie-break */
		for (j = 0; j < hits->len; j++)
			if (g_array_index (scores, int, j) < s1)
				break;
		g_ptr_array_insert (hits, j, sess);
		g_array_insert_val (scores, j, s1);
	}

	nx_store_clear (jump_store);
	nx_store_freeze (jump_store);
	for (i = 0; i < hits->len; i++)
	{
		session *sess = g_ptr_array_index (hits, i);
		nx_row_set (nx_store_append (jump_store, NULL), COL_NAME, sess_name (sess),
						COL_NET, sess->type == SESS_SERVER ? "" : sess_network (sess),
						COL_SESS, sess, -1);
	}
	nx_store_thaw (jump_store);
	if (nx_store_first (jump_store))
		nx_view_set_cursor (jump_view, nx_store_first (jump_store), NULL);

	g_ptr_array_free (hits, TRUE);
	g_array_free (scores, TRUE);
}

static void
jump_go (nx_row *row)
{
	session *sess = row ? nx_row_get_pointer (row, COL_SESS) : NULL;

	if (sess && is_session (sess))
		mg_bring_tofront_sess (sess);
	gtk_window_destroy (GTK_WINDOW (jump_win));
}

static void
jump_activate_cb (GtkWidget *view, nx_row *row, gpointer d)
{
	jump_go (row);
}

static void
jump_entry_activate_cb (GtkWidget *entry, gpointer d)
{
	nx_row *row = nx_view_get_selected (jump_view);
	jump_go (row ? row : nx_store_first (jump_store));
}

static void
jump_changed_cb (GtkWidget *entry, gpointer d)
{
	jump_fill ();
}

/* Up/Down in the entry move in the list, Escape closes */
static gboolean
jump_key_cb (GtkEventControllerKey *k, guint key, guint code, GdkModifierType mods, gpointer d)
{
	nx_row *row = nx_view_get_selected (jump_view);

	switch (key)
	{
	case GDK_KEY_Escape:
		gtk_window_destroy (GTK_WINDOW (jump_win));
		return TRUE;
	case GDK_KEY_Down:
	case GDK_KEY_Up:
		if (row)
			row = key == GDK_KEY_Down ? nx_row_next (row) : nx_row_prev (row);
		else
			row = nx_store_first (jump_store);
		if (row)
		{
			nx_view_set_cursor (jump_view, row, NULL);
			nx_view_scroll_to (jump_view, row, NX_SCROLL_MIN);
		}
		gtk_widget_grab_focus (jump_entry);
		return TRUE;
	}
	return FALSE;
}

static void
jump_destroy_cb (GtkWidget *w, gpointer d)
{
	g_clear_object (&jump_store);
	jump_win = NULL;
}

void
jumpto_open (void)
{
	GtkWidget *vbox;
	GtkEventController *k;
	nx_col *col;

	if (jump_win)
	{
		gtk_window_present (GTK_WINDOW (jump_win));
		return;
	}

	jump_win = gtkutil_window_new (_("Jump To"), "jumpto", 360, 320, 3);
	gtk_window_set_modal (GTK_WINDOW (jump_win), TRUE);
	g_signal_connect (jump_win, "destroy", G_CALLBACK (jump_destroy_cb), NULL);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	nx_set_border_width (vbox, 4);
	gtk_window_set_child (GTK_WINDOW (jump_win), vbox);

	jump_entry = gtk_entry_new ();
	gtk_box_append (GTK_BOX (vbox), jump_entry);
	g_signal_connect (jump_entry, "changed", G_CALLBACK (jump_changed_cb), NULL);
	g_signal_connect (jump_entry, "activate", G_CALLBACK (jump_entry_activate_cb), NULL);
	k = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (k, GTK_PHASE_CAPTURE);
	g_signal_connect (k, "key-pressed", G_CALLBACK (jump_key_cb), NULL);
	gtk_widget_add_controller (jump_win, k);

	jump_store = nx_store_new (FALSE, N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	jump_view = nx_view_new_scrolled (vbox, jump_store);
	gtk_widget_set_vexpand (gtk_widget_get_parent (jump_view), TRUE);
	nx_view_set_headers_visible (jump_view, FALSE);
	col = nx_view_add_column (jump_view, NULL, NX_COL_TEXT, COL_NAME);
	nx_col_set_expand (col, TRUE);
	nx_view_add_column (jump_view, NULL, NX_COL_TEXT, COL_NET);
	nx_view_on_activate (jump_view, jump_activate_cb, NULL);

	jump_fill ();
	nx_widget_show (jump_win);
	gtk_widget_grab_focus (jump_entry);
}
