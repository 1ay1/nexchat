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
#include "server.h"

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
char *mybouncer_own_password (ircnet *net) { return NULL; }
void mybouncer_identity (ircnet *net, char **nick, char **nick_from, char **login, char **account)
{ *nick = g_strdup (""); *nick_from = g_strdup (""); *login = g_strdup (""); *account = NULL; }
gboolean mybouncer_set_login (ircnet *net, const char *account, const char *password, char **err)
{ *err = NULL; return FALSE; }
void mybouncer_logged_in (server *serv, const char *nick) { }
const char *mybouncer_wanted_nick (ircnet *net) { return net->nick ? net->nick : prefs.hex_irc_nick1; }
gboolean mybouncer_release (ircnet *net, char **err) { *err = NULL; return FALSE; }
gboolean mybouncer_start (char **err) { *err = NULL; return FALSE; }
void mybouncer_stop (void) { }
char *mybouncer_status (void) { return NULL; }
/* everything else the header has: nothing to get, and setting fails
   (bouncer.c's /bouncer commands are built everywhere) */
#define NOT_HERE (*err = g_strdup (_("soju doesn't run on Windows.")), FALSE)
gboolean mybouncer_can_systemd (void) { return FALSE; }
gboolean mybouncer_channel_add (ircnet *net, const char *chan, char **err) { return NOT_HERE; }
gboolean mybouncer_channel_del (ircnet *net, const char *chan, char **err) { return NOT_HERE; }
gboolean mybouncer_channel_set (ircnet *net, const char *chan, const char *key, const char *value, char **err) { return NOT_HERE; }
char *mybouncer_channels (ircnet *net) { return NULL; }
char *mybouncer_log_tail (int lines) { return NULL; }
gboolean mybouncer_logs_as_files (void) { return FALSE; }
char *mybouncer_net_certfp (ircnet *net, gboolean make, char **err) { NOT_HERE; return NULL; }
char *mybouncer_net_get (ircnet *net, const char *key) { return NULL; }
gboolean mybouncer_net_set (ircnet *net, const char *key, const char *value, char **err) { return NOT_HERE; }
gboolean mybouncer_net_set_commands (ircnet *net, const char *lines, char **err) { return NOT_HERE; }
char *mybouncer_password (void) { return NULL; }
int mybouncer_port (void) { return 0; }
gboolean mybouncer_remote (void) { return FALSE; }
char *mybouncer_remote_certfp (void) { return NULL; }
int mybouncer_remote_port (void) { return 0; }
char *mybouncer_run (const char *const *words, GString *out) { return g_strdup (_("soju doesn't run on Windows.")); }
gboolean mybouncer_set_kept_by_systemd (gboolean on, char **err) { return NOT_HERE; }
gboolean mybouncer_set_logs_as_files (gboolean on, char **err) { return NOT_HERE; }
gboolean mybouncer_set_password (const char *pw, char **err) { return NOT_HERE; }
gboolean mybouncer_set_remote (gboolean on, char **err) { return NOT_HERE; }
gboolean mybouncer_set_title (const char *title, char **err) { return NOT_HERE; }
char *mybouncer_soju_name (ircnet *net) { return NULL; }
char *mybouncer_title (void) { return NULL; }
const char *mybouncer_user (void) { return "me"; }
#undef NOT_HERE
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

#ifndef G_OS_WIN32
/* in the child: a session of its own, so it isn't stopped with nexchat
   (what the setsid tool does; macOS doesn't have that tool) */
static void
own_session (gpointer data)
{
	setsid ();
}
#endif

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
		char *argv[] = { (char *) find ("soju"), "-config", cfg, NULL };
		int fd = g_open (log, O_WRONLY | O_CREAT | O_APPEND, 0600);
		GError *e = NULL;

#ifndef G_OS_WIN32
		ok = g_spawn_async_with_fds (NULL, argv, NULL, G_SPAWN_STDIN_FROM_DEV_NULL,
											  own_session, NULL, &child, -1, fd, fd, &e);
#else
		ok = g_spawn_async_with_fds (NULL, argv, NULL, G_SPAWN_STDIN_FROM_DEV_NULL,
											  NULL, NULL, &child, -1, fd, fd, &e);
