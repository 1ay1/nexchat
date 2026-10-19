/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <glib/gstdio.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/servlist.h"
#include "../common/mybouncer.h"
#include "../common/server.h"
#include "gtkutil.h"
#include "gtkcompat.h"
#include "bncwin.h"
#include "bncsetup.h"
#include "bncdetect.h"

typedef struct
{
	GtkWidget *win, *head, *sub, *list, *note;
	GtkWidget *start, *stop, *remove, *setup;
	GPtrArray *rows;	/* row widgets, data "net", "switch", "state" */
	char *status;		/* the last mybouncer_status () */
	guint tick;
	gboolean busy;		/* moving networks: the switches wait */
	ircnet *from;
	bnc_found *found;
} bncwin;

static bncwin *open_win;

static void refresh (bncwin *w);

static GtkWidget *
label (const char *markup, gboolean dim)
{
	GtkWidget *l = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (l), markup);
	gtk_label_set_xalign (GTK_LABEL (l), 0);
	gtk_label_set_wrap (GTK_LABEL (l), TRUE);
	gtk_label_set_wrap_mode (GTK_LABEL (l), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_width_chars (GTK_LABEL (l), 1);
	gtk_label_set_max_width_chars (GTK_LABEL (l), 1);
	gtk_label_set_natural_wrap_mode (GTK_LABEL (l), GTK_NATURAL_WRAP_WORD);
	if (dim)
		gtk_widget_add_css_class (l, "dim-label");
	return l;
}

/* what soju says about net, or NULL */
static char *
state_of (bncwin *w, ircnet *net, gboolean *up)
{
	char **lines, *res = NULL, *name;
	int i;

	*up = FALSE;
	if (!w->status)
		return NULL;
	/* soju has it under the entry's name with spaces and / made safe */
	name = g_strdup (net->name);
	g_strdelimit (name, " /:;", '-');
	lines = g_strsplit (w->status, "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		char **f = g_strsplit (lines[i], "\t", 3);
		if (g_strv_length (f) == 3 && !strcmp (f[0], name))
		{
			*up = g_str_has_prefix (f[1], "connected");
			if (*up)
				res = g_strdup_printf (_("in %s"), *f[2] ? f[2] : _("no channels"));
			else if (!strcmp (f[1], "disabled"))
				res = g_strdup (_("switched off"));
			else
				res = g_strdup_printf (_("can't connect: %s"), *f[2] ? f[2] : f[1]);
		}
		g_strfreev (f);
	}
	g_strfreev (lines);
	g_free (name);
	return res;
}

/* ---- the header: is it there, is it running ---- */

static void
update_head (bncwin *w)
{
	mybnc_state st = mybouncer_state ();
	gboolean running = st == MYBNC_SET_UP && mybouncer_running ();
	char *m;

	gtk_widget_set_visible (w->start, st == MYBNC_SET_UP && !running);
	gtk_widget_set_visible (w->stop, running);
	gtk_widget_set_visible (w->remove, st == MYBNC_SET_UP);
	gtk_widget_set_visible (w->setup, st == MYBNC_AVAILABLE);

	if (st == MYBNC_NONE)
	{
		gtk_label_set_markup (GTK_LABEL (w->head), _("<b>No bouncer yet</b>"));
		gtk_label_set_text (GTK_LABEL (w->sub),
			_("Install soju from your system's packages (the package is called soju), then open this "
			  "again: nexchat sets it up and keeps it running. Using one on another computer: "
			  "\"Use another bouncer...\" below."));
		return;
	}
	if (st == MYBNC_AVAILABLE)
	{
		gtk_label_set_markup (GTK_LABEL (w->head), _("<b>soju is installed</b>, not set up yet"));
		gtk_label_set_text (GTK_LABEL (w->sub),
			_("Set it up and nexchat runs it for you, in its own folder, only reachable from this "
			  "computer. Then switch on the networks it should stay on."));
		return;
	}
	m = g_markup_printf_escaped (running ? _("<b>Your bouncer is running</b>  <span alpha=\"60%%\">soju, %s</span>")
										  : _("<b>Your bouncer is stopped</b>  <span alpha=\"60%%\">soju, %s</span>"),
										  !strcmp (mybouncer_kept_by (), "systemd") ? _("kept running by systemd")
																							 : _("started by nexchat"));
	gtk_label_set_markup (GTK_LABEL (w->head), m);
	g_free (m);
	gtk_label_set_text (GTK_LABEL (w->sub), running ?
		_("The networks switched on stay connected when nexchat is closed, and you get what you "
		  "missed when you come back. Switching one off puts it back to connecting directly.") :
		_("Start it and the networks switched on are online again."));
}

