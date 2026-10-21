/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"   /* before glib: it sets GLIB_VERSION_* and G_LOG_DOMAIN */

#include <string.h>
#include <glib/gstdio.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/servlist.h"
#include "../common/mybouncer.h"
#include "../common/server.h"
#include "../common/bouncer.h"
#include "gtkutil.h"
#include "gtkcompat.h"
#include "bncwin.h"
#include "bncsetup.h"
#include "bncdetect.h"

typedef struct
{
	GtkWidget *win, *head, *sub, *list, *note;
	GtkWidget *start, *stop, *remove, *setup, *other;
	GPtrArray *rows;	/* row widgets, data "net", "switch", "state" */
	char *status;		/* the last mybouncer_status () */
	guint tick;
	gboolean busy;		/* moving networks: the switches wait */
	ircnet *from;
	bnc_found *found;
	/* Channels tab */
	GtkWidget *tabs;
	GtkWidget *ch_net, *ch_list, *ch_entry;
	GPtrArray *ch_nets;	/* ircnet* behind ch_net's items */
	/* Access tab */
	GtkWidget *ac_where, *ac_pass, *ac_remote, *ac_remote_info, *ac_systemd;
	/* Storage tab */
	GtkWidget *st_db, *st_files, *st_title, *st_log;
	gboolean filling;	/* setting widgets from soju: no callbacks */
	int remote;		/* soju servers elsewhere shown in the list */
	char *remote_key;
	guint serial;
} bncwin;

static bncwin *open_win;

static void refresh (bncwin *w);
static void say (bncwin *w, const char *kind, const char *text);

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
	if (w->other)
		gtk_button_set_label (GTK_BUTTON (w->other), w->remote ? _("_Add another bouncer...") : _("Use _another bouncer..."));

	/* Channels, Access, Storage are about our own soju */
	if (w->tabs)
	{
		gtk_notebook_set_show_tabs (GTK_NOTEBOOK (w->tabs), st == MYBNC_SET_UP);
		if (st != MYBNC_SET_UP)
			gtk_notebook_set_current_page (GTK_NOTEBOOK (w->tabs), 0);
	}
	if (st != MYBNC_SET_UP && w->remote)
	{
		gtk_label_set_markup (GTK_LABEL (w->head), w->remote > 1 ? _("<b>Your bouncers</b>") : _("<b>Your bouncer</b>"));
		gtk_label_set_text (GTK_LABEL (w->sub),
			_("It stays on your networks while nexchat is closed, and you get what you missed when you "
			  "come back. Connect to it here, and add or remove the networks it keeps you on."));
		gtk_widget_set_visible (w->setup, FALSE);
		return;
	}
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

/* the list is rebuilt on the next idle, not from a switch's own notify
   handler: gtk_switch_set_active still touches the switch after emitting
   notify, and a rebuild in there frees it first (GTK criticals, or worse) */
static guint win_serial;	/* bumps per window: an idle for a closed one does nothing */

static gboolean
refresh_idle (gpointer data)
{
	if (open_win && open_win->serial == GPOINTER_TO_UINT (data))
		refresh (open_win);
	return G_SOURCE_REMOVE;
}

static void
refresh_soon (bncwin *w)
{
	g_idle_add (refresh_idle, GUINT_TO_POINTER (w->serial));
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
		refresh_soon (w);
	}
	else
	{
		char *t = g_strdup_printf (_("%s connects directly again (when you next connect to it)."), net->name);
		gtk_label_set_text (GTK_LABEL (w->note), t);
		g_free (t);
		refresh_soon (w);
	}
}

/* ---- one network's nick and login ---- */

typedef struct
{
	bncwin *w;
	ircnet *net;
	GtkWidget *win, *nick, *same, *login_none, *login_sasl, *account, *pass, *note;
	GtkWidget *realname, *username, *srvpass, *away, *enabled, *commands, *certfp;
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

static char *
text_of (GtkWidget *tv)
{
	GtkTextBuffer *b = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));
	GtkTextIter s, e;
	gtk_text_buffer_get_bounds (b, &s, &e);
	return gtk_text_buffer_get_text (b, &s, &e, FALSE);
}

/* one setting: sent only when it changed from what we last set */
static gboolean
net_changed (ircnet *net, const char *key, const char *now, const char *dflt, char **err)
{
	char *was = mybouncer_net_get (net, key);
	gboolean same = !g_strcmp0 (was ? was : dflt, now), ok = TRUE;

	if (!same)
		ok = !strcmp (key, "commands") ? mybouncer_net_set_commands (net, now, err)
												 : mybouncer_net_set (net, key, now, err);
	g_free (was);
	return ok;
}