#endif
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

static int
free_port_any (void)
{
	GSocket *s = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL);
	GInetAddress *any = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
	GSocketAddress *a = g_inet_socket_address_new (any, 0), *got;
	int p = 0;

	if (s && g_socket_bind (s, a, FALSE, NULL) && (got = g_socket_get_local_address (s, NULL)))
	{
		p = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (got));
		g_object_unref (got);
	}
	g_object_unref (a);
	g_object_unref (any);
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

/* settings in small files next to it; the config is made from them */
static gboolean
setting_on (const char *name)
{
	char *v = read_file (name);
	gboolean on = v && !strcmp (v, "1");
	g_free (v);
	return on;
}

static int
setting_int (const char *name)
{
	char *v = read_file (name);
	int n = v ? atoi (v) : 0;
	g_free (v);
	return n;
}

/* soju's config, from the settings: loopback always; with \"remote\" a TLS
   listener on every address too, with a certificate made for it */
static void
write_config (void)
{
	GString *t = g_string_new ("# made by nexchat (Server > Bouncer): changed there, not here\n");
	char *sock = file ("admin.sock"), *db = file ("soju.db"), *title = read_file ("title");

	g_string_append_printf (t, "listen irc+insecure://127.0.0.1:%d\n", port ());
	g_string_append_printf (t, "listen unix+admin://%s\n", sock);
	if (setting_on ("remote") && setting_int ("remote-port"))
	{
		char *cert = file ("cert.pem"), *key = file ("key.pem");
		g_string_append_printf (t, "listen ircs://:%d\n", setting_int ("remote-port"));
		g_string_append_printf (t, "tls %s %s\n", cert, key);
		g_free (cert);
		g_free (key);
	}
	g_string_append (t, "hostname localhost\n");
	if (title && *title)
	{
		char *q = g_strescape (title, NULL);
		g_string_append_printf (t, "title \"%s\"\n", q);
		g_free (q);
	}
	g_string_append_printf (t, "db sqlite3 %s\n", db);
	if (setting_on ("logs-as-files"))
	{
		char *logs = file ("logs");
		g_string_append_printf (t, "message-store fs %s\n", logs);
		g_free (logs);
	}
	else
		g_string_append (t, "message-store db\n");
	write_file ("config", t->str, 0600);
	g_string_free (t, TRUE);
	g_free (sock);
	g_free (db);
	g_free (title);
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

/* sojudb ACTION me, the password on stdin. FALSE with its error line */
static gboolean
sojudb_user (const char *cfg, const char *action, const char *pass, char **errout)
{
	char *argv[] = { (char *) find ("sojudb"), "-config", (char *) cfg, (char *) action, USER, NULL, NULL };
	GSubprocess *sp;
	char *in = g_strdup_printf ("%s\n", pass);
	GBytes *ib = g_bytes_new_take (in, strlen (in)), *eb = NULL;
	gboolean ok;

	if (!strcmp (action, "create-user"))
		argv[5] = "-admin";
	sp = g_subprocess_newv ((const char *const *) argv, G_SUBPROCESS_FLAGS_STDIN_PIPE |
									G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE, NULL);
	ok = sp && g_subprocess_communicate (sp, ib, NULL, NULL, &eb, NULL) && g_subprocess_get_successful (sp);
	if (!ok && errout)
	{
		/* its last line; the first is a "Reading password from stdin" warning */
		char *all = eb ? g_strndup (g_bytes_get_data (eb, NULL), g_bytes_get_size (eb)) : g_strdup ("");
		char *last = strrchr (g_strstrip (all), '\n');
		g_free (*errout);
		*errout = g_strdup (last ? last + 1 : all);
		g_free (all);
	}
	g_bytes_unref (ib);
	if (eb)
		g_bytes_unref (eb);
	if (sp)
		g_object_unref (sp);
	return ok;
}

gboolean
mybouncer_setup (char **err)
{
	char *d = dir (), *cfg, *db, *sock, *pass;
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
	{
		char *ps = g_strdup_printf ("%d", p);
		write_file ("port", ps, 0600);
		g_free (ps);
	}
	write_config ();

	/* our account on it; there already when it was set up before */
	pass = random_password ();
	cfg = file ("config");
	if (!sojudb_user (cfg, "create-user", pass, &errout) &&
		 !(errout && strstr (errout, "UNIQUE") && sojudb_user (cfg, "change-password", pass, &errout)))
		*err = g_strdup_printf (_("Couldn't make an account on soju: %s"), errout ? errout : "?");
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

/* run a soju service command as our user; its error, or NULL. With
   out, what it printed goes there */
static char *
admin_out (const char *const *words, GString *out)
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
		if (out && strstr (line, " :"))
			g_string_append_printf (out, "%s\n", strstr (line, " :") + 2);
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

static char *
admin (const char *const *words)
{
	return admin_out (words, NULL);
}

/* ---- moving networks onto it ---- */

/* <bouncer>/direct: "name\tserver\ttls\tuser\tlogintype" per entry moved,
   so Remove can put them back as they were */
static void
restore_one (ircnet *net, char **f)
{
	char *pw;

	while (net->servlist)
		servlist_server_remove (net, net->servlist->data);
	servlist_server_add (net, f[1]);
	net->selected = 0;
	if (atoi (f[2]))
		net->flags |= FLAG_USE_SSL;
	g_free (net->user);
	net->user = *f[3] ? g_strdup (f[3]) : NULL;
	net->logintype = atoi (f[4]);
	/* its own password, kept in the bouncer folder (private) */
	pw = g_strdup_printf ("pass-%s", f[0]);
	g_strdelimit (pw, "/ ", '-');
	g_free (net->pass);
	net->pass = read_file (pw);
	g_free (pw);
}

static void
keep_pass (ircnet *net)
{
	char *f = g_strdup_printf ("pass-%s", net->name);
	g_strdelimit (f, "/ ", '-');
	if (net->pass && *net->pass)
		write_file (f, net->pass, 0600);
	g_free (f);
}

/* the "direct" line for net, removed from the file; NULL if none */
static char **
take_direct (ircnet *net)
{
	char *all = read_file ("direct"), **lines, **found = NULL;
	GString *rest = g_string_new (NULL);
	int i;

	if (!all)
		return NULL;
	lines = g_strsplit (all, "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		char **f = g_strsplit (lines[i], "\t", 5);
		if (!found && g_strv_length (f) == 5 && !strcmp (f[0], net->name))
			found = f;
		else
		{
			if (*lines[i])
				g_string_append_printf (rest, "%s%s", rest->len ? "\n" : "", lines[i]);
			g_strfreev (f);
		}
	}
	write_file ("direct", rest->str, 0600);
	g_string_free (rest, TRUE);
	g_strfreev (lines);
	g_free (all);
	return found;
}

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
			restore_one (net, f);
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

	/* the entry now goes through soju; its own address and login are kept,
	   to go back exactly as it was */
	pw = read_file ("password");
	remember_direct (net, sv->hostname, tls, net->user, net->logintype);
	keep_pass (net);
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
	/* the global nick setting stays as it was: change your nick there (or on
	   the network's own entry) and soju gets it on the next connect. The
	   user name is soju's login now, so it's no longer \"global\". */
	{
		gboolean global = (net->flags & FLAG_USE_GLOBAL) != 0;
		net->flags = (net->flags & ~(FLAG_USE_SSL | FLAG_USE_GLOBAL | FLAG_ALLOW_INVALID));
		if (global)
			g_clear_pointer (&net->nick, g_free);
		else if (!net->nick)
			net->nick = g_strdup (nick);
	}
	/* the channels stay on the entry: soju is in them already, so joining
	   again is a no-op, and they're there when it goes back */
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

gboolean
mybouncer_start (char **err)
{
	*err = NULL;
	return mybouncer_state () == MYBNC_SET_UP && start (err);
}

void
mybouncer_stop (void)
{
	if (!strcmp (mybouncer_kept_by (), "systemd"))
		systemctl ("stop", UNIT, NULL);
	else
	{
		char *cfg = file ("config"), *pat = g_strdup_printf ("soju -config %s", cfg);
		if (find ("pkill"))
			g_spawn_sync (NULL, (char *[]) { (char *) find ("pkill"), "-f", pat, NULL }, NULL,
							  G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		g_free (pat);
		g_free (cfg);
	}
}

gboolean
mybouncer_release (ircnet *net, char **err)
{
	char **f, *name;

	*err = NULL;
	if (!mybouncer_owns (net))
	{
		*err = g_strdup (_("That network isn't on your bouncer."));
		return FALSE;
	}
	name = safe_name (net->name);
	{
		const char *w[] = { "network", "delete", name, NULL };
		char *e = admin (w);
		/* gone already is fine */
		if (e && !strstr (e, "unknown network"))
		{
			*err = e;
			g_free (name);
			return FALSE;
		}
		g_free (e);
	}
	g_free (name);
	f = take_direct (net);
	if (f)
	{
		restore_one (net, f);
		g_strfreev (f);
	}
	servlist_save ();
	return TRUE;
}

/* "Lab (irc+insecure://h:p) [connected as dee]: 3 channels" per network */
char *
mybouncer_status (void)
{
	const char *w[] = { "network", "status", NULL };
	GString *out = g_string_new (NULL), *res = g_string_new (NULL);
	char *e, **lines;
	int i;

	if (!mybouncer_running ())
	{
		g_string_free (out, TRUE);
		g_string_free (res, TRUE);
		return NULL;
	}
	e = admin_out (w, out);
	g_free (e);
	lines = g_strsplit (out->str, "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		char *l = lines[i], *br = strchr (l, '['), *end, *name;
		if (!br || !(end = strchr (br, ']')))
			continue;
		name = g_strndup (l, strcspn (l, " ("));
		g_string_append_printf (res, "%s\t%.*s\t%s\n", name, (int) (end - br - 1), br + 1,
										end[1] == ':' ? g_strstrip (end + 2) : "");
		g_free (name);
	}
	g_strfreev (lines);
	g_string_free (out, TRUE);
	return g_string_free (res, FALSE);
}

const char *
mybouncer_wanted_nick (ircnet *net)
{
	return (net->flags & FLAG_USE_GLOBAL) || !net->nick || !*net->nick ? prefs.hex_irc_nick1 : net->nick;
}

void
mybouncer_logged_in (server *serv, const char *nick)
{
	ircnet *net = serv->network;
	const char *want;

	if (!net || !mybouncer_owns (net))
		return;
	want = mybouncer_wanted_nick (net);
	/* soju answers with the nick it has for that network: settings win */
	if (*want && serv->p_cmp (want, nick))
		tcp_sendf (serv, "NICK %s\r\n", want);
}

/* the \"direct\" record of net, without taking it out */
static char **
peek_direct (ircnet *net)
{
	char *all = read_file ("direct"), **lines, **found = NULL;
	int i;

	if (!all)
		return NULL;
	lines = g_strsplit (all, "\n", 0);
	for (i = 0; lines[i] && !found; i++)
	{
		char **f = g_strsplit (lines[i], "\t", 5);
		if (g_strv_length (f) == 5 && !strcmp (f[0], net->name))
			found = f;
		else
			g_strfreev (f);
	}
	g_strfreev (lines);
	g_free (all);
	return found;
}

static char *
own_pass (ircnet *net)
{
	char *f = g_strdup_printf ("pass-%s", net->name), *p;
	g_strdelimit (f, "/ ", '-');
	p = read_file (f);
	g_free (f);
	return p;
}

void
mybouncer_identity (ircnet *net, char **nick, char **nick_from, char **login, char **account)
{
	char **d = peek_direct (net), *pw = own_pass (net);
	int lt = d ? atoi (d[4]) : LOGIN_DEFAULT;
	const char *acct = d && *d[3] ? d[3] : NULL;

	*nick = g_strdup (mybouncer_wanted_nick (net));
	*nick_from = g_strdup ((net->flags & FLAG_USE_GLOBAL) || !net->nick || !*net->nick ?
								  _("your nick in the Network List") : _("this network's own nick"));
	*account = NULL;
	if (!pw || !*pw)
		*login = g_strdup (_("no login: you're connected as a guest"));
	else if (lt == LOGIN_SASL || lt == LOGIN_DEFAULT)
	{
		*account = g_strdup (acct ? acct : *nick);
		*login = g_strdup_printf (_("logged in to the account %s (SASL)"), *account);
	}
	else if (lt == LOGIN_NICKSERV || lt == LOGIN_MSG_NICKSERV)
	{
		*account = g_strdup (*nick);
		*login = g_strdup_printf (_("identified to NickServ as %s"), *nick);
	}
	else
		*login = g_strdup (_("with the server password"));
	g_strfreev (d);
	g_free (pw);
}

gboolean
mybouncer_set_login (ircnet *net, const char *account, const char *password, char **err)
{
	char *name = safe_name (net->name), *e, **d, *line, *pwf, *all, **lines;
	GString *rest;
	int i;

	*err = NULL;
	if (!mybouncer_owns (net))
	{
		*err = g_strdup (_("That network isn't on your bouncer."));
		g_free (name);
		return FALSE;
	}
	if (account && *account)
	{
		const char *w[] = { "sasl", "set-plain", "-network", name, account, password ? password : "", NULL };
		e = admin (w);
	}
	else
	{
		const char *w[] = { "sasl", "reset", "-network", name, NULL };
		e = admin (w);
	}
	if (e)
	{
		*err = e;
		g_free (name);
		return FALSE;
	}
	/* soju logs in again with it */
	{
		const char *w[] = { "network", "update", name, "-enabled", "false", NULL };
		const char *w2[] = { "network", "update", name, "-enabled", "true", NULL };
		g_free (admin (w));
		g_free (admin (w2));
	}
	g_free (name);

	/* the record and the kept password, for going back later */
	pwf = g_strdup_printf ("pass-%s", net->name);
	g_strdelimit (pwf, "/ ", '-');
	if (account && *account && password)
		write_file (pwf, password, 0600);
	else
	{
		char *f = file (pwf);
		g_unlink (f);
		g_free (f);
	}
	g_free (pwf);
	d = peek_direct (net);
	all = read_file ("direct");
	rest = g_string_new (NULL);
	lines = g_strsplit (all ? all : "", "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		char **f = g_strsplit (lines[i], "\t", 5);
		if (g_strv_length (f) == 5 && !strcmp (f[0], net->name))
			line = g_strdup_printf ("%s\t%s\t%s\t%s\t%d", f[0], f[1], f[2], account ? account : "",
											account && *account ? LOGIN_SASL : LOGIN_DEFAULT);
		else
			line = g_strdup (lines[i]);
		if (*line)
			g_string_append_printf (rest, "%s%s", rest->len ? "\n" : "", line);
		g_free (line);
		g_strfreev (f);
	}
	write_file ("direct", rest->str, 0600);
	g_string_free (rest, TRUE);
	g_strfreev (lines);
	g_free (all);
	g_strfreev (d);
	return TRUE;
}

char *
mybouncer_own_password (ircnet *net)
{
	return own_pass (net);
}

/* ---- everything else ---- */

char *
mybouncer_soju_name (ircnet *net)
{
	return safe_name (net->name);
}

char *
mybouncer_run (const char *const *words, GString *out)
{
	return admin_out (words, out);
}

static gboolean
ok_or (char *e, char **err)
{
	if (e)
	{
		*err = e;
		return FALSE;
	}
	*err = NULL;
	return TRUE;
}

/* soju can't tell us these back: what we last set, in nets.ini */
static void
net_remember (ircnet *net, const char *key, const char *value)
{
	char *f = file ("nets.ini"), *name = safe_name (net->name);
	GKeyFile *k = g_key_file_new ();

	g_key_file_load_from_file (k, f, G_KEY_FILE_NONE, NULL);
	g_key_file_set_string (k, name, key, value ? value : "");
	g_key_file_save_to_file (k, f, NULL);
	g_chmod (f, 0600);
	g_key_file_free (k);
	g_free (name);
	g_free (f);
}

char *
mybouncer_net_get (ircnet *net, const char *key)
{
	char *f = file ("nets.ini"), *name = safe_name (net->name), *v = NULL;
	GKeyFile *k = g_key_file_new ();

	if (g_key_file_load_from_file (k, f, G_KEY_FILE_NONE, NULL))
		v = g_key_file_get_string (k, name, key, NULL);
	g_key_file_free (k);
	g_free (name);
	g_free (f);
	return v;
}

gboolean
mybouncer_net_set (ircnet *net, const char *key, const char *value, char **err)
{
	char *name = safe_name (net->name), *flag = g_strdup_printf ("-%s", key);
	const char *w[] = { "network", "update", name, flag, value, NULL };
	gboolean ok = ok_or (admin (w), err);
	if (ok)
		net_remember (net, key, value);
	g_free (flag);
	g_free (name);
	return ok;
}

gboolean
mybouncer_net_set_commands (ircnet *net, const char *lines, char **err)
{
	char *name = safe_name (net->name), **l = g_strsplit (lines ? lines : "", "\n", 0);
	GPtrArray *w = g_ptr_array_new ();
	gboolean ok;
	int i;

	g_ptr_array_add (w, "network");
	g_ptr_array_add (w, "update");
	g_ptr_array_add (w, name);
	for (i = 0; l[i]; i++)
	{
		char *c = g_strstrip (l[i]);
		if (!*c)
			continue;
		/* "/msg NickServ ..." as people write it: a raw line for soju */
		if (!g_ascii_strncasecmp (c, "/msg ", 5) && strchr (c + 5, ' '))
		{
			char *sp = strchr (c + 5, ' ');
			char *raw = g_strdup_printf ("PRIVMSG %.*s :%s", (int) (sp - c - 5), c + 5, sp + 1);
			g_free (l[i]);
			l[i] = raw;
			c = raw;
		}
		else if (c[0] == '/')
			memmove (c, c + 1, strlen (c));
		g_ptr_array_add (w, "-connect-command");
		g_ptr_array_add (w, c);
	}
	/* none: soju takes an empty one as \"clear them\" */
	if (w->len == 3)
	{
		g_ptr_array_add (w, "-connect-command");
		g_ptr_array_add (w, "");
	}
	g_ptr_array_add (w, NULL);
	ok = ok_or (admin ((const char *const *) w->pdata), err);
	if (ok)
		net_remember (net, "commands", lines);
	g_ptr_array_free (w, TRUE);
	g_strfreev (l);
	g_free (name);
	return ok;
}

char *
mybouncer_net_certfp (ircnet *net, gboolean make, char **err)
{
	char *name = safe_name (net->name), *fp = NULL, *e;
	GString *out = g_string_new (NULL);

	*err = NULL;
	if (make)
	{
		const char *w[] = { "certfp", "generate", "-network", name, NULL };
		e = admin_out (w, out);
	}
	else
	{
		const char *w[] = { "certfp", "fingerprint", "-network", name, NULL };
		e = admin_out (w, out);
	}
	if (e)
		*err = e;
	else
	{
		char *p = strstr (out->str, "SHA-256 fingerprint: ");
		if (p)
			fp = g_strndup (p + 21, strcspn (p + 21, "\n"));
	}
	g_string_free (out, TRUE);
	g_free (name);
	return fp;
}

char *
mybouncer_channels (ircnet *net)
{
	char *name = safe_name (net->name), *e, **lines;
	const char *w[] = { "channel", "status", "-network", name, NULL };
	GString *out = g_string_new (NULL), *res = g_string_new (NULL);
	int i;

	e = admin_out (w, out);
	g_free (e);
	lines = g_strsplit (out->str, "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		/* "#chan [joined, detached]" */
		char *br = strchr (lines[i], '[');
		if (!br)
			continue;
		g_string_append_printf (res, "%.*s\t%.*s\n", (int) (br - lines[i] - 1), lines[i],
										(int) strcspn (br + 1, "]"), br + 1);
	}
	g_strfreev (lines);
	g_string_free (out, TRUE);
	g_free (name);
	return g_string_free (res, FALSE);
}

static char *
chan_ref (ircnet *net, const char *chan)
{
	char *name = safe_name (net->name), *r = g_strdup_printf ("%s/%s", chan, name);
	g_free (name);
	return r;
}

gboolean
mybouncer_channel_add (ircnet *net, const char *chan, char **err)
{
	char *r = chan_ref (net, chan);
	const char *w[] = { "channel", "create", r, NULL };
	char *e = admin (w);
	gboolean ok;
	/* already there: fine */
	if (e && strstr (e, "already exists"))
		g_clear_pointer (&e, g_free);
	ok = ok_or (e, err);
	g_free (r);
	/* the entry's favourites follow, so it's there when it goes back */
	if (ok && !servlist_favchan_find (net, (char *) chan, NULL))
	{
		servlist_favchan_add (net, (char *) chan);
		servlist_save ();
	}
	return ok;
}

gboolean
mybouncer_channel_del (ircnet *net, const char *chan, char **err)
{
	char *r = chan_ref (net, chan);
	const char *w[] = { "channel", "delete", r, NULL };
	gboolean ok = ok_or (admin (w), err);
	favchannel *f = servlist_favchan_find (net, (char *) chan, NULL);
	g_free (r);
	if (ok && f)
	{
		servlist_favchan_remove (net, f);
		servlist_save ();
	}
	return ok;
}

gboolean
mybouncer_channel_set (ircnet *net, const char *chan, const char *key, const char *value, char **err)
{
	char *r = chan_ref (net, chan), *flag = g_strdup_printf ("-%s", key);
	const char *w[] = { "channel", "update", r, flag, value, NULL };
	gboolean ok = ok_or (admin (w), err);
	g_free (flag);
	g_free (r);
	return ok;
}

char *
mybouncer_password (void)
{
	return read_file ("password");
}

const char *
mybouncer_user (void)
{
	return USER;
}

char *
mybouncer_remote_certfp (void)
{
	char *cert = file ("cert.pem"), *out = NULL, *fp = NULL, *p;
	const char *argv[] = { "openssl", "x509", "-noout", "-fingerprint", "-sha256", "-in", cert, NULL };

	if (g_file_test (cert, G_FILE_TEST_EXISTS) &&
		 g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
						 NULL, NULL, &out, NULL, NULL, NULL) && out && (p = strchr (out, '=')))
		fp = g_strdup (g_strstrip (p + 1));
	g_free (out);
	g_free (cert);
	return fp;
}

int
mybouncer_port (void)
{
	return port ();
}

gboolean
mybouncer_set_password (const char *pw, char **err)
{
	const char *w[] = { "user", "update", "-password", pw, NULL };
	char *old = read_file ("password");
	GSList *l;
	gboolean ok;

	if (!pw || strlen (pw) < 8)
	{
		*err = g_strdup (_("At least 8 characters, please."));
		g_free (old);
		return FALSE;
	}
	ok = ok_or (admin (w), err);
	if (ok)
	{
		write_file ("password", pw, 0600);
		/* the entries that log in to it with the old one */
		for (l = network_list; l; l = l->next)
		{
			ircnet *n = l->data;
			if (old && n->pass && !strcmp (n->pass, old))
			{
				g_free (n->pass);
				n->pass = g_strdup (pw);
			}
		}
		servlist_save ();
	}
	g_free (old);
	return ok;
}

static gboolean
restart (char **err)
{
	int i;

	/* stopped: the new config is used when it's started */
	if (!mybouncer_running ())
		return TRUE;
	mybouncer_stop ();
	for (i = 0; i < 50 && mybouncer_running (); i++)
		g_usleep (100000);
	return start (err);
}

gboolean
mybouncer_remote (void)
{
	return setting_on ("remote");
}

int
mybouncer_remote_port (void)
{
	return setting_int ("remote-port");
}

/* a self-signed certificate for its TLS port, made once */
static gboolean
make_cert (char **err)
{
	char *cert = file ("cert.pem"), *key = file ("key.pem"), *errout = NULL;
	gboolean ok = TRUE;
	int st = 0;

	if (!g_file_test (cert, G_FILE_TEST_EXISTS))
	{
		const char *ossl = find ("openssl");
		char *argv[] = { (char *) ossl, "req", "-x509", "-newkey", "rsa:3072", "-nodes", "-days", "3650",
							  "-subj", "/CN=nexchat bouncer", "-keyout", key, "-out", cert, NULL };
		if (!ossl)
		{
			*err = g_strdup (_("Making a certificate needs openssl, which isn't installed."));
			ok = FALSE;
		}
		else if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &errout, &st, NULL) ||
					!g_spawn_check_wait_status (st, NULL))
		{
			*err = g_strdup_printf (_("Couldn't make a certificate: %s"), errout ? g_strstrip (errout) : "?");
			ok = FALSE;
		}
		if (ok)
			g_chmod (key, 0600);
	}
	g_free (errout);
	g_free (cert);
	g_free (key);
	return ok;
}

