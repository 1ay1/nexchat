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
#include <stdlib.h>

#include <glib/gi18n.h>

#include "hexchat.h"
#include "bouncer.h"
#include "mybouncer.h"
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
	guint open_tag;		/* opening its networks, once the list has settled */
	GPtrArray *networks;	/* bnc_network* */
	/* ZNC: parsing a *status ListNetworks table */
	gboolean znc_listing;
	GPtrArray *znc_new;
	guint znc_quiet;		/* replies to our own *controlpanel lines to swallow */
	char *znc_error;	/* the first "Error: ..." among them */
	/* what we asked the bouncer to do, to tell the user how it went */
	char *pending;
	/* soju.im/search in progress: the (lastlog) window, targets still to
	   answer (in the order asked), the batch open now, matches so far */
	gboolean search_cap;
	session *search_out;
	GQueue search_targets;
	char *search_batch;
	char *search_text;
	int search_hits, search_here;
} bnc_state;

static GHashTable *states;	/* server -> bnc_state */

static void search_finish (bnc_state *s);

/* ircnet -> why its last login failed */
static GHashTable *last_errors;

/* ircnet -> "name host\n..." still to add on that bouncer */
static GHashTable *queued;

void
bouncer_queue_networks (ircnet *net, const char *lines)
{
	if (!queued)
		queued = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	if (lines && *lines)
		g_hash_table_insert (queued, net, g_strdup (lines));
	else
		g_hash_table_remove (queued, net);
}

static gboolean bnc_has_network (bnc_state *s, const char *name);
static gboolean bouncer_add_network_q (server *serv, const char *name, const char *host, int port,
													gboolean tls, char **err);

/* the bouncer's list is in: add the next queued network it doesn't have.
   One at a time, so each answer is matched to its request. */
static void
queue_next (server *serv, bnc_state *s)
{
	char *lines, **l;
	int i;
	GString *rest;
	gboolean sent = FALSE;

	if (!queued || !serv->network || s->pending || s->znc_quiet)
		return;
	lines = g_hash_table_lookup (queued, serv->network);
	if (!lines)
		return;
	l = g_strsplit (lines, "\n", 0);
	rest = g_string_new (NULL);
	for (i = 0; l[i]; i++)
	{
		char *name = l[i], *host, *colon, *err = NULL;
		int port = 0;
		gboolean tls = TRUE;

		if (!*name)
			continue;
		if (sent)
		{
			g_string_append_printf (rest, "%s\n", name);
			continue;
		}
		host = strchr (name, ' ');
		if (!host)
			continue;
		*host++ = 0;
		if (bnc_has_network (s, name))
			continue;
		colon = strrchr (host, ':');
		if (colon)
		{
			*colon = 0;
			if (colon[1] == '-')
				tls = FALSE, colon++;
			port = atoi (colon + 1);
		}
		if (bouncer_add_network_q (serv, name, host, port, tls, &err))
			sent = TRUE;
		g_free (err);
	}
	if (rest->len)
		g_hash_table_insert (queued, serv->network, g_string_free (rest, FALSE));
	else
	{
		g_hash_table_remove (queued, serv->network);
		g_string_free (rest, TRUE);
	}
	g_strfreev (l);
}

static void
network_free (gpointer p)
{
	bnc_network *n = p;
	g_free (n->id);
	g_free (n->name);
	g_free (n->host);
	g_free (n->nick);
	g_free (n->username);
	g_free (n->realname);
	g_free (n->state);
	g_free (n->error);
	g_free (n);
}