static void
editor_cert (GtkWidget *b, gpointer data)
{
	editor *e = data;
	char *err = NULL, *fp = mybouncer_net_certfp (e->net, FALSE, &err), *t;

	/* none yet: make one */
	if (!fp)
	{
		g_clear_pointer (&err, g_free);
		fp = mybouncer_net_certfp (e->net, TRUE, &err);
	}
	if (!fp)
	{
		gtk_label_set_text (GTK_LABEL (e->note), err ? err : "?");
		g_free (err);
		return;
	}
	t = g_strdup_printf (_("Its fingerprint, to add to your account (NickServ CERT ADD):\n%s"), fp);
	gtk_label_set_text (GTK_LABEL (e->certfp), t);
	gtk_label_set_selectable (GTK_LABEL (e->certfp), TRUE);
	g_free (t);
	g_free (fp);
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
	char *err = NULL, *kept = NULL, *cmds;
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
	/* the rest, each only when changed */
	cmds = text_of (e->commands);
	g_strstrip (cmds);
	if (!net_changed (net, "realname", nx_entry_get_text (e->realname), "", &err) ||
		 !net_changed (net, "username", nx_entry_get_text (e->username), "", &err) ||
		 (*nx_entry_get_text (e->srvpass) && !net_changed (net, "pass", nx_entry_get_text (e->srvpass), "", &err)) ||
		 !net_changed (net, "auto-away", gtk_check_button_get_active (GTK_CHECK_BUTTON (e->away)) ? "true" : "false", "true", &err) ||
		 !net_changed (net, "enabled", gtk_check_button_get_active (GTK_CHECK_BUTTON (e->enabled)) ? "true" : "false", "true", &err) ||
		 !net_changed (net, "commands", cmds, "", &err))
	{
		gtk_label_set_text (GTK_LABEL (e->note), err ? err : "?");
		g_free (err);
		g_free (cmds);
		refresh (e->w);
		return;
	}
	g_free (cmds);
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
	GtkWidget *outer, *scroll, *vbox, *grid, *bb, *save, *cancel, *cert, *sw;
	char *nick, *from, *login, *account, *t, *v;

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
	gtk_window_set_default_size (GTK_WINDOW (e->win), 480, 560);
	g_object_set_data_full (G_OBJECT (e->win), "editor", e, g_free);

	outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	nx_set_border_width (outer, 12);
	gtk_window_set_child (GTK_WINDOW (e->win), outer);
	scroll = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_box_append (GTK_BOX (outer), scroll);
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), vbox);

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

	/* the rest of what soju keeps for the network */
	gtk_box_append (GTK_BOX (vbox), label (_("<b>More</b>"), FALSE));
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	e->realname = erow (grid, 0, _("_Real name:"), gtk_entry_new ());
	e->username = erow (grid, 1, _("_User name:"), gtk_entry_new ());
	e->srvpass = erow (grid, 2, _("Server pass_word:"), gtk_entry_new ());
	gtk_entry_set_visibility (GTK_ENTRY (e->srvpass), FALSE);
	v = mybouncer_net_get (net, "realname");
	nx_entry_set_text (e->realname, v ? v : "");
	gtk_entry_set_placeholder_text (GTK_ENTRY (e->realname), _("the bouncer's default"));
	g_free (v);
	v = mybouncer_net_get (net, "username");
	nx_entry_set_text (e->username, v ? v : "");
	gtk_entry_set_placeholder_text (GTK_ENTRY (e->username), _("the bouncer's default"));
	g_free (v);
	v = mybouncer_net_get (net, "pass");
	gtk_entry_set_placeholder_text (GTK_ENTRY (e->srvpass), v && *v ? _("kept; type a new one to change it")
																		: _("only if the server asks for one"));
	g_free (v);
	gtk_box_append (GTK_BOX (vbox), grid);

	e->away = gtk_check_button_new_with_mnemonic (_("Show me as _away when no client is connected"));
	v = mybouncer_net_get (net, "auto-away");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (e->away), g_strcmp0 (v, "false") != 0);
	g_free (v);
	gtk_box_append (GTK_BOX (vbox), e->away);
	e->enabled = gtk_check_button_new_with_mnemonic (_("Stay _connected (off: kept on the bouncer, but offline)"));
	v = mybouncer_net_get (net, "enabled");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (e->enabled), g_strcmp0 (v, "false") != 0);
	g_free (v);
	gtk_box_append (GTK_BOX (vbox), e->enabled);

	gtk_box_append (GTK_BOX (vbox), label (_("Commands it sends when it connects, one per line "
															"(raw IRC, e.g. PRIVMSG NickServ :IDENTIFY pass):"), TRUE));
	e->commands = gtk_text_view_new ();
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (e->commands), TRUE);
	v = mybouncer_net_get (net, "commands");
	gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (e->commands)), v ? v : "", -1);
	g_free (v);
	sw = gtk_scrolled_window_new ();
	gtk_widget_set_size_request (sw, -1, 70);
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), e->commands);
	gtk_box_append (GTK_BOX (vbox), sw);

	gtk_box_append (GTK_BOX (vbox), label (_("Logging in with a certificate instead of a password: the bouncer "
															"makes one, you add its fingerprint to your account."), TRUE));
	cert = gtk_button_new_with_mnemonic (_("Make a c_ertificate"));
	gtk_widget_set_halign (cert, GTK_ALIGN_START);
	gtk_box_append (GTK_BOX (vbox), cert);
	e->certfp = label ("", TRUE);
	gtk_box_append (GTK_BOX (vbox), e->certfp);
	g_signal_connect (cert, "clicked", G_CALLBACK (editor_cert), e);
	{
		char *err = NULL, *fp = mybouncer_net_certfp (net, FALSE, &err);
		if (fp)
		{
			gtk_button_set_label (GTK_BUTTON (cert), _("Show its c_ertificate"));
			gtk_button_set_use_underline (GTK_BUTTON (cert), TRUE);
		}
		g_free (fp);
		g_free (err);
	}

	e->note = label ("", FALSE);
	gtk_box_append (GTK_BOX (outer), e->note);
	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (bb, GTK_ALIGN_END);
	cancel = gtk_button_new_with_mnemonic (_("Ca_ncel"));
	save = gtk_button_new_with_mnemonic (_("Sa_ve"));
	gtk_box_append (GTK_BOX (bb), cancel);
	gtk_box_append (GTK_BOX (bb), save);
	gtk_box_append (GTK_BOX (outer), bb);
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

/* ---- a soju elsewhere you're connected to (irc.agentty.org) ---- */

typedef struct
{
	bncwin *w;
	server *serv;
	char *id, *name;
	GtkWidget *win, *nick, *username, *realname, *pass, *sasl, *account, *apass, *note;
} reditor;

static void
reditor_free (gpointer p)
{
	reditor *e = p;
	g_free (e->id);
	g_free (e->name);
	g_free (e);
}

static void
reditor_toggled (GtkCheckButton *b, gpointer data)
{
	reditor *e = data;
	gboolean on = gtk_check_button_get_active (GTK_CHECK_BUTTON (e->sasl));
	gtk_widget_set_sensitive (e->account, on);
	gtk_widget_set_sensitive (e->apass, on);
}

static void
reditor_save (GtkWidget *b, gpointer data)
{
	reditor *e = data;
	const char *pass = nx_entry_get_text (e->pass);
	const char *acct = nx_entry_get_text (e->account), *apw = nx_entry_get_text (e->apass);
	gboolean sasl = gtk_check_button_get_active (GTK_CHECK_BUTTON (e->sasl));
	char *err = NULL;

	if (!is_server (e->serv) || !e->serv->connected)
	{
		gtk_label_set_text (GTK_LABEL (e->note), _("Not connected to the bouncer any more."));
		return;
	}
	if (sasl && (!*acct || !*apw))
	{
		gtk_label_set_text (GTK_LABEL (e->note), _("Fill in the account and its password, or untick the login."));
		return;
	}
	if (!bouncer_change_network (e->serv, e->id, nx_entry_get_text (e->nick), nx_entry_get_text (e->username),
										  nx_entry_get_text (e->realname), *pass ? pass : NULL, &err) ||
		 (sasl && !bouncer_set_sasl (e->serv, e->name, acct, apw, &err)))
	{
		gtk_label_set_text (GTK_LABEL (e->note), err ? err : "?");
		g_free (err);
		return;
	}
	gtk_window_destroy (GTK_WINDOW (e->win));
}

static void
reditor_cancel (GtkWidget *b, gpointer data)
{
	gtk_window_destroy (GTK_WINDOW (((reditor *) data)->win));
}

