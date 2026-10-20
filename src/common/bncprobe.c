/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* What a bouncer is, without a window: the login probe ("is this soju,
   ZNC, a network that keeps you on, or a plain server, and is the password
   right"), what this computer has (soju / ZNC installed, anything
   answering on the usual ports), and the address parser the setup uses.
   Shared by fe-gtk's bncsetup/bncwin and the TUI's bouncer windows, so
   both frontends find out the same things the same way. */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "hexchat.h"
#include "hexchatc.h"
#include "cfgfiles.h"
#include "mybouncer.h"
#include "bncprobe.h"

#ifdef USE_OPENSSL
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include "ssl.h"
#endif

/* ---- the login probe: one connection, CAP LS, PASS/NICK/USER, wait ----
   Runs in a thread over a non-blocking socket. TLS goes through OpenSSL like
   every other connection nexchat makes, not GIO's TLS, whose backend
   (glib-networking) often isn't there: Homebrew, the macOS bundle. */

#define PROBE_SECONDS 15

typedef struct
{
	char *host;
	int port;
	gboolean tls;
	char *user, *pass;
	gboolean znc, soju, persist;
	GTlsCertificateFlags cert_errors;	/* what's wrong with its certificate */
	bnc_probe_cb cb;
	gpointer data;
	/* the thread's */
	GSocketConnection *conn;
	GSocket *sock;
#ifdef USE_OPENSSL
	SSL_CTX *ctx;
	SSL *ssl;
#endif
	gint64 deadline;
	gboolean done;
	char *kind, *error;	/* the answer */
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

/* in the thread: the answer, once */
static void
probe_finish (probe *p, const char *kind, const char *error)
{
	if (p->done)
		return;
	p->done = TRUE;
	p->kind = g_strdup (kind);
	p->error = g_strdup (error);
}

/* on the main thread: hand it over */
static gboolean
probe_report (gpointer data)
{
	probe *p = data;
	bnc_probe_result *r;

	r = g_new0 (bnc_probe_result, 1);
	r->kind = p->kind;
	r->error = p->error;
	p->kind = p->error = NULL;
	if (!r->kind && !r->error)
		r->error = g_strdup (_("The connection was closed without an answer."));
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

	g_free (p->host);
	g_free (p->user);
	g_free (p->pass);
	g_free (p);
	return G_SOURCE_REMOVE;
}

/* time left before the deadline, in ms; 0 when it's passed */
static int
probe_left (probe *p)
{
	gint64 left = (p->deadline - g_get_monotonic_time ()) / 1000;
	return left > 0 ? (int) left : 0;
}

/* wait until the socket can be read (or written); FALSE on timeout */
static gboolean
probe_wait (probe *p, GIOCondition cond)
{
	int left = probe_left (p);
	GError *err = NULL;

	if (!left)
		return FALSE;
	g_socket_set_timeout (p->sock, (left + 999) / 1000);
	if (!g_socket_condition_timed_wait (p->sock, cond, (gint64) left * 1000, NULL, &err))
	{
		g_error_free (err);
		return FALSE;
	}
	return TRUE;
}

static void
probe_send (probe *p, const char *line)
{
	char *l = g_strconcat (line, "\r\n", NULL);
	gsize len = strlen (l), off = 0;

	while (off < len && !p->done)
	{
		gssize n;
#ifdef USE_OPENSSL
		if (p->ssl)
		{
			n = SSL_write (p->ssl, l + off, len - off);
			if (n <= 0)
			{
				int e = SSL_get_error (p->ssl, n);
				if ((e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) &&
					 probe_wait (p, e == SSL_ERROR_WANT_READ ? G_IO_IN : G_IO_OUT))
					continue;
				break;
			}
		}
		else
#endif
		{
			n = g_socket_send (p->sock, l + off, len - off, NULL, NULL);
			if (n <= 0)
				break;
		}
		off += n;
	}
	g_free (l);
}

/* some bytes, or -1 when it's closed, failed or timed out */
static gssize
probe_recv (probe *p, char *buf, gsize size)
{
	for (;;)
	{
		gssize n;
#ifdef USE_OPENSSL
		if (p->ssl)
		{
			n = SSL_read (p->ssl, buf, size);
			if (n > 0)
				return n;
			switch (SSL_get_error (p->ssl, n))
			{
			case SSL_ERROR_WANT_READ:
				if (!probe_wait (p, G_IO_IN))
					return -1;
				continue;
			case SSL_ERROR_WANT_WRITE:
				if (!probe_wait (p, G_IO_OUT))
					return -1;
				continue;
			default:
				return -1;
			}
		}
#endif
		if (!probe_wait (p, G_IO_IN))
			return -1;
		n = g_socket_receive (p->sock, buf, size, NULL, NULL);
		return n > 0 ? n : -1;
	}
}

/* one line from the server */
static void
probe_line (probe *p, char *line)
{
	char **w;

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
}

#ifdef USE_OPENSSL
/* look at everything, judge afterwards */
static int
probe_verify_any (int ok, X509_STORE_CTX *ctx)
{
	return 1;
}

/* encrypt the connection; FALSE (and the answer) when that fails */
static gboolean
probe_tls (probe *p)
{
	X509 *cert;
	long v;

	ERR_clear_error ();
	if (!(p->ctx = _SSL_context_init (NULL)) || _SSL_set_verify (p->ctx, probe_verify_any))
	{
		probe_finish (p, NULL, _("TLS couldn't be set up."));
		return FALSE;
	}
	p->ssl = SSL_new (p->ctx);
	SSL_set_fd (p->ssl, g_socket_get_fd (p->sock));
	SSL_set_connect_state (p->ssl);
	if (!g_hostname_is_ip_address (p->host))
		SSL_set_tlsext_host_name (p->ssl, p->host);

	for (;;)
	{
		int r = SSL_connect (p->ssl), e;
		unsigned long why;

		if (r == 1)
			break;
		e = SSL_get_error (p->ssl, r);
		if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
		{
			if (probe_wait (p, e == SSL_ERROR_WANT_READ ? G_IO_IN : G_IO_OUT))
				continue;
			probe_finish (p, NULL, _("No answer within 15 seconds."));
			return FALSE;
		}
		why = ERR_peek_last_error ();
		/* encrypted to a plain-text port: what came back wasn't TLS */
		if (ERR_GET_LIB (why) == ERR_LIB_SSL &&
			 (ERR_GET_REASON (why) == SSL_R_WRONG_VERSION_NUMBER ||
			  ERR_GET_REASON (why) == SSL_R_PACKET_LENGTH_TOO_LONG ||
			  ERR_GET_REASON (why) == SSL_R_HTTP_REQUEST))
			probe_finish (p, NULL, _("It doesn't speak TLS on that port. Write irc:// in front of the address "
											 "for a plain-text connection."));
		else if (why)
		{
			char buf[256];
			ERR_error_string_n (why, buf, sizeof buf);
			probe_finish (p, NULL, buf);
		}
		else
			probe_finish (p, NULL, _("The connection was closed during the TLS handshake."));
		return FALSE;
	}

	/* trusted? the same checks a real connection makes */
	v = SSL_get_verify_result (p->ssl);
	if (v == X509_V_ERR_CERT_HAS_EXPIRED || v == X509_V_ERR_CERT_NOT_YET_VALID)
		p->cert_errors |= G_TLS_CERTIFICATE_EXPIRED;
	else if (v != X509_V_OK)
		p->cert_errors |= G_TLS_CERTIFICATE_UNKNOWN_CA;
	if ((cert = SSL_get_peer_certificate (p->ssl)))
	{
		if (_SSL_check_hostname (cert, p->host) != 0)
			p->cert_errors |= G_TLS_CERTIFICATE_BAD_IDENTITY;
		X509_free (cert);
	}
	else
		p->cert_errors |= G_TLS_CERTIFICATE_UNKNOWN_CA;
	return TRUE;
}
#endif

static gpointer
probe_thread (gpointer data)
{
	probe *p = data;
	GSocketClient *client = g_socket_client_new ();
	GError *err = NULL;
	GString *buf;

	g_socket_client_set_timeout (client, PROBE_SECONDS);
	p->conn = g_socket_client_connect_to_host (client, p->host, p->port, NULL, &err);
	g_object_unref (client);
	if (!p->conn)
	{
		probe_finish (p, NULL, err->message);
		g_error_free (err);
		goto out;
	}
	p->sock = g_socket_connection_get_socket (p->conn);
	g_socket_set_blocking (p->sock, FALSE);

	if (p->tls)
	{
#ifdef USE_OPENSSL
		if (!probe_tls (p))
			goto out;
#else
		probe_finish (p, NULL, _("This nexchat was built without TLS."));
		goto out;
#endif
	}

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

	/* lines until it's decided */
	buf = g_string_new (NULL);
	while (!p->done)
	{
		char chunk[4096], *nl;
		gssize n = probe_recv (p, chunk, sizeof chunk);

		if (n < 0)
		{
			probe_finish (p, NULL, probe_left (p) ? _("The connection was closed without an answer.")
												  : _("No answer within 15 seconds."));
			break;
		}
		g_string_append_len (buf, chunk, n);
		while (!p->done && (nl = memchr (buf->str, '\n', buf->len)))
		{
			char *line = g_strndup (buf->str, nl - buf->str);
			g_string_erase (buf, 0, nl - buf->str + 1);
			if (!g_utf8_validate (line, -1, NULL))
			{
				char *u = g_utf8_make_valid (line, -1);
				g_free (line);
				line = u;
			}
			probe_line (p, line);
			g_free (line);
		}
	}
	g_string_free (buf, TRUE);

out:
#ifdef USE_OPENSSL
	if (p->ssl)
	{
		SSL_shutdown (p->ssl);
		SSL_free (p->ssl);
	}
	if (p->ctx)
		SSL_CTX_free (p->ctx);
#endif
	if (p->conn)
	{
		g_io_stream_close (G_IO_STREAM (p->conn), NULL, NULL);
		g_object_unref (p->conn);
	}
	g_idle_add (probe_report, p);
	return NULL;
}

void
bncsetup_probe (const char *host, int port, gboolean tls, const char *user,
					 const char *pass, bnc_probe_cb cb, gpointer data)
{
	probe *p = g_new0 (probe, 1);

	p->host = g_strdup (host);
	p->port = port;
	p->tls = tls;
	p->user = g_strdup (user ? user : "");
	p->pass = g_strdup (pass ? pass : "");
	p->cb = cb;
	p->data = data;
	p->deadline = g_get_monotonic_time () + (gint64) PROBE_SECONDS * G_USEC_PER_SEC;
	g_thread_unref (g_thread_new ("bnc-probe", probe_thread, p));
}

/* ---- the address: "host", "host:port", "ircs://host:port", "irc://..." ---- */

/* ---- the address: "host", "host:port", "ircs://host:port", "irc://..." ---- */


gboolean
bnc_parse_address (const char *in, address *a)
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

/* ---- what this computer has ---- */

static void
installed_free (gpointer p)
{
	bnc_installed *i = p;
	g_free (i->kind);
	g_free (i->path);
	g_free (i->version);
	g_free (i->config);
	g_free (i->service);
	g_free (i->service_why);
	g_free (i);
}

static void
listening_free (gpointer p)
{
	bnc_listening *l = p;
	g_free (l->kind);
	g_free (l);
}

void
bnc_found_free (bnc_found *f)
{
	if (!f)
		return;
	g_ptr_array_unref (f->installed);
	g_ptr_array_unref (f->listening);
	g_free (f);
}

/* ---- programs, configs, services ---- */

static char *
run_out (const char *const *argv)
{
	char *out = NULL;
	int st = 0;

	if (!argv[0] || !g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH,
											 NULL, NULL, &out, NULL, &st, NULL))
		return NULL;
	if (out)
		g_strstrip (out);
	return out;
}

