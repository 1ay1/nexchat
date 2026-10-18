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

#include <glib/gi18n.h>

#include "hexchat.h"
#include "bouncer.h"
#include "fe.h"
#include "server.h"
#include "text.h"
#include "util.h"
#include "hexchatc.h"
#include "servlist.h"

typedef struct
{
	bnc_kind kind;
	gboolean soju_cap;		/* soju.im/bouncer-networks acked */
	gboolean soju_notify;	/* ... -notify acked */
	gboolean persist_cap;	/* draft/persistence acked */
	int persist_client;		/* -1 unknown, 0/1 what we asked for */
	int persist_effective;	/* -1 unknown, 0/1 what the network does */
	char *bound;			/* soju netid this connection is on (BOUNCER_NETID) */
	GPtrArray *networks;	/* bnc_network* */
	/* ZNC: parsing a *status ListNetworks table */
	gboolean znc_listing;
	GPtrArray *znc_new;
	guint znc_quiet;		/* replies to our own *controlpanel lines to swallow */
	char *znc_error;	/* the first "Error: ..." among them */
	/* what we asked the bouncer to do, to tell the user how it went */
	char *pending;
} bnc_state;

static GHashTable *states;	/* server -> bnc_state */

static void
network_free (gpointer p)
{
	bnc_network *n = p;
	g_free (n->id);
	g_free (n->name);
	g_free (n->host);
	g_free (n->nick);
	g_free (n->state);
	g_free (n->error);
	g_free (n);
}

static void
state_free (gpointer p)
{
	bnc_state *s = p;
	g_free (s->bound);
	if (s->networks)
		g_ptr_array_unref (s->networks);
	if (s->znc_new)
		g_ptr_array_unref (s->znc_new);
	g_free (s->pending);
	g_free (s->znc_error);
	g_free (s);
}

static bnc_state *
state (server *serv)
{
	bnc_state *s;

	if (!states)
		states = g_hash_table_new_full (NULL, NULL, NULL, state_free);
	s = g_hash_table_lookup (states, serv);
	if (!s)
	{
		s = g_new0 (bnc_state, 1);
		s->persist_client = s->persist_effective = -1;
		s->networks = g_ptr_array_new_with_free_func (network_free);
		g_hash_table_insert (states, serv, s);
	}
	return s;
}

static void
changed (server *serv)
{
	fe_bouncer_changed (serv);
}

void
bouncer_reset (server *serv)
{
	if (states)
		g_hash_table_remove (states, serv);
	changed (serv);
}

bnc_kind
bouncer_kind (server *serv)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	return s ? s->kind : BNC_NONE;
}

const char *
bouncer_kind_name (bnc_kind k)
{
	switch (k)
	{
	case BNC_SOJU: return "soju";
	case BNC_ZNC: return "ZNC";
	case BNC_PERSIST: return _("the network itself");
	default: return "";
	}
}

GPtrArray *
bouncer_networks (server *serv)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	return s && (s->kind == BNC_SOJU || s->kind == BNC_ZNC) ? s->networks : NULL;
}

int
bouncer_persist_state (server *serv)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	return s ? s->persist_effective : -1;
}

static void
set_kind (server *serv, bnc_kind k)
{
	bnc_state *s = state (serv);
	/* a bouncer wins over what the network behind it offers */
	if (s->kind == BNC_NONE || (k != BNC_PERSIST && s->kind == BNC_PERSIST))
	{
		s->kind = k;
		changed (serv);
	}
}

/* ---- detection ---- */

void
bouncer_cap_ls (server *serv, const char *cap, const char *value, char *buffer, int buflen, gboolean *want)
{
	if (!strcmp (cap, "soju.im/bouncer-networks") || !strcmp (cap, "soju.im/bouncer-networks-notify") ||
		 !strcmp (cap, "draft/persistence"))
	{
		g_strlcat (buffer, cap, buflen);
		g_strlcat (buffer, " ", buflen);
		*want = TRUE;
	}
	/* ZNC's caps aren't enough: Ergo offers znc.in/self-message and
	   znc.in/playback too. ZNC is told by its name (irc.znc.in). */
	(void) value;
}