static void
redit_open (bncwin *w, server *serv, bnc_network *n)
{
	reditor *e = g_new0 (reditor, 1);
	GtkWidget *vbox, *grid, *bb, *save, *cancel;
	char *t;

	e->w = w;
	e->serv = serv;
	e->id = g_strdup (n->id);
	e->name = g_strdup (n->name);
	e->win = nx_window_new ();
	t = g_strdup_printf (_("%s on %s"), n->name, serv->servername);
	gtk_window_set_title (GTK_WINDOW (e->win), t);
	g_free (t);
	gtk_window_set_transient_for (GTK_WINDOW (e->win), GTK_WINDOW (w->win));
	gtk_window_set_modal (GTK_WINDOW (e->win), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (e->win), 440, -1);
	g_object_set_data_full (G_OBJECT (e->win), "reditor", e, reditor_free);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	nx_set_border_width (vbox, 12);
	gtk_window_set_child (GTK_WINDOW (e->win), vbox);
	t = g_markup_printf_escaped (_("<b>You on %s</b>"), n->name);
	gtk_box_append (GTK_BOX (vbox), label (t, FALSE));
	g_free (t);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	e->nick = erow (grid, 0, _("_Nick:"), gtk_entry_new ());
	e->username = erow (grid, 1, _("_User name:"), gtk_entry_new ());
	e->realname = erow (grid, 2, _("_Real name:"), gtk_entry_new ());
	e->pass = erow (grid, 3, _("Server pass_word:"), gtk_entry_new ());
	gtk_entry_set_visibility (GTK_ENTRY (e->pass), FALSE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (e->pass), _("only if the server asks for one"));
	nx_entry_set_text (e->nick, n->nick ? n->nick : "");
	nx_entry_set_text (e->username, n->username ? n->username : "");
	nx_entry_set_text (e->realname, n->realname ? n->realname : "");
	gtk_box_append (GTK_BOX (vbox), grid);

	e->sasl = gtk_check_button_new_with_mnemonic (_("_Log in to my account (SASL)"));
	gtk_box_append (GTK_BOX (vbox), e->sasl);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	e->account = erow (grid, 0, _("_Account:"), gtk_entry_new ());
	e->apass = erow (grid, 1, _("Pa_ssword:"), gtk_entry_new ());
	gtk_entry_set_visibility (GTK_ENTRY (e->apass), FALSE);
	nx_entry_set_text (e->account, n->nick ? n->nick : "");
	gtk_box_append (GTK_BOX (vbox), grid);
	gtk_box_append (GTK_BOX (vbox), label (_("Your account on that network (registered with NickServ), not the "
														"bouncer's. The bouncer reconnects to log in with it. Leave this "
														"unticked to keep the login it has."), TRUE));
	reditor_toggled (NULL, e);
	g_signal_connect (e->sasl, "toggled", G_CALLBACK (reditor_toggled), e);

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
	g_signal_connect (save, "clicked", G_CALLBACK (reditor_save), e);
	g_signal_connect (cancel, "clicked", G_CALLBACK (reditor_cancel), e);
	gtk_window_present (GTK_WINDOW (e->win));
}

static void
redit_cb (GtkWidget *b, gpointer data)
{
	server *serv = g_object_get_data (G_OBJECT (b), "serv");
	const char *id = g_object_get_data (G_OBJECT (b), "id");
	GPtrArray *nets;
	guint i;

	if (!is_server (serv) || !(nets = bouncer_networks (serv)))
		return;
	for (i = 0; i < nets->len; i++)
		if (!strcmp (((bnc_network *) nets->pdata[i])->id, id))
			redit_open (data, serv, nets->pdata[i]);
}

/* ---- bouncers elsewhere you log in to (irc.agentty.org) ----
   One card each, there whether it's connected or not: how it's doing in
   plain words, Connect / Disconnect, Edit, Forget, and once it's in, its
   networks with Open, Nick and login, Remove, and "Add a network". */

static void
rb_connect (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	ircnet *net = g_object_get_data (G_OBJECT (b), "net");
	server *sv = bouncer_server_of (net);

	if (sv)
		sv->disconnect (sv->server_session, TRUE, -1);
	else
		servlist_connect (NULL, net, TRUE);
	refresh (w);
}

static void
rb_edit (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	bncsetup_open (GTK_WINDOW (w->win), g_object_get_data (G_OBJECT (b), "net"), NULL);
}

static void
rb_forget (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	ircnet *net = g_object_get_data (G_OBJECT (b), "net");
	server *sv;

	/* the first press asks */
	if (!g_object_get_data (G_OBJECT (b), "sure"))
	{
		g_object_set_data (G_OBJECT (b), "sure", GINT_TO_POINTER (1));
		gtk_button_set_label (GTK_BUTTON (b), _("Forget it, sure?"));
		gtk_widget_add_css_class (b, "destructive-action");
		return;
	}
	if ((sv = bouncer_server_of (net)))
		sv->disconnect (sv->server_session, TRUE, -1);
	bouncer_forget (net);
	say (w, "ok", _("Forgotten here. The bouncer itself and what's on it are unchanged."));
	refresh (w);
}

/* a network of a connected bouncer: open its tab */
static void
rn_open (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	ircnet *base = g_object_get_data (G_OBJECT (b), "base");
	const char *name = g_object_get_data (G_OBJECT (b), "name");
	char *full = g_strdup_printf ("%s/%s", base->name, name);
	ircnet *e = servlist_net_find (full, NULL, strcmp);
	GSList *l;

	g_free (full);
	if (!e)
	{
		say (w, "no", _("Its entry isn't there yet; give it a second and try again."));
		return;
	}
	for (l = serv_list; l; l = l->next)
	{
		server *sv = l->data;
		if (sv->network == e)
		{
			session *s = sv->front_session ? sv->front_session : sv->server_session;
			fe_ctrl_gui (s, FE_GUI_FOCUS, 0);
			/* there but not in (an old address, a dropped line): again */
			if (!sv->connected && !sv->connecting)
				servlist_connect (sv->server_session, e, TRUE);
			return;
		}
	}
	servlist_connect (NULL, e, TRUE);
}

static void
rn_remove (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	server *serv = g_object_get_data (G_OBJECT (b), "serv");
	const char *id = g_object_get_data (G_OBJECT (b), "id");
	char *err = NULL;

	if (!g_object_get_data (G_OBJECT (b), "sure"))
	{
		g_object_set_data (G_OBJECT (b), "sure", GINT_TO_POINTER (1));
		gtk_button_set_label (GTK_BUTTON (b), _("Remove, sure?"));
		gtk_widget_add_css_class (b, "destructive-action");
		return;
	}
	if (!is_server (serv) || !bouncer_del_network (serv, id, &err))
	{
		say (w, "no", err ? err : _("Not connected to the bouncer any more."));
		g_free (err);
		return;
	}
	say (w, "ok", _("Removed from the bouncer: it leaves that network."));
}

/* "Add a network": a name and an address, or one of the usual ones */
typedef struct
{
	bncwin *w;
	server *serv;
	GtkWidget *win, *name, *addr, *note;
} adder;

static const char *const usual[][2] = {
	{ "Libera.Chat", "irc.libera.chat" },
	{ "OFTC", "irc.oftc.net" },
	{ "EFnet", "irc.efnet.org" },
	{ "IRCnet", "open.ircnet.net" },
	{ "Rizon", "irc.rizon.net" },
	{ "hackint", "irc.hackint.org" },
};

static void
adder_pick (GtkWidget *b, gpointer data)
{
	adder *a = data;
	int i = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "i"));
	nx_entry_set_text (a->name, usual[i][0]);
	nx_entry_set_text (a->addr, usual[i][1]);
}

static void
adder_add (GtkWidget *b, gpointer data)
{
	adder *a = data;
	const char *name = nx_entry_get_text (a->name);
	char *addr = g_strstrip (g_strdup (nx_entry_get_text (a->addr))), *host = addr, *colon, *err = NULL;
	gboolean tls = TRUE;
	int port = 6697;

	/* "host", "host:port", "host:+port", "irc://host" (plain), "ircs://host" */
	if (g_str_has_prefix (host, "ircs://"))
		host += 7;
	else if (g_str_has_prefix (host, "irc://"))
	{
		host += 6;
		tls = FALSE;
		port = 6667;
	}
	if ((colon = strrchr (host, ':')))
	{
		*colon = 0;
		if (colon[1] == '+')
			tls = TRUE, colon++;
		port = atoi (colon + 1);
		if (port == 6667 && colon[0] != '+')
			tls = FALSE;
	}
	if (strchr (host, '/'))
		*strchr (host, '/') = 0;
	if (!*host)
	{
		gtk_label_set_text (GTK_LABEL (a->note), _("Write the network's address, like irc.libera.chat."));
		g_free (addr);
		return;
	}
	if (!is_server (a->serv) || !bouncer_add_network (a->serv, *name ? name : host, host, port, tls,
															  NULL, &err))
	{
		gtk_label_set_text (GTK_LABEL (a->note), err ? err : _("Not connected to the bouncer any more."));
		g_free (err);
		g_free (addr);
		return;
	}
	say (a->w, "ok", _("Added: the bouncer is connecting to it, and its tab opens here when it's in."));
	g_free (addr);
	gtk_window_destroy (GTK_WINDOW (a->win));
}