static void
state_free (gpointer p)
{
	bnc_state *s = p;
	g_free (s->bound);
	if (s->open_tag)
		g_source_remove (s->open_tag);
	if (s->networks)
		g_ptr_array_unref (s->networks);
	if (s->znc_new)
		g_ptr_array_unref (s->znc_new);
	g_free (s->pending);
	g_free (s->znc_error);
	g_queue_clear_full (&s->search_targets, g_free);
	g_free (s->search_batch);
	g_free (s->search_text);
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

/* ---- the bouncer's networks in the Network List ----
   Each network the bouncer has gets an entry "<bouncer entry>/<name>":
   the same address and account, with "/<name>" on the user name so
   soju and ZNC put the connection on that network. Entries made this way
   carry the bouncer entry's name in front, so they're found again (and
   removed when the network goes away on the bouncer). */

static char *
sync_name (ircnet *base, const char *netname)
{
	return g_strdup_printf ("%s/%s", base->name, netname);
}

/* a tab (connected or not) is on this entry */
static gboolean
has_tab (ircnet *net)
{
	GSList *l;
	for (l = serv_list; l; l = l->next)
		if (((server *) l->data)->network == net)
			return TRUE;
	return FALSE;
}

/* a server is connected (or connecting) with this entry */
static gboolean
is_in_use (ircnet *net)
{
	GSList *l;
	for (l = serv_list; l; l = l->next)
	{
		server *sv = l->data;
		if (sv->network == net && (sv->connected || sv->connecting))
			return TRUE;
	}
	return FALSE;
}

/* an entry is going: tabs that still point at it let go, and don't come
   back on their own (soju closed them because the network was removed) */
static void
release_entry (ircnet *net)
{
	GSList *l;
	for (l = serv_list; l; l = l->next)
	{
		server *sv = l->data;
		if (sv->network == net)
		{
			if (sv->recondelay_tag)
			{
				fe_timeout_remove (sv->recondelay_tag);
				sv->recondelay_tag = 0;
			}
			sv->network = NULL;
		}
	}
}

/* the bouncer's own entry: the one we connected with, minus any /network */
static ircnet *
sync_base (server *serv)
{
	ircnet *net = serv->network;
	GSList *l;
	char *slash;

	if (!net || !net->user)
		return NULL;
	slash = strchr (net->user, '/');
	if (!slash)
		return net;
	/* connected to one network of it: find the entry without /network */
	for (l = network_list; l; l = l->next)
	{
		ircnet *n = l->data;
		if (n->user && strlen (n->user) == (size_t) (slash - net->user) &&
			 !strncmp (n->user, net->user, slash - net->user) && n->servlist && net->servlist &&
			 !g_ascii_strcasecmp (((ircserver *) n->servlist->data)->hostname,
									 ((ircserver *) net->servlist->data)->hostname))
			return n;
	}
	return NULL;
}

/* the bouncer's own connection (soju, no network picked) can't join
   anything: each of its networks opens in a tab of its own, like soju's
   own clients do. Ones already open aren't opened again; ones added
   later open when they come. */
static gboolean
open_networks_now (gpointer data)
{
	server *serv = data;
	bnc_state *s;
	ircnet *base;
	guint i;
	int opened = 0;

	if (!is_server (serv) || !states || !(s = g_hash_table_lookup (states, serv)))
		return G_SOURCE_REMOVE;
	s->open_tag = 0;
	base = serv->network;
	if (!base || s->kind != BNC_SOJU || s->bound)
		return G_SOURCE_REMOVE;
	for (i = 0; i < s->networks->len; i++)
	{
		bnc_network *n = s->networks->pdata[i];
		char *name = sync_name (base, n->name ? n->name : n->id);
		ircnet *e = servlist_net_find (name, NULL, strcmp);

		if (e && !has_tab (e))
		{
			/* in the background: you stay where you were typing */
			session *bg = new_ircwindow (NULL, NULL, SESS_SERVER, FALSE);
			servlist_connect (bg, e, TRUE);
			opened++;
		}
		g_free (name);
	}
	if (opened)
		PrintTextf (serv->server_session, g_dngettext (GETTEXT_PACKAGE,
						"\00303*\tThis tab is soju itself; its network opened in a tab of its own.\n",
						"\00303*\tThis tab is soju itself; its %d networks opened in tabs of their own.\n",
						opened), opened);
	return G_SOURCE_REMOVE;
}

static void
open_networks (server *serv, ircnet *base)
{
	bnc_state *s = g_hash_table_lookup (states, serv);

	if (!s || s->kind != BNC_SOJU || s->bound || serv->network != base || !s->networks->len)
		return;
	if (s->open_tag)
		g_source_remove (s->open_tag);
	s->open_tag = g_timeout_add (700, open_networks_now, serv);
}

/* an entry of a network on the bouncer: same address, TLS, password and
   login as the bouncer's own. TRUE if anything changed */
static gboolean
sync_from_base (ircnet *e, ircnet *base)
{
	const guint32 keep = FLAG_USE_SSL | FLAG_ALLOW_INVALID | FLAG_USE_PROXY | FLAG_CYCLE;
	gboolean dirty = FALSE, same = TRUE;
	GSList *a, *b;

	for (a = e->servlist, b = base->servlist; a && b; a = a->next, b = b->next)
		if (g_ascii_strcasecmp (((ircserver *) a->data)->hostname, ((ircserver *) b->data)->hostname))
			same = FALSE;
	if (!same || a || b)
	{
		while (e->servlist)
			servlist_server_remove (e, e->servlist->data);
		for (b = base->servlist; b; b = b->next)
			servlist_server_add (e, ((ircserver *) b->data)->hostname);
		e->selected = base->selected;
		dirty = TRUE;
	}
	if ((e->flags & keep) != (base->flags & keep))
	{
		e->flags = (e->flags & ~keep) | (base->flags & keep);
		dirty = TRUE;
	}
	if (g_strcmp0 (e->pass, base->pass))
	{
		g_free (e->pass);
		e->pass = g_strdup (base->pass);
		dirty = TRUE;
	}
	if (e->logintype != base->logintype)
	{
		e->logintype = base->logintype;
		dirty = TRUE;
	}
	return dirty;
}

static void
sync_networks (server *serv)
{
	bnc_state *s = g_hash_table_lookup (states, serv);
	ircnet *base = sync_base (serv);
	char *prefix;
	GSList *l, *next, *moved = NULL;
	guint i;
	gboolean dirty = FALSE;

	if (!s || !base || !base->servlist || (s->kind != BNC_SOJU && s->kind != BNC_ZNC))
		return;
	prefix = g_strdup_printf ("%s/", base->name);

	/* one entry per network on the bouncer */
	for (i = 0; i < s->networks->len; i++)
	{
		bnc_network *n = s->networks->pdata[i];
		const char *nn = n->name ? n->name : n->id;
		char *name = sync_name (base, nn);
		ircnet *e = servlist_net_find (name, NULL, strcmp);

		if (!e)
		{
			GSList *sl;

			e = servlist_net_add (name, "", FALSE);
			e->encoding = g_strdup (base->encoding ? base->encoding : IRC_DEFAULT_CHARSET);
			for (sl = base->servlist; sl; sl = sl->next)
				servlist_server_add (e, ((ircserver *) sl->data)->hostname);
			e->flags = (base->flags & ~(FLAG_AUTO_CONNECT | FLAG_FAVORITE)) & ~FLAG_USE_GLOBAL;
			e->nick = g_strdup (n->nick ? n->nick : base->nick ? base->nick : prefs.hex_irc_nick1);
			e->real = g_strdup (base->real);
			e->pass = g_strdup (base->pass);
			e->logintype = base->logintype;
			dirty = TRUE;
		}
		/* how to reach the bouncer is the bouncer entry's, always: an edited
		   address, port or password there must reach every network on it */
		if (sync_from_base (e, base))
		{
			dirty = TRUE;
			moved = g_slist_prepend (moved, e);
		}
		/* the nick soju has for that network is the one it'll be */
		if (n->nick && *n->nick && g_strcmp0 (e->nick, n->nick))
		{
			g_free (e->nick);
			e->nick = g_strdup (n->nick);
			dirty = TRUE;
		}
		/* soju picks by name or address, ZNC by name: the name it has */
		{
			char *u = g_strdup_printf ("%.*s/%s", (int) strcspn (base->user, "/"), base->user, nn);
			if (g_strcmp0 (e->user, u))
			{
				g_free (e->user);
				e->user = u;
				dirty = TRUE;
			}
			else
				g_free (u);
		}
		g_free (name);
	}

	/* entries for networks the bouncer doesn't have any more. Only the
	   bouncer's own connection decides that: a network tab's list can be
	   a moment behind, and would take away one that was just added */
	for (l = s->bound ? NULL : network_list; l; l = next)
	{
		ircnet *e = l->data;
		gboolean still = FALSE;

		next = l->next;
		if (!g_str_has_prefix (e->name, prefix))
			continue;
		for (i = 0; i < s->networks->len && !still; i++)
		{
			bnc_network *n = s->networks->pdata[i];
			still = !strcmp (e->name + strlen (prefix), n->name ? n->name : n->id);
		}
		/* gone from the bouncer: its entry goes too, even if a tab is still
		   on it (soju is closing that tab; it mustn't dial again) */
		if (!still)
		{
			release_entry (e);
			servlist_net_remove (e);
			dirty = TRUE;
		}
	}
	g_free (prefix);
	if (dirty)
		servlist_save ();
	/* tabs still trying the old address: try the right one now */
	for (l = moved; l; l = l->next)
	{
		GSList *sl;
		for (sl = serv_list; sl; sl = sl->next)
		{
			server *sv = sl->data;
			if (sv->network == l->data && !sv->end_of_motd)
			{
				sv->disconnect (sv->server_session, FALSE, -1);
				servlist_connect (sv->server_session, l->data, TRUE);
			}
		}
	}
	g_slist_free (moved);
	open_networks (serv, base);
}

gboolean
bouncer_sync_entry (ircnet *e)
{
	char *slash, *bname;
	ircnet *base;
	gboolean dirty;

	if (!e || !e->name || !(slash = strrchr (e->name, '/')))
		return FALSE;
	bname = g_strndup (e->name, slash - e->name);
	base = servlist_net_find (bname, NULL, strcmp);
	g_free (bname);
	if (!base || base == e || !base->servlist || !(base->flags & FLAG_BOUNCER) ||
		 !e->user || !strchr (e->user, '/'))
		return FALSE;
	if ((dirty = sync_from_base (e, base)))
		servlist_save ();
	return dirty;
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
		/* a bouncer elsewhere: remembered in the Bouncer window from now on */
		if (k == BNC_SOJU || k == BNC_ZNC)
		{
			ircnet *base = sync_base (serv);
			if (base && !(base->flags & FLAG_BOUNCER) && !mybouncer_owns (base))
			{
				base->flags |= FLAG_BOUNCER;
				servlist_save ();
			}
		}
		changed (serv);
	}
}