/* ---- the networks ---- */

static void
switched (GObject *o, GParamSpec *ps, gpointer data);

static void
moved (const char *text, gboolean done, gboolean ok, gpointer data)
{
	bncwin *w = open_win;
	ircnet *net = data;

	if (!w)
		return;
	if (!ok)
		gtk_label_set_markup (GTK_LABEL (w->note), text);
	if (done)
	{
		w->busy = FALSE;
		/* connected through soju now */
		if (ok && net && mybouncer_owns (net))
		{
			GSList *l;
			gboolean open = FALSE;
			for (l = serv_list; l; l = l->next)
				if (((server *) l->data)->network == net)
				{
					/* was connected directly: over to soju */
					server *s = l->data;
					open = TRUE;
					servlist_connect (s->server_session, net, TRUE);
				}
			if (!open)
				servlist_connect (NULL, net, TRUE);
		}
		refresh (w);
	}
}

static void
switched (GObject *o, GParamSpec *ps, gpointer data)
{
	bncwin *w = data;
	ircnet *net = g_object_get_data (o, "net");
	gboolean on = gtk_switch_get_active (GTK_SWITCH (o));
	char *err = NULL;

	if (w->busy || on == mybouncer_owns (net))
		return;
	gtk_label_set_text (GTK_LABEL (w->note), "");
	if (on)
	{
		GSList *one = g_slist_append (NULL, net);
		char *t = g_strdup_printf (_("Moving %s to your bouncer..."), net->name);
		w->busy = TRUE;
		gtk_label_set_text (GTK_LABEL (w->note), t);
		g_free (t);
		mybouncer_adopt (one, moved, net);
		g_slist_free (one);
	}
	else if (!mybouncer_release (net, &err))
	{
		char *m = g_markup_printf_escaped ("<span foreground=\"#c62828\">%s</span>", err);
		gtk_label_set_markup (GTK_LABEL (w->note), m);
		g_free (m);
		g_free (err);
		refresh (w);
	}
	else
	{
		char *t = g_strdup_printf (_("%s connects directly again (when you next connect to it)."), net->name);
		gtk_label_set_text (GTK_LABEL (w->note), t);
		g_free (t);
		refresh (w);
	}
}

/* ---- one network's nick and login ---- */

typedef struct
{
	bncwin *w;
	ircnet *net;
	GtkWidget *win, *nick, *same, *login_none, *login_sasl, *account, *pass, *note;
	gboolean had_account;
} editor;

static void
editor_toggled (GtkCheckButton *b, gpointer data)
{
	editor *e = data;
	gboolean sasl = gtk_check_button_get_active (GTK_CHECK_BUTTON (e->login_sasl));
	gboolean same = gtk_check_button_get_active (GTK_CHECK_BUTTON (e->same));

	gtk_widget_set_sensitive (e->account, sasl);
	gtk_widget_set_sensitive (e->pass, sasl);
	gtk_widget_set_sensitive (e->nick, !same);
	if (same)
		nx_entry_set_text (e->nick, prefs.hex_irc_nick1);
}