static void
rn_add (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	adder *a = g_new0 (adder, 1);
	GtkWidget *vbox, *grid, *flow, *bb, *add, *cancel;
	guint i;

	a->w = w;
	a->serv = g_object_get_data (G_OBJECT (b), "serv");
	a->win = nx_window_new ();
	gtk_window_set_title (GTK_WINDOW (a->win), _("Add a network to the bouncer"));
	gtk_window_set_transient_for (GTK_WINDOW (a->win), GTK_WINDOW (w->win));
	gtk_window_set_modal (GTK_WINDOW (a->win), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (a->win), 420, -1);
	g_object_set_data_full (G_OBJECT (a->win), "adder", a, g_free);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	nx_set_border_width (vbox, 12);
	gtk_window_set_child (GTK_WINDOW (a->win), vbox);
	gtk_box_append (GTK_BOX (vbox), label (_("<b>Which network should it stay on?</b>"), FALSE));
	flow = gtk_flow_box_new ();
	gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow), GTK_SELECTION_NONE);
	gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 3);
	for (i = 0; i < G_N_ELEMENTS (usual); i++)
	{
		GtkWidget *p = gtk_button_new_with_label (usual[i][0]);
		g_object_set_data (G_OBJECT (p), "i", GINT_TO_POINTER (i));
		g_signal_connect (p, "clicked", G_CALLBACK (adder_pick), a);
		gtk_flow_box_append (GTK_FLOW_BOX (flow), p);
	}
	gtk_box_append (GTK_BOX (vbox), flow);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	a->name = erow (grid, 0, _("_Name:"), gtk_entry_new ());
	a->addr = erow (grid, 1, _("_Address:"), gtk_entry_new ());
	gtk_entry_set_placeholder_text (GTK_ENTRY (a->name), "Libera.Chat");
	gtk_entry_set_placeholder_text (GTK_ENTRY (a->addr), "irc.libera.chat");
	gtk_box_append (GTK_BOX (vbox), grid);
	gtk_box_append (GTK_BOX (vbox), label (_("Encrypted (TLS, port 6697) unless you write irc:// in front or "
															 "another port. Your nick there is your nick on the bouncer."), TRUE));
	a->note = label ("", FALSE);
	gtk_box_append (GTK_BOX (vbox), a->note);
	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (bb, GTK_ALIGN_END);
	cancel = gtk_button_new_with_mnemonic (_("Ca_ncel"));
	add = gtk_button_new_with_mnemonic (_("A_dd"));
	gtk_widget_add_css_class (add, "suggested-action");
	gtk_box_append (GTK_BOX (bb), cancel);
	gtk_box_append (GTK_BOX (bb), add);
	gtk_box_append (GTK_BOX (vbox), bb);
	gtk_window_set_default_widget (GTK_WINDOW (a->win), add);
	g_signal_connect (add, "clicked", G_CALLBACK (adder_add), a);
	g_signal_connect_swapped (cancel, "clicked", G_CALLBACK (gtk_window_destroy), a->win);
	gtk_window_present (GTK_WINDOW (a->win));
	gtk_widget_grab_focus (a->name);
}

/* the soju/ZNC connection of a bouncer entry, logged in; or NULL */
static server *
rb_live (ircnet *net)
{
	server *sv = bouncer_server_of (net);
	return sv && sv->connected && sv->end_of_motd ? sv : NULL;
}

static GtkWidget *
small_button (const char *text, const char *tip)
{
	GtkWidget *b = gtk_button_new_with_mnemonic (text);
	gtk_widget_set_valign (b, GTK_ALIGN_CENTER);
	if (tip)
		gtk_widget_set_tooltip_text (b, tip);
	return b;
}