void
bouncer_cap_toggle (server *serv, const char *cap, gboolean enable)
{
	bnc_state *s;

	if (strcmp (cap, "soju.im/bouncer-networks") && strcmp (cap, "soju.im/bouncer-networks-notify") &&
		 strcmp (cap, "draft/persistence"))
		return;
	s = state (serv);
	if (!strcmp (cap, "soju.im/bouncer-networks"))
	{
		s->soju_cap = enable;
		if (enable)
			set_kind (serv, BNC_SOJU);
	}
	else if (!strcmp (cap, "soju.im/bouncer-networks-notify"))
		s->soju_notify = enable;
	else
	{
		s->persist_cap = enable;
		if (enable)
			set_kind (serv, BNC_PERSIST);
	}
}

void
bouncer_server_name (server *serv, const char *name)
{
	if (name && !g_ascii_strcasecmp (name, "irc.znc.in"))
		set_kind (serv, BNC_ZNC);
}

void
bouncer_isupport (server *serv, const char *token)
{
	if (g_str_has_prefix (token, "BOUNCER_NETID="))
	{
		bnc_state *s = state (serv);
		g_free (s->bound);
		s->bound = g_strdup (token + 14);
		set_kind (serv, BNC_SOJU);
	}
}

void
bouncer_login_end (server *serv)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	if (!s)
		return;
	/* without -notify the list doesn't come by itself */
	if (s->soju_cap && !s->soju_notify)
		tcp_sendf (serv, "BOUNCER LISTNETWORKS\r\n");
	if (s->kind == BNC_ZNC)
		bouncer_refresh (serv);
	if (s->persist_cap && s->persist_effective < 0)
		tcp_sendf (serv, "PERSISTENCE GET\r\n");
	/* set up with "Stay online": turn it on once we're logged in */
	if (s->persist_cap && serv->network && (((ircnet *) serv->network)->flags & FLAG_STAY_ONLINE))
	{
		char *err = NULL;
		if (!bouncer_set_persist (serv, TRUE, &err))
			g_free (err);
	}
}

/* ---- soju: BOUNCER NETWORK <id> <attrs> ---- */

static bnc_network *
find_net (bnc_state *s, const char *id)
{
	guint i;
	for (i = 0; i < s->networks->len; i++)
	{
		bnc_network *n = s->networks->pdata[i];
		if (!strcmp (n->id, id))
			return n;
	}
	return NULL;
}

/* message-tag style "a=b;c=d", values escaped like tags */
static void
apply_attrs (bnc_network *n, const char *attrs)
{
	char **kv = g_strsplit (attrs, ";", 0);
	int i;

	for (i = 0; kv[i]; i++)
	{
		char *k = kv[i], *v = strchr (k, '=');
		GString *val = g_string_new (NULL);
		const char *p;

		if (v)
			*v++ = 0;
		for (p = v ? v : ""; *p; p++)
		{
			if (*p != '\\' || !p[1])
			{
				g_string_append_c (val, *p);
				continue;
			}
			p++;
			switch (*p)
			{
			case ':': g_string_append_c (val, ';'); break;
			case 's': g_string_append_c (val, ' '); break;
			case 'r': g_string_append_c (val, '\r'); break;
			case 'n': g_string_append_c (val, '\n'); break;
			default: g_string_append_c (val, *p); break;
			}
		}
#define SET(field) do { g_free (n->field); n->field = val->len ? g_strdup (val->str) : NULL; } while (0)
		if (!strcmp (k, "name")) SET (name);
		else if (!strcmp (k, "host")) SET (host);
		else if (!strcmp (k, "nickname")) SET (nick);
		else if (!strcmp (k, "state")) SET (state);
		else if (!strcmp (k, "error")) SET (error);
		else if (!strcmp (k, "port")) n->port = atoi (val->str);
		else if (!strcmp (k, "tls")) n->tls = strcmp (val->str, "0") != 0;
#undef SET
		g_string_free (val, TRUE);
	}
	g_strfreev (kv);
	/* back up: the last error doesn't apply any more */
	if (n->state && !strcmp (n->state, "connected"))
		g_clear_pointer (&n->error, g_free);
}

