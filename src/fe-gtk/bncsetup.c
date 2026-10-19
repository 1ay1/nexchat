/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include <stdlib.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/servlist.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "gtkutil.h"
#include "gtkcompat.h"
#include "bncsetup.h"

/* ---- the login probe: one connection, CAP LS, PASS/NICK/USER, wait ---- */

typedef struct
{
	GSocketConnection *conn;
	GDataInputStream *in;
	GCancellable *cancel;
	char *user, *pass;
	gboolean znc, soju, persist;
	GTlsCertificateFlags cert_errors;	/* what's wrong with its certificate */
	bnc_probe_cb cb;
	gpointer data;
	guint timeout;
	gboolean done;
} probe;

void
bnc_probe_result_free (bnc_probe_result *r)
{
	if (!r)
		return;
	g_free (r->kind);
	g_free (r->error);
	g_free (r->networks);
	g_free (r->cert_why);
	g_free (r);
}

static void
probe_finish (probe *p, const char *kind, const char *error)
{
	bnc_probe_result *r;

	if (p->done)
		return;
	p->done = TRUE;
	if (p->timeout)
		g_source_remove (p->timeout);
	g_cancellable_cancel (p->cancel);
	r = g_new0 (bnc_probe_result, 1);
	r->kind = g_strdup (kind);
	r->error = g_strdup (error);
	r->cert_untrusted = p->cert_errors != 0;
	if (p->cert_errors)
		r->cert_why = g_strdup ((p->cert_errors & G_TLS_CERTIFICATE_UNKNOWN_CA) ?
			_("its certificate is self-signed or from an authority your system doesn't know") :
			(p->cert_errors & G_TLS_CERTIFICATE_BAD_IDENTITY) ?
			_("its certificate is for a different name than this address") :
			(p->cert_errors & G_TLS_CERTIFICATE_EXPIRED) ?
			_("its certificate has expired") : _("its certificate isn't valid"));
	p->cb (r, p->data);
	/* the read in flight finishes cancelled and frees the rest */
}

static void
probe_free (probe *p)
{
	if (p->in)
		g_object_unref (p->in);
	if (p->conn)
	{
		g_io_stream_close (G_IO_STREAM (p->conn), NULL, NULL);
		g_object_unref (p->conn);
	}
	g_object_unref (p->cancel);
	g_free (p->user);
	g_free (p->pass);
	g_free (p);
}

static void
probe_send (probe *p, const char *line)
{
	GOutputStream *out = g_io_stream_get_output_stream (G_IO_STREAM (p->conn));
	char *l = g_strconcat (line, "\r\n", NULL);
	g_output_stream_write_all (out, l, strlen (l), NULL, NULL, NULL);
	g_free (l);
}

static void probe_read (probe *p);

static void
probe_line (GObject *src, GAsyncResult *res, gpointer data)
{
	probe *p = data;
	GError *err = NULL;
	char *line = g_data_input_stream_read_line_finish_utf8 (p->in, res, NULL, &err);
	char **w;

	if (p->done || !line)
	{
		if (!p->done)
			probe_finish (p, NULL, err ? err->message : _("The connection was closed without an answer."));
		g_clear_error (&err);
		g_free (line);
		probe_free (p);
		return;
	}
	g_strchomp (line);
	/* tags off */
	if (line[0] == '@' && strchr (line, ' '))
		memmove (line, strchr (line, ' ') + 1, strlen (strchr (line, ' ') + 1) + 1);
	w = g_strsplit (line, " ", 4);

	if (w[0] && w[1] && !strcmp (w[1], "CAP") && w[2] && w[3])
	{
		/* ZNC answers CAP itself; soju and always-on networks say so in the list */
		if (!g_ascii_strcasecmp (w[0], ":irc.znc.in"))
			p->znc = TRUE;
		if (strstr (w[3], "soju.im/bouncer-networks"))
			p->soju = TRUE;
		if (strstr (w[3], "draft/persistence"))
			p->persist = TRUE;
	}
	else if (w[0] && w[1] && !strcmp (w[1], "001"))
	{
		probe_send (p, "QUIT");
		probe_finish (p, p->soju ? "soju" : p->znc ? "znc" : p->persist ? "persist" : "plain", NULL);
	}
	else if (w[0] && w[1] && (!strcmp (w[1], "464") || !strcmp (w[1], "904")))
		probe_finish (p, NULL, _("The user name or password is wrong."));
	else if (w[0] && !strcmp (w[0], "ERROR"))
		probe_finish (p, NULL, w[1] ? strchr (line, ':') ? strchr (line, ':') + 1 : line : line);
	else if (w[0] && !strcmp (w[0], "PING") && w[1])
	{
		char *pong = g_strdup_printf ("PONG %s", w[1]);
		probe_send (p, pong);
		g_free (pong);
	}
	g_strfreev (w);
	g_free (line);
	probe_read (p);
}

