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
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <fcntl.h>
#ifndef G_OS_WIN32
#include <gio/gunixsocketaddress.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "hexchat.h"
#include "hexchatc.h"
#include "cfgfiles.h"
#include "servlist.h"
#include "mybouncer.h"

#ifdef G_OS_WIN32
/* soju doesn't run on Windows: nothing to set up there */
mybnc_state mybouncer_state (void) { return MYBNC_NONE; }
const char *mybouncer_soju_path (void) { return NULL; }
gboolean mybouncer_running (void) { return FALSE; }
const char *mybouncer_kept_by (void) { return "nexchat"; }
gboolean mybouncer_setup (char **err) { *err = g_strdup (_("soju doesn't run on Windows.")); return FALSE; }
void mybouncer_adopt (GSList *nets, mybnc_progress cb, gpointer data) { cb ("", TRUE, FALSE, data); }
gboolean mybouncer_remove (char **err) { *err = NULL; return TRUE; }
void mybouncer_init (void) { }
gboolean mybouncer_owns (ircnet *net) { return FALSE; }
#else

#define UNIT "nexchat-bouncer.service"
#define USER "me"

/* <config>/bouncer: everything of ours */
static char *
dir (void)
{
	return g_build_filename (get_xdir (), "bouncer", NULL);
}

static char *
file (const char *name)
{
	char *d = dir (), *f = g_build_filename (d, name, NULL);
	g_free (d);
	return f;
}

static char *
read_file (const char *name)
{
	char *f = file (name), *s = NULL;
	if (g_file_get_contents (f, &s, NULL, NULL))
		g_strchomp (s);
	g_free (f);
	return s;
}

static gboolean
write_file (const char *name, const char *text, int mode)
{
	char *f = file (name);
	gboolean ok = g_file_set_contents (f, text, -1, NULL);
	if (ok)
		g_chmod (f, mode);
	g_free (f);
	return ok;
}

static const char *
find (const char *prog)
{
	static GHashTable *found;
	char *p;

	if (!found)
		found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	if (g_hash_table_contains (found, prog))
		return g_hash_table_lookup (found, prog);
	p = g_find_program_in_path (prog);
	/* tests: NEXCHAT_SOJU_DIR points at a soju not on PATH */
	if (!p && g_getenv ("NEXCHAT_SOJU_DIR"))
	{
		p = g_build_filename (g_getenv ("NEXCHAT_SOJU_DIR"), prog, NULL);
		if (!g_file_test (p, G_FILE_TEST_IS_EXECUTABLE))
			g_clear_pointer (&p, g_free);
	}
	g_hash_table_insert (found, g_strdup (prog), p);
	return p;
}

const char *
mybouncer_soju_path (void)
{
	return find ("soju");
}

mybnc_state
mybouncer_state (void)
{
	char *port;
	mybnc_state st;

	if (!find ("soju") || !find ("sojudb"))
		return MYBNC_NONE;
	port = read_file ("port");
	st = port ? MYBNC_SET_UP : MYBNC_AVAILABLE;
	g_free (port);
	return st;
}

static int
port (void)
{
	char *p = read_file ("port");
	int n = p ? atoi (p) : 0;
	g_free (p);
	return n;
}

gboolean
mybouncer_running (void)
{
	GSocketClient *c;
	GSocketConnection *conn;
	int p = port ();

	if (!p)
		return FALSE;
	c = g_socket_client_new ();
	g_socket_client_set_timeout (c, 1);
	conn = g_socket_client_connect_to_host (c, "127.0.0.1", p, NULL, NULL);
	g_object_unref (c);
	if (!conn)
		return FALSE;
	g_object_unref (conn);
	return TRUE;
}

/* ---- running it ---- */