/* one bouncer: its card */
static void
fill_one_remote (bncwin *w, ircnet *net)
{
	ircserver *isv = net->servlist->data;
	server *sv = bouncer_server_of (net), *live = rb_live (net);
	const char *err = bouncer_last_error (net);
	GPtrArray *nets = live ? bouncer_networks (live) : NULL;
	bnc_kind kind = live ? bouncer_kind (live) : BNC_NONE;
	GtkWidget *card, *top, *box, *btns, *b;
	const char *colour, *what;
	char *m, *where;

	card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_add_css_class (card, "card");
	gtk_widget_add_css_class (card, "nx-bnc-card");

	/* name, where, how it's doing */
	top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_hexpand (box, TRUE);
	/* "host/+port", "host/port" or just "host" */
	{
		const char *dflt = (net->flags & FLAG_USE_SSL) ? "6697" : "6667";
		char **hp = g_strsplit (isv->hostname, "/", 2);
		where = g_strdup_printf ("%s%s:%s", (net->flags & FLAG_USE_SSL) ? "" : "irc://", hp[0],
										 !hp[1] ? dflt : hp[1][0] == '+' ? hp[1] + 1 : hp[1]);
		g_strfreev (hp);
	}
	m = g_markup_printf_escaped ("<b>%s</b>  <span alpha=\"60%%\">%s, as %s</span>", net->name, where,
										  net->user ? net->user : "?");
	g_free (where);
	gtk_box_append (GTK_BOX (box), label (m, FALSE));
	g_free (m);

	if (live)
	{
		colour = "#2e7d32";
		what = kind == BNC_ZNC ? _("Connected (ZNC)") : _("Connected (soju)");
	}
	else if (sv)
	{
		colour = "#f9a825";
		what = _("Connecting...");
	}
	else if (err)
	{
		colour = "#c62828";
		what = _("Couldn't log in");
	}
	else
	{
		colour = "#9e9e9e";
		what = _("Not connected");
	}
	m = g_markup_printf_escaped ("<span foreground=\"%s\">\xe2\x97\x8f</span> %s%s%s", colour, what,
										  !live && err ? ": " : "", !live && err ? err : "");
	gtk_box_append (GTK_BOX (box), label (m, TRUE));
	g_free (m);
	gtk_box_append (GTK_BOX (top), box);

	btns = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	b = small_button (sv ? _("_Disconnect") : _("_Connect"), NULL);
	if (!sv)
		gtk_widget_add_css_class (b, "suggested-action");
	g_object_set_data (G_OBJECT (b), "net", net);
	g_signal_connect (b, "clicked", G_CALLBACK (rb_connect), w);
	gtk_box_append (GTK_BOX (btns), b);
	b = small_button (_("_Edit..."), _("Its address, user name and password"));
	g_object_set_data (G_OBJECT (b), "net", net);
	g_signal_connect (b, "clicked", G_CALLBACK (rb_edit), w);
	gtk_box_append (GTK_BOX (btns), b);
	b = small_button (_("_Forget"), _("Take it out of nexchat. Nothing changes on the bouncer."));
	g_object_set_data (G_OBJECT (b), "net", net);
	g_signal_connect (b, "clicked", G_CALLBACK (rb_forget), w);
	gtk_box_append (GTK_BOX (btns), b);
	gtk_box_append (GTK_BOX (top), btns);
	gtk_box_append (GTK_BOX (card), top);

	/* what to do next, when it isn't in */
	if (!live)
	{
		const char *hint = sv ? _("Logging in. Its networks show here once it's in.")
			: err && strstr (err, _("password")) ? _("Check the user name and password with Edit, then Connect again.")
			: err ? _("Fix it with Edit, then Connect again.")
			: _("Connect to see the networks it keeps you on, and to add more.");
		GtkWidget *h = label (hint, TRUE);
		gtk_widget_set_margin_start (h, 14);
		gtk_box_append (GTK_BOX (card), h);
		gtk_box_append (GTK_BOX (w->list), card);
		return;
	}

	/* its networks */
	if (!nets || !nets->len)
	{
		GtkWidget *h = label (_("It isn't on any network yet. Add one and it stays there for you, "
										"even while nexchat is closed."), TRUE);
		gtk_widget_set_margin_start (h, 14);
		gtk_box_append (GTK_BOX (card), h);
	}
	else
	{
		guint i;
		for (i = 0; i < nets->len; i++)
		{
			bnc_network *n = nets->pdata[i];
			GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
			GtkWidget *nb = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
			gboolean up = n->state && !strcmp (n->state, "connected");
			gboolean going = n->state && !strcmp (n->state, "connecting");
			const char *nm = n->name ? n->name : n->id;

			gtk_widget_set_hexpand (nb, TRUE);
			m = g_markup_printf_escaped ("<b>%s</b>  <span alpha=\"60%%\">%s</span>", nm, n->host ? n->host : "");
			gtk_box_append (GTK_BOX (nb), label (m, FALSE));
			g_free (m);
			m = g_markup_printf_escaped (_("<span foreground=\"%s\">\xe2\x97\x8f</span> %s as <b>%s</b>%s%s"),
												  up ? "#2e7d32" : going ? "#f9a825" : "#c62828",
												  up ? _("online") : going ? _("connecting") : _("offline"),
												  n->nick ? n->nick : "?", n->error ? ": " : "", n->error ? n->error : "");
			gtk_box_append (GTK_BOX (nb), label (m, TRUE));
			g_free (m);
			gtk_box_append (GTK_BOX (row), nb);

			if (kind == BNC_SOJU)
			{
				b = small_button (_("Open"), _("Its tab, with its channels"));
				g_object_set_data (G_OBJECT (b), "base", net);
				g_object_set_data_full (G_OBJECT (b), "name", g_strdup (nm), g_free);
				g_signal_connect (b, "clicked", G_CALLBACK (rn_open), w);
				gtk_box_append (GTK_BOX (row), b);
				b = small_button (_("Nick and login..."), NULL);
				g_object_set_data (G_OBJECT (b), "serv", live);
				g_object_set_data_full (G_OBJECT (b), "id", g_strdup (n->id), g_free);
				g_signal_connect (b, "clicked", G_CALLBACK (redit_cb), w);
				gtk_box_append (GTK_BOX (row), b);
			}
			b = small_button (_("Remove"), _("The bouncer leaves this network"));
			g_object_set_data (G_OBJECT (b), "serv", live);
			g_object_set_data_full (G_OBJECT (b), "id", g_strdup (n->id), g_free);
			g_signal_connect (b, "clicked", G_CALLBACK (rn_remove), w);
			gtk_box_append (GTK_BOX (row), b);
			gtk_widget_set_margin_start (row, 14);
			gtk_box_append (GTK_BOX (card), row);
		}
	}
	b = gtk_button_new_with_mnemonic (_("_Add a network..."));
	gtk_widget_set_halign (b, GTK_ALIGN_START);
	gtk_widget_set_margin_start (b, 14);
	g_object_set_data (G_OBJECT (b), "serv", live);
	g_signal_connect (b, "clicked", G_CALLBACK (rn_add), w);
	gtk_box_append (GTK_BOX (card), b);
	gtk_box_append (GTK_BOX (w->list), card);
}

/* every bouncer elsewhere you use */
static int
fill_remote (bncwin *w)
{
	GSList *nets = bouncer_entries (), *l;
	int shown = 0;

	for (l = nets; l; l = l->next, shown++)
		fill_one_remote (w, l->data);
	g_slist_free (nets);
	return shown;
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
	w->remote = fill_remote (w);
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
	if (!w->rows->len && !w->remote)
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
	fill_list (w);
	update_head (w);
	update_states (w);
}

/* what the remote part of the list shows, to notice changes */
static char *
remote_key (void)
{
	GString *k = g_string_new (NULL);
	GSList *ents = bouncer_entries (), *l;

	for (l = ents; l; l = l->next)
	{
		ircnet *net = l->data;
		server *sv = bouncer_server_of (net), *live = rb_live (net);
		GPtrArray *nets = live ? bouncer_networks (live) : NULL;
		guint i;
		g_string_append_printf (k, "%p:%p:%d:%s:", net, sv, live != NULL, bouncer_last_error (net) ? bouncer_last_error (net) : "");
		for (i = 0; nets && i < nets->len; i++)
		{
			bnc_network *n = nets->pdata[i];
			g_string_append_printf (k, "%s/%s/%s/%s/%s;", n->id, n->name, n->nick, n->state, n->error);
		}
	}
	g_slist_free (ents);
	return g_string_free (k, FALSE);
}

static gboolean
tick (gpointer data)
{
	bncwin *w = data;
	char *k = remote_key ();

	if (g_strcmp0 (k, w->remote_key))
	{
		g_free (w->remote_key);
		w->remote_key = k;
		if (!w->busy)
			refresh (w);
	}
	else
		g_free (k);
	if (!w->busy)
	{
		update_head (w);
		update_states (w);
	}
	return G_SOURCE_CONTINUE;
}

static guint poke_tag;

static gboolean
poke_now (gpointer data)
{
	char *k;

	poke_tag = 0;
	if (!open_win || open_win->busy)
		return G_SOURCE_REMOVE;
	k = remote_key ();
	if (g_strcmp0 (k, open_win->remote_key))
	{
		g_free (open_win->remote_key);
		open_win->remote_key = k;
		refresh (open_win);
	}
	else
		g_free (k);
	return G_SOURCE_REMOVE;
}