/* the package's version, from whatever package manager there is */
static char *
version_of (const char *pkg)
{
	char *v = NULL;

	if (g_find_program_in_path ("pacman"))
	{
		const char *a[] = { "pacman", "-Q", pkg, NULL };
		char *o = run_out (a);
		if (o && strchr (o, ' '))
			v = g_strdup (strchr (o, ' ') + 1);
		g_free (o);
	}
	else if (g_find_program_in_path ("dpkg-query"))
	{
		const char *a[] = { "dpkg-query", "-W", "-f=${Version}", pkg, NULL };
		v = run_out (a);
	}
	else if (g_find_program_in_path ("rpm"))
	{
		const char *a[] = { "rpm", "-q", "--qf", "%{VERSION}", pkg, NULL };
		v = run_out (a);
	}
	if (v && (!*v || strstr (v, "not installed") || strstr (v, "was not found")))
		g_clear_pointer (&v, g_free);
	/* "0.11.1-1": the package's own release number off */
	if (v && strrchr (v, '-'))
		*strrchr (v, '-') = 0;
	return v;
}

/* its system service, and why it stopped if it did */
static void
service_of (bnc_installed *i, const char *unit)
{
	const char *a[] = { "systemctl", "show", unit, "-p", "LoadState,ActiveState,UnitFileState", NULL };
	char *o = run_out (a);

	if (!o || !strstr (o, "LoadState=loaded"))
	{
		g_free (o);
		return;
	}
	if (strstr (o, "ActiveState=active"))
		i->service = g_strdup ("running");
	else if (strstr (o, "ActiveState=failed"))
	{
		const char *j[] = { "journalctl", "-u", unit, "-n", "20", "--no-pager", "-o", "cat", NULL };
		char *log = run_out (j), **lines;
		int n;

		i->service = g_strdup ("failed");
		/* the last line from soju/znc itself, not systemd's own */
		lines = g_strsplit (log ? log : "", "\n", 0);
		for (n = g_strv_length (lines) - 1; n >= 0; n--)
		{
			char *l = lines[n];
			if (!*l || g_str_has_prefix (l, unit) || strstr (l, "Scheduled restart") ||
				 strstr (l, "Start request repeated") || strstr (l, "Failed to start") ||
				 strstr (l, "Main process exited") || strstr (l, "Failed with result") ||
				 strstr (l, "Started ") || strstr (l, "warning:"))
				continue;
			/* soju: "2026/10/19 03:38:49 failed to listen..." */
			if (strlen (l) > 20 && l[4] == '/' && l[7] == '/')
				l += 20;
			i->service_why = g_strdup (l);
			break;
		}
		g_strfreev (lines);
		g_free (log);
	}
	else if (strstr (o, "UnitFileState=enabled") || strstr (o, "ActiveState=inactive"))
		i->service = g_strdup ("stopped");
	g_free (o);
}