static gboolean
have_systemd_user (void)
{
	const char *sc = find ("systemctl");
	char *out = NULL;
	int status = 1;

	if (!sc || g_getenv ("NEXCHAT_BOUNCER_NO_SYSTEMD"))
		return FALSE;
	/* "running" or "degraded": a user manager is there */
	if (!g_spawn_sync (NULL, (char *[]) { (char *) sc, "--user", "is-system-running", NULL }, NULL,
							 G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &status, NULL))
		return FALSE;
	g_strstrip (out);
	status = !strcmp (out, "running") || !strcmp (out, "degraded");
	g_free (out);
	return status;
}

const char *
mybouncer_kept_by (void)
{
	char *k = read_file ("kept-by");
	static char buf[16];
	g_strlcpy (buf, k ? k : "nexchat", sizeof buf);
	g_free (k);
	return buf;
}

static gboolean
systemctl (const char *a, const char *b, char **err)
{
	char *errout = NULL;
	int status = 0;
	gboolean ok = g_spawn_sync (NULL, (char *[]) { (char *) find ("systemctl"), "--user", (char *) a, (char *) b, NULL },
										 NULL, G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &errout, &status, NULL) &&
						g_spawn_check_wait_status (status, NULL);
	if (!ok && err)
		*err = g_strdup_printf (_("systemctl --user %s failed: %s"), a, errout ? g_strstrip (errout) : "");
	g_free (errout);
	return ok;
}

static GPid child;	/* ours, when nexchat keeps it */

static gboolean
start (char **err)
{
	char *cfg = file ("config"), *log = file ("soju.log");
	gboolean ok;

	if (mybouncer_running ())
		goto done;
	if (!strcmp (mybouncer_kept_by (), "systemd"))
	{
		ok = systemctl ("start", UNIT, err);
		if (!ok)
			goto out;
	}
	else
	{
		/* in a session of its own, so it stays up after nexchat quits (it's
		   found running next time); only a reboot stops it */
		char *argv[] = { "setsid", (char *) find ("soju"), "-config", cfg, NULL };
		int fd = g_open (log, O_WRONLY | O_CREAT | O_APPEND, 0600);
		GError *e = NULL;

		if (!find ("setsid"))
			argv[0] = (char *) find ("soju"), argv[1] = "-config", argv[2] = cfg, argv[3] = NULL;
		ok = g_spawn_async_with_fds (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDIN_FROM_DEV_NULL,
											  NULL, NULL, &child, -1, fd, fd, &e);
		if (fd >= 0)
			close (fd);
		if (!ok)
		{
			*err = g_strdup (e->message);
			g_error_free (e);
			goto out;
		}
	}
done:
	/* up within 5 s */
	{
		int i;
		for (i = 0; i < 50 && !mybouncer_running (); i++)
			g_usleep (100000);
		ok = mybouncer_running ();
		if (!ok)
		{
			char *l = file ("soju.log"), *tail = NULL;
			g_file_get_contents (l, &tail, NULL, NULL);
			*err = g_strdup_printf (_("soju didn't start. Its log (%s) says: %s"), l,
											tail ? (strrchr (g_strchomp (tail), '\n') ? strrchr (tail, '\n') + 1 : tail) : "-");
			g_free (tail);
			g_free (l);
		}
	}
out:
	g_free (cfg);
	g_free (log);
	return ok;
}

void
mybouncer_init (void)
{
	char *err = NULL;

	if (mybouncer_state () != MYBNC_SET_UP)
		return;
	if (!start (&err))
	{
		g_warning ("bouncer: %s", err);
		g_free (err);
	}
}

/* ---- setting up ---- */

static int
free_port (void)
{
	GSocket *s = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL);
	GInetAddress *lo = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
	GSocketAddress *a = g_inet_socket_address_new (lo, 0), *got;
	int p = 0;

	if (s && g_socket_bind (s, a, FALSE, NULL) && (got = g_socket_get_local_address (s, NULL)))
	{
		p = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (got));
		g_object_unref (got);
	}
	g_object_unref (a);
	g_object_unref (lo);
	if (s)
		g_object_unref (s);
	return p;
}

