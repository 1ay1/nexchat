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
#include "../common/bouncer.h"
#include "../common/mybouncer.h"
#include "bncdetect.h"

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
	g_free (r->seen);
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
	r->seen = g_strdup (p->soju ? "soju" : p->znc ? "znc" : p->persist ? "persist" : NULL);
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
		{
			/* encrypted to a plain-text port (or the other way round) */
			if (g_error_matches (err, G_TLS_ERROR, G_TLS_ERROR_NOT_TLS) ||
				 (err->domain == G_TLS_ERROR && strstr (err->message, "TLS packet")))
				probe_finish (p, NULL, _("It doesn't speak TLS on that port. Write irc:// in front of the address "
												 "for a plain-text connection."));
			else
				probe_finish (p, NULL, err->message);
		}
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

/* ---- the window ----
   A few steps in one window, each a page of a GtkStack:
     choose:   what you have (a bouncer / a network that can keep you on /
               explain)
     bouncer:  address, user, password; checked as you type, and the
               check says what it is and whether the password works
     networks: which IRC networks the bouncer should be on (popular ones,
               or your own); added on the bouncer once connected
     persist:  a network from your list and your account on it
     explain:  what a bouncer is, in plain words
   Nothing is saved until Connect. */

typedef enum { STEP_CHOOSE, STEP_BOUNCER, STEP_NETWORKS, STEP_PERSIST, STEP_EXPLAIN } step;

typedef struct
{
	GtkWidget *win, *stack, *back, *next;
	step at;
	/* bouncer */
	GtkWidget *addr, *user, *pass, *trust, *status;
	guint check_tag;
	gboolean checking;
	int check_gen;			/* the newest check: older answers are dropped */
	char *kind;				/* what the check found: "soju", "znc", NULL */
	gboolean cert_bad;
	/* networks */
	GtkWidget *netbox;		/* the check buttons */
	GtkWidget *other_name, *other_host;
	GPtrArray *net_checks;	/* GtkCheckButton*, data "host" */
	/* persist */
	GtkWidget *pnet, *paccount, *ppass, *pstatus;
	GStrv pnet_names;
	char *pkind;			/* what the network's check found */
	/* what this computer has (bncdetect), shown on the first page */
	GtkWidget *found_box;
	GtkWidget *net_intro, *other_grid;
	bnc_found *found;
	gboolean mine;			/* the networks page is for our own soju */
	GtkWidget *mine_status;
	/* where it came from */
	ircnet *net;
	void (*done) (ircnet *net);
} setup;

static setup *open_setup;

/* the networks offered on the networks page: name, address */
static const char *const popular[][2] = {
	{ "Libera.Chat", "irc.libera.chat" },
	{ "OFTC", "irc.oftc.net" },
	{ "EFnet", "irc.efnet.org" },
	{ "IRCnet", "open.ircnet.net" },
	{ "Rizon", "irc.rizon.net" },
	{ "hackint", "irc.hackint.org" },
	{ "Snoonet", "irc.snoonet.org" },
	{ "Undernet", "irc.undernet.org" },
};

/* ---- the address: "host", "host:port", "ircs://host:port", "irc://..." ---- */

typedef struct
{
	char *host;
	int port;
	gboolean tls;
} address;

static gboolean
parse_address (const char *in, address *a)
{
	char *s = g_strstrip (g_strdup (in)), *p, *colon;

	a->host = NULL;
	a->tls = TRUE;
	a->port = 0;
	p = s;
	if (g_str_has_prefix (p, "ircs://"))
		p += 7;
	else if (g_str_has_prefix (p, "irc+insecure://"))
		p += 15, a->tls = FALSE;
	else if (g_str_has_prefix (p, "irc://"))
		p += 6, a->tls = FALSE;
	/* a path after it (irc://host/#chan) */
	if (strchr (p, '/'))
		*strchr (p, '/') = 0;
	colon = strrchr (p, ':');
	if (colon && !strchr (p, ']'))
	{
		*colon = 0;
		/* "+6697" is TLS too, as in HexChat's server list */
		if (colon[1] == '+')
			a->tls = TRUE, colon++;
		a->port = atoi (colon + 1);
		/* a bare 6667 means plain text */
		if (a->port == 6667 && !g_str_has_prefix (in, "ircs://"))
			a->tls = FALSE;
	}
	if (*p && !strpbrk (p, " \t/"))
		a->host = g_strdup (p);
	g_free (s);
	if (!a->port)
		a->port = a->tls ? 6697 : 6667;
	return a->host != NULL;
}

/* ---- small widgets ---- */

static GtkWidget *
label (const char *markup, gboolean dim)
{
	GtkWidget *l = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (l), markup);
	gtk_label_set_xalign (GTK_LABEL (l), 0);
	gtk_label_set_wrap (GTK_LABEL (l), TRUE);
	gtk_label_set_wrap_mode (GTK_LABEL (l), PANGO_WRAP_WORD_CHAR);
	/* wrap at the window's width instead of making it wider: a wrapped
	   label's natural width is the whole text on one line otherwise */
	gtk_label_set_width_chars (GTK_LABEL (l), 1);
	gtk_label_set_max_width_chars (GTK_LABEL (l), 1);
	gtk_label_set_natural_wrap_mode (GTK_LABEL (l), GTK_NATURAL_WRAP_WORD);
	if (dim)
		gtk_widget_add_css_class (l, "dim-label");
	return l;
}

static GtkWidget *
entry (const char *placeholder, gboolean secret)
{
	GtkWidget *e = gtk_entry_new ();
	if (placeholder)
		gtk_entry_set_placeholder_text (GTK_ENTRY (e), placeholder);
	gtk_entry_set_visibility (GTK_ENTRY (e), !secret);
	gtk_entry_set_activates_default (GTK_ENTRY (e), FALSE);
	gtk_widget_set_hexpand (e, TRUE);
	return e;
}