static void
editor_save (GtkWidget *b, gpointer data)
{
	editor *e = data;
	ircnet *net = e->net;
	gboolean same = gtk_check_button_get_active (GTK_CHECK_BUTTON (e->same));
	gboolean sasl = gtk_check_button_get_active (GTK_CHECK_BUTTON (e->login_sasl));
	const char *nick = nx_entry_get_text (e->nick);
	const char *acct = nx_entry_get_text (e->account), *pw = nx_entry_get_text (e->pass);
	char *err = NULL, *kept = NULL;
	GSList *l;

	if (!same && (!*nick || strpbrk (nick, " ,*?!@")))
	{
		gtk_label_set_text (GTK_LABEL (e->note), _("A nick can't be empty or have spaces in it."));
		return;
	}
	/* the password left empty keeps the one it has */
	if (sasl && !*pw && e->had_account)
	{
		kept = mybouncer_own_password (net);
		pw = kept ? kept : "";
	}
	if (sasl && (!*acct || !*pw))
	{
		gtk_label_set_text (GTK_LABEL (e->note), _("Fill in the account and its password, or pick \"No login\"."));
		g_free (kept);
		return;
	}
	/* the nick: in the entry (the global one, or its own) */
	g_free (net->nick);
	net->nick = same ? NULL : g_strdup (nick);
	servlist_save ();
	/* the login: on soju, which logs in again */
	if (!mybouncer_set_login (net, sasl ? acct : NULL, sasl ? pw : NULL, &err))
	{
		gtk_label_set_text (GTK_LABEL (e->note), err);
		g_free (err);
		g_free (kept);
		return;
	}
	g_free (kept);
	/* connected now: the new nick right away */
	for (l = serv_list; l; l = l->next)
	{
		server *s = l->data;
		if (s->network == net && s->connected)
			tcp_sendf (s, "NICK %s\r\n", mybouncer_wanted_nick (net));
	}
	refresh (e->w);
	gtk_window_destroy (GTK_WINDOW (e->win));
}

static GtkWidget *
erow (GtkWidget *grid, int r, const char *text, GtkWidget *w)
{
	GtkWidget *l = gtk_label_new_with_mnemonic (text);
	gtk_label_set_xalign (GTK_LABEL (l), 0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (l), w);
	gtk_grid_attach (GTK_GRID (grid), l, 0, r, 1, 1);
	gtk_widget_set_hexpand (w, TRUE);
	gtk_grid_attach (GTK_GRID (grid), w, 1, r, 1, 1);
	return w;
}

static void
edit_open (bncwin *w, ircnet *net)
{
	editor *e = g_new0 (editor, 1);
	GtkWidget *vbox, *grid, *bb, *save, *cancel;
	char *nick, *from, *login, *account, *t;

	e->w = w;
	e->net = net;
	mybouncer_identity (net, &nick, &from, &login, &account);
	e->had_account = account != NULL;
	e->win = nx_window_new ();
	t = g_strdup_printf (_("%s on your bouncer"), net->name);
	gtk_window_set_title (GTK_WINDOW (e->win), t);
	g_free (t);
	gtk_window_set_transient_for (GTK_WINDOW (e->win), GTK_WINDOW (w->win));
	gtk_window_set_modal (GTK_WINDOW (e->win), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (e->win), 440, -1);
	g_object_set_data_full (G_OBJECT (e->win), "editor", e, g_free);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	nx_set_border_width (vbox, 12);
	gtk_window_set_child (GTK_WINDOW (e->win), vbox);

	t = g_markup_printf_escaped (_("<b>Your nick on %s</b>"), net->name);
	gtk_box_append (GTK_BOX (vbox), label (t, FALSE));
	g_free (t);
	e->same = gtk_check_button_new_with_mnemonic (_("The _same as everywhere (Network List > Nick name)"));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (e->same), !net->nick || !*net->nick);
	gtk_box_append (GTK_BOX (vbox), e->same);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	e->nick = erow (grid, 0, _("_Nick:"), gtk_entry_new ());
	nx_entry_set_text (e->nick, nick);
	gtk_box_append (GTK_BOX (vbox), grid);

	t = g_markup_printf_escaped (_("<b>Logging in to %s</b>"), net->name);
	gtk_box_append (GTK_BOX (vbox), label (t, FALSE));
	g_free (t);
	e->login_none = gtk_check_button_new_with_mnemonic (_("N_o login (as a guest)"));
	e->login_sasl = gtk_check_button_new_with_mnemonic (_("With my _account (SASL)"));
	gtk_check_button_set_group (GTK_CHECK_BUTTON (e->login_sasl), GTK_CHECK_BUTTON (e->login_none));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (account ? e->login_sasl : e->login_none), TRUE);
	gtk_box_append (GTK_BOX (vbox), e->login_none);
	gtk_box_append (GTK_BOX (vbox), e->login_sasl);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	gtk_widget_set_margin_start (grid, 24);
	e->account = erow (grid, 0, _("A_ccount:"), gtk_entry_new ());
	e->pass = erow (grid, 1, _("_Password:"), gtk_entry_new ());
	gtk_entry_set_visibility (GTK_ENTRY (e->pass), FALSE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (e->pass), account ? _("kept; type a new one to change it") : "");
	nx_entry_set_text (e->account, account ? account : nick);
	gtk_box_append (GTK_BOX (vbox), grid);
	gtk_box_append (GTK_BOX (vbox), label (_("Your account on that network (the one you registered with NickServ), "
														"not the bouncer's. The bouncer logs in with it for you, also while "
														"nexchat is closed."), TRUE));

	e->note = label ("", FALSE);
	gtk_box_append (GTK_BOX (vbox), e->note);
	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (bb, GTK_ALIGN_END);
	cancel = gtk_button_new_with_mnemonic (_("Ca_ncel"));
	save = gtk_button_new_with_mnemonic (_("Sa_ve"));
	gtk_box_append (GTK_BOX (bb), cancel);
	gtk_box_append (GTK_BOX (bb), save);
	gtk_box_append (GTK_BOX (vbox), bb);
	gtk_window_set_default_widget (GTK_WINDOW (e->win), save);
	g_signal_connect (e->same, "toggled", G_CALLBACK (editor_toggled), e);
	g_signal_connect (e->login_sasl, "toggled", G_CALLBACK (editor_toggled), e);
	g_signal_connect (save, "clicked", G_CALLBACK (editor_save), e);
	g_signal_connect_swapped (cancel, "clicked", G_CALLBACK (gtk_window_destroy), e->win);
	editor_toggled (NULL, e);
	g_free (nick);
	g_free (from);
	g_free (login);
	g_free (account);
	gtk_window_present (GTK_WINDOW (e->win));
}