static void
probe_read (probe *p)
{
	g_data_input_stream_read_line_async (p->in, G_PRIORITY_DEFAULT, p->cancel, probe_line, p);
}

static void
probe_connected (GObject *src, GAsyncResult *res, gpointer data)
{
	probe *p = data;
	GError *err = NULL;

	p->conn = g_socket_client_connect_to_host_finish (G_SOCKET_CLIENT (src), res, &err);
	g_object_unref (src);
	if (!p->conn)
	{
		if (!p->done)
			probe_finish (p, NULL, err->message);
		g_error_free (err);
		probe_free (p);
		return;
	}
	p->in = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (p->conn)));
	g_data_input_stream_set_newline_type (p->in, G_DATA_STREAM_NEWLINE_TYPE_ANY);
	probe_send (p, "CAP LS 302");
	if (*p->pass)
	{
		char *l = g_strdup_printf ("PASS %s", p->pass);
		probe_send (p, l);
		g_free (l);
	}
	probe_send (p, "NICK nxcheck");
	{
		char *l = g_strdup_printf ("USER %s 0 * :nexchat", *p->user ? p->user : "nexchat");
		probe_send (p, l);
		g_free (l);
	}
	probe_send (p, "CAP END");
	probe_read (p);
}

static gboolean
probe_timeout (gpointer data)
{
	probe *p = data;
	p->timeout = 0;
	probe_finish (p, NULL, _("No answer within 15 seconds."));
	return G_SOURCE_REMOVE;
}

static gboolean
accept_any (GTlsConnection *c, GTlsCertificate *cert, GTlsCertificateFlags errors, gpointer d)
{
	probe *p = d;
	/* go on to see what it is, but remember it isn't trusted */
	p->cert_errors = errors;
	return TRUE;
}

static void
probe_tls_event (GSocketClient *client, GSocketClientEvent event, GSocketConnectable *conn,
					  GIOStream *stream, gpointer data)
{
	if (event == G_SOCKET_CLIENT_TLS_HANDSHAKING)
		g_signal_connect (stream, "accept-certificate", G_CALLBACK (accept_any), data);
}

void
bncsetup_probe (const char *host, int port, gboolean tls, const char *user,
					 const char *pass, bnc_probe_cb cb, gpointer data)
{
	probe *p = g_new0 (probe, 1);
	GSocketClient *client = g_socket_client_new ();

	p->cancel = g_cancellable_new ();
	p->user = g_strdup (user ? user : "");
	p->pass = g_strdup (pass ? pass : "");
	p->cb = cb;
	p->data = data;
	g_socket_client_set_tls (client, tls);
	if (tls)
		g_signal_connect (client, "event", G_CALLBACK (probe_tls_event), p);
	g_socket_client_set_timeout (client, 15);
	p->timeout = g_timeout_add_seconds (15, probe_timeout, p);
	g_socket_client_connect_to_host_async (client, host, port, p->cancel, probe_connected, p);
}

/* ---- the window ---- */

