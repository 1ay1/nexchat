/* nexchat TUI — the bouncer bridge (bridge-bnc.h).
 *
 * Each function is the GUI's own logic (fe-gtk/bncwin.c, bncsetup.c) with
 * the widgets taken out: the same core calls, the same rules for what's
 * listed and what's written, the same sentences. Where the GUI's code is
 * the source, the function says which.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/servlist.h"
#include "../common/server.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/bouncer.h"
#include "../common/mybouncer.h"
#include "../common/bncprobe.h"

#include "bridge-bnc.h"

void
nx_core_free (void *p)
{
	g_free (p);
}

static ircnet *
net_at (int i)
{
	return i >= 0 ? g_slist_nth_data (network_list, i) : NULL;
}

static int
index_of (ircnet *n)
{
	return g_slist_index (network_list, n);
}

/* ── our own soju ──────────────────────────────────────────────────── */

int nx_bnc_state (void) { return mybouncer_state (); }
int nx_bnc_running (void) { return mybouncer_running (); }
const char *nx_bnc_kept_by (void) { return mybouncer_kept_by (); }
const char *nx_bnc_soju_path (void) { return mybouncer_soju_path (); }
int nx_bnc_setup (char **err) { return mybouncer_setup (err); }
int nx_bnc_start (char **err) { return mybouncer_start (err); }
void nx_bnc_stop (void) { mybouncer_stop (); }
int nx_bnc_remove (char **err) { return mybouncer_remove (err); }