static void
row (GtkWidget *grid, int r, const char *text, GtkWidget *w)
{
	GtkWidget *l = gtk_label_new_with_mnemonic (text);
	gtk_label_set_xalign (GTK_LABEL (l), 0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (l), w);
	gtk_grid_attach (GTK_GRID (grid), l, 0, r, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), w, 1, r, 1, 1);
}

static GtkWidget *
page (const char *title)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	char *m = g_markup_printf_escaped ("<b>%s</b>", title);
	gtk_box_append (GTK_BOX (box), label (m, FALSE));
	g_free (m);
	return box;
}

static void
set_status (GtkWidget *l, const char *markup)
{
	gtk_label_set_markup (GTK_LABEL (l), markup ? markup : "");
}

/* "ok" green, "no" red, "wait" plain */
static void
set_status_kind (GtkWidget *l, const char *kind, const char *text)
{
	char *m;

	if (!strcmp (kind, "ok"))
		m = g_markup_printf_escaped ("<span foreground=\"#2e7d32\">\xe2\x9c\x93 %s</span>", text);
	else if (!strcmp (kind, "no"))
		m = g_markup_printf_escaped ("<span foreground=\"#c62828\">\xe2\x9c\x97 %s</span>", text);
	else
		m = g_markup_printf_escaped ("%s", text);
	set_status (l, m);
	g_free (m);
}

/* ---- moving between steps ---- */

static void update_buttons (setup *s);
static gboolean bouncer_check_now (gpointer data);
static void next_cb (GtkWidget *b, gpointer data);

static void
go (setup *s, step to)
{
	static const char *const names[] = { "choose", "bouncer", "networks", "persist", "explain" };

	s->at = to;
	gtk_stack_set_visible_child_name (GTK_STACK (s->stack), names[to]);
	switch (to)
	{
	case STEP_BOUNCER: gtk_widget_grab_focus (s->addr); break;
	case STEP_PERSIST: gtk_widget_grab_focus (s->paccount); break;
	default: break;
	}
}

static void
update_buttons (setup *s)
{
	gtk_button_set_use_underline (GTK_BUTTON (s->next), TRUE);
	gtk_widget_set_visible (s->back, s->at != STEP_CHOOSE);
	switch (s->at)
	{
	case STEP_CHOOSE:
	case STEP_EXPLAIN:
		gtk_widget_set_visible (s->next, FALSE);
		break;
	case STEP_BOUNCER:
		gtk_widget_set_visible (s->next, TRUE);
		gtk_button_set_label (GTK_BUTTON (s->next), _("_Next"));
		gtk_widget_set_sensitive (s->next, s->kind && !s->checking &&
										  (!s->cert_bad || gtk_check_button_get_active (GTK_CHECK_BUTTON (s->trust))));
		break;
	case STEP_NETWORKS:
		gtk_widget_set_visible (s->next, TRUE);
		gtk_button_set_label (GTK_BUTTON (s->next), _("Co_nnect"));
		gtk_widget_set_sensitive (s->next, TRUE);
		break;
	case STEP_PERSIST:
		gtk_widget_set_visible (s->next, TRUE);
		gtk_button_set_label (GTK_BUTTON (s->next), _("Co_nnect"));
		gtk_widget_set_sensitive (s->next, s->pkind && !strcmp (s->pkind, "persist") &&
										  *nx_entry_get_text (s->paccount) && *nx_entry_get_text (s->ppass));
		break;
	}
}

/* ---- the bouncer page: checked as you type ---- */

static void
bouncer_checked (bnc_probe_result *r, gpointer data)
{
	int *gen = data;
	setup *s = open_setup;

	/* the window is gone, or something was typed since */
	if (!s || *gen != s->check_gen)
	{
		g_free (gen);
		bnc_probe_result_free (r);
		return;
	}
	g_free (gen);
	s->checking = FALSE;
	g_clear_pointer (&s->kind, g_free);
	s->cert_bad = FALSE;
	gtk_widget_set_visible (s->trust, FALSE);

	if (!r->kind)
		set_status_kind (s->status, "no", r->error);
	else if (!strcmp (r->kind, "soju") || !strcmp (r->kind, "znc"))
	{
		char *t = g_strdup_printf (_("That's %s, and the password is right."),
											!strcmp (r->kind, "soju") ? "soju" : "ZNC");
		s->kind = g_strdup (r->kind);
		set_status_kind (s->status, "ok", t);
		g_free (t);
		if (r->cert_untrusted)
		{
			char *w = g_markup_printf_escaped (_("\n<span foreground=\"#c62828\">But %s.</span> "
															 "Fine for your own bouncer with a self-made certificate; "
															 "otherwise someone could be listening in."), r->cert_why);
			char *both = g_strconcat (gtk_label_get_label (GTK_LABEL (s->status)), w, NULL);
			set_status (s->status, both);
			g_free (both);
			g_free (w);
			s->cert_bad = TRUE;
			gtk_widget_set_visible (s->trust, TRUE);
		}
	}
	else if (!strcmp (r->kind, "persist"))
		set_status_kind (s->status, "no", _("That's an IRC network, not a bouncer. But it can keep you online "
														"by itself: go back and pick \"Keep me online on my network\"."));
	else
		set_status_kind (s->status, "no", _("That's a plain IRC server, not a bouncer. Check the address and port."));
	update_buttons (s);
	bnc_probe_result_free (r);
}

static gboolean
bouncer_check_now (gpointer data)
{
	setup *s = data;
	address a;
	int *gen;

	s->check_tag = 0;
	if (!parse_address (nx_entry_get_text (s->addr), &a))
	{
		set_status (s->status, *nx_entry_get_text (s->addr) ?
						_("That doesn't look like an address: a name like bnc.example.org, maybe with :port.") : "");
		return G_SOURCE_REMOVE;
	}
	if (!*nx_entry_get_text (s->user) || !*nx_entry_get_text (s->pass))
	{
		set_status (s->status, _("Fill in your user name and password on the bouncer."));
		g_free (a.host);
		return G_SOURCE_REMOVE;
	}
	s->checking = TRUE;
	gen = g_new (int, 1);
	*gen = ++s->check_gen;
	{
		char *t = g_strdup_printf (_("Checking %s:%d%s..."), a.host, a.port, a.tls ? _(" (encrypted)") : "");
		set_status_kind (s->status, "wait", t);
		g_free (t);
	}
	update_buttons (s);
	bncsetup_probe (a.host, a.port, a.tls, nx_entry_get_text (s->user), nx_entry_get_text (s->pass),
						 bouncer_checked, gen);
	g_free (a.host);
	return G_SOURCE_REMOVE;
}