/* ---- detection ---- */

void
bouncer_cap_ls (server *serv, const char *cap, const char *value, char *buffer, int buflen, gboolean *want)
{
	if (!strcmp (cap, "soju.im/bouncer-networks") || !strcmp (cap, "soju.im/bouncer-networks-notify") ||
		 !strcmp (cap, "draft/persistence") || !strcmp (cap, "soju.im/search"))
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
		 strcmp (cap, "draft/persistence") && strcmp (cap, "soju.im/search"))
		return;
	s = state (serv);
	if (!strcmp (cap, "soju.im/search"))
		s->search_cap = enable;
	else if (!strcmp (cap, "soju.im/bouncer-networks"))
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

static gboolean
queue_later (gpointer data)
{
	server *serv = data;
	bnc_state *st;

	if (!is_server (serv) || !states || !(st = g_hash_table_lookup (states, serv)))
		return G_SOURCE_REMOVE;
	queue_next (serv, st);
	return G_SOURCE_REMOVE;
}

void
bouncer_login_end (server *serv)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	/* in: whatever went wrong before is past */
	if (last_errors && serv->network && g_hash_table_remove (last_errors, serv->network))
		changed (serv);
	if (!s)
		return;
	/* without -notify the list doesn't come by itself */
	if (s->soju_cap && !s->soju_notify)
		tcp_sendf (serv, "BOUNCER LISTNETWORKS\r\n");
	if (s->kind == BNC_ZNC)
		bouncer_refresh (serv);
	if (s->persist_cap && s->persist_effective < 0)
		tcp_sendf (serv, "PERSISTENCE GET\r\n");
	/* soju's list has come by now (it's sent with the login): what
	   \"Stay online\" asked for goes next */
	if (s->kind == BNC_SOJU && queued && serv->network && g_hash_table_lookup (queued, serv->network))
		g_timeout_add_seconds (1, queue_later, serv);
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
		else if (!strcmp (k, "username")) SET (username);
		else if (!strcmp (k, "realname")) SET (realname);
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
			char *was = NULL;

			if (!n)
			{
				n = g_new0 (bnc_network, 1);
				n->id = g_strdup (arg);
				n->tls = TRUE;
				g_ptr_array_add (s->networks, n);
			}
			else
				was = g_strdup (n->state);
			apply_attrs (n, attrs);
			/* the network this connection is on: say so when it drops or
			   comes back, in plain words (we're still connected to the bouncer) */
			if (was && s->bound && !strcmp (s->bound, n->id) && g_strcmp0 (was, n->state))
			{
				if (!g_strcmp0 (n->state, "disconnected"))
					PrintTextf (serv->server_session,
									n->error ? _("\00304*\tsoju lost %s: %s. It keeps trying; you'll see it here when it's back.\n")
											  : _("\00304*\tsoju lost %s%s. It keeps trying; you'll see it here when it's back.\n"),
									n->name ? n->name : n->id, n->error ? n->error : "");
				else if (!g_strcmp0 (n->state, "connected") && !g_strcmp0 (was, "disconnected"))
					PrintTextf (serv->server_session, _("\00303*\tsoju is back on %s.\n"), n->name ? n->name : n->id);
			}
			g_free (was);
		}
		set_kind (serv, BNC_SOJU);
		if (prefs.hex_irc_bouncer_sync)
			sync_networks (serv);
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
		queue_next (serv, s);
	}
}

