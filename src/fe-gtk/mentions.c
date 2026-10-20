/* nexchat
 * Copyright (C) 2026 nexchat contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Window > Mentions (Cmd+Shift+M / Ctrl+Shift+M): every highlight and
   private message since nexchat started, from every network, newest at the
   top. Enter or a double click goes to that tab. Kept while nexchat runs
   (the logs have the rest); the window can be opened any time. */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/server.h"
#include "gtkutil.h"
#include "maingui.h"
#include "mentions.h"
#include "nxlist.h"

enum { COL_TIME, COL_WHERE, COL_WHO, COL_TEXT, COL_SESS, N_COLS };

#define MAX_MENTIONS 500

typedef struct
{
	time_t stamp;
	char *where, *who, *text;
	session *sess;
} mention;

static GQueue mentions = G_QUEUE_INIT;	/* newest first */
static GtkWidget *mentions_window;
static nx_store *mentions_store;

static void
mention_free (mention *m)
{
	g_free (m->where);
	g_free (m->who);
	g_free (m->text);
	g_free (m);
}

static void
row_fill (nx_row *row, mention *m)
{
	char t[32];
	struct tm *tm = localtime (&m->stamp);

	strftime (t, sizeof t, "%a %H:%M", tm);
	nx_row_set (row, COL_TIME, t, COL_WHERE, m->where, COL_WHO, m->who,
					COL_TEXT, m->text, COL_SESS, m->sess, -1);
}

void
fe_mention (struct session *sess, const char *nick, const char *text, time_t stamp)
{
	mention *m;
	char *name;

	if (!sess || !sess->server)
		return;
	m = g_new0 (mention, 1);
	m->stamp = stamp ? stamp : time (NULL);
	name = sess->type == SESS_DIALOG ? g_strdup (_("private")) : g_strdup (sess->channel);
	m->where = g_strdup_printf ("%s / %s", server_get_network (sess->server, TRUE), name);
	g_free (name);
	m->who = g_strdup (nick);
	/* colours and bold out: the list is plain */
	m->text = strip_color (text, -1, STRIP_ALL);
	m->sess = sess;
	g_queue_push_head (&mentions, m);
	while (g_queue_get_length (&mentions) > MAX_MENTIONS)
		mention_free (g_queue_pop_tail (&mentions));

	if (mentions_store)
		row_fill (nx_store_prepend (mentions_store, NULL), m);
}

/* a closed tab's mentions stay listed, but can't be jumped to */
void
mentions_session_gone (session *sess)
{
	GList *l;
	nx_row *row;

	for (l = mentions.head; l; l = l->next)
		if (((mention *) l->data)->sess == sess)
			((mention *) l->data)->sess = NULL;
	if (mentions_store)
		while ((row = nx_store_find_pointer (mentions_store, COL_SESS, sess)))
			nx_row_set (row, COL_SESS, NULL, -1);
}

static void
mentions_activate (GtkWidget *view, nx_row *row, gpointer d)
{
	session *sess = nx_row_get_pointer (row, COL_SESS);

	if (sess && is_session (sess))
	{
		mg_bring_tofront_sess (sess);
		gtk_window_present (GTK_WINDOW (nx_widget_get_toplevel (sess->gui->window)));
	}
}

static void
mentions_clear (GtkWidget *w, gpointer d)
{
	g_queue_clear_full (&mentions, (GDestroyNotify) mention_free);
	if (mentions_store)
		nx_store_clear (mentions_store);
}

static void
mentions_closed (GtkWidget *w, gpointer d)
{
	mentions_window = NULL;
	mentions_store = NULL;
}

void
mentions_open (void)
{
	GtkWidget *vbox, *hbox, *view;
	GList *l;
	char buf[128];

	if (mentions_window)
	{
		mg_bring_tofront (mentions_window);
		return;
	}

	g_snprintf (buf, sizeof buf, _("Mentions - %s"), _(DISPLAY_NAME));
	mentions_window = mg_create_generic_tab ("Mentions", buf, FALSE, TRUE, mentions_closed, NULL,
														  640, 320, &vbox, 0);
	gtkutil_destroy_on_esc (mentions_window);

	mentions_store = nx_store_new (FALSE, N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
											 G_TYPE_STRING, G_TYPE_POINTER);
	view = nx_view_new_scrolled (vbox, mentions_store);
	g_object_unref (mentions_store);
	nx_view_add_column (view, _("When"), NX_COL_TEXT, COL_TIME);
	nx_view_add_column (view, _("Where"), NX_COL_TEXT, COL_WHERE);
	nx_view_add_column (view, _("Who"), NX_COL_TEXT, COL_WHO);
	nx_view_add_column (view, _("Message"), NX_COL_TEXT, COL_TEXT);
	nx_view_set_search_column (view, COL_TEXT);
	nx_view_on_activate (view, mentions_activate, NULL);
	nx_widget_show (view);

	nx_store_freeze (mentions_store);
	for (l = mentions.head; l; l = l->next)
		row_fill (nx_store_append (mentions_store, NULL), l->data);
	nx_store_thaw (mentions_store);

	hbox = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
	nx_set_border_width (hbox, 5);
	nx_box_pack_end (vbox, hbox, 0, 0, 0);
	gtkutil_button (hbox, NX_STOCK_CLEAR, _("Clear the list"), mentions_clear, 0, _("Clear"));
	nx_widget_show (hbox);

	nx_widget_show (mentions_window);

	/* tests: NEXCHAT_MENTIONS_GO=n activates row n once the window is up */
	if (g_getenv ("NEXCHAT_MENTIONS_GO"))
	{
		nx_row *r = nx_store_nth (mentions_store, NULL, atoi (g_getenv ("NEXCHAT_MENTIONS_GO")));
		if (r)
		{
			mentions_activate (view, r, NULL);
			g_printerr ("mentions: went to %s\n", current_sess ? current_sess->channel : "?");
		}
	}
}