/* typing: check 0.8 s after the last key */
static void
bouncer_changed (GtkEditable *e, gpointer data)
{
	setup *s = data;

	s->check_gen++;	/* an answer still on its way is out of date */
	s->checking = FALSE;
	g_clear_pointer (&s->kind, g_free);
	gtk_widget_set_visible (s->trust, FALSE);
	if (s->check_tag)
		g_source_remove (s->check_tag);
	s->check_tag = g_timeout_add (800, bouncer_check_now, s);
	update_buttons (s);
}

static void
trust_toggled (GtkCheckButton *b, gpointer data)
{
	update_buttons (data);
}

/* ---- the networks page ---- */

/* our own soju: your own networks, the ones you use ticked (favourites,
   auto-connect, with channels, the one it was opened from) */
static void
networks_fill_mine (setup *s)
{
	GSList *l;

	for (l = network_list; l; l = l->next)
	{
		ircnet *n = l->data;
		ircserver *sv = n->servlist ? n->servlist->data : NULL;
		GtkWidget *b, *lb;
		char *t;

		/* already on it, or a bouncer's own entries: not offered */
		if (!sv || mybouncer_owns (n) || n->logintype == LOGIN_PASS || strchr (n->name, '/'))
			continue;
		t = g_markup_printf_escaped ("%s  <span alpha=\"60%%\">%s%s</span>", n->name, sv->hostname,
											  n->favchanlist ? _(", with its channels") : "");
		b = gtk_check_button_new ();
		lb = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (lb), t);
		gtk_check_button_set_child (GTK_CHECK_BUTTON (b), lb);
		g_object_set_data (G_OBJECT (b), "net", n);
		if (n == s->net || (n->flags & (FLAG_FAVORITE | FLAG_AUTO_CONNECT)) || n->favchanlist)
			gtk_check_button_set_active (GTK_CHECK_BUTTON (b), TRUE);
		gtk_box_append (GTK_BOX (s->netbox), b);
		g_ptr_array_add (s->net_checks, b);
		g_free (t);
	}
}

static void
networks_fill (setup *s)
{
	GtkWidget *c;
	guint i;

	/* clear what was there (going back and forth) */
	while ((c = gtk_widget_get_first_child (s->netbox)))
		gtk_box_remove (GTK_BOX (s->netbox), c);
	g_ptr_array_set_size (s->net_checks, 0);

	if (s->mine)
	{
		gtk_label_set_text (GTK_LABEL (s->net_intro),
			_("These are from your Network List. The ticked ones move to your bouncer: their "
			  "address, nick, login and channels go with them, and from now on they connect "
			  "through it. /bouncer remove puts them back."));
		gtk_widget_set_visible (s->other_grid, FALSE);
		networks_fill_mine (s);
		return;
	}
	gtk_label_set_text (GTK_LABEL (s->net_intro),
		_("nexchat adds these on the bouncer when it connects. Ones it already has stay as "
		  "they are; you can change them later with /bouncer or in the Network List."));
	gtk_widget_set_visible (s->other_grid, TRUE);

	for (i = 0; i < G_N_ELEMENTS (popular); i++)
	{
		char *t = g_strdup_printf ("%s  <span alpha=\"60%%\">%s</span>", popular[i][0], popular[i][1]);
		GtkWidget *b = gtk_check_button_new ();
		GtkWidget *l = gtk_label_new (NULL);

		gtk_label_set_markup (GTK_LABEL (l), t);
		gtk_check_button_set_child (GTK_CHECK_BUTTON (b), l);
		g_object_set_data (G_OBJECT (b), "name", (gpointer) popular[i][0]);
		g_object_set_data (G_OBJECT (b), "host", (gpointer) popular[i][1]);
		/* the one it came from in the Network List is ticked */
		if (s->net && servlist_net_find ((char *) popular[i][0], NULL, g_ascii_strcasecmp) == s->net)
			gtk_check_button_set_active (GTK_CHECK_BUTTON (b), TRUE);
		gtk_box_append (GTK_BOX (s->netbox), b);
		g_ptr_array_add (s->net_checks, b);
		g_free (t);
	}
}

/* ---- the persist page ---- */

static void
persist_checked (bnc_probe_result *r, gpointer data)
{
	int *gen = data;
	setup *s = open_setup;

	if (!s || *gen != s->check_gen)
	{
		g_free (gen);
		bnc_probe_result_free (r);
		return;
	}
	g_free (gen);
	g_clear_pointer (&s->pkind, g_free);
	if (!r->kind)
		set_status_kind (s->pstatus, "no", r->error);
	else if (!strcmp (r->kind, "persist"))
	{
		s->pkind = g_strdup ("persist");
		set_status_kind (s->pstatus, "ok", _("This network can keep you online. Give your account on it and Connect."));
	}
	else
		set_status_kind (s->pstatus, "no", _("This network can't keep you online by itself; you'd need a bouncer for it. "
														 "Go back and pick \"I have a bouncer\", or ask the network's "
														 "people which bouncers they recommend."));
	update_buttons (s);
	bnc_probe_result_free (r);
}

