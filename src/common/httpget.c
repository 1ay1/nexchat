/* nexchat
 * Copyright (C) 2026 nexchat contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* See httpget.h. No HTTP library: GIO resolves and connects (with a
   timeout), OpenSSL does TLS on the socket, and the rest is a GET and a
   header parser. */

#include <string.h>
#include <gio/gio.h>
#ifdef G_OS_WIN32
#include <winsock2.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include "httpget.h"

#define TIMEOUT_SECS 8
#define MAX_REDIRECTS 3
#define MAX_HEADER (16 * 1024)

typedef struct
{
	char *url;
	gsize max_bytes;
	httpget_cb cb;
	gpointer data;
	/* POST */
	GBytes *body;
	char *content_type, *filename, *auth;
	/* the address came from our own server, not a link: a bouncer on
	   the LAN or this machine is fine */
	gboolean allow_local;
} job;

static void
job_free (job *j)
{
	g_free (j->url);
	if (j->body)
		g_bytes_unref (j->body);
	g_free (j->content_type);
	g_free (j->filename);
	g_free (j->auth);
	g_free (j);
}

static void
result_free (httpget_result *r)
{
	g_free (r->content_type);
	if (r->body)
		g_bytes_unref (r->body);
	g_free (r->error);
	g_free (r->final_url);
	g_free (r);
}

/* somewhere a link has no business sending us: loopback, the LAN,
   link-local, carrier NAT, multicast... */
static gboolean
address_is_public (GInetAddress *a)
{
	const guint8 *b;

	if (g_inet_address_get_is_loopback (a) || g_inet_address_get_is_any (a) ||
		 g_inet_address_get_is_link_local (a) || g_inet_address_get_is_site_local (a) ||
		 g_inet_address_get_is_multicast (a))
		return FALSE;
	b = g_inet_address_to_bytes (a);
	if (g_inet_address_get_family (a) == G_SOCKET_FAMILY_IPV4)
	{
		if (b[0] == 0 || b[0] >= 240 ||				/* this network, reserved */
			 (b[0] == 100 && (b[1] & 0xc0) == 64))	/* 100.64/10 carrier NAT */
			return FALSE;
	}
	else
	{
		if ((b[0] & 0xfe) == 0xfc)						/* fc00::/7 unique local */
			return FALSE;
		/* ::ffff:a.b.c.d: judge the IPv4 inside */
		if (!memcmp (b, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12))
		{
			GInetAddress *v4 = g_inet_address_new_from_bytes (b + 12, G_SOCKET_FAMILY_IPV4);
			gboolean ok = address_is_public (v4);
			g_object_unref (v4);
			return ok;
		}
	}
	return TRUE;
}

/* the name's addresses we may use (public ones, unless allow_local), in
   the resolver's order; NULL if none */
static GList *
resolve_public (const char *host, gboolean allow_local, GCancellable *c, char **err)
{
	GResolver *r = g_resolver_get_default ();
	GList *l, *addrs = g_resolver_lookup_by_name (r, host, c, NULL);
	GList *ok = NULL;

	g_object_unref (r);
	if (!addrs)
	{
		*err = g_strdup ("can't resolve the name");
		return NULL;
	}
	for (l = addrs; l; l = l->next)
		if (allow_local || address_is_public (l->data) || g_getenv ("NEXCHAT_HTTPGET_ALLOW_LOCAL"))
			ok = g_list_prepend (ok, g_object_ref (l->data));
	g_resolver_free_addresses (addrs);
	if (!ok)
		*err = g_strdup ("not a public address");
	return g_list_reverse (ok);
}

/* one connection, plain or TLS, read and written the same way */
typedef struct
{
	GSocket *sock;
	SSL_CTX *ctx;
	SSL *ssl;
} conn;

static void
conn_close (conn *c)
{
	if (c->ssl)
	{
		SSL_shutdown (c->ssl);
		SSL_free (c->ssl);
	}
	if (c->ctx)
		SSL_CTX_free (c->ctx);
	if (c->sock)
	{
		g_socket_close (c->sock, NULL);
		g_object_unref (c->sock);
	}
}

static gssize
conn_write (conn *c, const char *buf, gsize len)
{
	if (c->ssl)
		return SSL_write (c->ssl, buf, len);
	return g_socket_send (c->sock, buf, len, NULL, NULL);
}