static char *
random_password (void)
{
	static const char abc[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	char p[25];
	int i;
	for (i = 0; i < 24; i++)
		p[i] = abc[g_random_int_range (0, sizeof abc - 1)];
	p[24] = 0;
	return g_strdup (p);
}

static char *
unit_text (void)
{
	char *cfg = file ("config");
	char *t = g_strdup_printf (
		"[Unit]\n"
		"Description=soju IRC bouncer for nexchat (stays in your channels)\n"
		"After=network-online.target\n\n"
		"[Service]\n"
		"ExecStart=%s -config %s\n"
		"Restart=on-failure\n"
		"RestartSec=10\n\n"
		"[Install]\n"
		"WantedBy=default.target\n", find ("soju"), cfg);
	g_free (cfg);
	return t;
}

gboolean
mybouncer_setup (char **err)
{
	char *d = dir (), *cfg, *db, *sock, *pass, *text;
	int p, status = 0;
	char *errout = NULL;

	*err = NULL;
	if (!find ("soju") || !find ("sojudb"))
	{
		*err = g_strdup (_("soju isn't installed. Install it from your system's packages (it's called soju) and try again."));
		g_free (d);
		return FALSE;
	}
	if (mybouncer_state () == MYBNC_SET_UP)
	{
		g_free (d);
		return start (err);
	}
	g_mkdir_with_parents (d, 0700);
	g_chmod (d, 0700);
	p = free_port ();
	db = file ("soju.db");
	sock = file ("admin.sock");
	text = g_strdup_printf ("# made by nexchat: your own soju, only reachable from this computer\n"
									"listen irc+insecure://127.0.0.1:%d\n"
									"listen unix+admin://%s\n"
									"hostname localhost\n"
									"db sqlite3 %s\n"
									"message-store db\n", p, sock, db);
	write_file ("config", text, 0600);
	g_free (text);

	/* our account on it */
	pass = random_password ();
	cfg = file ("config");
	{
		char *argv[] = { (char *) find ("sojudb"), "-config", cfg, "create-user", USER, "-admin", NULL };
		GSubprocess *sp = g_subprocess_newv ((const char * const *) argv,
														 G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
														 G_SUBPROCESS_FLAGS_STDERR_PIPE, NULL);
		char *in = g_strdup_printf ("%s\n", pass);
		GBytes *ib = g_bytes_new_take (in, strlen (in)), *eb = NULL;

		if (!sp || !g_subprocess_communicate (sp, ib, NULL, NULL, &eb, NULL) || !g_subprocess_get_successful (sp))
		{
			errout = eb ? g_strndup (g_bytes_get_data (eb, NULL), g_bytes_get_size (eb)) : NULL;
			*err = g_strdup_printf (_("Couldn't make an account on soju: %s"), errout ? g_strstrip (errout) : "?");
		}
		g_bytes_unref (ib);
		if (eb)
			g_bytes_unref (eb);
		if (sp)
			g_object_unref (sp);
	}
	g_free (cfg);
	if (*err)
		goto fail;
	write_file ("password", pass, 0600);

	/* kept running: by systemd when there is one, so it stays when nexchat
	   is closed; by nexchat otherwise */
	if (have_systemd_user ())
	{
		char *unitdir = g_build_filename (g_get_user_config_dir (), "systemd", "user", NULL);
		char *unit = g_build_filename (unitdir, UNIT, NULL);
		char *u = unit_text ();

		g_mkdir_with_parents (unitdir, 0755);
		g_file_set_contents (unit, u, -1, NULL);
		g_free (u);
		systemctl ("daemon-reload", NULL, NULL);
		if (systemctl ("enable", UNIT, NULL))
			write_file ("kept-by", "systemd", 0600);
		else
			write_file ("kept-by", "nexchat", 0600);
		g_free (unit);
		g_free (unitdir);
	}
	else
		write_file ("kept-by", "nexchat", 0600);
	{
		char *ps = g_strdup_printf ("%d", p);
		write_file ("port", ps, 0600);
		g_free (ps);
	}
	if (!start (err))
		goto fail;
	g_free (pass);
	g_free (db);
	g_free (sock);
	g_free (d);
	g_free (errout);
	(void) status;
	return TRUE;

fail:
	g_free (pass);
	g_free (db);
	g_free (sock);
	g_free (d);
	g_free (errout);
	return FALSE;
}

/* ---- the admin socket (what sojuctl does) ---- */

/* one word for soju's command line: quoted, \ and " escaped */
static void
word (GString *s, const char *w)
{
	const char *p;
	if (s->len)
		g_string_append_c (s, ' ');
	g_string_append_c (s, '"');
	for (p = w; *p; p++)
	{
		if (*p == '"' || *p == '\\')
			g_string_append_c (s, '\\');
		g_string_append_c (s, *p);
	}
	g_string_append_c (s, '"');
}

/* run a soju service command as our user; its error, or NULL */
static char *
admin (const char *const *words)
{
	char *path = file ("admin.sock"), *line, *res = NULL;
	GSocketClient *c = g_socket_client_new ();
	GSocketAddress *a = g_unix_socket_address_new (path);
	GSocketConnection *conn;
	GString *cmd = g_string_new (NULL), *q = g_string_new (NULL);
	GDataInputStream *in;
	GError *e = NULL;
	int i;

	word (cmd, "user");
	word (cmd, "run");
	word (cmd, USER);
	for (i = 0; words[i]; i++)
		word (cmd, words[i]);
	/* BOUNCERSERV takes the whole command as one parameter */
	g_string_append (q, "BOUNCERSERV :");
	g_string_append (q, cmd->str);
	g_string_append (q, "\r\n");

	conn = g_socket_client_connect (c, G_SOCKET_CONNECTABLE (a), NULL, &e);
	if (!conn)
	{
		res = g_strdup (e->message);
		g_error_free (e);
		goto out;
	}
	g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (conn)), q->str, q->len, NULL, NULL, NULL);
	in = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (conn)));
	g_data_input_stream_set_newline_type (in, G_DATA_STREAM_NEWLINE_TYPE_ANY);
	while ((line = g_data_input_stream_read_line_utf8 (in, NULL, NULL, NULL)))
	{
		/* "BOUNCERSERV OK", "PRIVMSG ... :text" or an error */
		if (strstr (line, "BOUNCERSERV OK"))
		{
			g_free (line);
			break;
		}
		if (!strstr (line, "PRIVMSG"))
		{
			char *colon = strstr (line, " :");
			res = g_strdup (colon ? colon + 2 : line);
			g_free (line);
			break;
		}
		g_free (line);
	}
	g_object_unref (in);
	g_object_unref (conn);