/* bncwin.c state_of: what soju says about net */
static char *
state_of (const char *status, ircnet *net, int *up)
{
	char **lines, *res = NULL, *name;
	int i;

	*up = FALSE;
	if (!status)
		return NULL;
	name = g_strdup (net->name);
	g_strdelimit (name, " /:;", '-');
	lines = g_strsplit (status, "\n", 0);
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

/* bncwin.c fill_list + update_states */
int
nx_bnc_rows (int from, nx_bnc_row **rows)
{
	GArray *a = g_array_new (FALSE, TRUE, sizeof (nx_bnc_row));
	char *status = mybouncer_status ();
	ircnet *fromnet = net_at (from);
	GSList *l;

	if (mybouncer_state () == MYBNC_SET_UP)
		for (l = network_list; l; l = l->next)
		{
			ircnet *n = l->data;
			ircserver *sv = n->servlist ? n->servlist->data : NULL;
			gboolean mine = mybouncer_owns (n);
			nx_bnc_row r = { 0 };

			if (!sv || (!mine && n->logintype == LOGIN_PASS))
				continue;
			if (!mine && !(n->flags & (FLAG_FAVORITE | FLAG_AUTO_CONNECT)) && !n->favchanlist && n != fromnet)
				continue;
			r.net = index_of (n);
			r.name = g_strdup (n->name);
			r.mine = mine;
			if (!mine)
			{
				r.state = g_strdup_printf (_("connects directly (%s)"), sv->hostname);
				r.who = g_strdup ("");
			}
			else
			{
				char *nk, *f, *login, *acct;
				r.state = state_of (status, n, &r.up);
				if (!r.state)
					r.state = g_strdup (mybouncer_running () ? _("joining...") : _("bouncer stopped"));
				mybouncer_identity (n, &nk, &f, &login, &acct);
				r.who = g_strdup_printf (_("as %s (%s), %s"), nk, f, login);
				g_free (nk); g_free (f); g_free (login); g_free (acct);
			}
			g_array_append_val (a, r);
		}
	g_free (status);
	*rows = (nx_bnc_row *) a->data;
	{
		int n = a->len;
		g_array_free (a, FALSE);
		return n;
	}
}

void
nx_bnc_rows_free (nx_bnc_row *rows, int n)
{
	int i;
	for (i = 0; i < n; i++)
	{
		g_free (rows[i].name);
		g_free (rows[i].state);
		g_free (rows[i].who);
	}
	g_free (rows);
}

typedef struct { nx_bnc_progress cb; void *ud; ircnet *net; } adopt_ctx;

/* bncwin.c moved: when it's on soju, connect through it */
static void
adopted (const char *text, gboolean done, gboolean ok, gpointer data)
{
	adopt_ctx *c = data;
	if (done && ok && c->net && mybouncer_owns (c->net))
	{
		GSList *l;
		gboolean open = FALSE;
		for (l = serv_list; l; l = l->next)
			if (((server *) l->data)->network == c->net)
			{
				server *s = l->data;
				open = TRUE;
				servlist_connect (s->server_session, c->net, TRUE);
			}
		if (!open)
			servlist_connect (NULL, c->net, TRUE);
	}
	if (c->cb)
		c->cb (c->ud, text, done, ok);
	if (done)
		g_free (c);
}

void
nx_bnc_adopt (int net, nx_bnc_progress cb, void *ud)
{
	ircnet *n = net_at (net);
	adopt_ctx *c;
	GSList *one;

	if (!n)
		return;
	c = g_new0 (adopt_ctx, 1);
	c->cb = cb; c->ud = ud; c->net = n;
	one = g_slist_append (NULL, n);
	mybouncer_adopt (one, adopted, c);
	g_slist_free (one);
}

int
nx_bnc_release (int net, char **err)
{
	ircnet *n = net_at (net);
	return n ? mybouncer_release (n, err) : FALSE;
}

/* ── one network's nick and login (bncwin.c edit_open / editor_save) ── */

void
nx_bnc_ident_get (int net, nx_bnc_ident *e)
{
	ircnet *n = net_at (net);
	char *v, *fp, *err = NULL;

	memset (e, 0, sizeof *e);
	if (!n)
		return;
	mybouncer_identity (n, &e->nick, &e->nick_from, &e->login, &e->account);
	e->same = !n->nick || !*n->nick;
	e->realname = mybouncer_net_get (n, "realname");
	if (!e->realname) e->realname = g_strdup ("");
	e->username = mybouncer_net_get (n, "username");
	if (!e->username) e->username = g_strdup ("");
	v = mybouncer_net_get (n, "pass");
	e->has_srvpass = v && *v;
	g_free (v);
	e->has_account = e->account != NULL;
	/* the account field starts with the nick when there's no login yet */
	if (!e->account)
		e->account = g_strdup (e->nick ? e->nick : "");
	v = mybouncer_net_get (n, "auto-away");
	e->away = !v || strcmp (v, "false");
	g_free (v);
	v = mybouncer_net_get (n, "enabled");
	e->enabled = !v || strcmp (v, "false");
	g_free (v);
	e->commands = mybouncer_net_get (n, "commands");
	if (!e->commands) e->commands = g_strdup ("");
	fp = mybouncer_net_certfp (n, FALSE, &err);
	e->has_cert = fp != NULL;
	g_free (fp);
	g_free (err);
}

void
nx_bnc_ident_free (nx_bnc_ident *e)
{
	g_free (e->nick); g_free (e->nick_from); g_free (e->login); g_free (e->account);
	g_free (e->realname); g_free (e->username); g_free (e->commands);
	memset (e, 0, sizeof *e);
}

/* bncwin.c net_changed: sent only when it changed from what we last set */
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

int
nx_bnc_ident_save (int netidx, const char *nick, const char *acct, const char *pw,
						 const char *realname, const char *username, const char *srvpass,
						 int away, int enabled, const char *commands, char **err)
{
	ircnet *net = net_at (netidx);
	char *kept = NULL, *cmds;
	gboolean had_account;
	GSList *l;

	if (!net)
		return FALSE;
	{
		char *a, *b, *c, *d;
		mybouncer_identity (net, &a, &b, &c, &d);
		had_account = d != NULL;
		g_free (a); g_free (b); g_free (c); g_free (d);
	}
	if (nick && (!*nick || strpbrk (nick, " ,*?!@")))
	{
		*err = g_strdup (_("A nick can't be empty or have spaces in it."));
		return FALSE;
	}
	/* the password left empty keeps the one it has */
	if (acct && (!pw || !*pw) && had_account)
	{
		kept = mybouncer_own_password (net);
		pw = kept ? kept : "";
	}
	if (acct && (!*acct || !pw || !*pw))
	{
		*err = g_strdup (_("Fill in the account and its password, or pick \"No login\"."));
		g_free (kept);
		return FALSE;
	}
	g_free (net->nick);
	net->nick = nick ? g_strdup (nick) : NULL;
	servlist_save ();
	if (!mybouncer_set_login (net, acct, acct ? pw : NULL, err))
	{
		g_free (kept);
		return FALSE;
	}
	g_free (kept);
	cmds = g_strstrip (g_strdup (commands ? commands : ""));
	if (!net_changed (net, "realname", realname ? realname : "", "", err) ||
		 !net_changed (net, "username", username ? username : "", "", err) ||
		 (srvpass && *srvpass && !net_changed (net, "pass", srvpass, "", err)) ||
		 !net_changed (net, "auto-away", away ? "true" : "false", "true", err) ||
		 !net_changed (net, "enabled", enabled ? "true" : "false", "true", err) ||
		 !net_changed (net, "commands", cmds, "", err))
	{
		g_free (cmds);
		return FALSE;
	}
	g_free (cmds);
	/* connected now: the new nick right away */
	for (l = serv_list; l; l = l->next)
	{
		server *s = l->data;
		if (s->network == net && s->connected)
			tcp_sendf (s, "NICK %s\r\n", mybouncer_wanted_nick (net));
	}
	return TRUE;
}

char *
nx_bnc_certfp (int net, int make, char **err)
{
	ircnet *n = net_at (net);
	char *fp;
	if (!n)
		return NULL;
	fp = mybouncer_net_certfp (n, FALSE, err);
	if (!fp && make)
	{
		g_clear_pointer (err, g_free);
		fp = mybouncer_net_certfp (n, TRUE, err);
	}
	return fp;
}

/* ── Channels ──────────────────────────────────────────────────────── */

int
nx_bnc_owned_count (void)
{
	GSList *l;
	int n = 0;
	for (l = network_list; l; l = l->next)
		n += mybouncer_owns (l->data);
	return n;
}

int
nx_bnc_owned (int i)
{
	GSList *l;
	for (l = network_list; l; l = l->next)
		if (mybouncer_owns (l->data) && i-- == 0)
			return index_of (l->data);
	return -1;
}

char *
nx_bnc_channels (int net)
{
	ircnet *n = net_at (net);
	return n ? mybouncer_channels (n) : NULL;
}

int
nx_bnc_channel_add (int net, const char *chan, char **err)
{
	ircnet *n = net_at (net);
	return n ? mybouncer_channel_add (n, chan, err) : FALSE;
}

int
nx_bnc_channel_del (int net, const char *chan, char **err)
{
	ircnet *n = net_at (net);
	return n ? mybouncer_channel_del (n, chan, err) : FALSE;
}

int
nx_bnc_channel_detach (int net, const char *chan, int detached, char **err)
{
	ircnet *n = net_at (net);
	return n ? mybouncer_channel_set (n, chan, "detached", detached ? "true" : "false", err) : FALSE;
}

/* ── Access, Storage ───────────────────────────────────────────────── */

int nx_bnc_port (void) { return mybouncer_port (); }
const char *nx_bnc_user (void) { return mybouncer_user (); }
char *nx_bnc_password (void) { return mybouncer_password (); }
int nx_bnc_set_password (const char *pw, char **err) { return mybouncer_set_password (pw, err); }
int nx_bnc_remote (void) { return mybouncer_remote (); }
int nx_bnc_set_remote (int on, char **err) { return mybouncer_set_remote (on, err); }
int nx_bnc_remote_port (void) { return mybouncer_remote_port (); }
char *nx_bnc_remote_certfp (void) { return mybouncer_remote_certfp (); }
int nx_bnc_can_systemd (void) { return mybouncer_can_systemd (); }
int nx_bnc_set_systemd (int on, char **err) { return mybouncer_set_kept_by_systemd (on, err); }
int nx_bnc_logs_as_files (void) { return mybouncer_logs_as_files (); }
int nx_bnc_set_logs_as_files (int on, char **err) { return mybouncer_set_logs_as_files (on, err); }
char *nx_bnc_title (void) { return mybouncer_title (); }
int nx_bnc_set_title (const char *t, char **err) { return mybouncer_set_title (t, err); }
char *nx_bnc_log_tail (int lines) { return mybouncer_log_tail (lines); }

/* ── bouncers elsewhere (bncwin.c fill_one_remote) ─────────────────── */

static server *
rb_live (ircnet *net)
{
	server *sv = bouncer_server_of (net);
	return sv && sv->connected && sv->end_of_motd ? sv : NULL;
}

int
nx_rbnc_list (nx_rbnc **out)
{
	GSList *ents = bouncer_entries (), *l;
	GArray *a = g_array_new (FALSE, TRUE, sizeof (nx_rbnc));
	int n;

	for (l = ents; l; l = l->next)
	{
		ircnet *net = l->data;
		ircserver *isv = net->servlist ? net->servlist->data : NULL;
		server *sv = bouncer_server_of (net), *live = rb_live (net);
		GPtrArray *nets = live ? bouncer_networks (live) : NULL;
		const char *err = bouncer_last_error (net);
		nx_rbnc r = { 0 };
		guint i;

		if (!isv)
			continue;
		r.net = index_of (net);
		r.name = g_strdup (net->name);
		{
			const char *dflt = (net->flags & FLAG_USE_SSL) ? "6697" : "6667";
			char **hp = g_strsplit (isv->hostname, "/", 2);
			r.where = g_strdup_printf ("%s%s:%s", (net->flags & FLAG_USE_SSL) ? "" : "irc://", hp[0],
												!hp[1] ? dflt : hp[1][0] == '+' ? hp[1] + 1 : hp[1]);
			g_strfreev (hp);
		}
		r.user = g_strdup (net->user ? net->user : "?");
		r.live = live != NULL;
		r.connecting = sv && !live;
		r.soju = live ? bouncer_kind (live) != BNC_ZNC : TRUE;
		r.error = err ? g_strdup (err) : NULL;
		if (nets && nets->len)
		{
			r.n = nets->len;
			r.nets = g_new0 (nx_rnet, nets->len);
			for (i = 0; i < nets->len; i++)
			{
				bnc_network *bn = nets->pdata[i];
				r.nets[i].id = g_strdup (bn->id);
				r.nets[i].name = g_strdup (bn->name ? bn->name : bn->id);
				r.nets[i].host = g_strdup (bn->host ? bn->host : "");
				r.nets[i].nick = g_strdup (bn->nick ? bn->nick : "?");
				r.nets[i].state = g_strdup (bn->state ? bn->state : "");
				r.nets[i].error = bn->error ? g_strdup (bn->error) : NULL;
			}
		}
		g_array_append_val (a, r);
	}
	g_slist_free (ents);
	n = a->len;
	*out = (nx_rbnc *) g_array_free (a, FALSE);
	return n;
}

void
nx_rbnc_list_free (nx_rbnc *l, int n)
{
	int i, j;
	for (i = 0; i < n; i++)
	{
		for (j = 0; j < l[i].n; j++)
		{
			g_free (l[i].nets[j].id); g_free (l[i].nets[j].name); g_free (l[i].nets[j].host);
			g_free (l[i].nets[j].nick); g_free (l[i].nets[j].state); g_free (l[i].nets[j].error);
		}
		g_free (l[i].nets);
		g_free (l[i].name); g_free (l[i].where); g_free (l[i].user); g_free (l[i].error);
	}
	g_free (l);
}

/* bncwin.c remote_key */
char *
nx_rbnc_key (void)
{
	GString *k = g_string_new (NULL);
	GSList *ents = bouncer_entries (), *l;
	char *status = mybouncer_status ();

	for (l = ents; l; l = l->next)
	{
		ircnet *net = l->data;
		server *sv = bouncer_server_of (net), *live = rb_live (net);
		GPtrArray *nets = live ? bouncer_networks (live) : NULL;
		guint i;
		g_string_append_printf (k, "%p:%p:%d:%s:", (void *) net, (void *) sv, live != NULL,
										bouncer_last_error (net) ? bouncer_last_error (net) : "");
		for (i = 0; nets && i < nets->len; i++)
		{
			bnc_network *n = nets->pdata[i];
			g_string_append_printf (k, "%s/%s/%s/%s/%s;", n->id, n->name, n->nick, n->state, n->error);
		}
	}
	g_slist_free (ents);
	/* our own soju's too: its networks' states, running or not */
	g_string_append_printf (k, "|%d:%d:%s", mybouncer_state (), mybouncer_running (), status ? status : "");
	g_free (status);
	return g_string_free (k, FALSE);
}

void
nx_rbnc_connect (int netidx)
{
	ircnet *net = net_at (netidx);
	server *sv;
	if (!net)
		return;
	if ((sv = bouncer_server_of (net)))
		sv->disconnect (sv->server_session, TRUE, -1);
	else
		servlist_connect (NULL, net, TRUE);
}

void
nx_rbnc_forget (int netidx)
{
	ircnet *net = net_at (netidx);
	server *sv;
	if (!net)
		return;
	if ((sv = bouncer_server_of (net)))
		sv->disconnect (sv->server_session, TRUE, -1);
	bouncer_forget (net);
}

static server *
live_of (int netidx)
{
	ircnet *net = net_at (netidx);
	server *s = net ? rb_live (net) : NULL;
	return s && is_server (s) ? s : NULL;
}

/* bncwin.c adder_add: the address as the GUI reads it */
int
nx_rbnc_add (int netidx, const char *name, const char *address, char **err)
{
	server *serv = live_of (netidx);
	char *addr = g_strstrip (g_strdup (address ? address : "")), *host = addr, *colon;
	gboolean tls = TRUE, ok;
	int port = 6697;

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
		*err = g_strdup (_("Write the network's address, like irc.libera.chat."));
		g_free (addr);
		return FALSE;
	}
	if (!serv)
	{
		*err = g_strdup (_("Not connected to the bouncer any more."));
		g_free (addr);
		return FALSE;
	}
	ok = bouncer_add_network (serv, name && *name ? name : host, host, port, tls, NULL, err);
	g_free (addr);
	return ok;
}

