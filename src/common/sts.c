/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* IRCv3 Strict Transport Security (https://ircv3.net/specs/extensions/sts).
   A server says "only talk to me over TLS, on this port, for this long".
   The policy is kept in sts.conf, one "host port expiry" line each, and a
   plain connection to such a host goes over TLS instead. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hexchat.h"
#include "cfgfiles.h"
#include "sts.h"

typedef struct
{
	int port;
	gint64 expiry;	/* unix time */
} sts_policy;

static GHashTable *policies;	/* lower-case host -> sts_policy */

static void
sts_load (void)
{
	FILE *fp;
	char line[512], host[256];
	int port;
	long long expiry;

	if (policies)
		return;
	policies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	fp = hexchat_fopen_file ("sts.conf", "r", 0);
	if (!fp)
		return;
	while (fgets (line, sizeof line, fp))
	{
		if (sscanf (line, "%255s %d %lld", host, &port, &expiry) == 3 &&
			 port > 0 && port < 65536)
		{
			sts_policy *p = g_new (sts_policy, 1);
			p->port = port;
			p->expiry = expiry;
			g_hash_table_replace (policies, g_ascii_strdown (host, -1), p);
		}
	}
	fclose (fp);
}

static void
sts_save (void)
{
	FILE *fp = hexchat_fopen_file ("sts.conf", "w", 0);
	GHashTableIter it;
	gpointer k, v;

	if (!fp)
		return;
	g_hash_table_iter_init (&it, policies);
	while (g_hash_table_iter_next (&it, &k, &v))
	{
		sts_policy *p = v;
		fprintf (fp, "%s %d %lld\n", (char *) k, p->port, (long long) p->expiry);
	}
	fclose (fp);
}

/* the TLS port a connection to host must use, or 0 if no policy */
int
sts_port_for (const char *host)
{
	char *key;
	sts_policy *p;
	int port = 0;

	sts_load ();
	key = g_ascii_strdown (host, -1);
	p = g_hash_table_lookup (policies, key);
	if (p)
	{
		if (p->expiry > g_get_real_time () / G_USEC_PER_SEC)
			port = p->port;
		else
		{
			g_hash_table_remove (policies, key);
			sts_save ();
		}
	}
	g_free (key);
	return port;
}

/* the value of key=... in an sts cap value ("port=6697,duration=300") */
static char *
sts_param (const char *value, const char *key)
{
	char **kv = g_strsplit (value, ",", 0);
	char *ret = NULL;
	gsize klen = strlen (key);
	int i;

	for (i = 0; kv[i]; i++)
		if (!strncmp (kv[i], key, klen) && kv[i][klen] == '=')
		{
			ret = g_strdup (kv[i] + klen + 1);
			break;
		}
	g_strfreev (kv);
	return ret;
}

/* a server advertised sts=value. Over TLS that's the policy to keep (port
   is the one we're on); in plain text it only says where TLS is: the
   return is that port to reconnect to, else 0. */
int
sts_handle (const char *host, int port, gboolean tls, const char *value)
{
	char *s;

	if (!value)
		return 0;
	if (!tls)
	{
		int tls_port;

		s = sts_param (value, "port");
		tls_port = s ? atoi (s) : 0;
		g_free (s);
		return tls_port > 0 && tls_port < 65536 ? tls_port : 0;
	}

	s = sts_param (value, "duration");
	if (s)
	{
		gint64 duration = g_ascii_strtoll (s, NULL, 10);
		char *key = g_ascii_strdown (host, -1);

		sts_load ();
		if (duration <= 0)
			g_hash_table_remove (policies, key);
		else
		{
			sts_policy *p = g_new (sts_policy, 1);
			p->port = port;
			p->expiry = g_get_real_time () / G_USEC_PER_SEC + duration;
			g_hash_table_replace (policies, key, p);
			key = NULL;
		}
		g_free (key);
		g_free (s);
		sts_save ();
	}
	return 0;
}