out:
	g_object_unref (a);
	g_object_unref (c);
	g_string_free (cmd, TRUE);
	g_string_free (q, TRUE);
	g_free (path);
	return res;
}

/* ---- moving networks onto it ---- */

/* <bouncer>/direct: "name\tserver\ttls\tuser\tlogintype" per entry moved,
   so Remove can put them back as they were */
static void
remember_direct (ircnet *net, const char *server, gboolean tls, const char *user, int logintype)
{
	char *old = read_file ("direct"), *line, *all;

	line = g_strdup_printf ("%s\t%s\t%d\t%s\t%d", net->name, server, tls, user ? user : "", logintype);
	all = g_strconcat (old ? old : "", old && *old ? "\n" : "", line, NULL);
	write_file ("direct", all, 0600);
	g_free (all);
	g_free (line);
	g_free (old);
}

static void
restore_direct (void)
{
	char *all = read_file ("direct"), **lines;
	int i;

	if (!all)
		return;
	lines = g_strsplit (all, "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		char **f = g_strsplit (lines[i], "\t", 5);
		ircnet *net;

		if (g_strv_length (f) == 5 && (net = servlist_net_find (f[0], NULL, strcmp)) && mybouncer_owns (net))
		{
			while (net->servlist)
				servlist_server_remove (net, net->servlist->data);
			servlist_server_add (net, f[1]);
			net->selected = 0;
			if (atoi (f[2]))
				net->flags |= FLAG_USE_SSL;
			g_free (net->user);
			net->user = *f[3] ? g_strdup (f[3]) : NULL;
			/* the network's own password went to soju; it's asked for again */
			g_clear_pointer (&net->pass, g_free);
			net->logintype = atoi (f[4]);
		}
		g_strfreev (f);
	}
	g_strfreev (lines);
	g_free (all);
	servlist_save ();
}

