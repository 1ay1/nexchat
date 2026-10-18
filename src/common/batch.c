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
#include "fe.h"
#include "modes.h"
#include "batch.h"
#include "util.h"

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
		if (!g_ascii_strcasecmp (b->type, "chathistory"))
			chathistory_begin (serv, word[5]);
	}
	else if (ref[0] == '-')
	{
		irc_batch *b = g_hash_table_lookup (batches (serv), ref + 1);
		if (b)
		{
			if (is_split_type (b->type))
				batch_end (serv, b);
			else if (!g_ascii_strcasecmp (b->type, "chathistory"))
				chathistory_end (serv, b->params);
			g_hash_table_remove (batches (serv), ref + 1);
		}
	}
}

/* the line with this batch id is history (a chathistory batch, or a batch
   inside one): shown, but not alerted, counted or logged again */
gboolean
batch_is_history (server *serv, const char *id)
{
	irc_batch *b;

	if (!id || !serv->batches)
		return FALSE;
	b = g_hash_table_lookup (serv->batches, id);
	return b && !g_ascii_strcasecmp (b->type, "chathistory");
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

/* ---- draft/chathistory ---- */

#define HISTORY_LINES 100

/* on joining: what was said since the last line we have, or the last
   HISTORY_LINES if we have nothing (a new channel, scrollback off) */
void
chathistory_request (session *sess)
{
	server *serv = sess->server;

	if (!serv->have_chathistory || !serv->have_batch || !prefs.hex_irc_chathistory)
		return;
	if (sess->history_until > 0)
	{
		GDateTime *dt = g_date_time_new_from_unix_utc (sess->history_until);
		/* AFTER is exclusive: the line we have isn't sent again */
		char *iso = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%S.000Z");
		tcp_sendf (serv, "CHATHISTORY AFTER %s timestamp=%s %d\r\n", sess->channel, iso, HISTORY_LINES);
		g_free (iso);
		g_date_time_unref (dt);
	}
	else
		tcp_sendf (serv, "CHATHISTORY LATEST %s * %d\r\n", sess->channel, HISTORY_LINES);
}

void
chathistory_begin (server *serv, const char *target)
{
	session *sess = target && *target ? find_channel (serv, (char *) target) : NULL;

	if (!sess && target && *target)
		sess = find_dialog (serv, (char *) target);
	if (sess)
		PrintTextf (sess, _("\00323*\t--- History from the server ---\n"));
}

void
chathistory_end (server *serv, const char *target)
{
	session *sess = target && *target ? find_channel (serv, (char *) target) : NULL;

	if (!sess && target && *target)
		sess = find_dialog (serv, (char *) target);
	if (sess)
		PrintTextf (sess, _("\00323*\t--- End of history ---\n"));
}

/* ---- draft/read-marker ---- */

void
readmarker_from_server (server *serv, const char *target, time_t t)
{
	session *sess = find_channel (serv, (char *) target);

	if (!sess)
		sess = find_dialog (serv, (char *) target);
	if (!sess || t <= 0)
		return;
	if (t > sess->read_until)
		sess->read_until = t;
	/* read elsewhere up to (or past) the newest line we have: not unread here */
	if (sess->read_until >= sess->history_until && (sess->unread || sess->unread_hilight))
		fe_set_tab_color (sess, FE_COLOR_NONE);
}

void
readmarker_send (session *sess)
{
	server *serv = sess->server;
	GDateTime *dt;
	char *iso;

	if (!serv || !serv->connected || !serv->have_readmarker || !sess->channel[0] ||
		 (sess->type != SESS_CHANNEL && sess->type != SESS_DIALOG))
		return;
	/* nothing newer than what the server already has */
	if (sess->history_until <= sess->read_until)
		return;
	sess->read_until = sess->history_until;
	dt = g_date_time_new_from_unix_utc (sess->read_until);
	iso = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%S.000Z");
	tcp_sendf (serv, "MARKREAD %s timestamp=%s\r\n", sess->channel, iso);
	g_free (iso);
	g_date_time_unref (dt);
}

static gboolean
readmarker_timer (gpointer data)
{
	session *sess = data;

	if (is_session (sess))
	{
		sess->readmarker_tag = 0;
		readmarker_send (sess);
	}
	return G_SOURCE_REMOVE;
}

void
readmarker_soon (session *sess)
{
	if (!sess->server->have_readmarker || sess->readmarker_tag)
		return;
	sess->readmarker_tag = g_timeout_add_seconds (5, readmarker_timer, sess);
}

/* ---- +typing ---- */

/* a typer goes stale after 6 s of nothing (active) or 30 s (paused), as
   the spec says; sess->typers maps nick -> expiry (monotonic seconds) */
static void
typing_changed (session *sess)
{
	fe_typing_changed (sess);
}

void
typing_from (server *serv, const char *nick, const char *target, const char *state)
{
	session *sess;
	gint64 now = g_get_monotonic_time () / G_USEC_PER_SEC;

	if (!prefs.hex_gui_typing_show)
		return;
	/* to a channel, or to us (then it's their query) */
	sess = is_channel (serv, (char *) target) ? find_channel (serv, (char *) target)
		: find_dialog (serv, (char *) nick);
	if (!sess)
		return;
	if (!sess->typers)
		sess->typers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	if (!strcmp (state, "active"))
		g_hash_table_replace (sess->typers, g_strdup (nick), GINT_TO_POINTER ((int) (now + 6)));
	else if (!strcmp (state, "paused"))
		g_hash_table_replace (sess->typers, g_strdup (nick), GINT_TO_POINTER ((int) (now + 30)));
	else
		g_hash_table_remove (sess->typers, nick);
	typing_changed (sess);
}

void
typing_clear (session *sess, const char *nick)
{
	if (sess->typers && g_hash_table_remove (sess->typers, nick))
		typing_changed (sess);
}

char *
typing_text (session *sess)
{
	GHashTableIter it;
	gpointer k, v;
	GPtrArray *who;
	gint64 now = g_get_monotonic_time () / G_USEC_PER_SEC;
	char *text = NULL;

	if (!sess->typers || !prefs.hex_gui_typing_show)
		return NULL;
	who = g_ptr_array_new ();
	g_hash_table_iter_init (&it, sess->typers);
	while (g_hash_table_iter_next (&it, &k, &v))
	{
		if (GPOINTER_TO_INT (v) < now)
			g_hash_table_iter_remove (&it);
		else
			g_ptr_array_add (who, k);
	}
	if (who->len == 1)
		text = g_strdup_printf (_("%s is typing..."), (char *) who->pdata[0]);
	else if (who->len == 2)
		text = g_strdup_printf (_("%s and %s are typing..."), (char *) who->pdata[0], (char *) who->pdata[1]);
	else if (who->len > 2)
		text = g_strdup_printf (_("%d people are typing..."), who->len);
	g_ptr_array_free (who, TRUE);
	return text;
}

void
typing_send (session *sess, gboolean active)
{
	server *serv = sess->server;
	gint64 now = g_get_monotonic_time () / G_USEC_PER_SEC;

	if (!prefs.hex_gui_typing_send || !serv || !serv->connected || !serv->have_message_tags ||
		 !sess->channel[0] || (sess->type != SESS_CHANNEL && sess->type != SESS_DIALOG))
		return;
	if (active)
	{
		/* at most every 3 s while typing goes on */
		if (sess->typing_sent && now - sess->typing_sent < 3)
			return;
		sess->typing_sent = now;
		tcp_sendf (serv, "@+typing=active TAGMSG %s\r\n", sess->channel);
	}
	else if (sess->typing_sent)
	{
		sess->typing_sent = 0;
		tcp_sendf (serv, "@+typing=done TAGMSG %s\r\n", sess->channel);
	}
}

/* ---- +draft/reply ---- */

/* the last REPLY_KEEP lines of each window: msgid -> "nick\ttext" */
#define REPLY_KEEP 200

typedef struct
{
	GHashTable *byid;	/* msgid -> "nick\ttext" */
	GQueue order;		/* msgids, oldest first (owned by byid) */
	GHashTable *lastby;	/* nick (casefolded) -> msgid of their last line */
} reply_store;

static GHashTable *stores;	/* session -> reply_store */

static void
reply_store_free (gpointer p)
{
	reply_store *s = p;
	g_hash_table_destroy (s->lastby);
	g_hash_table_destroy (s->byid);
	g_queue_clear (&s->order);
	g_free (s);
}

static reply_store *
reply_get (session *sess, gboolean make)
{
	reply_store *s;

	if (!stores)
	{
		if (!make)
			return NULL;
		stores = g_hash_table_new_full (NULL, NULL, NULL, reply_store_free);
	}
	s = g_hash_table_lookup (stores, sess);
	if (!s && make)
	{
		s = g_new0 (reply_store, 1);
		s->byid = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		s->lastby = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		g_queue_init (&s->order);
		g_hash_table_insert (stores, sess, s);
	}
	return s;
}

void
reply_remember (session *sess, const char *msgid, const char *nick, const char *text)
{
	reply_store *s;
	char *id;

	if (!msgid || !*msgid || !nick)
		return;
	s = reply_get (sess, TRUE);
	if (g_hash_table_contains (s->byid, msgid))
		return;
	id = g_strdup (msgid);
	g_hash_table_insert (s->byid, id, g_strdup_printf ("%s\t%s", nick, text ? text : ""));
	g_queue_push_tail (&s->order, id);
	g_hash_table_insert (s->lastby, g_utf8_casefold (nick, -1), g_strdup (msgid));
	while (g_queue_get_length (&s->order) > REPLY_KEEP)
		g_hash_table_remove (s->byid, g_queue_pop_head (&s->order));
}

const char *
reply_last_from (session *sess, const char *nick)
{
	reply_store *s = reply_get (sess, FALSE);
	char *key;
	const char *id;

	if (!s)
		return NULL;
	key = g_utf8_casefold (nick, -1);
	id = g_hash_table_lookup (s->lastby, key);
	g_free (key);
	return id;
}

void
reply_show (session *sess, const char *msgid, time_t stamp)
{
	reply_store *s;
	const char *line;
	char *plain, *quote;

	if (!prefs.hex_gui_replies || !msgid || !*msgid)
		return;
	s = reply_get (sess, FALSE);
	line = s ? g_hash_table_lookup (s->byid, msgid) : NULL;
	if (!line)
	{
		/* scrolled out of what we keep, or from before we joined */
		PrintTextTimeStamp (sess, _("\00314\035replying to an earlier message\017"), stamp);
		return;
	}
	plain = strip_color (strchr (line, '\t') + 1, -1, STRIP_ALL);
	/* one short line: the first 60 characters */
	if (g_utf8_strlen (plain, -1) > 60)
	{
		char *cut = g_strndup (plain, g_utf8_offset_to_pointer (plain, 60) - plain);
		g_free (plain);
		plain = g_strconcat (cut, "...", NULL);
		g_free (cut);
	}
	quote = g_strdup_printf (_("\00314\035replying to %.*s: %s\017"),
							 (int) (strchr (line, '\t') - line), line, plain);
	PrintTextTimeStamp (sess, quote, stamp);
	g_free (quote);
	g_free (plain);
}

void
reply_free (session *sess)
{
	if (stores)
		g_hash_table_remove (stores, sess);
}