static void
edit_cb (GtkWidget *b, gpointer data)
{
	edit_open (data, g_object_get_data (G_OBJECT (b), "net"));
}

/* tests: the editor, filled in and saved as a person would */
static void
edit_fill_and_save (bncwin *w, ircnet *net, const char *nick, const char *acct, const char *pw)
{
	GListModel *tl;
	guint i;

	edit_open (w, net);
	tl = gtk_window_get_toplevels ();
	for (i = 0; i < g_list_model_get_n_items (tl); i++)
	{
		GtkWidget *win = g_list_model_get_item (tl, i);
		editor *e = g_object_get_data (G_OBJECT (win), "editor");
		g_object_unref (win);
		if (!e)
			continue;
		gtk_check_button_set_active (GTK_CHECK_BUTTON (e->same), FALSE);
		nx_entry_set_text (e->nick, nick);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (e->login_sasl), *acct != 0);
		nx_entry_set_text (e->account, acct);
		nx_entry_set_text (e->pass, pw);
		editor_save (NULL, e);
		break;
	}
}

static void
fill_list (bncwin *w)
{
	GtkWidget *c;
	GSList *l;
	gboolean set = mybouncer_state () == MYBNC_SET_UP;

	while ((c = gtk_widget_get_first_child (w->list)))
		gtk_box_remove (GTK_BOX (w->list), c);
	g_ptr_array_set_size (w->rows, 0);
	if (!set)
		return;
	for (l = network_list; l; l = l->next)
	{
		ircnet *n = l->data;
		ircserver *sv = n->servlist ? n->servlist->data : NULL;
		GtkWidget *row, *box, *name, *state, *sw;
		gboolean mine = mybouncer_owns (n);
		char *m;

		/* entries of a bouncer elsewhere aren't ours to move */
		if (!sv || (!mine && n->logintype == LOGIN_PASS))
			continue;
		/* only the ones you use, and the ones on it */
		if (!mine && !(n->flags & (FLAG_FAVORITE | FLAG_AUTO_CONNECT)) && !n->favchanlist && n != w->from)
			continue;
		row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_hexpand (box, TRUE);
		m = g_markup_printf_escaped ("<b>%s</b>", n->name);
		name = label (m, FALSE);
		g_free (m);
		state = label ("", TRUE);
		gtk_box_append (GTK_BOX (box), name);
		gtk_box_append (GTK_BOX (box), state);
		/* on it: who it's connected as, and how it logs in */
		if (mine)
		{
			char *nk, *from, *login, *acct, *m2;
			GtkWidget *who, *edit;
			mybouncer_identity (n, &nk, &from, &login, &acct);
			m2 = g_markup_printf_escaped (_("as <b>%s</b> <span alpha=\"60%%\">(%s)</span>, %s"), nk, from, login);
			who = label (m2, TRUE);
			gtk_box_append (GTK_BOX (box), who);
			g_object_set_data (G_OBJECT (row), "who", who);
			edit = gtk_button_new_with_label (_("Nick and login..."));
			gtk_widget_set_valign (edit, GTK_ALIGN_CENTER);
			g_object_set_data (G_OBJECT (edit), "net", n);
			g_signal_connect (edit, "clicked", G_CALLBACK (edit_cb), w);
			gtk_box_append (GTK_BOX (row), box);
			gtk_box_append (GTK_BOX (row), edit);
			g_free (m2);
			g_free (nk);
			g_free (from);
			g_free (login);
			g_free (acct);
		}
		else
			gtk_box_append (GTK_BOX (row), box);
		sw = gtk_switch_new ();
		gtk_widget_set_valign (sw, GTK_ALIGN_CENTER);
		gtk_switch_set_active (GTK_SWITCH (sw), mine);
		gtk_widget_set_tooltip_text (sw, _("On: stays connected through your bouncer when nexchat is closed"));
		g_object_set_data (G_OBJECT (sw), "net", n);
		g_signal_connect (sw, "notify::active", G_CALLBACK (switched), w);
		gtk_box_append (GTK_BOX (row), sw);
		g_object_set_data (G_OBJECT (row), "net", n);
		g_object_set_data (G_OBJECT (row), "switch", sw);
		g_object_set_data (G_OBJECT (row), "state", state);
		gtk_box_append (GTK_BOX (w->list), row);
		g_ptr_array_add (w->rows, row);
	}
	if (!w->rows->len)
		gtk_box_append (GTK_BOX (w->list), label (_("No networks to put on it yet: mark the ones you use as a "
																  "favorite in the Network List, or open this from a network's tab."), TRUE));
}

