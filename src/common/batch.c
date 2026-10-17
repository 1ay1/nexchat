/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* IRCv3 batch (https://ircv3.net/specs/extensions/batch). A netsplit or
   netjoin comes as one batch: instead of a QUIT or JOIN line per person in
   every channel, each channel gets one line naming them all when the batch
   ends. Other batch types are passed through line by line. */

#include <string.h>

#include "hexchat.h"
#include "hexchatc.h"
#include "text.h"
#include "server.h"
#include "batch.h"

typedef struct
{
	char *type;			/* "netsplit", "netjoin", ... */
	char *params;		/* the two server names, for netsplit/netjoin */
	GHashTable *nicks;	/* channel session -> GString of nicks */
} irc_batch;

static void
batch_free (irc_batch *b)
{
	g_free (b->type);
	g_free (b->params);
	g_hash_table_destroy (b->nicks);
	g_free (b);
}

static void
gstring_free (gpointer s)
{
	g_string_free (s, TRUE);
}

static GHashTable *
batches (server *serv)
{
	if (!serv->batches)
		serv->batches = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
															(GDestroyNotify) batch_free);
	return serv->batches;
}

static gboolean
is_split_type (const char *type)
{
	return !g_ascii_strcasecmp (type, "netsplit") || !g_ascii_strcasecmp (type, "netjoin");
}

static void
batch_end (server *serv, irc_batch *b)
{
	GHashTableIter it;
	gpointer k, v;
	gboolean split = !g_ascii_strcasecmp (b->type, "netsplit");

	g_hash_table_iter_init (&it, b->nicks);
	while (g_hash_table_iter_next (&it, &k, &v))
	{
		session *sess = k;
		GString *nicks = v;

		if (!is_session (sess))
			continue;
		if (split)
			PrintTextf (sess, _("\00323*\tNetsplit (%s): %s\n"), b->params, nicks->str);
		else
			PrintTextf (sess, _("\00323*\tNetsplit over (%s), back: %s\n"), b->params, nicks->str);
	}
}

/* BATCH +id type params... / BATCH -id */
void
batch_command (server *serv, char *word[], char *word_eol[])
{
	char *ref = word[3];

	if (!ref[0] || !ref[1])
		return;
	if (ref[0] == '+')
	{
		irc_batch *b = g_new0 (irc_batch, 1);
		b->type = g_strdup (word[4]);
		b->params = g_strdup (word_eol[5][0] ? word_eol[5] : "");
		b->nicks = g_hash_table_new_full (NULL, NULL, NULL, gstring_free);
		g_hash_table_replace (batches (serv), g_strdup (ref + 1), b);
	}
	else if (ref[0] == '-')
	{
		irc_batch *b = g_hash_table_lookup (batches (serv), ref + 1);
		if (b)
		{
			if (is_split_type (b->type))
				batch_end (serv, b);
			g_hash_table_remove (batches (serv), ref + 1);
		}
	}
}

/* a QUIT or JOIN of nick in sess, tagged with batch id: TRUE if it was
   taken into a netsplit/netjoin summary (so nothing should be printed) */
gboolean
batch_collect (server *serv, const char *id, session *sess, const char *nick)
{
	irc_batch *b;
	GString *s;

	if (!id || !serv->batches)
		return FALSE;
	b = g_hash_table_lookup (serv->batches, id);
	if (!b || !is_split_type (b->type))
		return FALSE;
	s = g_hash_table_lookup (b->nicks, sess);
	if (!s)
		g_hash_table_insert (b->nicks, sess, (s = g_string_new (nick)));
	else
		g_string_append_printf (s, ", %s", nick);
	return TRUE;
}

/* connection gone: batches that never ended are dropped */
void
batch_reset (server *serv)
{
	g_clear_pointer (&serv->batches, g_hash_table_destroy);
}