/* the chosen network's first server, as an address */
static gboolean
persist_address (setup *s, address *a)
{
	guint i = gtk_drop_down_get_selected (GTK_DROP_DOWN (s->pnet));
	ircnet *net;
	ircserver *sv;
	char *h;
	gboolean ok;

	if (i == GTK_INVALID_LIST_POSITION || !s->pnet_names[i])
		return FALSE;
	net = servlist_net_find (s->pnet_names[i], NULL, strcmp);
	if (!net || !net->servlist)
		return FALSE;
	sv = net->servlist->data;
	/* "host/+6697" in the list: the + or the entry's TLS setting decides */
	h = g_strdup (sv->hostname);
	if (strchr (h, '/'))
	{
		char *slash = strchr (h, '/');
		*slash = ':';
	}
	ok = parse_address (h, a);
	if (ok)
	{
		const char *port = strchr (sv->hostname, '/');
		a->tls = (port && port[1] == '+') || (net->flags & FLAG_USE_SSL);
		if (!port)
			a->port = a->tls ? 6697 : 6667;
	}
	g_free (h);
	return ok;
}

static void
persist_net_changed (GObject *o, GParamSpec *ps, gpointer data)
{
	setup *s = data;
	address a;
	int *gen;

	g_clear_pointer (&s->pkind, g_free);
	if (!persist_address (s, &a))
	{
		set_status (s->pstatus, "");
		update_buttons (s);
		return;
	}
	gen = g_new (int, 1);
	*gen = ++s->check_gen;
	set_status_kind (s->pstatus, "wait", _("Checking whether this network can keep you online..."));
	update_buttons (s);
	bncsetup_probe (a.host, a.port, a.tls, "", "", persist_checked, gen);
	g_free (a.host);
}

static void
persist_entry_changed (GtkEditable *e, gpointer data)
{
	update_buttons (data);
}

static GtkWidget *
choice (setup *s, const char *title, const char *what, step to);
static void networks_fill (setup *s);

/* ---- what's on this computer ---- */

/* "Use soju on this computer": set ours up (nexchat's own soju, in its
   folder), then the networks page, ticked with what's in your list */
static void
use_local_soju (GtkWidget *b, gpointer data)
{
	setup *s = data;
	char *err = NULL;

	gtk_widget_set_sensitive (b, FALSE);
	if (!mybouncer_setup (&err))
	{
		GtkWidget *l = label ("", FALSE);
		set_status_kind (l, "no", err);
		gtk_box_append (GTK_BOX (s->found_box), l);
		g_free (err);
		gtk_widget_set_sensitive (b, TRUE);
		return;
	}
	s->mine = TRUE;
	networks_fill (s);
	go (s, STEP_NETWORKS);
}

/* a bouncer answering on this computer: its address filled in, then
   the login page with the password field ready */
static void
use_listening (GtkWidget *b, gpointer data)
{
	setup *s = data;
	int port = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "port"));
	gboolean tls = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "tls"));
	char *a = g_strdup_printf ("%s127.0.0.1:%d", tls ? "" : "irc://", port);

	nx_entry_set_text (s->addr, a);
	g_free (a);
	go (s, STEP_BOUNCER);
	gtk_widget_grab_focus (s->user);
}

static GtkWidget *
action (setup *s, const char *mnemonic, const char *what, GCallback cb)
{
	GtkWidget *b = choice (s, mnemonic, what, STEP_CHOOSE);
	/* not a step: does its own thing */
	g_signal_handlers_disconnect_matched (b, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, s);
	g_signal_connect (b, "clicked", cb, s);
	return b;
}

static void
detected (bnc_found *f, gpointer data)
{
	setup *s = open_setup;
	GtkWidget *c;
	bnc_installed *soju = NULL, *znc = NULL;
	guint i;
	gboolean any = FALSE;

	/* the window was closed meanwhile */
	if (!s || s != data)
	{
		bnc_found_free (f);
		return;
	}
	s->found = f;
	while ((c = gtk_widget_get_first_child (s->found_box)))
		gtk_box_remove (GTK_BOX (s->found_box), c);

	for (i = 0; i < f->installed->len; i++)
	{
		bnc_installed *b = f->installed->pdata[i];
		if (b->path && !strcmp (b->kind, "soju"))
			soju = b;
		else if (b->path && !strcmp (b->kind, "znc"))
			znc = b;
	}

	/* ours, already running: nothing to set up */
	if (soju && soju->ours)
	{
		char *t = g_strdup_printf (_("nexchat already runs a soju for you%s. Pick networks to put on it:"),
											mybouncer_running () ? "" : _(" (stopped right now: it starts again)"));
		gtk_box_append (GTK_BOX (s->found_box), action (s, _("_Use my bouncer"), t, G_CALLBACK (use_local_soju)));
		g_free (t);
		any = TRUE;
	}
	else if (soju)
	{
		GString *t = g_string_new (NULL);

		g_string_append_printf (t, _("soju%s%s is installed on this computer. nexchat sets up its own copy "
											 "(in its own folder, only reachable from here), keeps it running and "
											 "moves your networks onto it, logins and channels included. Nothing "
											 "to type."), soju->version ? " " : "", soju->version ? soju->version : "");
		/* the system's own soju, if it's there and broken: say so, plainly */
		if (soju->service && !strcmp (soju->service, "failed"))
			g_string_append_printf (t, _("\n\nThe system's soju service is set to start but fails (%s). "
												 "That one needs root to fix; nexchat's own copy doesn't need it."),
											soju->service_why ? soju->service_why : _("see journalctl -u soju"));
		gtk_box_append (GTK_BOX (s->found_box), action (s, _("_Use soju on this computer"), t->str,
																			 G_CALLBACK (use_local_soju)));
		g_string_free (t, TRUE);
		any = TRUE;
	}

	/* anything answering here that isn't ours: offer it, address filled in */
	for (i = 0; i < f->listening->len; i++)
	{
		bnc_listening *l = f->listening->pdata[i];
		GtkWidget *b;
		char *title, *what;

		if (l->ours || !strcmp (l->kind, "persist"))
			continue;
		title = g_strdup_printf (_("A %s running here, on port %d"), !strcmp (l->kind, "znc") ? "ZNC" : "soju", l->port);
		what = g_strdup (_("Use it with your account on it: the address is filled in for you."));
		b = action (s, title, what, G_CALLBACK (use_listening));
		g_object_set_data (G_OBJECT (b), "port", GINT_TO_POINTER (l->port));
		g_object_set_data (G_OBJECT (b), "tls", GINT_TO_POINTER (l->tls));
		gtk_box_append (GTK_BOX (s->found_box), b);
		g_free (title);
		g_free (what);
		any = TRUE;
	}

	/* ZNC installed, not running: say it's there; setting it up for you is
	   soju's job here (ZNC's own setup asks a lot of questions) */
	if (znc && !any)
		gtk_box_append (GTK_BOX (s->found_box), label (_("ZNC is installed on this computer but isn't running. "
																			 "Start it (znc), then come back here; or install soju and "
																			 "nexchat sets it all up for you."), TRUE));
	else if (!any)
		gtk_box_append (GTK_BOX (s->found_box), label (_("No bouncer on this computer. The easiest: install soju "
																			 "from your system's packages, open this again, and nexchat "
																			 "does the rest."), TRUE));
}