static gssize
conn_read (conn *c, char *buf, gsize len)
{
	if (c->ssl)
	{
		int n = SSL_read (c->ssl, buf, len);
		if (n <= 0)
		{
			int e = SSL_get_error (c->ssl, n);
			return e == SSL_ERROR_ZERO_RETURN ? 0 : -1;
		}
		return n;
	}
	return g_socket_receive (c->sock, buf, len, NULL, NULL);
}

static gboolean
conn_open (conn *c, const char *host, int port, gboolean tls, gboolean allow_local,
			  GCancellable *cancel, char **err)
{
	GList *addrs, *l;
	GError *e = NULL;

	memset (c, 0, sizeof *c);
	if (!(addrs = resolve_public (host, allow_local, cancel, err)))
		return FALSE;
	/* each address in turn: a name with IPv6 and IPv4 where only one
	   answers (localhost: ::1 first) still connects */
	for (l = addrs; l && !c->sock; l = l->next)
	{
		GSocketAddress *sa;
		GSocket *s = g_socket_new (g_inet_address_get_family (l->data), G_SOCKET_TYPE_STREAM,
											G_SOCKET_PROTOCOL_TCP, NULL);
		if (!s)
			continue;
		g_socket_set_timeout (s, TIMEOUT_SECS);
		sa = g_inet_socket_address_new (l->data, port);
		if (g_socket_connect (s, sa, cancel, l->next ? NULL : &e))
			c->sock = s;
		else
			g_object_unref (s);
		g_object_unref (sa);
	}
	g_list_free_full (addrs, g_object_unref);
	if (!c->sock)
	{
		*err = g_strdup (e ? e->message : "can't connect");
		g_clear_error (&e);
		return FALSE;
	}
	/* GSocket is non-blocking underneath; OpenSSL reads the fd itself and
	   wants it blocking (the timeout below still applies: SO_RCVTIMEO) */
	g_socket_set_blocking (c->sock, TRUE);
	{
		struct timeval tv = { TIMEOUT_SECS, 0 };
		int fd = g_socket_get_fd (c->sock);
#ifdef G_OS_WIN32
		DWORD ms = TIMEOUT_SECS * 1000;
		setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &ms, sizeof ms);
		setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, (const char *) &ms, sizeof ms);
		(void) tv;
#else
		setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
		{
			/* non-blocking at the fd level too (GSocket set O_NONBLOCK) */
#ifndef G_OS_WIN32
			int fl = fcntl (fd, F_GETFL);
			if (fl >= 0)
				fcntl (fd, F_SETFL, fl & ~O_NONBLOCK);
#endif
		}
	}
	if (!tls)
		return TRUE;

	/* the system's (or SSL_CERT_FILE's) roots, the name checked */
	c->ctx = SSL_CTX_new (TLS_client_method ());
	SSL_CTX_set_min_proto_version (c->ctx, TLS1_2_VERSION);
	SSL_CTX_set_default_verify_paths (c->ctx);
	SSL_CTX_set_verify (c->ctx, SSL_VERIFY_PEER, NULL);
	c->ssl = SSL_new (c->ctx);
	SSL_set_tlsext_host_name (c->ssl, host);
	SSL_set1_host (c->ssl, host);
	SSL_set_fd (c->ssl, g_socket_get_fd (c->sock));
	if (SSL_connect (c->ssl) != 1)
	{
		long v = SSL_get_verify_result (c->ssl);
		unsigned long e = ERR_get_error ();
		char why[256] = "";

		if (e)
			ERR_error_string_n (e, why, sizeof why);
		*err = v != X509_V_OK ? g_strdup_printf ("certificate: %s", X509_verify_cert_error_string (v))
									 : g_strdup_printf ("TLS handshake failed %s", why);
		return FALSE;
	}
	return TRUE;
}

/* "Name: value" from the header block, case-insensitively */
static char *
header_get (const char *head, const char *name)
{
	gsize n = strlen (name);
	const char *p = head;

	while ((p = strchr (p, '\n')))
	{
		p++;
		if (!g_ascii_strncasecmp (p, name, n) && p[n] == ':')
		{
			const char *v = p + n + 1, *end;
			while (*v == ' ' || *v == '\t')
				v++;
			end = strpbrk (v, "\r\n");
			return g_strndup (v, end ? (gsize) (end - v) : strlen (v));
		}
	}
	return NULL;
}

