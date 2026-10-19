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
#include <gio/gio.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "bncdetect.h"
#include "bncsetup.h"
#include "../common/mybouncer.h"
#include "../common/cfgfiles.h"

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