/* ---- Connect ---- */

/* bouncer networks to add once connected: "name host" lines, kept on the
   network until done (bouncer.c sends them after login) */
static void
finish_bouncer (setup *s)
{
	address a;
	ircnet *net = s->net;
	char *srv;
	GString *todo = g_string_new (NULL);
	guint i;

	if (!parse_address (nx_entry_get_text (s->addr), &a))
		return;
	if (!net || net->logintype != LOGIN_PASS)
	{
		/* a new entry; the one it came from stays as it is */
		char *name = g_strdup_printf (_("My bouncer (%s)"), a.host);
		net = servlist_net_find (name, NULL, strcmp);
		if (!net)
		{
			net = servlist_net_add (name, "", TRUE);
			net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
		}
		g_free (name);
	}
	while (net->servlist)
		servlist_server_remove (net, net->servlist->data);
	net->selected = 0;
	srv = g_strdup_printf ("%s/%s%d", a.host, a.tls ? "+" : "", a.port);
	servlist_server_add (net, srv);
	g_free (srv);
	net->flags = (net->flags & ~(FLAG_USE_SSL | FLAG_ALLOW_INVALID | FLAG_USE_GLOBAL)) | FLAG_CYCLE;
	if (a.tls)
		net->flags |= FLAG_USE_SSL;
	if (a.tls && s->cert_bad && gtk_check_button_get_active (GTK_CHECK_BUTTON (s->trust)))
		net->flags |= FLAG_ALLOW_INVALID;
	if (!net->nick)
		net->nick = g_strdup (prefs.hex_irc_nick1);
	g_free (net->user);
	net->user = g_strdup (nx_entry_get_text (s->user));
	g_free (net->pass);
	net->pass = g_strdup (nx_entry_get_text (s->pass));
	net->logintype = LOGIN_PASS;

	/* the networks to put on it */
	for (i = 0; i < s->net_checks->len; i++)
	{
		GtkWidget *b = s->net_checks->pdata[i];
		if (gtk_check_button_get_active (GTK_CHECK_BUTTON (b)))
			g_string_append_printf (todo, "%s %s\n", (char *) g_object_get_data (G_OBJECT (b), "name"),
											(char *) g_object_get_data (G_OBJECT (b), "host"));
	}
	{
		const char *on = nx_entry_get_text (s->other_name), *oh = nx_entry_get_text (s->other_host);
		address oa;
		if (*oh && parse_address (oh, &oa))
		{
			char *name = *on ? g_strdup (on) : g_strdup (oa.host);
			g_strdelimit (name, " ", '-');
			g_string_append_printf (todo, "%s %s:%s%d\n", name, oa.host, oa.tls ? "" : "-", oa.port);
			g_free (name);
			g_free (oa.host);
		}
	}
	bouncer_queue_networks (net, todo->str);
	g_string_free (todo, TRUE);
	servlist_save ();

	if (s->done)
		s->done (net);
	servlist_connect (NULL, net, TRUE);
	g_free (a.host);
}

static void
finish_persist (setup *s)
{
	guint i = gtk_drop_down_get_selected (GTK_DROP_DOWN (s->pnet));
	ircnet *net;

	if (i == GTK_INVALID_LIST_POSITION || !s->pnet_names[i])
		return;
	net = servlist_net_find (s->pnet_names[i], NULL, strcmp);
	if (!net)
		return;
	net->flags &= ~FLAG_USE_GLOBAL;
	g_free (net->nick);
	net->nick = g_strdup (nx_entry_get_text (s->paccount));
	g_free (net->user);
	net->user = g_strdup (nx_entry_get_text (s->paccount));
	g_free (net->pass);
	net->pass = g_strdup (nx_entry_get_text (s->ppass));
	/* always-on is tied to an account: log in with it */
	net->logintype = LOGIN_SASL;
	net->flags |= FLAG_STAY_ONLINE;
	servlist_save ();
	if (s->done)
		s->done (net);
	servlist_connect (NULL, net, TRUE);
}

/* our own soju: the ticked networks are moved (one by one, with what
   happened shown), then each connects through it */
static void
mine_progress (const char *text, gboolean done, gboolean ok, gpointer data)
{
	setup *s = open_setup;
	GSList *nets = data, *l;

	if (s && s == g_object_get_data (G_OBJECT (s->win), "setup"))
	{
		set_status_kind (s->mine_status, done ? (ok ? "ok" : "no") : "wait", text);
		if (!done)
			return;
	}
	else if (!done)
		return;
	/* moved: connect them, through soju now */
	for (l = nets; l; l = l->next)
	{
		ircnet *n = l->data;
		if (mybouncer_owns (n))
			servlist_connect (NULL, n, TRUE);
	}
	if (s && s->done)
		s->done (nets ? nets->data : NULL);
	g_slist_free (nets);
	if (s)
		nx_widget_destroy (s->win);
}