static void
update_states (bncwin *w)
{
	guint i;

	g_free (w->status);
	w->status = mybouncer_status ();
	for (i = 0; i < w->rows->len; i++)
	{
		GtkWidget *row = w->rows->pdata[i];
		ircnet *n = g_object_get_data (G_OBJECT (row), "net");
		GtkWidget *st = g_object_get_data (G_OBJECT (row), "state");
		gboolean up;
		char *s;

		if (!mybouncer_owns (n))
		{
			ircserver *sv = n->servlist->data;
			char *t = g_strdup_printf (_("connects directly (%s)"), sv->hostname);
			gtk_label_set_text (GTK_LABEL (st), t);
			g_free (t);
			continue;
		}
		s = state_of (w, n, &up);
		if (!s)
			s = g_strdup (mybouncer_running () ? _("joining...") : _("bouncer stopped"));
		{
			char *m = up ? g_markup_printf_escaped ("<span foreground=\"#2e7d32\">\xe2\x97\x8f</span> %s", s)
							 : g_markup_printf_escaped ("<span foreground=\"#c62828\">\xe2\x97\x8f</span> %s", s);
			gtk_label_set_markup (GTK_LABEL (st), m);
			g_free (m);
		}
		g_free (s);
	}
}

static void
refresh (bncwin *w)
{
	update_head (w);
	fill_list (w);
	update_states (w);
}

static gboolean
tick (gpointer data)
{
	bncwin *w = data;
	if (!w->busy)
	{
		update_head (w);
		update_states (w);
	}
	return G_SOURCE_CONTINUE;
}

/* ---- buttons ---- */

static void
say (bncwin *w, const char *kind, const char *text)
{
	char *m = !strcmp (kind, "no") ? g_markup_printf_escaped ("<span foreground=\"#c62828\">%s</span>", text)
											 : g_markup_escape_text (text, -1);
	gtk_label_set_markup (GTK_LABEL (w->note), m);
	g_free (m);
}