gboolean
mybouncer_set_remote (gboolean on, char **err)
{
	*err = NULL;
	if (on)
	{
		if (!make_cert (err))
			return FALSE;
		if (!setting_int ("remote-port"))
			write_file ("remote-port", "6697", 0600);
	}
	write_file ("remote", on ? "1" : "0", 0600);
	write_config ();
	if (!restart (err) && on)
	{
		/* 6697 taken: another port */
		char *p = g_strdup_printf ("%d", free_port_any ());
		write_file ("remote-port", p, 0600);
		g_free (p);
		g_clear_pointer (err, g_free);
		write_config ();
		return restart (err);
	}
	return *err == NULL;
}

gboolean
mybouncer_logs_as_files (void)
{
	return setting_on ("logs-as-files");
}

gboolean
mybouncer_set_logs_as_files (gboolean on, char **err)
{
	*err = NULL;
	write_file ("logs-as-files", on ? "1" : "0", 0600);
	write_config ();
	return restart (err);
}

char *
mybouncer_title (void)
{
	return read_file ("title");
}

gboolean
mybouncer_set_title (const char *title, char **err)
{
	*err = NULL;
	write_file ("title", title ? title : "", 0600);
	write_config ();
	return restart (err);
}

char *
mybouncer_log_tail (int n)
{
	char *f = file ("soju.log"), *all = NULL, **lines, *res;
	int len, from;

	if (!strcmp (mybouncer_kept_by (), "systemd"))
	{
		const char *j = find ("journalctl");
		char *ns = g_strdup_printf ("%d", n), *out = NULL;
		if (j)
			g_spawn_sync (NULL, (char *[]) { (char *) j, "--user", "-u", UNIT, "-n", ns, "--no-pager", "-o", "cat", NULL },
							  NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, NULL, NULL);
		g_free (ns);
		g_free (f);
		return out ? out : g_strdup ("");
	}
	if (!g_file_get_contents (f, &all, NULL, NULL))
	{
		g_free (f);
		return g_strdup ("");
	}
	g_free (f);
	lines = g_strsplit (all, "\n", 0);
	len = g_strv_length (lines);
	from = len > n ? len - n : 0;
	res = g_strjoinv ("\n", lines + from);
	g_strfreev (lines);
	g_free (all);
	return res;
}

gboolean
mybouncer_can_systemd (void)
{
	return have_systemd_user ();
}

gboolean
mybouncer_set_kept_by_systemd (gboolean on, char **err)
{
	char *unitdir = g_build_filename (g_get_user_config_dir (), "systemd", "user", NULL);
	char *unit = g_build_filename (unitdir, UNIT, NULL);

	*err = NULL;
	mybouncer_stop ();
	if (on)
	{
		char *u = unit_text ();
		g_mkdir_with_parents (unitdir, 0755);
		g_file_set_contents (unit, u, -1, NULL);
		g_free (u);
		systemctl ("daemon-reload", NULL, NULL);
		if (!systemctl ("enable", UNIT, err))
			on = FALSE;
	}
	else if (g_file_test (unit, G_FILE_TEST_EXISTS))
	{
		systemctl ("disable", UNIT, NULL);
		g_unlink (unit);
		systemctl ("daemon-reload", NULL, NULL);
	}
	write_file ("kept-by", on ? "systemd" : "nexchat", 0600);
	g_free (unit);
	g_free (unitdir);
	if (*err)
	{
		char *e2 = NULL;
		start (&e2);
		g_free (e2);
		return FALSE;
	}
	return start (err);
}

#endif