static void
finish_mine (setup *s)
{
	GSList *nets = NULL;
	guint i;

	for (i = 0; i < s->net_checks->len; i++)
	{
		GtkWidget *b = s->net_checks->pdata[i];
		if (gtk_check_button_get_active (GTK_CHECK_BUTTON (b)))
			nets = g_slist_append (nets, g_object_get_data (G_OBJECT (b), "net"));
	}
	if (!nets)
	{
		set_status_kind (s->mine_status, "no", _("Tick at least one network."));
		return;
	}
	gtk_widget_set_sensitive (s->next, FALSE);
	gtk_widget_set_sensitive (s->back, FALSE);
	mybouncer_adopt (nets, mine_progress, g_slist_copy (nets));
	g_slist_free (nets);
}

static void
next_cb (GtkWidget *b, gpointer data)
{
	setup *s = data;

	if (!gtk_widget_get_sensitive (s->next))
		return;
	switch (s->at)
	{
	case STEP_BOUNCER:
		networks_fill (s);
		go (s, STEP_NETWORKS);
		break;
	case STEP_NETWORKS:
		if (s->mine)
		{
			finish_mine (s);
			break;
		}
		finish_bouncer (s);
		nx_widget_destroy (s->win);
		break;
	case STEP_PERSIST:
		finish_persist (s);
		nx_widget_destroy (s->win);
		break;
	default:
		break;
	}
}

/* Enter in a field: on the bouncer page check now (no waiting for the
   pause), and go on when that's already done */
static void
enter_cb (setup *s)
{
	if (s->at == STEP_BOUNCER && s->check_tag)
	{
		g_source_remove (s->check_tag);
		bouncer_check_now (s);
		return;
	}
	next_cb (NULL, s);
}

static void
back_cb (GtkWidget *b, gpointer data)
{
	setup *s = data;
	go (s, s->at == STEP_NETWORKS && !s->mine ? STEP_BOUNCER : STEP_CHOOSE);
	if (s->at == STEP_CHOOSE)
		s->mine = FALSE;
}

static void
choose_cb (GtkWidget *b, gpointer data)
{
	setup *s = data;
	go (s, GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "step")));
	if (s->at == STEP_PERSIST)
		persist_net_changed (NULL, NULL, s);
}

static gboolean
choice_mnemonic (GtkWidget *b, gboolean cycling, gpointer data)
{
	g_signal_emit_by_name (b, "clicked");
	return TRUE;
}

static GtkWidget *
choice (setup *s, const char *title, const char *what, step to)
{
	GtkWidget *b = gtk_button_new ();
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget *t = gtk_label_new_with_mnemonic (title);
	char *m = g_markup_printf_escaped ("<b>%s</b>", gtk_label_get_text (GTK_LABEL (t)));

	gtk_label_set_markup_with_mnemonic (GTK_LABEL (t), title);
	gtk_label_set_xalign (GTK_LABEL (t), 0);
	gtk_widget_add_css_class (t, "heading");
	gtk_box_append (GTK_BOX (box), t);
	gtk_box_append (GTK_BOX (box), label (what, TRUE));
	gtk_button_set_child (GTK_BUTTON (b), box);
	gtk_label_set_mnemonic_widget (GTK_LABEL (t), b);
	/* the label's Alt+key picks it, like a plain button's would */
	g_signal_connect (b, "mnemonic-activate", G_CALLBACK (choice_mnemonic), NULL);
	g_object_set_data (G_OBJECT (b), "step", GINT_TO_POINTER (to));
	g_signal_connect (b, "clicked", G_CALLBACK (choose_cb), s);
	g_free (m);
	return b;
}

static void
setup_free (gpointer data)
{
	setup *s = data;

	if (open_setup == s)
		open_setup = NULL;
	if (s->check_tag)
		g_source_remove (s->check_tag);
	g_free (s->kind);
	g_free (s->pkind);
	g_strfreev (s->pnet_names);
	g_ptr_array_unref (s->net_checks);
	bnc_found_free (s->found);
	g_free (s);
}

/* tests: the found list and the networks page's ticks, one line each */
char *
bncsetup_describe_more (void)
{
	setup *s = open_setup;
	GString *f = g_string_new ("bncfound:"), *n = g_string_new ("bncnets:");
	GtkWidget *c;
	guint i;

	if (!s)
	{
		g_string_free (n, TRUE);
		g_string_append (f, "\nbncnets:");
		return g_string_free (f, FALSE);
	}
	for (c = gtk_widget_get_first_child (s->found_box); c; c = gtk_widget_get_next_sibling (c))
	{
		GtkWidget *w = c;
		if (GTK_IS_BUTTON (c))
			w = gtk_widget_get_first_child (gtk_button_get_child (GTK_BUTTON (c)));
		while (w)
		{
			if (GTK_IS_LABEL (w))
				g_string_append_printf (f, " | %s", gtk_label_get_text (GTK_LABEL (w)));
			w = GTK_IS_BUTTON (c) ? gtk_widget_get_next_sibling (w) : NULL;
		}
	}
	for (i = 0; i < s->net_checks->len; i++)
	{
		GtkWidget *b = s->net_checks->pdata[i];
		GtkWidget *l = gtk_check_button_get_child (GTK_CHECK_BUTTON (b));
		g_string_append_printf (n, " [%s] %s", gtk_check_button_get_active (GTK_CHECK_BUTTON (b)) ? "x" : " ",
										l && GTK_IS_LABEL (l) ? gtk_label_get_text (GTK_LABEL (l)) : "?");
	}
	for (i = 0; i < f->len; i++)
		if (f->str[i] == '\n')
			f->str[i] = ' ';
	g_string_append_c (f, '\n');
	g_string_append (f, n->str);
	g_string_free (n, TRUE);
	return g_string_free (f, FALSE);
}