static char *
safe_name (const char *n)
{
	char *s = g_strdup (n), *p;
	for (p = s; *p; p++)
		if (*p == ' ' || *p == '/' || *p == ':' || *p == ';')
			*p = '-';
	return s;
}

gboolean
mybouncer_owns (ircnet *net)
{
	char *pw = read_file ("password");
	gboolean yes = FALSE;
	int p = port ();

	if (pw && p && net->servlist && net->pass && !strcmp (net->pass, pw))
	{
		char *mine = g_strdup_printf ("127.0.0.1/%d", p);
		yes = !strcmp (((ircserver *) net->servlist->data)->hostname, mine);
		g_free (mine);
	}
	g_free (pw);
	return yes;
}

/* one network: soju gets its address, nick and login, and its channels;
   the entry is changed to log in to soju as me/<name>. NULL or why not */
static char *
adopt_one (ircnet *net)
{
	ircserver *sv;
	char *host, *slash, *addr, *name, *res, *pw, *u;
	gboolean tls;
	GSList *l;
	const char *nick;

	if (mybouncer_owns (net))
		return NULL;
	if (!net->servlist)
		return g_strdup (_("it has no server address"));
	sv = g_slist_nth_data (net->servlist, net->selected);
	if (!sv)
		sv = net->servlist->data;
	host = g_strdup (sv->hostname);
	slash = strchr (host, '/');
	tls = (net->flags & FLAG_USE_SSL) || (slash && slash[1] == '+');
	if (slash)
	{
		int portn = atoi (slash + 1 + (slash[1] == '+'));
		*slash = 0;
		addr = g_strdup_printf ("%s://%s:%d", tls ? "ircs" : "irc+insecure", host, portn ? portn : (tls ? 6697 : 6667));
	}
	else
		addr = g_strdup_printf ("%s://%s", tls ? "ircs" : "irc+insecure", host);
	name = safe_name (net->name);
	nick = (net->flags & FLAG_USE_GLOBAL) || !net->nick ? prefs.hex_irc_nick1 : net->nick;

	{
		const char *w[] = { "network", "create", "-addr", addr, "-name", name, "-nick", nick, NULL };
		res = admin (w);
	}
	/* already there from an earlier try: fine */
	if (res && strstr (res, "already exists"))
		g_clear_pointer (&res, g_free);
	if (res)
		goto out;

	/* its login: SASL as soju does it, or the commands nexchat sent */
	if (net->pass && *net->pass)
	{
		const char *who = net->user && !(net->flags & FLAG_USE_GLOBAL) ? net->user : nick;
		if (net->logintype == LOGIN_SASL || net->logintype == LOGIN_DEFAULT)
		{
			const char *w[] = { "sasl", "set-plain", "-network", name, who, net->pass, NULL };
			g_free (admin (w));
		}
		else if (net->logintype == LOGIN_NICKSERV || net->logintype == LOGIN_MSG_NICKSERV)
		{
			char *cmd = g_strdup_printf ("PRIVMSG NickServ :IDENTIFY %s", net->pass);
			const char *w[] = { "network", "update", name, "-connect-command", cmd, NULL };
			g_free (admin (w));
			g_free (cmd);
		}
		else if (net->logintype == LOGIN_PASS)
		{
			const char *w[] = { "network", "update", name, "-pass", net->pass, NULL };
			g_free (admin (w));
		}
	}
	/* channels to be in, from the entry's favourites */
	for (l = net->favchanlist; l; l = l->next)
	{
		favchannel *f = l->data;
		char *chan = g_strdup_printf ("%s/%s", f->name, name);
		const char *w[] = { "channel", "create", chan, NULL };
		g_free (admin (w));
		g_free (chan);
	}

	/* the entry now goes through soju; its own address is kept, to go back */
	pw = read_file ("password");
	remember_direct (net, sv->hostname, tls, net->user, net->logintype);
	while (net->servlist)
		servlist_server_remove (net, net->servlist->data);
	{
		char *mine = g_strdup_printf ("127.0.0.1/%d", port ());
		servlist_server_add (net, mine);
		g_free (mine);
	}
	net->selected = 0;
	u = g_strdup_printf ("%s/%s", USER, name);
	g_free (net->user);
	net->user = u;
	g_free (net->pass);
	net->pass = pw;
	net->logintype = LOGIN_PASS;
	net->flags = (net->flags & ~(FLAG_USE_SSL | FLAG_USE_GLOBAL | FLAG_ALLOW_INVALID));
	if (!net->nick)
		net->nick = g_strdup (nick);
	/* soju joins them now; nexchat joining as well is harmless but noisy */
	g_slist_free_full (net->favchanlist, (GDestroyNotify) servlist_favchan_free);
	net->favchanlist = NULL;
out:
	g_free (host);
	g_free (addr);
	g_free (name);
	return res;
}