void
bouncer_command (server *serv, const char *sub, const char *arg, const char *attrs)
{
	bnc_state *s = state (serv);

	if (!g_ascii_strcasecmp (sub, "NETWORK") && *arg)
	{
		bnc_network *n = find_net (s, arg);

		if (!strcmp (attrs, "*"))
		{
			if (n)
				g_ptr_array_remove (s->networks, n);
		}
		else
		{
			if (!n)
			{
				n = g_new0 (bnc_network, 1);
				n->id = g_strdup (arg);
				n->tls = TRUE;
				g_ptr_array_add (s->networks, n);
			}
			apply_attrs (n, attrs);
		}
		set_kind (serv, BNC_SOJU);
		changed (serv);
	}
	else if (!g_ascii_strcasecmp (sub, "ADDNETWORK") || !g_ascii_strcasecmp (sub, "DELNETWORK") ||
				!g_ascii_strcasecmp (sub, "CHANGENETWORK"))
	{
		if (s->pending)
		{
			PrintTextf (serv->server_session, "%s\n", s->pending);
			g_clear_pointer (&s->pending, g_free);
		}
		/* without -notify, list again to see the change */
		if (!s->soju_notify)
			tcp_sendf (serv, "BOUNCER LISTNETWORKS\r\n");
		changed (serv);
	}
}

gboolean
bouncer_fail (server *serv, char *word[], const char *text)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	if (!s || (g_ascii_strcasecmp (word[3], "BOUNCER") && g_ascii_strcasecmp (word[3], "PERSISTENCE")))
		return FALSE;
	PrintTextf (serv->server_session, _("\00304*\tThe bouncer said no: %s\n"), text);
	g_clear_pointer (&s->pending, g_free);
	return TRUE;
}

/* ---- draft/persistence: PERSISTENCE STATUS <client> <effective> ---- */

static int
on_off (const char *v)
{
	if (!g_ascii_strcasecmp (v, "ON"))
		return 1;
	if (!g_ascii_strcasecmp (v, "OFF"))
		return 0;
	return -1;
}

void
bouncer_persistence (server *serv, const char *sub, const char *client, const char *effective)
{
	bnc_state *s = state (serv);

	if (g_ascii_strcasecmp (sub, "STATUS"))
		return;
	s->persist_client = on_off (client);
	s->persist_effective = on_off (effective);
	set_kind (serv, BNC_PERSIST);
	if (s->pending)
	{
		PrintTextf (serv->server_session, "%s\n", s->pending);
		g_clear_pointer (&s->pending, g_free);
	}
	changed (serv);
}

/* ---- ZNC: *status ListNetworks, *controlpanel ---- */

/* "| ergo    | Yes    | irc.libera.chat | ayush | 3 |" -> fields */
static char **
table_row (const char *text)
{
	char **cells, **out;
	int i, n = 0;

	if (text[0] != '|')
		return NULL;
	cells = g_strsplit (text + 1, "|", 0);
	out = g_new0 (char *, g_strv_length (cells) + 1);
	for (i = 0; cells[i]; i++)
	{
		char *c = g_strstrip (cells[i]);
		if (!cells[i + 1] && !*c)
			break;	/* after the last | */
		out[n++] = g_strdup (c);
	}
	g_strfreev (cells);
	return out;
}