void
bncwin_poke (void)
{
	if (open_win && !poke_tag)
		poke_tag = g_idle_add (poke_now, NULL);
}

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
	if (w->ch_nets)
		g_ptr_array_unref (w->ch_nets);
	g_free (w->status);
	g_free (w->remote_key);
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
	/* tests: "network\tnick\trealname\taccount\tpassword" in a file: the
	   editor of that network on a soju elsewhere is filled in and saved */
	{
		const char *rf = g_getenv ("NEXCHAT_BNCWIN_REDIT");
		char *line = NULL;
		if (rf && g_file_get_contents (rf, &line, NULL, NULL))
		{
			char **f = g_strsplit (g_strstrip (line), "\t", 5);
			GSList *servs = bouncer_soju_servers (), *l;
			g_unlink (rf);
			for (l = servs; l && g_strv_length (f) == 5; l = l->next)
			{
				GPtrArray *nets = bouncer_networks (l->data);
				for (i = 0; nets && i < nets->len; i++)
				{
					bnc_network *n = nets->pdata[i];
					reditor *e;
					GtkWidget *win;
					if (g_strcmp0 (n->name, f[0]))
						continue;
					redit_open (w, l->data, n);
					win = GTK_WIDGET (gtk_window_get_transient_for (GTK_WINDOW (w->win)));
					{
						GListModel *tl = gtk_window_get_toplevels ();
						guint j;
						for (j = 0; j < g_list_model_get_n_items (tl); j++)
						{
							win = g_list_model_get_item (tl, j);
							e = g_object_get_data (G_OBJECT (win), "reditor");
							g_object_unref (win);
							if (!e)
								continue;
							nx_entry_set_text (e->nick, f[1]);
							nx_entry_set_text (e->realname, f[2]);
							gtk_check_button_set_active (GTK_CHECK_BUTTON (e->sasl), *f[3] != 0);
							nx_entry_set_text (e->account, f[3]);
							nx_entry_set_text (e->apass, f[4]);
							reditor_save (NULL, e);
							break;
						}
					}
				}
			}
			g_slist_free (servs);
			g_strfreev (f);
			g_free (line);
		}
	}
	/* tests: a file with a tab number: that tab is shown, once */
	{
		const char *tf = g_getenv ("NEXCHAT_BNCWIN_TAB");
		char *t = NULL;
		if (tf && g_file_get_contents (tf, &t, NULL, NULL))
		{
			g_unlink (tf);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (w->tabs), atoi (t));
			g_free (t);
		}
	}
	s = g_string_new (gtk_label_get_text (GTK_LABEL (w->head)));
	switch (gtk_notebook_get_current_page (GTK_NOTEBOOK (w->tabs)))
	{
	case 1:
		{
			GtkWidget *row, *c;
			g_string_append (s, " | channels:");
			for (row = gtk_widget_get_first_child (w->ch_list); row; row = gtk_widget_get_next_sibling (row))
				for (c = gtk_widget_get_first_child (row); c; c = gtk_widget_get_next_sibling (c))
					if (GTK_IS_LABEL (c))
						g_string_append_printf (s, " %s", gtk_label_get_text (GTK_LABEL (c)));
					else if (GTK_IS_CHECK_BUTTON (c) && gtk_check_button_get_active (GTK_CHECK_BUTTON (c)))
						g_string_append (s, " [detached]");
			g_string_append_printf (s, " | note: %s", gtk_label_get_text (GTK_LABEL (w->note)));
			return g_string_free (s, FALSE);
		}
	case 2:
		g_string_append_printf (s, " | access: %s | remote %s: %s | note: %s",
										gtk_label_get_text (GTK_LABEL (w->ac_where)),
										gtk_check_button_get_active (GTK_CHECK_BUTTON (w->ac_remote)) ? "on" : "off",
										gtk_label_get_text (GTK_LABEL (w->ac_remote_info)),
										gtk_label_get_text (GTK_LABEL (w->note)));
		return g_string_free (s, FALSE);
	case 3:
		g_string_append_printf (s, " | storage: %s, title \"%s\" | note: %s",
										gtk_check_button_get_active (GTK_CHECK_BUTTON (w->st_files)) ? "files" : "db",
										nx_entry_get_text (w->st_title), gtk_label_get_text (GTK_LABEL (w->note)));
		return g_string_free (s, FALSE);
	}
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
	{
		GSList *servs = bouncer_soju_servers (), *l;
		for (l = servs; l; l = l->next)
		{
			GPtrArray *nets = bouncer_networks (l->data);
			for (i = 0; nets && i < nets->len; i++)
			{
				bnc_network *n = nets->pdata[i];
				g_string_append_printf (s, " | remote %s: %s as %s", n->name, n->state, n->nick);
			}
		}
		g_slist_free (servs);
	}
	return g_string_free (s, FALSE);
}

/* ---- Channels ---- */

static void ch_fill (bncwin *w);

static ircnet *
ch_current (bncwin *w)
{
	guint i = gtk_drop_down_get_selected (GTK_DROP_DOWN (w->ch_net));
	return w->ch_nets && i < w->ch_nets->len ? w->ch_nets->pdata[i] : NULL;
}

static void
ch_done (bncwin *w, gboolean ok, char *err)
{
	gtk_label_set_text (GTK_LABEL (w->note), ok ? "" : err ? err : "?");
	g_free (err);
	ch_fill (w);
}

static void
ch_detached (GtkCheckButton *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;
	gboolean ok;

	if (w->filling || !ch_current (w))
		return;
	ok = mybouncer_channel_set (ch_current (w), g_object_get_data (G_OBJECT (b), "chan"), "detached",
										 gtk_check_button_get_active (b) ? "true" : "false", &err);
	ch_done (w, ok, err);
}

static void
ch_remove (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;
	gboolean ok;

	if (!ch_current (w))
		return;
	ok = mybouncer_channel_del (ch_current (w), g_object_get_data (G_OBJECT (b), "chan"), &err);
	ch_done (w, ok, err);
}

static void
ch_add (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	const char *c = nx_entry_get_text (w->ch_entry);
	char *err = NULL, *name;
	gboolean ok;

	if (!*c || !ch_current (w))
		return;
	name = strchr ("#&!+", *c) ? g_strdup (c) : g_strdup_printf ("#%s", c);
	ok = mybouncer_channel_add (ch_current (w), name, &err);
	g_free (name);
	if (ok)
		nx_entry_set_text (w->ch_entry, "");
	ch_done (w, ok, err);
}

static void
ch_fill (bncwin *w)
{
	ircnet *net = ch_current (w);
	GtkWidget *c;
	char *all, **lines;
	int i, n = 0;

	while ((c = gtk_widget_get_first_child (w->ch_list)))
		gtk_box_remove (GTK_BOX (w->ch_list), c);
	if (!net)
	{
		gtk_box_append (GTK_BOX (w->ch_list), label (_("No networks on the bouncer yet."), TRUE));
		return;
	}
	w->filling = TRUE;
	all = mybouncer_channels (net);
	lines = g_strsplit (all, "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		char *tab = strchr (lines[i], '\t');
		GtkWidget *row, *name, *st, *det, *rm;
		char *chan;

		if (!tab)
			continue;
		chan = g_strndup (lines[i], tab - lines[i]);
		row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
		name = gtk_label_new (chan);
		gtk_label_set_xalign (GTK_LABEL (name), 0);
		gtk_widget_set_hexpand (name, TRUE);
		st = label (tab + 1, TRUE);
		gtk_label_set_text (GTK_LABEL (st), tab + 1);
		det = gtk_check_button_new_with_label (_("Detached"));
		gtk_widget_set_tooltip_text (det, _("It stays in the channel but doesn't show it to you "
														 "until you /join it again"));
		gtk_check_button_set_active (GTK_CHECK_BUTTON (det), strstr (tab + 1, "detached") != NULL);
		rm = gtk_button_new_with_label (_("Leave"));
		gtk_widget_set_tooltip_text (rm, _("The bouncer parts it and stops rejoining it"));
		g_object_set_data_full (G_OBJECT (det), "chan", g_strdup (chan), g_free);
		g_object_set_data_full (G_OBJECT (rm), "chan", chan, g_free);
		g_signal_connect (det, "toggled", G_CALLBACK (ch_detached), w);
		g_signal_connect (rm, "clicked", G_CALLBACK (ch_remove), w);
		gtk_box_append (GTK_BOX (row), name);
		gtk_box_append (GTK_BOX (row), st);
		gtk_box_append (GTK_BOX (row), det);
		gtk_box_append (GTK_BOX (row), rm);
		gtk_box_append (GTK_BOX (w->ch_list), row);
		n++;
	}
	if (!n)
		gtk_box_append (GTK_BOX (w->ch_list), label (_("It isn't in any channel on this network."), TRUE));
	g_strfreev (lines);
	g_free (all);
	w->filling = FALSE;
}