typedef struct
{
	GtkWidget *win;
	GtkWidget *have, *network, *learn;	/* the three choices */
	GtkWidget *bnc_box, *net_box;
	GtkWidget *host, *port, *tls, *user, *pass, *upstream;
	GtkWidget *nhost, *nport, *ntls, *naccount, *npass;
	GtkWidget *status, *check, *save;
	GtkWidget *trust;	/* "connect even though the certificate isn't trusted" */
	char *kind;	/* what Check found */
	ircnet *net;
	void (*done) (ircnet *net);
	gboolean checking;
} setup;

static setup *open_setup;

static void
set_status (setup *s, const char *markup)
{
	gtk_label_set_markup (GTK_LABEL (s->status), markup);
}

static void
update_sensitive (setup *s)
{
	gboolean have = gtk_check_button_get_active (GTK_CHECK_BUTTON (s->have));
	gboolean net = gtk_check_button_get_active (GTK_CHECK_BUTTON (s->network));

	gtk_widget_set_visible (s->bnc_box, have);
	gtk_widget_set_visible (s->net_box, net);
	gtk_widget_set_sensitive (s->check, (have || net) && !s->checking);
	gtk_widget_set_sensitive (s->save, (have || net) && !s->checking);
	if (!have && !net)
		set_status (s, "");
}

static void
choice_cb (GtkCheckButton *b, gpointer data)
{
	setup *s = data;
	g_clear_pointer (&s->kind, g_free);
	gtk_widget_set_visible (s->trust, FALSE);
	update_sensitive (s);
	if (gtk_check_button_get_active (GTK_CHECK_BUTTON (s->learn)))
		set_status (s, _("IRC only delivers messages to people who are connected. When your computer "
							  "is off or asleep, you're not in your channels and what's said there is "
							  "gone.\n\nA <b>bouncer</b> is a small program on a computer that's always "
							  "on (a cheap server, a Raspberry Pi, your router). It stays in your channels "
							  "for you and keeps what you missed; nexchat connects to it instead of the "
							  "network. <b>soju</b> and <b>ZNC</b> are the usual ones, and some people "
							  "share one with friends.\n\nSome networks (ones running Ergo) can keep you "
							  "connected themselves, with no bouncer: pick the second choice to check."));
}

static int
port_of (GtkWidget *e, gboolean tls)
{
	int p = atoi (nx_entry_get_text (e));
	return p > 0 && p < 65536 ? p : (tls ? 6697 : 6667);
}

static const char *
kind_label (const char *kind)
{
	if (!strcmp (kind, "soju")) return "soju";
	if (!strcmp (kind, "znc")) return "ZNC";
	return kind;
}

static void
probe_done (bnc_probe_result *r, gpointer data)
{
	setup *s = data;
	char *m;

	s->checking = FALSE;
	update_sensitive (s);
	g_clear_pointer (&s->kind, g_free);
	if (!r->kind)
	{
		m = g_markup_printf_escaped ("<span foreground=\"#cc0000\">%s</span>", r->error);
		set_status (s, m);
		g_free (m);
	}
	else
	{
		gboolean bnc = gtk_check_button_get_active (GTK_CHECK_BUTTON (s->have));
		s->kind = g_strdup (r->kind);
		if (bnc && (!strcmp (r->kind, "soju") || !strcmp (r->kind, "znc")))
			m = g_markup_printf_escaped (_("<b>That works:</b> it's %s and the password is right. "
													 "Save and connect, then add the networks you want "
													 "it to stay on (or keep the one it has)."),
												  kind_label (r->kind));
		else if (!strcmp (r->kind, "persist"))
			m = g_strdup (bnc ? _("<b>That's not a bouncer</b>, but this network can keep you online by "
										 "itself. Pick the second choice instead.")
								: _("<b>Yes:</b> this network can keep you online. Save and connect, and "
									 "nexchat turns it on after you've logged in."));
		else if (bnc)
			m = g_strdup (_("<span foreground=\"#cc0000\">That's a plain IRC server, not a bouncer. Check the "
								 "address and port of your bouncer.</span>"));
		else
			m = g_strdup (_("<span foreground=\"#cc0000\">This network can't keep you online by itself. "
								 "You'll need a bouncer for it.</span>"));
		if (r->cert_untrusted)
		{
			char *w = g_markup_printf_escaped (_("\n\n<span foreground=\"#cc0000\"><b>Careful:</b> %s.</span> "
															 "If it's your own bouncer with a self-made certificate, tick "
															 "the box below; otherwise someone could be in the middle."),
														  r->cert_why);
			char *both = g_strconcat (m, w, NULL);
			set_status (s, both);
			g_free (both);
			g_free (w);
		}
		else
			set_status (s, m);
		gtk_widget_set_visible (s->trust, r->cert_untrusted);
		g_free (m);
	}
	bnc_probe_result_free (r);
}