gboolean
bouncer_fail (server *serv, char *word[], const char *text)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;

	/* a search the bouncer won't do: that place is done, say why once */
	if (s && s->search_out && !g_ascii_strcasecmp (word[3], "SEARCH"))
	{
		char *t = g_queue_pop_head (&s->search_targets);
		if (is_session (s->search_out))
			PrintTextf (s->search_out, _("%s: the bouncer said: %s\n"), t ? t : "?", text);
		g_free (t);
		if (g_queue_is_empty (&s->search_targets))
			search_finish (s);
		return TRUE;
	}
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
			if (prefs.hex_irc_bouncer_sync)
				sync_networks (serv);
			changed (serv);
			queue_next (serv, s);
		}
		return TRUE;
	}
	if (!strcmp (text, "You have no networks"))
	{
		g_ptr_array_set_size (s->networks, 0);
		s->znc_listing = FALSE;
		if (prefs.hex_irc_bouncer_sync)
			sync_networks (serv);
		changed (serv);
		queue_next (serv, s);
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

static gboolean
bnc_has_network (bnc_state *s, const char *name)
{
	guint i;
	for (i = 0; i < s->networks->len; i++)
	{
		bnc_network *n = s->networks->pdata[i];
		if (!g_ascii_strcasecmp (n->name ? n->name : n->id, name))
			return TRUE;
	}
	return FALSE;
}

static gboolean
bouncer_add_network_q (server *serv, const char *name, const char *host, int port,
							  gboolean tls, char **err)
{
	return bouncer_add_network (serv, name, host, port, tls, NULL, err);
}

/* a word for BouncerServ: quoted, with \ and " escaped */
static void
service_word (GString *s, const char *w)
{
	g_string_append (s, " \"");
	for (; *w; w++)
	{
		if (*w == '\\' || *w == '"')
			g_string_append_c (s, '\\');
		g_string_append_c (s, *w);
	}
	g_string_append_c (s, '"');
}

gboolean
bouncer_change_network (server *serv, const char *id, const char *nick, const char *username,
								const char *realname, const char *pass, char **err)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	GString *a;

	*err = NULL;
	if (!s || !serv->connected || s->kind != BNC_SOJU)
	{
		*err = g_strdup (_("Not connected to a soju bouncer."));
		return FALSE;
	}
	if (nick && (!*nick || strpbrk (nick, " ,*?!@")))
	{
		*err = g_strdup (_("A nick can't be empty or have spaces in it."));
		return FALSE;
	}
	a = g_string_new (NULL);
	attr (a, "nickname", nick);
	/* attr skips empty values; an empty one clears it on soju */
	if (username)
		*username ? attr (a, "username", username) : g_string_append_printf (a, "%susername=", a->len ? ";" : "");
	if (realname)
		*realname ? attr (a, "realname", realname) : g_string_append_printf (a, "%srealname=", a->len ? ";" : "");
	if (pass)
		*pass ? attr (a, "pass", pass) : g_string_append_printf (a, "%spass=", a->len ? ";" : "");
	if (a->len)
	{
		g_free (s->pending);
		s->pending = g_strdup (_("\00303*\tSaved on the bouncer.\n"));
		tcp_sendf (serv, "BOUNCER CHANGENETWORK %s %s\r\n", id, a->str);
	}
	g_string_free (a, TRUE);
	return TRUE;
}