/* the dropdown: networks on the bouncer */
static void
ch_nets_fill (bncwin *w)
{
	ircnet *was = ch_current (w);
	GPtrArray *names = g_ptr_array_new ();
	GtkStringList *model;
	GSList *l;
	guint sel = 0;

	if (w->ch_nets)
		g_ptr_array_set_size (w->ch_nets, 0);
	else
		w->ch_nets = g_ptr_array_new ();
	for (l = network_list; l; l = l->next)
		if (mybouncer_owns (l->data))
		{
			if (l->data == was || (!was && l->data == w->from))
				sel = w->ch_nets->len;
			g_ptr_array_add (w->ch_nets, l->data);
			g_ptr_array_add (names, ((ircnet *) l->data)->name);
		}
	g_ptr_array_add (names, NULL);
	w->filling = TRUE;
	model = gtk_string_list_new ((const char *const *) names->pdata);
	gtk_drop_down_set_model (GTK_DROP_DOWN (w->ch_net), G_LIST_MODEL (model));
	g_object_unref (model);
	gtk_drop_down_set_selected (GTK_DROP_DOWN (w->ch_net), sel);
	w->filling = FALSE;
	g_ptr_array_free (names, TRUE);
	ch_fill (w);
}

static void
ch_net_changed (GObject *o, GParamSpec *ps, gpointer data)
{
	bncwin *w = data;
	if (!w->filling)
		ch_fill (w);
}

static GtkWidget *
channels_page (bncwin *w)
{
	GtkWidget *v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8), *row, *sw, *add;

	nx_set_border_width (v, 8);
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append (GTK_BOX (row), gtk_label_new_with_mnemonic (_("_Network:")));
	w->ch_net = gtk_drop_down_new (NULL, NULL);
	gtk_box_append (GTK_BOX (row), w->ch_net);
	gtk_box_append (GTK_BOX (v), row);
	g_signal_connect (w->ch_net, "notify::selected", G_CALLBACK (ch_net_changed), w);
	gtk_box_append (GTK_BOX (v), label (_("The channels the bouncer stays in, with nexchat open or not."), TRUE));

	w->ch_list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	nx_set_border_width (w->ch_list, 4);
	sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), w->ch_list);
	gtk_box_append (GTK_BOX (v), sw);

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	w->ch_entry = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (w->ch_entry), "#channel");
	gtk_widget_set_hexpand (w->ch_entry, TRUE);
	add = gtk_button_new_with_mnemonic (_("_Join"));
	gtk_box_append (GTK_BOX (row), w->ch_entry);
	gtk_box_append (GTK_BOX (row), add);
	gtk_box_append (GTK_BOX (v), row);
	g_signal_connect (add, "clicked", G_CALLBACK (ch_add), w);
	g_signal_connect (w->ch_entry, "activate", G_CALLBACK (ch_add), w);
	return v;
}

/* ---- Access ---- */

static void
ac_fill (bncwin *w)
{
	char *pw = mybouncer_password (), *t, *fp;

	w->filling = TRUE;
	t = g_markup_printf_escaped (_("On this computer: <b>127.0.0.1</b> port <b>%d</b>, no TLS\n"
											 "User name: <b>%s/</b><i>network</i>  (e.g. %s/Libera)"),
										 mybouncer_port (), mybouncer_user (), mybouncer_user ());
	gtk_label_set_markup (GTK_LABEL (w->ac_where), t);
	g_free (t);
	nx_entry_set_text (w->ac_pass, pw ? g_strstrip (pw) : "");
	g_free (pw);

	gtk_check_button_set_active (GTK_CHECK_BUTTON (w->ac_remote), mybouncer_remote ());
	if (mybouncer_remote ())
	{
		fp = mybouncer_remote_certfp ();
		t = g_markup_printf_escaped (_("From other devices: this computer's address, port <b>%d</b>, TLS.\n"
												 "Its certificate is self-signed; check it's this one when your app asks:\n%s"),
											 mybouncer_remote_port (), fp ? fp : _("(not made yet)"));
		g_free (fp);
	}
	else
		t = g_strdup (_("Only nexchat and other apps on this computer can reach it."));
	gtk_label_set_markup (GTK_LABEL (w->ac_remote_info), t);
	g_free (t);

	gtk_check_button_set_active (GTK_CHECK_BUTTON (w->ac_systemd), !strcmp (mybouncer_kept_by (), "systemd"));
	gtk_widget_set_sensitive (w->ac_systemd, mybouncer_can_systemd ());
	if (!mybouncer_can_systemd ())
		gtk_widget_set_tooltip_text (w->ac_systemd, _("This computer doesn't run systemd for you"));
	w->filling = FALSE;
}

static void
ac_result (bncwin *w, gboolean ok, char *err, const char *done)
{
	gtk_label_set_text (GTK_LABEL (w->note), ok ? done : err ? err : "?");
	g_free (err);
	ac_fill (w);
	refresh (w);
}

static void
ac_pass_set (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	const char *pw = nx_entry_get_text (w->ac_pass);
	char *err = NULL;

	if (strlen (pw) < 8)
	{
		gtk_label_set_text (GTK_LABEL (w->note), _("Make it at least 8 characters."));
		return;
	}
	ac_result (w, mybouncer_set_password (pw, &err), err, _("Password changed."));
}

static void
ac_remote_toggled (GtkCheckButton *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;

	if (w->filling)
		return;
	ac_result (w, mybouncer_set_remote (gtk_check_button_get_active (b), &err), err,
				  gtk_check_button_get_active (b) ? _("Reachable from other devices now.")
														: _("Only reachable from this computer now."));
}

static void
ac_systemd_toggled (GtkCheckButton *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;

	if (w->filling)
		return;
	ac_result (w, mybouncer_set_kept_by_systemd (gtk_check_button_get_active (b), &err), err, "");
}

static void
ac_show_toggled (GtkCheckButton *b, gpointer data)
{
	gtk_entry_set_visibility (GTK_ENTRY (data), gtk_check_button_get_active (b));
}