static void
check_cb (GtkWidget *b, gpointer data)
{
	setup *s = data;
	gboolean bnc = gtk_check_button_get_active (GTK_CHECK_BUTTON (s->have));
	GtkWidget *host = bnc ? s->host : s->nhost, *port = bnc ? s->port : s->nport;
	GtkWidget *tls = bnc ? s->tls : s->ntls;
	gboolean t = gtk_check_button_get_active (GTK_CHECK_BUTTON (tls));
	const char *h = nx_entry_get_text (host);

	if (!*h)
	{
		set_status (s, _("<span foreground=\"#cc0000\">Fill in the address first.</span>"));
		gtk_widget_grab_focus (host);
		return;
	}
	s->checking = TRUE;
	update_sensitive (s);
	set_status (s, _("Checking..."));
	bncsetup_probe (h, port_of (port, t), t,
						 bnc ? nx_entry_get_text (s->user) : "",
						 bnc ? nx_entry_get_text (s->pass) : "", probe_done, s);
}

/* the entry, written the way this kind of server wants to be logged in to */
static void
save_cb (GtkWidget *b, gpointer data)
{
	setup *s = data;
	gboolean bnc = gtk_check_button_get_active (GTK_CHECK_BUTTON (s->have));
	GtkWidget *hostw = bnc ? s->host : s->nhost, *portw = bnc ? s->port : s->nport;
	gboolean tls = gtk_check_button_get_active (GTK_CHECK_BUTTON (bnc ? s->tls : s->ntls));
	const char *host = nx_entry_get_text (hostw);
	char *srv;
	ircnet *net = s->net;

	if (!*host)
	{
		set_status (s, _("<span foreground=\"#cc0000\">Fill in the address first.</span>"));
		gtk_widget_grab_focus (hostw);
		return;
	}

	if (!net)
	{
		char *name = g_strdup_printf (bnc ? _("My bouncer (%s)") : "%s", host);
		net = servlist_net_add (name, "", TRUE);
		net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
		g_free (name);
	}
	/* one server: the bouncer or the network, as given */
	while (net->servlist)
		servlist_server_remove (net, net->servlist->data);
	net->selected = 0;
	srv = g_strdup_printf ("%s/%s%d", host, tls ? "+" : "", port_of (portw, tls));
	servlist_server_add (net, srv);
	g_free (srv);

	if (tls)
		net->flags |= FLAG_USE_SSL;
	else
		net->flags &= ~FLAG_USE_SSL;
	if (tls && gtk_widget_get_visible (s->trust) && gtk_check_button_get_active (GTK_CHECK_BUTTON (s->trust)))
		net->flags |= FLAG_ALLOW_INVALID;
	else
		net->flags &= ~FLAG_ALLOW_INVALID;
	net->flags |= FLAG_CYCLE;

	if (bnc)
	{
		const char *user = nx_entry_get_text (s->user);
		const char *up = nx_entry_get_text (s->upstream);

		/* the user name goes with it: the bouncer's account, not ours */
		net->flags &= ~FLAG_USE_GLOBAL;
		if (!net->nick)
			net->nick = g_strdup (prefs.hex_irc_nick1);
		g_free (net->user);
		/* ZNC picks a network with user/network; soju with the same */
		net->user = *up ? g_strdup_printf ("%s/%s", user, up) : g_strdup (user);
		g_free (net->pass);
		net->pass = g_strdup (nx_entry_get_text (s->pass));
		/* PASS works on both soju and ZNC; soju also takes it with SASL */
		net->logintype = LOGIN_PASS;
	}
	else
	{
		const char *acct = nx_entry_get_text (s->naccount);
		const char *pw = nx_entry_get_text (s->npass);

		/* always-on needs an account: log in with SASL */
		if (*acct && *pw)
		{
			net->flags &= ~FLAG_USE_GLOBAL;
			if (!net->nick)
				net->nick = g_strdup (acct);
			g_free (net->user);
			net->user = g_strdup (acct);
			g_free (net->pass);
			net->pass = g_strdup (pw);
			net->logintype = LOGIN_SASL;
		}
		/* turned on after login (bouncer.c), once we know it's there */
		net->flags |= FLAG_STAY_ONLINE;
	}
	servlist_save ();

	if (s->done)
		s->done (net);
	nx_widget_destroy (s->win);
	servlist_connect (NULL, net, TRUE);
}