gboolean
bouncer_znc_reply (server *serv, const char *from, const char *text)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	char **row;

	if (!s || s->kind != BNC_ZNC)
		return FALSE;

	if (!g_ascii_strcasecmp (from, "*controlpanel"))
	{
		if (!s->znc_quiet)
			return FALSE;
		s->znc_quiet--;
		if (!s->znc_error && g_str_has_prefix (text, "Error: "))
			s->znc_error = g_strdup (text + 7);
		/* the user asked for this: say how it went, once, in plain words */
		if (!s->znc_quiet)
		{
			if (s->znc_error)
				PrintTextf (serv->server_session, _("\00304*\tZNC said no: %s\n"), s->znc_error);
			else if (s->pending)
				PrintTextf (serv->server_session, "%s\n", s->pending);
			g_clear_pointer (&s->pending, g_free);
			g_clear_pointer (&s->znc_error, g_free);
			bouncer_refresh (serv);
		}
		return TRUE;
	}

	if (g_ascii_strcasecmp (from, "*status") || !s->znc_listing)
		return FALSE;

	/* the borders, and the header row */
	if (text[0] == '+')
	{
		/* the border after the rows: the table is done */
		if (s->znc_new)
		{
			g_ptr_array_unref (s->networks);
			s->networks = s->znc_new;
			s->znc_new = NULL;
			s->znc_listing = FALSE;
			changed (serv);
		}
		return TRUE;
	}
	if (!strcmp (text, "You have no networks"))
	{
		g_ptr_array_set_size (s->networks, 0);
		s->znc_listing = FALSE;
		changed (serv);
		return TRUE;
	}
	row = table_row (text);
	if (!row)
		return FALSE;
	if (row[0] && strcmp (row[0], "Network"))
	{
		bnc_network *n = g_new0 (bnc_network, 1);
		n->id = g_strdup (row[0]);
		n->name = g_strdup (row[0]);
		n->state = g_strdup (row[1] && !g_ascii_strcasecmp (row[1], "Yes") ? "connected" : "disconnected");
		n->host = row[1] && row[2] && *row[2] ? g_strdup (row[2]) : NULL;
		n->nick = row[1] && row[2] && row[3] && *row[3] ? g_strdup (row[3]) : NULL;
		if (!s->znc_new)
			s->znc_new = g_ptr_array_new_with_free_func (network_free);
		g_ptr_array_add (s->znc_new, n);
	}
	g_strfreev (row);
	return TRUE;
}

void
bouncer_refresh (server *serv)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	if (!s || !serv->connected)
		return;
	if (s->kind == BNC_SOJU && s->soju_cap)
		tcp_sendf (serv, "BOUNCER LISTNETWORKS\r\n");
	else if (s->kind == BNC_ZNC)
	{
		s->znc_listing = TRUE;
		g_clear_pointer (&s->znc_new, g_ptr_array_unref);
		tcp_sendf (serv, "PRIVMSG *status :ListNetworks\r\n");
	}
	else if (s->persist_cap)
		tcp_sendf (serv, "PERSISTENCE GET\r\n");
}

/* ---- changing things ---- */

/* a value for "name=value;..." (message-tag escaping) */
static void
attr (GString *out, const char *key, const char *val)
{
	const char *p;

	if (!val || !*val)
		return;
	if (out->len)
		g_string_append_c (out, ';');
	g_string_append_printf (out, "%s=", key);
	for (p = val; *p; p++)
	{
		switch (*p)
		{
		case ';': g_string_append (out, "\\:"); break;
		case ' ': g_string_append (out, "\\s"); break;
		case '\\': g_string_append (out, "\\\\"); break;
		case '\r': g_string_append (out, "\\r"); break;
		case '\n': g_string_append (out, "\\n"); break;
		default: g_string_append_c (out, *p);
		}
	}
}

/* names and hosts go into one IRC line word: nothing that breaks it */
static gboolean
plain_word (const char *s)
{
	return s && *s && !strpbrk (s, " \r\n:;");
}