int
nx_rbnc_del (int netidx, const char *id, char **err)
{
	server *serv = live_of (netidx);
	if (!serv)
	{
		*err = g_strdup (_("Not connected to the bouncer any more."));
		return FALSE;
	}
	return bouncer_del_network (serv, id, err);
}

int
nx_rbnc_change (int netidx, const char *id, const char *nick, const char *username,
					 const char *realname, const char *pass, char **err)
{
	server *serv = live_of (netidx);
	if (!serv || !serv->connected)
	{
		*err = g_strdup (_("Not connected to the bouncer any more."));
		return FALSE;
	}
	return bouncer_change_network (serv, id, nick, username, realname, pass && *pass ? pass : NULL, err);
}

int
nx_rbnc_sasl (int netidx, const char *netname, const char *account, const char *password, char **err)
{
	server *serv = live_of (netidx);
	if (!serv)
	{
		*err = g_strdup (_("Not connected to the bouncer any more."));
		return FALSE;
	}
	if (account && (!*account || !password || !*password))
	{
		*err = g_strdup (_("Fill in the account and its password, or untick the login."));
		return FALSE;
	}
	return bouncer_set_sasl (serv, netname, account, password, err);
}

/* bncwin.c rn_open */
void
nx_rbnc_open (int netidx, const char *name)
{
	ircnet *base = net_at (netidx), *e;
	char *full;
	GSList *l;

	if (!base)
		return;
	full = g_strdup_printf ("%s/%s", base->name, name);
	e = servlist_net_find (full, NULL, strcmp);
	g_free (full);
	if (!e)
	{
		fe_message (_("Its entry isn't there yet; give it a second and try again."), FE_MSG_INFO);
		return;
	}
	for (l = serv_list; l; l = l->next)
	{
		server *sv = l->data;
		if (sv->network == e)
		{
			session *s = sv->front_session ? sv->front_session : sv->server_session;
			fe_ctrl_gui (s, FE_GUI_FOCUS, 0);
			if (!sv->connected && !sv->connecting)
				servlist_connect (sv->server_session, e, TRUE);
			return;
		}
	}
	servlist_connect (NULL, e, TRUE);
}