static GtkWidget *
access_page (bncwin *w)
{
	GtkWidget *v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8), *row, *set, *show;

	nx_set_border_width (v, 8);
	gtk_box_append (GTK_BOX (v), label (_("<b>Connecting to it</b>"), FALSE));
	w->ac_where = label ("", TRUE);
	gtk_label_set_selectable (GTK_LABEL (w->ac_where), TRUE);
	gtk_box_append (GTK_BOX (v), w->ac_where);

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append (GTK_BOX (row), gtk_label_new_with_mnemonic (_("_Password:")));
	w->ac_pass = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (w->ac_pass), FALSE);
	gtk_widget_set_hexpand (w->ac_pass, TRUE);
	show = gtk_check_button_new_with_mnemonic (_("S_how"));
	set = gtk_button_new_with_mnemonic (_("C_hange"));
	gtk_box_append (GTK_BOX (row), w->ac_pass);
	gtk_box_append (GTK_BOX (row), show);
	gtk_box_append (GTK_BOX (row), set);
	gtk_box_append (GTK_BOX (v), row);
	g_signal_connect (show, "toggled", G_CALLBACK (ac_show_toggled), w->ac_pass);
	g_signal_connect (set, "clicked", G_CALLBACK (ac_pass_set), w);
	gtk_box_append (GTK_BOX (v), label (_("nexchat uses it for you; you need it to log in from another app."), TRUE));

	gtk_box_append (GTK_BOX (v), label (_("<b>Other devices</b>"), FALSE));
	w->ac_remote = gtk_check_button_new_with_mnemonic (_("Let my _phone and other computers connect to it"));
	gtk_box_append (GTK_BOX (v), w->ac_remote);
	w->ac_remote_info = label ("", TRUE);
	gtk_label_set_selectable (GTK_LABEL (w->ac_remote_info), TRUE);
	gtk_box_append (GTK_BOX (v), w->ac_remote_info);
	g_signal_connect (w->ac_remote, "toggled", G_CALLBACK (ac_remote_toggled), w);

	gtk_box_append (GTK_BOX (v), label (_("<b>Keeping it running</b>"), FALSE));
	w->ac_systemd = gtk_check_button_new_with_mnemonic (_("Start it with the _computer (systemd keeps it running)"));
	gtk_box_append (GTK_BOX (v), w->ac_systemd);
	g_signal_connect (w->ac_systemd, "toggled", G_CALLBACK (ac_systemd_toggled), w);
	gtk_box_append (GTK_BOX (v), label (_("Off: nexchat starts it when you open nexchat, and it keeps "
														"running after you close nexchat until the computer restarts."), TRUE));
	return v;
}

/* ---- Storage ---- */

static void
st_log_fill (bncwin *w)
{
	char *t = mybouncer_log_tail (200);
	GtkTextBuffer *b = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w->st_log));
	GtkTextIter end;

	gtk_text_buffer_set_text (b, t ? t : "", -1);
	gtk_text_buffer_get_end_iter (b, &end);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (w->st_log), &end, 0, FALSE, 0, 0);
	g_free (t);
}

static void
st_fill (bncwin *w)
{
	char *t = mybouncer_title ();

	w->filling = TRUE;
	gtk_check_button_set_active (GTK_CHECK_BUTTON (mybouncer_logs_as_files () ? w->st_files : w->st_db), TRUE);
	nx_entry_set_text (w->st_title, t ? g_strstrip (t) : "");
	g_free (t);
	w->filling = FALSE;
	st_log_fill (w);
}

static void
st_logs_toggled (GtkCheckButton *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;
	gboolean ok;

	if (w->filling || !gtk_check_button_get_active (b))
		return;
	ok = mybouncer_set_logs_as_files (GTK_WIDGET (b) == w->st_files, &err);
	gtk_label_set_text (GTK_LABEL (w->note), ok ? _("Changed; the bouncer restarted to use it.") : err ? err : "?");
	g_free (err);
	st_fill (w);
	refresh (w);
}

static void
st_title_set (GtkWidget *b, gpointer data)
{
	bncwin *w = data;
	char *err = NULL;
	gboolean ok = mybouncer_set_title (nx_entry_get_text (w->st_title), &err);

	gtk_label_set_text (GTK_LABEL (w->note), ok ? _("Name saved.") : err ? err : "?");
	g_free (err);
	st_fill (w);
	refresh (w);
}

static void
st_log_refresh (GtkWidget *b, gpointer data)
{
	st_log_fill (data);
}

static GtkWidget *
storage_page (bncwin *w)
{
	GtkWidget *v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8), *row, *set, *sw, *again;

	nx_set_border_width (v, 8);
	gtk_box_append (GTK_BOX (v), label (_("<b>History</b>"), FALSE));
	w->st_db = gtk_check_button_new_with_mnemonic (_("In the bouncer's _database (search and missed messages work)"));
	w->st_files = gtk_check_button_new_with_mnemonic (_("As plain _text files, one per channel and day"));
	gtk_check_button_set_group (GTK_CHECK_BUTTON (w->st_files), GTK_CHECK_BUTTON (w->st_db));
	gtk_box_append (GTK_BOX (v), w->st_db);
	gtk_box_append (GTK_BOX (v), w->st_files);
	g_signal_connect (w->st_db, "toggled", G_CALLBACK (st_logs_toggled), w);
	g_signal_connect (w->st_files, "toggled", G_CALLBACK (st_logs_toggled), w);

	gtk_box_append (GTK_BOX (v), label (_("<b>Its name</b>"), FALSE));
	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	w->st_title = gtk_entry_new ();
	gtk_entry_set_placeholder_text (GTK_ENTRY (w->st_title), _("shown to apps that connect, e.g. in their network list"));
	gtk_widget_set_hexpand (w->st_title, TRUE);
	set = gtk_button_new_with_mnemonic (_("Sa_ve"));
	gtk_box_append (GTK_BOX (row), w->st_title);
	gtk_box_append (GTK_BOX (row), set);
	gtk_box_append (GTK_BOX (v), row);
	g_signal_connect (set, "clicked", G_CALLBACK (st_title_set), w);
	g_signal_connect (w->st_title, "activate", G_CALLBACK (st_title_set), w);

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append (GTK_BOX (row), label (_("<b>Its log</b>"), FALSE));
	gtk_widget_set_hexpand (gtk_widget_get_first_child (row), TRUE);
	again = gtk_button_new_with_mnemonic (_("Re_load"));
	gtk_box_append (GTK_BOX (row), again);
	gtk_box_append (GTK_BOX (v), row);
	g_signal_connect (again, "clicked", G_CALLBACK (st_log_refresh), w);
	w->st_log = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (w->st_log), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (w->st_log), TRUE);
	sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), w->st_log);
	gtk_box_append (GTK_BOX (v), sw);
	return v;
}

static void
page_switched (GtkNotebook *nb, GtkWidget *page, guint n, gpointer data)
{
	bncwin *w = data;

	if (mybouncer_state () != MYBNC_SET_UP)
		return;
	if (n == 1)
		ch_nets_fill (w);
	else if (n == 2)
		ac_fill (w);
	else if (n == 3)
		st_fill (w);
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
	w->serial = ++win_serial;
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
	w->tabs = gtk_notebook_new ();
	gtk_widget_set_vexpand (w->tabs, TRUE);
	gtk_notebook_append_page (GTK_NOTEBOOK (w->tabs), sw, gtk_label_new_with_mnemonic (_("Net_works")));
	gtk_notebook_append_page (GTK_NOTEBOOK (w->tabs), channels_page (w), gtk_label_new_with_mnemonic (_("C_hannels")));
	gtk_notebook_append_page (GTK_NOTEBOOK (w->tabs), access_page (w), gtk_label_new_with_mnemonic (_("_Access")));
	gtk_notebook_append_page (GTK_NOTEBOOK (w->tabs), storage_page (w), gtk_label_new_with_mnemonic (_("St_orage")));
	g_signal_connect (w->tabs, "switch-page", G_CALLBACK (page_switched), w);
	gtk_box_append (GTK_BOX (vbox), w->tabs);

	w->note = label ("", FALSE);
	gtk_label_set_selectable (GTK_LABEL (w->note), TRUE);
	gtk_box_append (GTK_BOX (vbox), w->note);

	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	w->other = other = gtk_button_new_with_mnemonic (_("Use _another bouncer..."));
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