static void
setup_free (gpointer data)
{
	setup *s = data;
	if (open_setup == s)
		open_setup = NULL;
	g_free (s->kind);
	g_free (s);
}

char *
bncsetup_describe (void)
{
	setup *s = open_setup;
	const char *c;

	if (!s)
		return NULL;
	c = gtk_check_button_get_active (GTK_CHECK_BUTTON (s->have)) ? "bouncer" :
		 gtk_check_button_get_active (GTK_CHECK_BUTTON (s->network)) ? "network" :
		 gtk_check_button_get_active (GTK_CHECK_BUTTON (s->learn)) ? "explain" : "none";
	{
		char *st = g_strdup (gtk_label_get_text (GTK_LABEL (s->status))), *p, *out;
		for (p = st; *p; p++)
			if (*p == '\n')
				*p = ' ';
		out = g_strdup_printf ("%s | %s", c, st);
		g_free (st);
		return out;
	}
}

static GtkWidget *
row (GtkWidget *grid, int r, const char *label, GtkWidget *w)
{
	GtkWidget *l = gtk_label_new_with_mnemonic (label);
	gtk_label_set_xalign (GTK_LABEL (l), 0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (l), w);
	gtk_grid_attach (GTK_GRID (grid), l, 0, r, 1, 1);
	gtk_widget_set_hexpand (w, TRUE);
	gtk_grid_attach (GTK_GRID (grid), w, 1, r, 1, 1);
	return w;
}

static GtkWidget *
entry (const char *text, const char *placeholder)
{
	GtkWidget *e = gtk_entry_new ();
	if (text)
		nx_entry_set_text (e, text);
	if (placeholder)
		gtk_entry_set_placeholder_text (GTK_ENTRY (e), placeholder);
	gtk_entry_set_activates_default (GTK_ENTRY (e), TRUE);
	return e;
}

static GtkWidget *
hint (const char *text)
{
	GtkWidget *l = gtk_label_new (text);
	gtk_label_set_xalign (GTK_LABEL (l), 0);
	gtk_label_set_wrap (GTK_LABEL (l), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (l), 60);
	gtk_widget_add_css_class (l, "dim-label");
	return l;
}