static bnc_installed *
look_for (const char *kind, const char *prog, const char *pkg, const char *unit,
			 const char *const *configs)
{
	bnc_installed *i = g_new0 (bnc_installed, 1);
	int c;

	i->kind = g_strdup (kind);
	i->path = g_find_program_in_path (prog);
	if (!i->path && !strcmp (kind, "soju") && mybouncer_soju_path ())
		i->path = g_strdup (mybouncer_soju_path ());
	if (!i->path)
		return i;
	i->version = version_of (pkg);
	for (c = 0; configs[c]; c++)
	{
		char *p = configs[c][0] == '~' ? g_build_filename (g_get_home_dir (), configs[c] + 2, NULL)
												 : g_strdup (configs[c]);
		if (g_file_test (p, G_FILE_TEST_EXISTS))
		{
			i->config = p;
			break;
		}
		g_free (p);
	}
	if (g_find_program_in_path ("systemctl"))
		service_of (i, unit);
	i->ours = !strcmp (kind, "soju") && mybouncer_state () == MYBNC_SET_UP;
	return i;
}

/* ---- ports: who answers on this computer ---- */

typedef struct
{
	bnc_found *f;
	int pending;
	bnc_detect_cb cb;
	gpointer data;
	int our_port;
} job;

typedef struct
{
	job *j;
	int port;
	gboolean tls;
} portprobe;