/* ── Stay online setup (bncsetup.c) ────────────────────────────────── */

typedef struct { nx_found_cb cb; void *ud; } detect_ctx;

static void
detected (bnc_found *f, gpointer data)
{
	detect_ctx *c = data;
	nx_found out = { 0 };
	GPtrArray *names = g_ptr_array_new (), *addrs = g_ptr_array_new ();
	guint i;

	for (i = 0; i < f->installed->len; i++)
	{
		bnc_installed *b = f->installed->pdata[i];
		if (b->path && !strcmp (b->kind, "soju"))
		{
			out.soju = TRUE;
			out.ours = b->ours;
			out.soju_version = b->version;
			if (b->service && !strcmp (b->service, "failed"))
				out.soju_failed = b->service_why ? b->service_why : _("see journalctl -u soju");
		}
		else if (b->path && !strcmp (b->kind, "znc"))
			out.znc = TRUE;
	}
	/* bncsetup.c detected: anything answering here that isn't ours */
	for (i = 0; i < f->listening->len; i++)
	{
		bnc_listening *b = f->listening->pdata[i];
		if (b->ours || !strcmp (b->kind, "plain"))
			continue;
		g_ptr_array_add (names, g_strdup_printf (_("%s answering on port %d%s"),
															  !strcmp (b->kind, "soju") ? "soju" : !strcmp (b->kind, "znc") ? "ZNC" : b->kind,
															  b->port, b->tls ? " (TLS)" : ""));
		g_ptr_array_add (addrs, g_strdup_printf ("%s127.0.0.1:%d", b->tls ? "" : "irc://", b->port));
	}
	out.n_listen = names->len;
	g_ptr_array_add (names, NULL);
	g_ptr_array_add (addrs, NULL);
	out.listen = (char **) names->pdata;
	out.listen_addr = (char **) addrs->pdata;
	c->cb (c->ud, &out);
	g_strfreev ((char **) g_ptr_array_free (names, FALSE));
	g_strfreev ((char **) g_ptr_array_free (addrs, FALSE));
	bnc_found_free (f);
	g_free (c);
}