/* what's there already: a server of this network, and its login */
static void
fill_from (setup *s, ircnet *net)
{
	ircserver *sv = net && net->servlist ? g_slist_nth_data (net->servlist, net->selected) : NULL;
	char *host = NULL, *port = NULL;
	gboolean tls = !net || (net->flags & FLAG_USE_SSL);

	if (!sv && net && net->servlist)
		sv = net->servlist->data;
	if (sv)
	{
		char *slash = strchr (sv->hostname, '/');
		host = slash ? g_strndup (sv->hostname, slash - sv->hostname) : g_strdup (sv->hostname);
		if (slash)
		{
			if (slash[1] == '+')
				tls = TRUE;
			port = g_strdup (slash + 1 + (slash[1] == '+'));
		}
	}
	nx_entry_set_text (s->nhost, host ? host : "");
	nx_entry_set_text (s->nport, port ? port : "");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (s->ntls), tls);
	if (net && net->logintype == LOGIN_SASL && net->user)
	{
		nx_entry_set_text (s->naccount, net->user);
		nx_entry_set_text (s->npass, net->pass ? net->pass : "");
	}
	/* a network already set up for a bouncer opens on that */
	if (net && net->logintype == LOGIN_PASS && net->user && net->pass)
	{
		char *slashu = strchr (net->user, '/');
		nx_entry_set_text (s->host, host ? host : "");
		nx_entry_set_text (s->port, port ? port : "");
		gtk_check_button_set_active (GTK_CHECK_BUTTON (s->tls), tls);
		if (slashu)
		{
			char *u = g_strndup (net->user, slashu - net->user);
			nx_entry_set_text (s->user, u);
			nx_entry_set_text (s->upstream, slashu + 1);
			g_free (u);
		}
		else
			nx_entry_set_text (s->user, net->user);
		nx_entry_set_text (s->pass, net->pass);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (s->have), TRUE);
	}
	else
		gtk_check_button_set_active (GTK_CHECK_BUTTON (s->tls), TRUE);
	g_free (host);
	g_free (port);
}