static void
setup_cb (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;

	if (!mybouncer_setup (&err))
	{
		say (w, "no", err);
		g_free (err);
		return;
	}
	say (w, "ok", "");
	refresh (w);
	/* opened from a network: that one goes on it right away */
	if (w->from && !mybouncer_owns (w->from))
	{
		guint i;
		for (i = 0; i < w->rows->len; i++)
		{
			GtkWidget *row = w->rows->pdata[i];
			if (g_object_get_data (G_OBJECT (row), "net") == w->from)
				gtk_switch_set_active (GTK_SWITCH (g_object_get_data (G_OBJECT (row), "switch")), TRUE);
		}
	}
}

static void
start_cb (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;
	if (!mybouncer_start (&err))
	{
		say (w, "no", err ? err : "?");
		g_free (err);
	}
	refresh (w);
}

static void
stop_cb (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	mybouncer_stop ();
	say (w, "ok", _("Stopped. The networks on it are offline until you start it again."));
	refresh (w);
}

static void
remove_cb (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;
	if (!mybouncer_remove (&err))
	{
		say (w, "no", err ? err : "?");
		g_free (err);
	}
	else
		say (w, "ok", _("Removed. Every network connects directly again, as it did before."));
	refresh (w);
}

static void
other_cb (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	bncsetup_open (GTK_WINDOW (w->win), NULL, NULL);
}

static void
win_free (gpointer data)
{
	bncwin *w = data;
	if (open_win == w)
		open_win = NULL;
	if (w->tick)
		g_source_remove (w->tick);
	g_ptr_array_unref (w->rows);
	g_free (w->status);
	g_free (w);
}

char *
bncwin_describe (void)
{
	bncwin *w = open_win;
	GString *s;
	guint i;
	const char *flip = g_getenv ("NEXCHAT_BNCWIN_FLIP");

	if (!w)
		return NULL;
	/* tests: a file naming a network: its switch is flipped, once */
	if (flip && g_file_test (flip, G_FILE_TEST_EXISTS))
	{
		char *name = NULL;
		g_file_get_contents (flip, &name, NULL, NULL);
		g_unlink (flip);
		if (name)
		{
			g_strstrip (name);
			for (i = 0; i < w->rows->len; i++)
			{
				GtkWidget *row = w->rows->pdata[i];
				ircnet *n = g_object_get_data (G_OBJECT (row), "net");
				GtkWidget *sw = g_object_get_data (G_OBJECT (row), "switch");
				if (!strcmp (n->name, name))
					gtk_switch_set_active (GTK_SWITCH (sw), !gtk_switch_get_active (GTK_SWITCH (sw)));
			}
			g_free (name);
		}
	}
	/* tests: "network\tnick\taccount\tpassword" in a file: the editor is
	   opened for it, filled in and saved, once */
	{
		const char *ef = g_getenv ("NEXCHAT_BNCWIN_EDIT");
		char *line = NULL;
		if (ef && g_file_get_contents (ef, &line, NULL, NULL))
		{
			char **f = g_strsplit (g_strstrip (line), "\t", 4);
			g_unlink (ef);
			if (g_strv_length (f) == 4)
			{
				ircnet *n = servlist_net_find (f[0], NULL, strcmp);
				if (n)
					edit_fill_and_save (w, n, f[1], f[2], f[3]);
			}
			g_strfreev (f);
			g_free (line);
		}
	}
	s = g_string_new (gtk_label_get_text (GTK_LABEL (w->head)));
	for (i = 0; i < w->rows->len; i++)
	{
		GtkWidget *row = w->rows->pdata[i];
		ircnet *n = g_object_get_data (G_OBJECT (row), "net");
		GtkWidget *who = g_object_get_data (G_OBJECT (row), "who");
		g_string_append_printf (s, " | %s [%s] %s%s%s", n->name,
										gtk_switch_get_active (GTK_SWITCH (g_object_get_data (G_OBJECT (row), "switch"))) ? "on" : "off",
										gtk_label_get_text (GTK_LABEL (g_object_get_data (G_OBJECT (row), "state"))),
										who ? " / " : "", who ? gtk_label_get_text (GTK_LABEL (who)) : "");
	}
	g_string_append_printf (s, " | note: %s", gtk_label_get_text (GTK_LABEL (w->note)));
	return g_string_free (s, FALSE);
}