char *
bncsetup_describe (void)
{
	setup *s = open_setup;
	static const char *const names[] = { "choose", "bouncer", "networks", "persist", "explain" };
	GtkWidget *st;
	char *text, *p, *out;

	if (!s)
		return NULL;
	st = s->at == STEP_PERSIST ? s->pstatus : s->status;
	text = g_strdup (gtk_label_get_text (GTK_LABEL (st)));
	for (p = text; *p; p++)
		if (*p == '\n')
			*p = ' ';
	out = g_strdup_printf ("%s | %s | next=%s", names[s->at], text,
								  gtk_widget_get_visible (s->next) && gtk_widget_get_sensitive (s->next) ? "yes" : "no");
	g_free (text);
	return out;
}

/* the start values from the network it was opened on */
static void
fill_from (setup *s, ircnet *net)
{
	ircserver *sv;
	guint i;

	if (!net || !net->servlist)
		return;
	/* already set up for a bouncer: its address and login */
	if (net->logintype == LOGIN_PASS && net->user && net->pass)
	{
		char *h, *slash;
		gboolean tls;

		sv = g_slist_nth_data (net->servlist, net->selected);
		if (!sv)
			sv = net->servlist->data;
		/* "host/port" or "host/+port": written the way the address field reads
		   it, so a plain-text bouncer isn't taken for TLS */
		slash = strchr (sv->hostname, '/');
		tls = (net->flags & FLAG_USE_SSL) || (slash && slash[1] == '+');
		if (slash)
			h = g_strdup_printf ("%s%.*s:%s", tls ? "" : "irc://", (int) (slash - sv->hostname), sv->hostname,
									 slash + 1 + (slash[1] == '+'));
		else
			h = g_strdup_printf ("%s%s", tls ? "" : "irc://", sv->hostname);
		nx_entry_set_text (s->addr, h);
		g_free (h);
		nx_entry_set_text (s->user, net->user);
		nx_entry_set_text (s->pass, net->pass);
	}
	/* the network page starts on this network */
	for (i = 0; s->pnet_names[i]; i++)
		if (!strcmp (s->pnet_names[i], net->name))
			gtk_drop_down_set_selected (GTK_DROP_DOWN (s->pnet), i);
	if (net->logintype == LOGIN_SASL && net->user)
	{
		nx_entry_set_text (s->paccount, net->user);
		nx_entry_set_text (s->ppass, net->pass ? net->pass : "");
	}
}