static void
finish (job *j)
{
	if (--j->pending > 0)
		return;
	j->cb (j->f, j->data);
	g_free (j);
}

static void
port_done (bnc_probe_result *r, gpointer data)
{
	portprobe *p = data;

	/* only bouncers (and networks that keep you on) are of interest; a
	   plain IRC server on this computer is something else. Our made-up
	   login fails, but CAP LS already said what it is. */
	const char *k = r->kind && strcmp (r->kind, "plain") ? r->kind : r->seen;

	if (k)
	{
		bnc_listening *l = g_new0 (bnc_listening, 1);
		l->kind = g_strdup (k);
		l->port = p->port;
		l->tls = p->tls;
		l->ours = p->port == p->j->our_port;
		g_ptr_array_add (p->j->f->listening, l);
	}
	bnc_probe_result_free (r);
	finish (p->j);
	g_free (p);
}

/* is anything listening there (a quick connect), before the IRC probe */
static void
port_open_cb (GObject *src, GAsyncResult *res, gpointer data)
{
	portprobe *p = data;
	GSocketConnection *c = g_socket_client_connect_to_host_finish (G_SOCKET_CLIENT (src), res, NULL);

	g_object_unref (src);
	if (!c)
	{
		finish (p->j);
		g_free (p);
		return;
	}
	g_io_stream_close (G_IO_STREAM (c), NULL, NULL);
	g_object_unref (c);
	/* the login fails without an account, but CAP LS still says what it is */
	bncsetup_probe ("127.0.0.1", p->port, p->tls, "nexchat-detect", "x", port_done, p);
}