void
bncsetup_open (GtkWindow *parent, ircnet *net, void (*done) (ircnet *net))
{
	setup *s = g_new0 (setup, 1);
	GtkWidget *vbox, *grid, *bb, *cancel, *title;
	char *t;

	s->net = net;
	s->done = done;
	s->win = nx_window_new ();
	t = g_strdup_printf (_("Stay online - %s"), net ? net->name : _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (s->win), t);
	g_free (t);
	gtk_window_set_transient_for (GTK_WINDOW (s->win), parent);
	gtk_window_set_modal (GTK_WINDOW (s->win), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (s->win), 520, -1);
	g_object_set_data_full (G_OBJECT (s->win), "setup", s, setup_free);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	nx_set_border_width (vbox, 10);
	gtk_window_set_child (GTK_WINDOW (s->win), vbox);

	title = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (title),
		_("<b>Stay in your channels when nexchat is closed</b>\nand see everything you missed when you come back."));
	gtk_label_set_xalign (GTK_LABEL (title), 0);
	nx_box_pack_start (vbox, title, FALSE, FALSE, 4);

	s->have = gtk_check_button_new_with_mnemonic (_("I have a _bouncer (soju, ZNC, ...)"));
	s->network = gtk_check_button_new_with_mnemonic (_("This network can _keep me online by itself"));
	s->learn = gtk_check_button_new_with_mnemonic (_("I don't know, _explain"));
	gtk_check_button_set_group (GTK_CHECK_BUTTON (s->network), GTK_CHECK_BUTTON (s->have));
	gtk_check_button_set_group (GTK_CHECK_BUTTON (s->learn), GTK_CHECK_BUTTON (s->have));

	/* bouncer fields */
	nx_box_pack_start (vbox, s->have, FALSE, FALSE, 0);
	s->bnc_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start (s->bnc_box, 24);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
	s->host = row (grid, 0, _("_Address:"), entry (NULL, "bnc.example.org"));
	s->port = row (grid, 1, _("_Port:"), entry (NULL, "6697"));
	s->tls = gtk_check_button_new_with_mnemonic (_("Use _TLS (encrypted)"));
	gtk_grid_attach (GTK_GRID (grid), s->tls, 1, 2, 1, 1);
	s->user = row (grid, 3, _("_User name:"), entry (NULL, NULL));
	s->pass = row (grid, 4, _("Pass_word:"), entry (NULL, NULL));
	gtk_entry_set_visibility (GTK_ENTRY (s->pass), FALSE);
	s->upstream = row (grid, 5, _("Network on _it:"), entry (NULL, _("optional")));
	nx_box_pack_start (s->bnc_box, grid, FALSE, FALSE, 0);
	nx_box_pack_start (s->bnc_box, hint (_("The user name and password of your account on the bouncer. "
														"\"Network on it\" picks one when it has several (ZNC, soju); "
														"leave it empty to choose later with /bouncer.")), FALSE, FALSE, 0);
	nx_box_pack_start (vbox, s->bnc_box, FALSE, FALSE, 0);

	/* always-on network fields */
	nx_box_pack_start (vbox, s->network, FALSE, FALSE, 0);
	s->net_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start (s->net_box, 24);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
	s->nhost = row (grid, 0, _("A_ddress:"), entry (NULL, "irc.example.org"));
	s->nport = row (grid, 1, _("P_ort:"), entry (NULL, "6697"));
	s->ntls = gtk_check_button_new_with_mnemonic (_("Use T_LS (encrypted)"));
	gtk_grid_attach (GTK_GRID (grid), s->ntls, 1, 2, 1, 1);
	s->naccount = row (grid, 3, _("Accou_nt:"), entry (NULL, NULL));
	s->npass = row (grid, 4, _("Passwo_rd:"), entry (NULL, NULL));
	gtk_entry_set_visibility (GTK_ENTRY (s->npass), FALSE);
	nx_box_pack_start (s->net_box, grid, FALSE, FALSE, 0);
	nx_box_pack_start (s->net_box, hint (_("Your registered account on that network: staying online "
														"is tied to it. No account yet? Connect and use /msg NickServ "
														"REGISTER first.")), FALSE, FALSE, 0);
	nx_box_pack_start (vbox, s->net_box, FALSE, FALSE, 0);

	nx_box_pack_start (vbox, s->learn, FALSE, FALSE, 0);

	s->status = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (s->status), 0);
	gtk_label_set_wrap (GTK_LABEL (s->status), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (s->status), 60);
	gtk_label_set_selectable (GTK_LABEL (s->status), TRUE);
	nx_box_pack_start (vbox, s->status, FALSE, FALSE, 6);
	s->trust = gtk_check_button_new_with_mnemonic (_("Connect an_yway: I trust this certificate"));
	gtk_widget_set_visible (s->trust, FALSE);
	nx_box_pack_start (vbox, s->trust, FALSE, FALSE, 0);

	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (bb, GTK_ALIGN_END);
	s->check = gtk_button_new_with_mnemonic (_("C_heck"));
	gtk_widget_set_tooltip_text (s->check, _("Log in once to see what it is and whether the password works"));
	cancel = gtk_button_new_with_mnemonic (_("_Cancel"));
	s->save = gtk_button_new_with_mnemonic (_("_Save and connect"));
	gtk_box_append (GTK_BOX (bb), s->check);
	gtk_box_append (GTK_BOX (bb), cancel);
	gtk_box_append (GTK_BOX (bb), s->save);
	nx_box_pack_start (vbox, bb, FALSE, FALSE, 0);
	gtk_window_set_default_widget (GTK_WINDOW (s->win), s->save);

	g_signal_connect (s->have, "toggled", G_CALLBACK (choice_cb), s);
	g_signal_connect (s->network, "toggled", G_CALLBACK (choice_cb), s);
	g_signal_connect (s->learn, "toggled", G_CALLBACK (choice_cb), s);
	g_signal_connect (s->check, "clicked", G_CALLBACK (check_cb), s);
	g_signal_connect (s->save, "clicked", G_CALLBACK (save_cb), s);
	g_signal_connect_swapped (cancel, "clicked", G_CALLBACK (gtk_window_destroy), s->win);

	fill_from (s, net);
	update_sensitive (s);
	open_setup = s;
	gtk_window_present (GTK_WINDOW (s->win));
}