typedef struct
{
	GSList *nets;
	mybnc_progress cb;
	gpointer data;
	int ok, failed;
} adopt_job;

static gboolean
adopt_step (gpointer d)
{
	adopt_job *j = d;
	ircnet *net;
	char *why, *msg;

	if (!j->nets)
	{
		servlist_save ();
		msg = j->failed ? g_strdup_printf (_("%d moved to your bouncer, %d couldn't be."), j->ok, j->failed)
							 : g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "%d network moved to your bouncer.",
																	  "%d networks moved to your bouncer.", j->ok), j->ok);
		j->cb (msg, TRUE, j->failed == 0, j->data);
		g_free (msg);
		g_free (j);
		return G_SOURCE_REMOVE;
	}
	net = j->nets->data;
	j->nets = g_slist_delete_link (j->nets, j->nets);
	why = adopt_one (net);
	if (why)
	{
		msg = g_strdup_printf (_("%s: %s"), net->name, why);
		j->failed++;
	}
	else
	{
		msg = g_strdup_printf (_("%s: done"), net->name);
		j->ok++;
	}
	j->cb (msg, FALSE, why == NULL, j->data);
	g_free (msg);
	g_free (why);
	return G_SOURCE_CONTINUE;
}

void
mybouncer_adopt (GSList *nets, mybnc_progress cb, gpointer data)
{
	adopt_job *j = g_new0 (adopt_job, 1);
	j->nets = g_slist_copy (nets);
	j->cb = cb;
	j->data = data;
	g_idle_add (adopt_step, j);
}

gboolean
mybouncer_remove (char **err)
{
	*err = NULL;
	restore_direct ();
	if (!strcmp (mybouncer_kept_by (), "systemd"))
	{
		char *unit = g_build_filename (g_get_user_config_dir (), "systemd", "user", UNIT, NULL);
		systemctl ("disable", UNIT, NULL);
		systemctl ("stop", UNIT, NULL);
		g_unlink (unit);
		systemctl ("daemon-reload", NULL, NULL);
		g_free (unit);
	}
	else
	{
		/* started in a session of its own: found by its config, which is ours alone */
		char *cfg = file ("config"), *pat = g_strdup_printf ("soju -config %s", cfg);
		if (find ("pkill"))
			g_spawn_sync (NULL, (char *[]) { (char *) find ("pkill"), "-f", pat, NULL }, NULL,
							  G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		else if (child)
			kill (child, SIGTERM);
		g_free (pat);
		g_free (cfg);
	}
	{
		char *p = file ("port");
		g_unlink (p);
		g_free (p);
	}
	return TRUE;
}

#endif