gboolean
bouncer_set_sasl (server *serv, const char *netname, const char *account, const char *password, char **err)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	GString *c;
	int i;

	*err = NULL;
	if (!s || !serv->connected || s->kind != BNC_SOJU)
	{
		*err = g_strdup (_("Not connected to a soju bouncer."));
		return FALSE;
	}
	c = g_string_new ("PRIVMSG BouncerServ :sasl ");
	g_string_append (c, account && *account ? "set-plain -network" : "reset -network");
	service_word (c, netname);
	if (account && *account)
	{
		service_word (c, account);
		service_word (c, password ? password : "");
	}
	g_string_append (c, "\r\n");
	tcp_send_len (serv, c->str, c->len);
	g_string_free (c, TRUE);
	/* soju only stores it: reconnect so it logs in with it now */
	for (i = 0; i < 2; i++)
	{
		c = g_string_new ("PRIVMSG BouncerServ :network update");
		service_word (c, netname);
		g_string_append_printf (c, " -enabled %s\r\n", i ? "true" : "false");
		tcp_send_len (serv, c->str, c->len);
		g_string_free (c, TRUE);
	}
	return TRUE;
}

GSList *
bouncer_soju_servers (void)
{
	GSList *out = NULL, *l;

	for (l = serv_list; l; l = l->next)
	{
		server *sv = l->data;
		bnc_state *s = states ? g_hash_table_lookup (states, sv) : NULL;
		/* the control connection, not one bound to a network */
		if (s && sv->connected && s->kind == BNC_SOJU && !s->bound)
			out = g_slist_append (out, sv);
	}
	return out;
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

/* ---- soju.im/search ---- */

/* the text as a SEARCH attribute value (message-tag escaping) */
static char *
search_value (const char *text)
{
	GString *a = g_string_new (NULL);
	char *v;

	attr (a, "text", text);
	v = g_strdup (a->str + 5);	/* without "text=" */
	g_string_free (a, TRUE);
	return v;
}

static void
search_finish (bnc_state *s)
{
	if (s->search_out && is_session (s->search_out))
	{
		if (!s->search_hits)
			PrintTextf (s->search_out, _("No matches for \"%s\" on the bouncer.\n"), s->search_text);
		else
			PrintTextf (s->search_out, g_dngettext (GETTEXT_PACKAGE, "%d match on the bouncer.\n",
														 "%d matches on the bouncer.\n", s->search_hits), s->search_hits);
	}
	s->search_out = NULL;
	g_clear_pointer (&s->search_text, g_free);
}

gboolean
bouncer_search (server *serv, session *out, char **targets, const char *text, char **err)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	char *v;
	int i, n = 0;

	*err = NULL;
	if (!s || !s->search_cap || !serv->connected)
	{
		*err = g_strdup (_("This server can't search its history (it takes soju)."));
		return FALSE;
	}
	if (s->search_out)
	{
		*err = g_strdup (_("A search on the bouncer is still running."));
		return FALSE;
	}
	v = search_value (text);
	for (i = 0; targets[i]; i++)
	{
		if (!plain_word (targets[i]))
			continue;
		tcp_sendf (serv, "SEARCH in=%s;text=%s;limit=100\r\n", targets[i], v);
		g_queue_push_tail (&s->search_targets, g_strdup (targets[i]));
		n++;
	}
	g_free (v);
	if (!n)
	{
		*err = g_strdup (_("No channels or private chats here to search."));
		return FALSE;
	}
	s->search_out = out;
	s->search_text = g_strdup (text);
	s->search_hits = 0;
	PrintTextf (out, g_dngettext (GETTEXT_PACKAGE, "Searching the bouncer's history of %d place for \"%s\"...\n",
										 "Searching the bouncer's history of %d places for \"%s\"...\n", n), n, text);
	return TRUE;
}