void
bncwin_open (GtkWindow *parent, ircnet *from)
{
	bncwin *w;
	GtkWidget *vbox, *bb, *sw, *other, *close;
	char *t;

	if (open_win)
	{
		gtk_window_present (GTK_WINDOW (open_win->win));
		return;
	}
	w = g_new0 (bncwin, 1);
	open_win = w;
	w->from = from;
	w->rows = g_ptr_array_new ();
	w->win = nx_window_new ();
	t = g_strdup_printf (_("Bouncer - %s"), _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (w->win), t);
	g_free (t);
	if (parent)
		gtk_window_set_transient_for (GTK_WINDOW (w->win), parent);
	gtk_window_set_default_size (GTK_WINDOW (w->win), 560, 420);
	g_object_set_data_full (G_OBJECT (w->win), "bncwin", w, win_free);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	nx_set_border_width (vbox, 12);
	gtk_window_set_child (GTK_WINDOW (w->win), vbox);

	w->head = label ("", FALSE);
	w->sub = label ("", TRUE);
	gtk_box_append (GTK_BOX (vbox), w->head);
	gtk_box_append (GTK_BOX (vbox), w->sub);

	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	w->setup = gtk_button_new_with_mnemonic (_("_Set it up"));
	w->start = gtk_button_new_with_mnemonic (_("_Start"));
	w->stop = gtk_button_new_with_mnemonic (_("S_top"));
	w->remove = gtk_button_new_with_mnemonic (_("_Remove..."));
	gtk_widget_set_tooltip_text (w->remove, _("Stop it and put every network back to connecting directly"));
	gtk_box_append (GTK_BOX (bb), w->setup);
	gtk_box_append (GTK_BOX (bb), w->start);
	gtk_box_append (GTK_BOX (bb), w->stop);
	gtk_box_append (GTK_BOX (bb), w->remove);
	gtk_box_append (GTK_BOX (vbox), bb);
	g_signal_connect (w->setup, "clicked", G_CALLBACK (setup_cb), w);
	g_signal_connect (w->start, "clicked", G_CALLBACK (start_cb), w);
	g_signal_connect (w->stop, "clicked", G_CALLBACK (stop_cb), w);
	g_signal_connect (w->remove, "clicked", G_CALLBACK (remove_cb), w);

	w->list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	nx_set_border_width (w->list, 4);
	sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), w->list);
	gtk_box_append (GTK_BOX (vbox), sw);

	w->note = label ("", FALSE);
	gtk_label_set_selectable (GTK_LABEL (w->note), TRUE);
	gtk_box_append (GTK_BOX (vbox), w->note);

	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	other = gtk_button_new_with_mnemonic (_("Use _another bouncer..."));
	gtk_widget_set_tooltip_text (other, _("One on another computer, or that someone runs for you"));
	close = gtk_button_new_with_mnemonic (_("_Close"));
	gtk_widget_set_hexpand (other, TRUE);
	gtk_widget_set_halign (other, GTK_ALIGN_START);
	gtk_box_append (GTK_BOX (bb), other);
	gtk_box_append (GTK_BOX (bb), close);
	gtk_box_append (GTK_BOX (vbox), bb);
	g_signal_connect (other, "clicked", G_CALLBACK (other_cb), w);
	g_signal_connect_swapped (close, "clicked", G_CALLBACK (gtk_window_destroy), w->win);

	refresh (w);
	w->tick = g_timeout_add_seconds (3, tick, w);
	gtk_window_present (GTK_WINDOW (w->win));

	/* opened from a network that isn't on it: it goes on now, no questions.
	   Not set up yet: that's the one button to press */
	if (from && mybouncer_state () == MYBNC_SET_UP && !mybouncer_owns (from))
	{
		guint i;
		for (i = 0; i < w->rows->len; i++)
		{
			GtkWidget *row = w->rows->pdata[i];
			if (g_object_get_data (G_OBJECT (row), "net") == from)
				gtk_switch_set_active (GTK_SWITCH (g_object_get_data (G_OBJECT (row), "switch")), TRUE);
		}
	}
}