/* one request; a redirect comes back as *location */
static void
fetch_once (job *j, const char *url, gsize max_bytes, GCancellable *cancel,
				httpget_result *res, char **location)
{
	GUri *uri = g_uri_parse (url, G_URI_FLAGS_ENCODED, NULL);
	const char *scheme, *host;
	char *path, *req, *head = NULL;
	GByteArray *in;
	gboolean tls;
	int port;
	conn c;
	char buf[16384];
	gssize n;
	gsize head_len = 0;

	*location = NULL;
	if (!uri)
	{
		res->error = g_strdup ("not a URL");
		return;
	}
	scheme = g_uri_get_scheme (uri);
	host = g_uri_get_host (uri);
	if (!host || !*host || (g_ascii_strcasecmp (scheme, "http") && g_ascii_strcasecmp (scheme, "https")))
	{
		res->error = g_strdup ("only http and https");
		g_uri_unref (uri);
		return;
	}
	tls = !g_ascii_strcasecmp (scheme, "https");
	port = g_uri_get_port (uri) > 0 ? g_uri_get_port (uri) : tls ? 443 : 80;
	if (!conn_open (&c, host, port, tls, j->allow_local, cancel, &res->error))
	{
		conn_close (&c);
		g_uri_unref (uri);
		return;
	}

	path = g_strdup_printf ("%s%s%s", *g_uri_get_path (uri) ? g_uri_get_path (uri) : "/",
									g_uri_get_query (uri) ? "?" : "", g_uri_get_query (uri) ? g_uri_get_query (uri) : "");
	if (j->body)
	{
		/* the file is the whole body (soju.im/FILEHOST); its name goes in
		   Content-Disposition, quotes and line breaks taken out */
		char *name = g_strdup (j->filename ? j->filename : "file");
		g_strdelimit (name, "\"\\\r\n", '_');
		req = g_strdup_printf ("POST %s HTTP/1.0\r\nHost: %s\r\n"
									  "User-Agent: nexchat\r\n"
									  "Content-Type: %s\r\n"
									  "Content-Disposition: attachment; filename=\"%s\"\r\n"
									  "Content-Length: %" G_GSIZE_FORMAT "\r\n"
									  "%s%s%s"
									  "Connection: close\r\n\r\n",
									  path, host, j->content_type ? j->content_type : "application/octet-stream",
									  name, g_bytes_get_size (j->body),
									  j->auth ? "Authorization: " : "", j->auth ? j->auth : "", j->auth ? "\r\n" : "");
		g_free (name);
	}
	else
		req = g_strdup_printf ("GET %s HTTP/1.0\r\nHost: %s\r\n"
									  "User-Agent: nexchat (link preview)\r\n"
									  "Accept: text/html, image/*;q=0.9\r\nConnection: close\r\n\r\n",
									  path, host);
	g_free (path);
	if (conn_write (&c, req, strlen (req)) <= 0)
	{
		res->error = g_strdup ("couldn't send the request");
		g_free (req);
		conn_close (&c);
		g_uri_unref (uri);
		return;
	}
	g_free (req);
	if (j->body)
	{
		gsize len, off = 0;
		const char *b = g_bytes_get_data (j->body, &len);
		while (off < len && !g_cancellable_is_cancelled (cancel))
		{
			gssize w = conn_write (&c, b + off, MIN (len - off, 65536));
			if (w <= 0)
				break;
			off += w;
		}
		if (off < len)
		{
			res->error = g_strdup ("the upload was cut off");
			conn_close (&c);
			g_uri_unref (uri);
			return;
		}
	}

	in = g_byte_array_new ();
	while ((n = conn_read (&c, buf, sizeof buf)) > 0 && !g_cancellable_is_cancelled (cancel))
	{
		g_byte_array_append (in, (guint8 *) buf, n);
		if (!head)
		{
			gsize k;
			guint8 *e = NULL;
			for (k = 3; k < in->len; k++)
				if (!memcmp (in->data + k - 3, "\r\n\r\n", 4))
				{
					e = in->data + k - 3;
					break;
				}
			if (e)
			{
				head_len = e - in->data + 4;
				head = g_strndup ((char *) in->data, head_len);
			}
			else if (in->len > MAX_HEADER)
				break;
		}
		if (head && in->len - head_len > max_bytes)
		{
			res->truncated = TRUE;
			break;
		}
	}
	conn_close (&c);

	if (!head)
		res->error = g_strdup ("no HTTP reply");
	else
	{
		char *ct = header_get (head, "Content-Type");
		sscanf (head, "HTTP/%*d.%*d %d", &res->status);
		if (ct)
		{
			char *semi = strchr (ct, ';');
			if (semi)
				*semi = 0;
			res->content_type = g_ascii_strdown (g_strstrip (ct), -1);
			g_free (ct);
		}
		/* a redirect to follow, or (uploads) where the file now is */
		if ((res->status >= 300 && res->status < 400) || (j->body && res->status == 201))
		{
			char *loc = header_get (head, "Location");
			if (loc)
			{
				GUri *to = g_uri_parse_relative (uri, loc, G_URI_FLAGS_ENCODED, NULL);
				if (to)
				{
					*location = g_uri_to_string (to);
					g_uri_unref (to);
				}
				g_free (loc);
			}
		}
		else
			res->body = g_bytes_new (in->data + head_len, MIN (in->len - head_len, max_bytes));
		g_free (head);
	}
	g_byte_array_unref (in);
	g_uri_unref (uri);
}