void
bouncer_search_batch (server *serv, const char *id, gboolean start)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	char *target;

	if (!s || !s->search_out)
		return;
	if (start)
	{
		g_free (s->search_batch);
		s->search_batch = g_strdup (id);
		s->search_here = 0;
		return;
	}
	if (g_strcmp0 (s->search_batch, id))
		return;
	g_clear_pointer (&s->search_batch, g_free);
	/* that target is answered */
	target = g_queue_pop_head (&s->search_targets);
	g_free (target);
	if (g_queue_is_empty (&s->search_targets))
		search_finish (s);
}

gboolean
bouncer_search_take (server *serv, const char *batch, const char *line, time_t when)
{
	bnc_state *s = states ? g_hash_table_lookup (states, serv) : NULL;
	char **w, *nick, *text, *stamp;
	const char *target;

	if (!s || !s->search_batch || !batch || strcmp (batch, s->search_batch))
		return FALSE;
	if (!s->search_out || !is_session (s->search_out))
		return TRUE;
	/* ":nick!u@h PRIVMSG #chan :text" */
	w = g_strsplit (line, " ", 4);
	if (!w[0] || !w[1] || !w[2] || !w[3] || w[0][0] != ':')
	{
		g_strfreev (w);
		return TRUE;
	}
	nick = g_strndup (w[0] + 1, strcspn (w[0] + 1, "!"));
	text = w[3][0] == ':' ? w[3] + 1 : w[3];
	target = g_queue_peek_head (&s->search_targets);
	if (!s->search_here++)
		PrintTextf (s->search_out, "\002%s\002\n", target ? target : w[2]);
	{
		GDateTime *dt = when ? g_date_time_new_from_unix_local (when) : NULL;
		stamp = dt ? g_date_time_format (dt, "%Y-%m-%d %H:%M") : g_strdup ("");
		if (dt)
			g_date_time_unref (dt);
	}
	if (!strcmp (w[1], "NOTICE"))
		PrintTextf (s->search_out, "  %s  -%s- %s\n", stamp, nick, text);
	else if (!strncmp (text, "\001ACTION ", 8))
	{
		char *act = g_strndup (text + 8, strcspn (text + 8, "\001"));
		PrintTextf (s->search_out, "  %s  * %s %s\n", stamp, nick, act);
		g_free (act);
	}
	else
		PrintTextf (s->search_out, "  %s  <%s> %s\n", stamp, nick, text);
	s->search_hits++;
	g_free (stamp);
	g_free (nick);
	g_strfreev (w);
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
		PrintText (sess, _("This is a plain IRC server: when nexchat is closed, you're not in your channels and miss what's said.\n"));
		if (mybouncer_state () == MYBNC_AVAILABLE)
			PrintText (sess, _("soju is installed on this computer: /bouncer setup and nexchat sets it up and moves your networks onto it.\n"));
		else
			PrintText (sess, _("Server > Stay Online... sets you up with a bouncer.\n"));
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

static void
setup_progress (const char *text, gboolean done, gboolean ok, gpointer data)
{
	session *sess = data;
	if (!is_session (sess))
		sess = current_sess;
	if (sess)
		PrintTextf (sess, "%s%s\n", done ? (ok ? "\00303*\t" : "\00304*\t") : "  ", text);
	if (done && ok && sess)
		PrintText (sess, _("Connect to your networks as before: they now go through your bouncer and stay in your channels when nexchat is closed.\n"));
}

/* /bouncer setup: our own soju, with every network in the list moved
   onto it; /bouncer setup NAME... just those */
static void
cmd_setup (session *sess, char *word[])
{
	char *err = NULL;
	GSList *nets = NULL, *l;
	int i;

	if (mybouncer_state () == MYBNC_NONE)
	{
		PrintText (sess, _("soju isn't installed on this computer. Install it from your system's packages "
								 "(it's called soju), then /bouncer setup again: nexchat does the rest.\n"));
		return;
	}
	if (!mybouncer_setup (&err))
	{
		PrintTextf (sess, "\00304*\t%s\n", err);
		g_free (err);
		return;
	}
	PrintTextf (sess, !strcmp (mybouncer_kept_by (), "systemd") ?
					_("\00303*\tYour bouncer is running, and stays running when nexchat is closed (a systemd user service).\n") :
					_("\00303*\tYour bouncer is running while nexchat is.\n"));
	for (i = 3; *word[i]; i++)
	{
		ircnet *n = servlist_net_find (word[i], NULL, g_ascii_strcasecmp);
		if (n)
			nets = g_slist_append (nets, n);
		else
			PrintTextf (sess, _("No network called %s in the Network List.\n"), word[i]);
	}
	if (i == 3)
		for (l = network_list; l; l = l->next)
		{
			ircnet *n = l->data;
			/* only the ones you use: favourites, auto-connect, or with channels */
			if ((n->flags & (FLAG_FAVORITE | FLAG_AUTO_CONNECT)) || n->favchanlist)
				nets = g_slist_append (nets, n);
		}
	if (!nets)
	{
		PrintText (sess, _("Nothing to move yet: /bouncer setup <network> moves one, e.g. /bouncer setup Libera.Chat\n"));
		return;
	}
	mybouncer_adopt (nets, setup_progress, sess);
	g_slist_free (nets);
}

static gboolean
word_on (const char *w)
{
	return !g_ascii_strcasecmp (w, "on") || !g_ascii_strcasecmp (w, "yes") || !strcmp (w, "1");
}

/* /bouncer set ...: our own soju's settings */
static void
cmd_set (session *sess, char *word[], char *word_eol[])
{
	const char *k = word[3];
	char *err = NULL;
	gboolean ok = TRUE;

	if (mybouncer_state () != MYBNC_SET_UP)
	{
		PrintText (sess, _("nexchat isn't running a bouncer for you: /bouncer setup first.\n"));
		return;
	}
	if (!*k)
	{
		char *t = mybouncer_title ();
		PrintTextf (sess, _("remote %s (port %d), logs %s, title \"%s\", kept by %s\n"),
						mybouncer_remote () ? "on" : "off", mybouncer_remote_port (),
						mybouncer_logs_as_files () ? "files" : "db", t ? t : "", mybouncer_kept_by ());
		PrintText (sess, _("/bouncer set remote on|off, logs files|db, title TEXT, password PW, systemd on|off,\n"
								 "  net NETWORK KEY VALUE (realname, username, pass, auto-away, enabled),\n"
								 "  commands NETWORK LINE|LINE, channel NETWORK #CHAN add|del|detached on|off\n"));
		g_free (t);
		return;
	}
	if (!g_ascii_strcasecmp (k, "remote"))
		ok = mybouncer_set_remote (word_on (word[4]), &err);
	else if (!g_ascii_strcasecmp (k, "logs"))
		ok = mybouncer_set_logs_as_files (!g_ascii_strcasecmp (word[4], "files"), &err);
	else if (!g_ascii_strcasecmp (k, "title"))
		ok = mybouncer_set_title (word_eol[4], &err);
	else if (!g_ascii_strcasecmp (k, "password"))
		ok = mybouncer_set_password (word[4], &err);
	else if (!g_ascii_strcasecmp (k, "systemd"))
		ok = mybouncer_set_kept_by_systemd (word_on (word[4]), &err);
	else if (!g_ascii_strcasecmp (k, "net") || !g_ascii_strcasecmp (k, "commands") ||
				!g_ascii_strcasecmp (k, "channel"))
	{
		ircnet *n = servlist_net_find (word[4], NULL, g_ascii_strcasecmp);
		if (!n || !mybouncer_owns (n))
		{
			PrintTextf (sess, _("%s isn't on your bouncer.\n"), word[4]);
			return;
		}
		if (!g_ascii_strcasecmp (k, "commands"))
		{
			char *lines = g_strdup (word_eol[5]), *p;
			for (p = lines; *p; p++)
				if (*p == '|')
					*p = '\n';
			ok = mybouncer_net_set_commands (n, lines, &err);
			g_free (lines);
		}
		else if (!g_ascii_strcasecmp (k, "net"))
			ok = mybouncer_net_set (n, word[5], word_eol[6], &err);
		else if (!g_ascii_strcasecmp (word[6], "add"))
			ok = mybouncer_channel_add (n, word[5], &err);
		else if (!g_ascii_strcasecmp (word[6], "del"))
			ok = mybouncer_channel_del (n, word[5], &err);
		else
			ok = mybouncer_channel_set (n, word[5], word[6], word_on (word[7]) ? "true" : "false", &err);
	}
	else
	{
		PrintTextf (sess, _("No setting called %s: /bouncer set lists them.\n"), k);
		return;
	}
	if (ok)
		PrintText (sess, _("\00303*\tDone.\n"));
	else
		PrintTextf (sess, "\00304*\t%s\n", err ? err : "?");
	g_free (err);
}

int
bouncer_cmd (session *sess, char *word[], char *word_eol[])
{
	server *serv = sess->server;
	char *err = NULL;
	gboolean ok = TRUE;

	if (!g_ascii_strcasecmp (word[2], "SETUP"))
	{
		cmd_setup (sess, word);
		return TRUE;
	}
	if (!g_ascii_strcasecmp (word[2], "SET"))
	{
		cmd_set (sess, word, word_eol);
		return TRUE;
	}
	if (!g_ascii_strcasecmp (word[2], "LOG"))
	{
		char *t = mybouncer_log_tail (20);
		PrintTextf (sess, "%s\n", t);
		g_free (t);
		return TRUE;
	}
	if (!g_ascii_strcasecmp (word[2], "REMOVE") && !*word[3])
	{
		if (mybouncer_state () != MYBNC_SET_UP)
			PrintText (sess, _("nexchat isn't running a bouncer for you.\n"));
		else if (mybouncer_remove (&err))
			PrintText (sess, _("\00303*\tYour bouncer is stopped; your networks connect directly again, as they did before.\n"));
		else
			PrintTextf (sess, "%s\n", err);
		g_free (err);
		return TRUE;
	}

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

/* ---- bouncers you log in to, remembered while they're not connected ---- */

GSList *
bouncer_entries (void)
{
	GSList *out = NULL, *l;

	for (l = network_list; l; l = l->next)
	{
		ircnet *n = l->data;
		/* "My bouncer (host)": made by the setup before the flag existed */
		gboolean made = n->logintype == LOGIN_PASS && n->name && g_str_has_prefix (n->name, "My bouncer (");
		if (((n->flags & FLAG_BOUNCER) || made) && n->servlist && !mybouncer_owns (n) &&
			 !(n->user && strchr (n->user, '/')))
			out = g_slist_append (out, n);
	}
	return out;
}

server *
bouncer_server_of (ircnet *net)
{
	GSList *l;
	for (l = serv_list; l; l = l->next)
	{
		server *sv = l->data;
		if (sv->network == net && (sv->connected || sv->connecting))
			return sv;
	}
	return NULL;
}

const char *
bouncer_last_error (ircnet *net)
{
	return last_errors && net ? g_hash_table_lookup (last_errors, net) : NULL;
}

void
bouncer_note_error (server *serv, const char *why)
{
	ircnet *net = serv->network;

	if (!net || !why || !*why)
		return;
	/* only the bouncers' (ones known, or just set up) */
	if (!(net->flags & FLAG_BOUNCER) && net->logintype != LOGIN_PASS)
		return;
	if (!last_errors)
		last_errors = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	g_hash_table_insert (last_errors, net, g_strdup (why));
	changed (serv);
}

void
bouncer_forget (ircnet *net)
{
	char *prefix = g_strdup_printf ("%s/", net->name);
	GSList *l, *next;

	for (l = network_list; l; l = next)
	{
		ircnet *e = l->data;
		next = l->next;
		if (g_str_has_prefix (e->name, prefix) && !is_in_use (e))
		{
			release_entry (e);
			servlist_net_remove (e);
		}
	}
	g_free (prefix);
	if (last_errors)
		g_hash_table_remove (last_errors, net);
	if (queued)
		g_hash_table_remove (queued, net);
	if (is_in_use (net))
		net->flags &= ~FLAG_BOUNCER;
	else
	{
		release_entry (net);
		servlist_net_remove (net);
	}
	servlist_save ();
}