void
bncsetup_open (GtkWindow *parent, ircnet *net, void (*done) (ircnet *net))
{
	setup *s;
	GtkWidget *vbox, *p, *grid, *bb, *cancel, *sw;
	GSList *l;
	GPtrArray *names;
	char *t;

	if (open_setup)
	{
		gtk_window_present (GTK_WINDOW (open_setup->win));
		return;
	}
	s = g_new0 (setup, 1);
	s->net = net;
	s->done = done;
	s->net_checks = g_ptr_array_new ();
	s->win = nx_window_new ();
	t = g_strdup_printf (_("Stay online - %s"), _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (s->win), t);
	g_free (t);
	if (parent)
		gtk_window_set_transient_for (GTK_WINDOW (s->win), parent);
	gtk_window_set_modal (GTK_WINDOW (s->win), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (s->win), 560, -1);
	gtk_widget_set_size_request (s->win, 560, -1);
	/* a small window that keeps its size between pages */
	gtk_window_set_resizable (GTK_WINDOW (s->win), FALSE);
	g_object_set_data_full (G_OBJECT (s->win), "setup", s, setup_free);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	nx_set_border_width (vbox, 12);
	gtk_window_set_child (GTK_WINDOW (s->win), vbox);

	s->stack = gtk_stack_new ();
	gtk_stack_set_vhomogeneous (GTK_STACK (s->stack), FALSE);
	gtk_stack_set_hhomogeneous (GTK_STACK (s->stack), FALSE);
	gtk_stack_set_transition_type (GTK_STACK (s->stack), GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_stack_set_interpolate_size (GTK_STACK (s->stack), FALSE);
	gtk_widget_set_vexpand (s->stack, TRUE);
	gtk_box_append (GTK_BOX (vbox), s->stack);

	/* choose */
	p = page (_("Stay in your channels when nexchat is closed"));
	gtk_box_append (GTK_BOX (p), label (_("IRC only gives messages to people who are connected. To get "
														"everything said while your computer is off, something has to "
														"stay connected for you."), TRUE));
	s->found_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_append (GTK_BOX (s->found_box), label (_("Looking for bouncers on this computer..."), TRUE));
	gtk_box_append (GTK_BOX (p), s->found_box);
	gtk_box_append (GTK_BOX (p), choice (s, _("I _have a bouncer"),
		_("On another computer, or one a friend or service runs for you. You'll need its address and your login."),
		STEP_BOUNCER));
	gtk_box_append (GTK_BOX (p), choice (s, _("_Keep me online on my network"),
		_("Some networks can keep you connected themselves (ones running Ergo). Needs an account there."),
		STEP_PERSIST));
	gtk_box_append (GTK_BOX (p), choice (s, _("_What's a bouncer?"),
		_("How it works, and how to get one."), STEP_EXPLAIN));
	gtk_stack_add_named (GTK_STACK (s->stack), p, "choose");

	/* bouncer */
	p = page (_("Your bouncer"));
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	s->addr = entry ("bnc.example.org:6697", FALSE);
	s->user = entry (NULL, FALSE);
	s->pass = entry (NULL, TRUE);
	row (grid, 0, _("_Address:"), s->addr);
	row (grid, 1, _("_User name:"), s->user);
	row (grid, 2, _("_Password:"), s->pass);
	gtk_box_append (GTK_BOX (p), grid);
	gtk_box_append (GTK_BOX (p), label (_("The address is encrypted (TLS) unless you write irc:// in front or use port 6667. "
														"It's checked as you type."), TRUE));
	s->status = label ("", FALSE);
	gtk_label_set_selectable (GTK_LABEL (s->status), TRUE);
	gtk_box_append (GTK_BOX (p), s->status);
	s->trust = gtk_check_button_new_with_mnemonic (_("It's mine, _trust this certificate"));
	gtk_widget_set_visible (s->trust, FALSE);
	gtk_box_append (GTK_BOX (p), s->trust);
	gtk_stack_add_named (GTK_STACK (s->stack), p, "bouncer");
	g_signal_connect (s->addr, "changed", G_CALLBACK (bouncer_changed), s);
	g_signal_connect (s->user, "changed", G_CALLBACK (bouncer_changed), s);
	g_signal_connect (s->pass, "changed", G_CALLBACK (bouncer_changed), s);
	g_signal_connect_swapped (s->addr, "activate", G_CALLBACK (enter_cb), s);
	g_signal_connect_swapped (s->user, "activate", G_CALLBACK (enter_cb), s);
	g_signal_connect_swapped (s->pass, "activate", G_CALLBACK (enter_cb), s);
	g_signal_connect (s->trust, "toggled", G_CALLBACK (trust_toggled), s);

	/* networks */
	p = page (_("Which networks should it stay on?"));
	s->net_intro = label ("", TRUE);
	gtk_box_append (GTK_BOX (p), s->net_intro);
	s->netbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (sw), 220);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), s->netbox);
	gtk_box_append (GTK_BOX (p), sw);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	s->other_name = entry (_("name, e.g. work"), FALSE);
	s->other_host = entry ("irc.example.org", FALSE);
	row (grid, 0, _("Another _one:"), s->other_host);
	row (grid, 1, _("Its na_me:"), s->other_name);
	gtk_box_append (GTK_BOX (p), grid);
	s->other_grid = grid;
	s->mine_status = label ("", FALSE);
	gtk_box_append (GTK_BOX (p), s->mine_status);
	gtk_stack_add_named (GTK_STACK (s->stack), p, "networks");

	/* persist: the networks from the Network List */
	p = page (_("Keep me online on my network"));
	names = g_ptr_array_new ();
	for (l = network_list; l; l = l->next)
		g_ptr_array_add (names, g_strdup (((ircnet *) l->data)->name));
	g_ptr_array_add (names, NULL);
	s->pnet_names = (GStrv) g_ptr_array_free (names, FALSE);
	s->pnet = gtk_drop_down_new_from_strings ((const char * const *) s->pnet_names);
	gtk_drop_down_set_enable_search (GTK_DROP_DOWN (s->pnet), TRUE);
	grid = gtk_grid_new ();
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	s->paccount = entry (NULL, FALSE);
	s->ppass = entry (NULL, TRUE);
	row (grid, 0, _("N_etwork:"), s->pnet);
	row (grid, 1, _("_Your account:"), s->paccount);
	row (grid, 2, _("Pa_ssword:"), s->ppass);
	gtk_box_append (GTK_BOX (p), grid);
	s->pstatus = label ("", FALSE);
	gtk_box_append (GTK_BOX (p), s->pstatus);
	gtk_box_append (GTK_BOX (p), label (_("No account yet? Connect to the network and type "
														"/msg NickServ REGISTER password email"), TRUE));
	gtk_stack_add_named (GTK_STACK (s->stack), p, "persist");
	g_signal_connect (s->pnet, "notify::selected", G_CALLBACK (persist_net_changed), s);
	g_signal_connect (s->paccount, "changed", G_CALLBACK (persist_entry_changed), s);
	g_signal_connect (s->ppass, "changed", G_CALLBACK (persist_entry_changed), s);
	g_signal_connect_swapped (s->paccount, "activate", G_CALLBACK (enter_cb), s);
	g_signal_connect_swapped (s->ppass, "activate", G_CALLBACK (enter_cb), s);

	/* explain */
	p = page (_("What's a bouncer?"));
	gtk_box_append (GTK_BOX (p), label (_(
		"IRC only gives messages to people who are connected right then. When your computer "
		"is off or asleep, you're not in your channels, and what's said there is gone for you.\n\n"
		"A <b>bouncer</b> is a small program on a computer that's always on: a cheap server, a "
		"Raspberry Pi, a home router. It stays in your channels for you and keeps what you "
		"missed. nexchat connects to the bouncer instead of the network, and you get it all "
		"when you come back, also on your phone.\n\n"
		"<b>soju</b> is the easiest to run, <b>ZNC</b> the most used. Some communities and "
		"services give out bouncer accounts. And some networks (ones running Ergo) can keep you "
		"online themselves, with just an account."), FALSE));
	gtk_box_append (GTK_BOX (p), label (_("<a href=\"https://soju.im\">soju.im</a>  ·  "
														"<a href=\"https://wiki.znc.in\">wiki.znc.in</a>"), FALSE));
	gtk_stack_add_named (GTK_STACK (s->stack), p, "explain");

	/* buttons */
	bb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	s->back = gtk_button_new_with_mnemonic (_("_Back"));
	cancel = gtk_button_new_with_mnemonic (_("_Cancel"));
	s->next = gtk_button_new_with_mnemonic (_("_Next"));
	gtk_widget_set_hexpand (cancel, TRUE);
	gtk_widget_set_halign (cancel, GTK_ALIGN_END);
	gtk_box_append (GTK_BOX (bb), s->back);
	gtk_box_append (GTK_BOX (bb), cancel);
	gtk_box_append (GTK_BOX (bb), s->next);
	gtk_box_append (GTK_BOX (vbox), bb);
	gtk_window_set_default_widget (GTK_WINDOW (s->win), s->next);
	g_signal_connect (s->back, "clicked", G_CALLBACK (back_cb), s);
	g_signal_connect (s->next, "clicked", G_CALLBACK (next_cb), s);
	g_signal_connect_swapped (cancel, "clicked", G_CALLBACK (gtk_window_destroy), s->win);

	open_setup = s;
	fill_from (s, net);
	bncdetect_run (detected, s);
	/* already on a bouncer elsewhere: straight to its page. On ours: the
	   first page, which says nexchat runs it and offers more networks */
	go (s, net && net->logintype == LOGIN_PASS && net->user && net->pass && !mybouncer_owns (net) ?
		 STEP_BOUNCER : STEP_CHOOSE);
	gtk_window_present (GTK_WINDOW (s->win));
}