gboolean
bouncer_add_network (server *serv, const char *name, const char *host, int port,
							gboolean tls, const char *nick, char **err)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	*err = NULL;
	if (!s || !serv->connected || (s->kind != BNC_SOJU && s->kind != BNC_ZNC))
	{
		*err = g_strdup (_("Not connected to a bouncer."));
		return FALSE;
	}
	if (!plain_word (host) || (s->kind == BNC_ZNC && !plain_word (name)))
	{
		*err = g_strdup (_("The name and address can't have spaces, \":\" or \";\" in them."));
		return FALSE;
	}
	if (port <= 0 || port > 65535)
		port = tls ? 6697 : 6667;

	g_free (s->pending);
	s->pending = g_strdup_printf (_("\00303*\t%s added: the bouncer is connecting to it now.\n"), name && *name ? name : host);
	if (s->kind == BNC_SOJU)
	{
		GString *a = g_string_new (NULL);
		char *p = g_strdup_printf ("%d", port);
		attr (a, "name", name);
		attr (a, "host", host);
		attr (a, "port", p);
		attr (a, "tls", tls ? "1" : "0");
		attr (a, "nickname", nick);
		tcp_sendf (serv, "BOUNCER ADDNETWORK %s\r\n", a->str);
		g_string_free (a, TRUE);
		g_free (p);
	}
	else
	{
		const char *user = serv->network && ((ircnet *) serv->network)->user ?
								 ((ircnet *) serv->network)->user : prefs.hex_irc_user_name;
		char *u = g_strndup (user, strcspn (user, "/@"));
		/* $me works in newer ZNC; our own login name works everywhere */
		tcp_sendf (serv, "PRIVMSG *controlpanel :AddNetwork %s %s\r\n", u, name);
		tcp_sendf (serv, "PRIVMSG *controlpanel :AddServer %s %s %s %s%d\r\n", u, name, host, tls ? "+" : "", port);
		s->znc_quiet = 2;
		if (nick && *nick)
		{
			tcp_sendf (serv, "PRIVMSG *controlpanel :SetNetwork Nick %s %s %s\r\n", u, name, nick);
			s->znc_quiet++;
		}
		g_free (u);
	}
	return TRUE;
}

gboolean
bouncer_del_network (server *serv, const char *id, char **err)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	*err = NULL;
	if (!s || !serv->connected || (s->kind != BNC_SOJU && s->kind != BNC_ZNC))
	{
		*err = g_strdup (_("Not connected to a bouncer."));
		return FALSE;
	}
	if (!plain_word (id))
	{
		*err = g_strdup (_("No such network on the bouncer."));
		return FALSE;
	}
	g_free (s->pending);
	s->pending = g_strdup_printf (_("\00303*\tRemoved from the bouncer.\n"));
	if (s->kind == BNC_SOJU)
		tcp_sendf (serv, "BOUNCER DELNETWORK %s\r\n", id);
	else
	{
		const char *user = serv->network && ((ircnet *) serv->network)->user ?
								 ((ircnet *) serv->network)->user : prefs.hex_irc_user_name;
		char *u = g_strndup (user, strcspn (user, "/@"));
		tcp_sendf (serv, "PRIVMSG *controlpanel :DelNetwork %s %s\r\n", u, id);
		s->znc_quiet = 1;
		g_free (u);
	}
	return TRUE;
}

gboolean
bouncer_set_persist (server *serv, gboolean on, char **err)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	*err = NULL;
	if (!s || !s->persist_cap || !serv->connected)
	{
		*err = g_strdup (_("This network can't keep you online by itself."));
		return FALSE;
	}
	g_free (s->pending);
	s->pending = g_strdup (on ? _("\00303*\tThe network will keep you online now, also while nexchat is closed.")
							  : _("\00303*\tThe network won't keep you online after you quit any more."));
	tcp_sendf (serv, "PERSISTENCE SET %s\r\n", on ? "ON" : "OFF");
	return TRUE;
}

char *
bouncer_describe (server *serv)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	if (!s || s->kind == BNC_NONE)
		return NULL;
	if (s->kind == BNC_PERSIST)
	{
		if (s->persist_effective == 1)
			return g_strdup (_("Online 24/7: this network keeps you connected"));
		return g_strdup (_("This network can keep you online 24/7 (off)"));
	}
	if (s->networks->len)
		return g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "Online 24/7 via %s (%d network)",
														 "Online 24/7 via %s (%d networks)", s->networks->len),
										bouncer_kind_name (s->kind), s->networks->len);
	return g_strdup_printf (_("Online 24/7 via %s"), bouncer_kind_name (s->kind));
}