static void
try_port (job *j, int port, gboolean tls)
{
	portprobe *p = g_new0 (portprobe, 1);
	GSocketClient *c = g_socket_client_new ();

	p->j = j;
	p->port = port;
	p->tls = tls;
	j->pending++;
	g_socket_client_set_timeout (c, 1);
	g_socket_client_connect_to_host_async (c, "127.0.0.1", port, NULL, port_open_cb, p);
}

void
bncdetect_run (bnc_detect_cb cb, gpointer data)
{
	static const char *const soju_configs[] = { "/etc/soju/config", "~/.config/soju/config", NULL };
	static const char *const znc_configs[] = { "~/.znc/configs/znc.conf", "/var/lib/znc/configs/znc.conf", NULL };
	job *j = g_new0 (job, 1);

	j->f = g_new0 (bnc_found, 1);
	j->f->installed = g_ptr_array_new_with_free_func (installed_free);
	j->f->listening = g_ptr_array_new_with_free_func (listening_free);
	j->cb = cb;
	j->data = data;
	j->pending = 1;	/* until every port is started */

	g_ptr_array_add (j->f->installed, look_for ("soju", "soju", "soju", "soju.service", soju_configs));
	g_ptr_array_add (j->f->installed, look_for ("znc", "znc", "znc", "znc.service", znc_configs));

	/* the usual ports, and ours */
	{
		char *s = NULL, *mine = g_build_filename (get_xdir (), "bouncer", "port", NULL);
		if (g_file_get_contents (mine, &s, NULL, NULL))
			j->our_port = atoi (s);
		g_free (s);
		g_free (mine);
	}
	try_port (j, 6697, TRUE);
	try_port (j, 6667, FALSE);
	try_port (j, 6698, TRUE);	/* ZNC's habit */
	try_port (j, 1025, FALSE);
	if (j->our_port && j->our_port != 6667)
		try_port (j, j->our_port, FALSE);
	/* tests: a port to look at as well */
	if (g_getenv ("NEXCHAT_DETECT_PORTS"))
	{
		char **ps = g_strsplit (g_getenv ("NEXCHAT_DETECT_PORTS"), ",", 0);
		int i;
		for (i = 0; ps[i]; i++)
			if (atoi (ps[i] + (ps[i][0] == '+')))
				try_port (j, atoi (ps[i] + (ps[i][0] == '+')), ps[i][0] == '+');
		g_strfreev (ps);
	}
	finish (j);
}

char *
bnc_found_describe (bnc_found *f)
{
	GString *s = g_string_new (NULL);
	guint i;

	for (i = 0; i < f->installed->len; i++)
	{
		bnc_installed *b = f->installed->pdata[i];
		if (!b->path)
			g_string_append_printf (s, "installed %s: no\n", b->kind);
		else
			g_string_append_printf (s, "installed %s: %s version=%s config=%s service=%s why=%s ours=%d\n",
											b->kind, b->path, b->version ? b->version : "-", b->config ? b->config : "-",
											b->service ? b->service : "-", b->service_why ? b->service_why : "-", b->ours);
	}
	for (i = 0; i < f->listening->len; i++)
	{
		bnc_listening *l = f->listening->pdata[i];
		g_string_append_printf (s, "listening %s: port %d%s%s\n", l->kind, l->port, l->tls ? " tls" : "",
										l->ours ? " ours" : "");
	}
	return g_string_free (s, FALSE);
}