void
nx_bnc_detect (nx_found_cb cb, void *ud)
{
	detect_ctx *c = g_new0 (detect_ctx, 1);
	c->cb = cb;
	c->ud = ud;
	bncdetect_run (detected, c);
}

typedef struct { nx_probe_cb cb; void *ud; } probe_ctx;

static void
probed (bnc_probe_result *r, gpointer data)
{
	probe_ctx *c = data;
	nx_probe p = { r->kind, r->error, r->networks, r->cert_untrusted, r->cert_why };
	c->cb (c->ud, &p);
	bnc_probe_result_free (r);
	g_free (c);
}

int
nx_bnc_probe (const char *addr, const char *user, const char *pass, nx_probe_cb cb, void *ud)
{
	address a;
	probe_ctx *c;

	if (!addr || !*addr || !bnc_parse_address (addr, &a))
		return FALSE;
	c = g_new0 (probe_ctx, 1);
	c->cb = cb;
	c->ud = ud;
	bncsetup_probe (a.host, a.port, a.tls, user, pass, probed, c);
	g_free (a.host);
	return TRUE;
}

/* bncsetup.c finish_bouncer */
int
nx_bnc_setup_bouncer (int from, const char *addr, const char *user, const char *pass,
							 int trust_cert, const char *networks, const char *other_host,
							 const char *other_name, char **err)
{
	address a;
	ircnet *net = net_at (from);
	char *srv;

	if (!bnc_parse_address (addr, &a))
	{
		*err = g_strdup (_("That address doesn't look right."));
		return -1;
	}
	if (!net || net->logintype != LOGIN_PASS)
	{
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
	net->flags = (net->flags & ~(FLAG_USE_SSL | FLAG_ALLOW_INVALID | FLAG_USE_GLOBAL)) | FLAG_CYCLE | FLAG_BOUNCER;
	if (a.tls)
		net->flags |= FLAG_USE_SSL;
	if (a.tls && trust_cert)
		net->flags |= FLAG_ALLOW_INVALID;
	if (!net->nick || !*net->nick)
	{
		g_free (net->nick);
		net->nick = g_strdup (*prefs.hex_irc_nick1 ? prefs.hex_irc_nick1 : user);
	}
	g_free (net->user);
	net->user = g_strdup (user);
	g_free (net->pass);
	net->pass = g_strdup (pass);
	net->logintype = LOGIN_PASS;
	{
		/* bncsetup.c: the "another one" host parsed, "name host:[-]port" */
		GString *todo = g_string_new (networks ? networks : "");
		address oa;
		if (other_host && *other_host && bnc_parse_address (other_host, &oa))
		{
			char *name = other_name && *other_name ? g_strdup (other_name) : g_strdup (oa.host);
			g_strdelimit (name, " ", '-');
			g_string_append_printf (todo, "%s %s:%s%d\n", name, oa.host, oa.tls ? "" : "-", oa.port);
			g_free (name);
			g_free (oa.host);
		}
		bouncer_queue_networks (net, todo->str);
		g_string_free (todo, TRUE);
	}
	servlist_save ();
	servlist_connect (NULL, net, TRUE);
	g_free (a.host);
	return index_of (net);
}

/* bncsetup.c finish_persist */
int
nx_bnc_setup_persist (int netidx, const char *account, const char *password, char **err)
{
	ircnet *net = net_at (netidx);
	if (!net)
	{
		*err = g_strdup (_("Pick a network."));
		return FALSE;
	}
	net->flags &= ~FLAG_USE_GLOBAL;
	g_free (net->nick);
	net->nick = g_strdup (account);
	g_free (net->user);
	net->user = g_strdup (account);
	g_free (net->pass);
	net->pass = g_strdup (password);
	net->logintype = LOGIN_SASL;
	net->flags |= FLAG_STAY_ONLINE;
	servlist_save ();
	servlist_connect (NULL, net, TRUE);
	return TRUE;
}

/* bncsetup.c popular[] */
int
nx_bnc_popular (int i, const char **name, const char **host)
{
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
	if (i < 0 || i >= (int) G_N_ELEMENTS (popular))
		return FALSE;
	*name = popular[i][0];
	*host = popular[i][1];
	return TRUE;
}