/* ---- /BOUNCER ---- */

static void
print_list (session *sess, server *serv)
{
	bnc_state *s = state (serv);
	guint i;

	if (s->kind == BNC_PERSIST || (s->kind == BNC_NONE && s->persist_cap))
	{
		PrintTextf (sess, "%s\n", s->persist_effective == 1 ?
						_("You stay online on this network even when nexchat is closed.") :
						_("This network can keep you online when nexchat is closed. Turn it on with /bouncer on"));
		return;
	}
	if (s->kind == BNC_NONE)
	{
		PrintText (sess, _("This is a plain IRC server: when nexchat is closed, you're not in your channels and miss what's said.\n"
								 "To stay online all the time, use a bouncer (soju or ZNC) and connect to it instead. "
								 "Network List > Edit > \"Stay online\" sets it up.\n"));
		return;
	}
	PrintTextf (sess, _("Connected through %s. Its networks:\n"), bouncer_kind_name (s->kind));
	if (!s->networks->len)
		PrintText (sess, _("  (none yet: add one with /bouncer add <name> <host>[:port])\n"));
	for (i = 0; i < s->networks->len; i++)
	{
		bnc_network *n = s->networks->pdata[i];
		gboolean here = s->bound && !strcmp (s->bound, n->id);
		char *addr = n->port ? g_strdup_printf ("%s:%d", n->host ? n->host : "", n->port)
							  : g_strdup (n->host ? n->host : "");
		PrintTextf (sess, "  %s%s  %s  %s%s%s\n",
						n->name ? n->name : n->id, here ? _(" (this tab)") : "", addr,
						n->state ? n->state : "",
						n->error ? " - " : "", n->error ? n->error : "");
		g_free (addr);
	}
}

int
bouncer_cmd (session *sess, char *word[], char *word_eol[])
{
	server *serv = sess->server;
	char *err = NULL;
	gboolean ok = TRUE;

	if (!serv || !serv->connected)
	{
		PrintText (sess, _("Not connected.\n"));
		return TRUE;
	}
	if (!*word[2] || !g_ascii_strcasecmp (word[2], "LIST"))
	{
		print_list (sess, serv);
		bouncer_refresh (serv);
	}
	else if (!g_ascii_strcasecmp (word[2], "ADD") && *word[3] && *word[4])
	{
		/* /bouncer add <name> <host>[:port] [-notls] [nick] */
		char *host = g_strdup (word[4]), *colon = strrchr (host, ':');
		int port = 0;
		gboolean tls = TRUE;
		const char *nick = NULL;
		int i;

		if (colon)
		{
			*colon = 0;
			port = atoi (colon + 1);
		}
		for (i = 5; *word[i]; i++)
		{
			if (!g_ascii_strcasecmp (word[i], "-notls"))
				tls = FALSE;
			else
				nick = word[i];
		}
		if (port == 6667 && i == 5)
			tls = FALSE;
		ok = bouncer_add_network (serv, word[3], host, port, tls, nick, &err);
		g_free (host);
	}
	else if ((!g_ascii_strcasecmp (word[2], "DEL") || !g_ascii_strcasecmp (word[2], "REMOVE")) && *word[3])
	{
		bnc_state *s = state (serv);
		const char *id = word[3];
		guint i;
		/* by name too */
		for (i = 0; i < s->networks->len; i++)
		{
			bnc_network *n = s->networks->pdata[i];
			if (n->name && !g_ascii_strcasecmp (n->name, word[3]))
				id = n->id;
		}
		ok = bouncer_del_network (serv, id, &err);
	}
	else if (!g_ascii_strcasecmp (word[2], "ON") || !g_ascii_strcasecmp (word[2], "OFF"))
		ok = bouncer_set_persist (serv, !g_ascii_strcasecmp (word[2], "ON"), &err);
	else
		return FALSE;

	if (!ok)
	{
		PrintTextf (sess, "%s\n", err);
		g_free (err);
	}
	return TRUE;
}