static void
fetch_thread (GTask *task, gpointer src, gpointer data, GCancellable *cancel)
{
	job *j = data;
	httpget_result *res = g_new0 (httpget_result, 1);
	char *url = g_strdup (j->url);
	int hops;

	for (hops = 0; hops <= MAX_REDIRECTS; hops++)
	{
		char *next = NULL;

		g_clear_pointer (&res->error, g_free);
		g_clear_pointer (&res->content_type, g_free);
		res->status = 0;
		fetch_once (j, url, j->max_bytes, cancel, res, &next);
		/* an upload: no redirects; Location is the answer */
		if (j->body)
		{
			if (next)
			{
				g_free (url);
				url = next;
			}
			else if (!res->error && res->status != 201)
				res->error = g_strdup_printf ("the server said %d", res->status);
			break;
		}
		if (!next)
			break;
		g_free (url);
		url = next;
		if (hops == MAX_REDIRECTS)
			res->error = g_strdup ("too many redirects");
	}
	res->final_url = url;
	g_task_return_pointer (task, res, (GDestroyNotify) result_free);
}

static void
fetch_done (GObject *src, GAsyncResult *ar, gpointer data)
{
	job *j = data;
	httpget_result *res = g_task_propagate_pointer (G_TASK (ar), NULL);

	if (res)
	{
		j->cb (res, j->data);
		result_free (res);
	}
	job_free (j);
}

void
httpget (const char *url, gsize max_bytes, GCancellable *cancel, httpget_cb cb, gpointer data)
{
	job *j = g_new0 (job, 1);
	GTask *t;

	j->url = g_strdup (url);
	j->max_bytes = max_bytes;
	j->cb = cb;
	j->data = data;
	t = g_task_new (NULL, cancel, fetch_done, j);
	g_task_set_task_data (t, j, NULL);
	g_task_run_in_thread (t, fetch_thread);
	g_object_unref (t);
}

void
httppost (const char *url, GBytes *body, const char *content_type, const char *filename,
			 const char *auth, GCancellable *cancel, httpget_cb cb, gpointer data)
{
	job *j = g_new0 (job, 1);
	GTask *t;

	j->url = g_strdup (url);
	j->max_bytes = 64 * 1024;	/* the reply: a short body, at most */
	j->body = g_bytes_ref (body);
	j->content_type = g_strdup (content_type);
	j->filename = g_strdup (filename);
	j->auth = g_strdup (auth);
	/* the URL is the server's own (ISUPPORT), not a link someone pasted */
	j->allow_local = TRUE;
	j->cb = cb;
	j->data = data;
	t = g_task_new (NULL, cancel, fetch_done, j);
	g_task_set_task_data (t, j, NULL);
	g_task_run_in_thread (t, fetch_thread);
	g_object_unref (t);
}
