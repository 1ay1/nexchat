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
#include "bouncer.h"

typedef struct
{
	char *type;			/* "netsplit", "netjoin", ... */
	char *params;		/* the two server names, for netsplit/netjoin */
	GHashTable *nicks;	/* channel session -> GString of nicks */
	/* draft/multiline */
	char *tags;			/* the BATCH line's tags (time, msgid, account...) */
	char *head;			/* ":nick!u@h PRIVMSG #chan" of its first line */
	GString *text;		/* the message so far, lines joined by \n */
	int lines;			/* multiline: lines so far; history: messages in it */
	session *older;	/* history: the window that asked BEFORE (lines go on top) */
} irc_batch;

/* tags of the line being handled (irc_inline), for a BATCH + line */
static const char *line_tags;

static void
batch_free (irc_batch *b)
{
	g_free (b->type);
	g_free (b->params);
	g_hash_table_destroy (b->nicks);
	g_free (b->tags);
	g_free (b->head);
	if (b->text)
		g_string_free (b->text, TRUE);
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

/* ZNC's buffer replayed on connect */
static gboolean
is_playback_type (const char *type)
{
	return !g_ascii_strcasecmp (type, "znc.in/playback");
}

static gboolean
is_multiline_type (const char *type)
{
	return !g_ascii_strcasecmp (type, "draft/multiline") || !g_ascii_strcasecmp (type, "multiline");
}

/* the BATCH line's tags without msgid, for the lines after the first */
static char *
tags_without_msgid (const char *tags)
{
	char **t = g_strsplit (tags, ";", 0);
	GString *out = g_string_new (NULL);
	int i;

	for (i = 0; t[i]; i++)
	{
		if (!*t[i] || !strcmp (t[i], "msgid") || g_str_has_prefix (t[i], "msgid="))
			continue;
		if (out->len)
			g_string_append_c (out, ';');
		g_string_append (out, t[i]);
	}
	g_strfreev (t);
	return g_string_free (out, FALSE);
}

/* the whole message is here: each of its lines handled like a line of its
   own from that person, the batch's tags (time, account) on all of them
   and its msgid on the first. Blank lines are left out. */
static void
multiline_end (server *serv, irc_batch *b)
{
	char **lines, *rest;
	int i, first = TRUE;

	if (!b->head || !b->text)
		return;
	rest = tags_without_msgid (b->tags);
	lines = g_strsplit (b->text->str, "\n", 0);
	for (i = 0; lines[i]; i++)
	{
		const char *t = first ? b->tags : rest;
		char *raw;

		if (!*lines[i])
			continue;
		raw = *t ? g_strdup_printf ("@%s %s :%s", t, b->head, lines[i])
				  : g_strdup_printf ("%s :%s", b->head, lines[i]);
		serv->p_inline (serv, raw, strlen (raw));
		g_free (raw);
		first = FALSE;
		if (!is_server (serv))
			break;
	}
	g_strfreev (lines);
	g_free (rest);
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
	batch_line (serv, word[3], word[4], word[5], word_eol[5]);
}

static session *history_target (server *serv, const char *target);

/* ref "+id"/"-id", then the type and its first parameter and all of them */
void
batch_line (server *serv, const char *ref, const char *type, const char *param1, const char *params)
{
	if (!ref[0] || !ref[1])
		return;
	if (ref[0] == '+')
	{
		irc_batch *b = g_new0 (irc_batch, 1);
		b->type = g_strdup (type);
		b->params = g_strdup (params[0] ? params : "");
		b->nicks = g_hash_table_new_full (NULL, NULL, NULL, gstring_free);
		g_hash_table_replace (batches (serv), g_strdup (ref + 1), b);
		if (!g_ascii_strcasecmp (b->type, "chathistory") || is_playback_type (b->type))
		{
			session *s = history_target (serv, param1);
			if (s && s->history_older_pending)
			{
				s->history_older_pending = FALSE;
				b->older = s;
			}
			chathistory_begin (serv, param1);
		}
		else if (!g_ascii_strcasecmp (b->type, "soju.im/search"))
			bouncer_search_batch (serv, ref + 1, TRUE);
		else if (is_multiline_type (b->type))
		{
			b->tags = g_strdup (line_tags ? line_tags : "");
			b->text = g_string_new (NULL);
		}
	}
	else if (ref[0] == '-')
	{
		irc_batch *b = g_hash_table_lookup (batches (serv), ref + 1);
		if (b)
		{
			if (is_split_type (b->type))
				batch_end (serv, b);
			else if (b->older)
			{
				if (is_session (b->older))
					fe_history_older_done (b->older, b->lines);
				if (is_session (b->older) && !b->lines)
					b->older->history_older_done = TRUE;
			}
			else if (!g_ascii_strcasecmp (b->type, "chathistory") || is_playback_type (b->type))
				chathistory_end (serv, b->params, b->lines);
			else if (!g_ascii_strcasecmp (b->type, "soju.im/search"))
				bouncer_search_batch (serv, ref + 1, FALSE);
			else if (is_multiline_type (b->type))
			{
				/* out of the table first: the replay goes through irc_inline */
				g_hash_table_steal (batches (serv), ref + 1);
				multiline_end (serv, b);
				batch_free (b);
				return;
			}
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
	return b && (!g_ascii_strcasecmp (b->type, "chathistory") || is_playback_type (b->type));
}

/* ...and from one asked BEFORE what's shown: it goes on top */
gboolean
batch_is_older (server *serv, const char *id)
{
	irc_batch *b;

	if (!id || !serv->batches)
		return FALSE;
	b = g_hash_table_lookup (serv->batches, id);
	return b && b->older;
}

/* a message in a history batch, about to be shown in sess: the first one
   gets the marker above it, all are counted for the line at the end */
void
batch_history_count (server *serv, const char *id, session *sess)
{
	irc_batch *b;

	if (!id || !serv->batches || !sess)
		return;
	b = g_hash_table_lookup (serv->batches, id);
	if (!b || (g_ascii_strcasecmp (b->type, "chathistory") && !is_playback_type (b->type)))
		return;
	if (b->older)
	{
		/* older lines: just counted, no "while you were away" */
		b->lines++;
		return;
	}
	if (!b->lines++)
		PrintTextf (sess, _("\00323*\t--- While you were away ---\n"));
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

void
batch_note_tags (const char *tags)
{
	line_tags = tags;
}

/* a bigger message than any server allows: the rest is dropped */
#define MULTILINE_MAX (64 * 1024)

gboolean
batch_multiline_take (server *serv, const char *id, const char *tags,
							 const char *line, const message_tags_data *tags_data)
{
	irc_batch *b;
	const char *p, *text;
	int words;

	if (!id || !serv->batches)
		return FALSE;
	b = g_hash_table_lookup (serv->batches, id);
	if (!b || !b->text)
		return FALSE;

	/* ":src CMD target :text": the text, and the part before it */
	for (p = line, words = 0; *p && words < 3; words++)
	{
		p = strchr (p, ' ');
		if (!p)
			return TRUE;	/* not a message: nothing to keep */
		while (*p == ' ')
			p++;
	}
	text = *p == ':' ? p + 1 : p;
	if (!b->head)
	{
		const char *end = p;
		while (end > line && end[-1] == ' ')
			end--;
		b->head = g_strndup (line, end - line);
	}
	if (b->text->len + strlen (text) + 1 > MULTILINE_MAX)
		return TRUE;
	if (b->lines++ && !tags_data->concat)
		g_string_append_c (b->text, '\n');
	g_string_append (b->text, text);
	(void) tags;
	return TRUE;
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

static session *
history_target (server *serv, const char *target)
{
	session *sess = target && *target ? find_channel (serv, (char *) target) : NULL;

	if (!sess && target && *target)
		sess = find_dialog (serv, (char *) target);
	return sess;
}

/* scrolled to the top: ask for what came before the oldest line shown
   (oldest: its time, 0 = from the newest). One at a time; nothing more
   once the server has run out. FALSE if it can't be asked. */
gboolean
chathistory_older (session *sess, time_t oldest)
{
	server *serv = sess->server;
	GDateTime *dt;
	char *iso;

	if (!serv || !serv->connected || !serv->have_chathistory || !serv->have_batch ||
		 !prefs.hex_irc_chathistory || !sess->channel[0] ||
		 (sess->type != SESS_CHANNEL && sess->type != SESS_DIALOG))
		return FALSE;
	if (sess->history_older_pending || sess->history_older_done)
		return FALSE;
	sess->history_older_pending = TRUE;
	if (oldest <= 0)
	{
		tcp_sendf (serv, "CHATHISTORY LATEST %s * %d\r\n", sess->channel, HISTORY_LINES);
		return TRUE;
	}
	dt = g_date_time_new_from_unix_utc (oldest);
	/* BEFORE is exclusive: the line we have isn't sent again */
	iso = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%S.000Z");
	tcp_sendf (serv, "CHATHISTORY BEFORE %s timestamp=%s %d\r\n", sess->channel, iso, HISTORY_LINES);
	g_free (iso);
	g_date_time_unref (dt);
	return TRUE;
}

/* the start is only marked when something comes: an empty replay prints
   nothing at all */
void
chathistory_begin (server *serv, const char *target)
{
	(void) serv;
	(void) target;
}

void
chathistory_end (server *serv, const char *target, int lines)
{
	session *sess = history_target (serv, target);

	if (!sess || lines <= 0)
		return;
	PrintTextf (sess, g_dngettext (GETTEXT_PACKAGE,
				"\00323*\t--- %d message you missed, up to here ---\n",
				"\00323*\t--- %d messages you missed, up to here ---\n", lines), lines);
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

/* "alice started typing" / "alice stopped typing" in the window and its
   log, when that's turned on. Not for our own echo or history replays. */
static void
typing_say (session *sess, const char *nick, gboolean started)
{
	if (!prefs.hex_gui_typing_chat || sess->server->line_is_history ||
		 !sess->server->p_cmp (nick, sess->server->nick))
		return;
	EMIT_SIGNAL (started ? XP_TE_TYPINGSTART : XP_TE_TYPINGSTOP, sess, (char *) nick, NULL, NULL, NULL, 0);
}

/* stale typers: they stopped without saying so */
void
typing_expire (session *sess)
{
	GHashTableIter it;
	gpointer k, v;
	gint64 now = g_get_monotonic_time () / G_USEC_PER_SEC;
	GPtrArray *gone;
	guint i;

	if (!sess->typers)
		return;
	gone = g_ptr_array_new_with_free_func (g_free);
	g_hash_table_iter_init (&it, sess->typers);
	while (g_hash_table_iter_next (&it, &k, &v))
		if (GPOINTER_TO_INT (v) < now)
		{
			g_ptr_array_add (gone, g_strdup (k));
			g_hash_table_iter_remove (&it);
		}
	for (i = 0; i < gone->len; i++)
		typing_say (sess, gone->pdata[i], FALSE);
	g_ptr_array_free (gone, TRUE);
}

/* while anyone's typing anywhere: every second, the stale ones go (and
   "stopped typing" is said), in every frontend */
static guint expire_tag;

static gboolean
typing_expire_all (gpointer d)
{
	GSList *l;
	gboolean any = FALSE;

	for (l = sess_list; l; l = l->next)
	{
		session *s = l->data;
		if (s->typers && g_hash_table_size (s->typers))
		{
			guint before = g_hash_table_size (s->typers);
			typing_expire (s);
			if (g_hash_table_size (s->typers) != before)
				typing_changed (s);
			any = any || g_hash_table_size (s->typers);
		}
	}
	if (!any)
		expire_tag = 0;
	return any ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
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
	if (!strcmp (state, "active") || !strcmp (state, "paused"))
	{
		gboolean was = g_hash_table_contains (sess->typers, nick);
		g_hash_table_replace (sess->typers, g_strdup (nick),
									 GINT_TO_POINTER ((int) (now + (!strcmp (state, "active") ? 6 : 30))));
		if (!was)
			typing_say (sess, nick, TRUE);
	}
	else if (g_hash_table_remove (sess->typers, nick))
		typing_say (sess, nick, FALSE);
	if (g_hash_table_size (sess->typers) && !expire_tag)
		expire_tag = g_timeout_add_seconds (1, typing_expire_all, NULL);
	typing_changed (sess);
}

/* their line came: they're done typing it (the line says so: no
   "stopped typing" for it) */
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
	char *text = NULL;

	if (!sess->typers || !prefs.hex_gui_typing_show)
		return NULL;
	typing_expire (sess);
	who = g_ptr_array_new ();
	g_hash_table_iter_init (&it, sess->typers);
	while (g_hash_table_iter_next (&it, &k, &v))
		g_ptr_array_add (who, k);
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
	/* scrolled-up history: older than all we keep; only known for lookups
	   (what a reply quotes), not the newest nor anyone's last line */
	if (sess->server && sess->server->line_is_older)
	{
		if (g_queue_get_length (&s->order) >= REPLY_KEEP)
			return;
		id = g_strdup (msgid);
		g_hash_table_insert (s->byid, id, g_strdup_printf ("%s\t%s", nick, text ? text : ""));
		g_queue_push_head (&s->order, id);
		return;
	}
	id = g_strdup (msgid);
	g_hash_table_insert (s->byid, id, g_strdup_printf ("%s\t%s", nick, text ? text : ""));
	g_queue_push_tail (&s->order, id);
	g_hash_table_insert (s->lastby, g_utf8_casefold (nick, -1), g_strdup (msgid));
	while (g_queue_get_length (&s->order) > REPLY_KEEP)
		g_hash_table_remove (s->byid, g_queue_pop_head (&s->order));
}

const char *
reply_lookup (session *sess, const char *msgid)
{
	reply_store *s = reply_get (sess, FALSE);

	return s && msgid ? g_hash_table_lookup (s->byid, msgid) : NULL;
}

GList *
reply_recent (session *sess, int n)
{
	reply_store *s = reply_get (sess, FALSE);
	GList *l, *out = NULL;

	if (!s)
		return NULL;
	for (l = s->order.tail; l && n > 0; l = l->prev, n--)
		out = g_list_append (out, l->data);
	return out;
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

/* ---- draft/message-redaction ----
   A line can't be taken out of the text view, so a deletion is said where
   it happened, quoting what was deleted (as the spec asks: deletions stay
   visible, for accountability). */
void
redact_from_server (server *serv, const char *nick, const char *target,
						  const char *msgid, const char *reason, time_t when)
{
	session *sess = find_channel (serv, (char *) target);
	reply_store *s;
	const char *line;
	char *quote = NULL;

	if (!sess)
		sess = find_dialog (serv, (char *) target);
	/* a private message: the target is us, the window is the sender's */
	if (!sess)
		sess = find_dialog (serv, (char *) nick);
	if (!sess)
		return;
	s = reply_get (sess, FALSE);
	line = s ? g_hash_table_lookup (s->byid, msgid) : NULL;
	if (line)
	{
		const char *tab = strchr (line, '\t');
		char *plain = strip_color (tab + 1, -1, STRIP_ALL);
		char *who = g_strndup (line, tab - line);
		char *key = g_utf8_casefold (who, -1);
		const char *last = g_hash_table_lookup (s->lastby, key);
		gpointer idkey = NULL;
		GList *l;

		quote = !serv->p_cmp (who, nick) ? g_strdup_printf ("\"%s\"", plain)
						: g_strdup_printf (_("%s's \"%s\""), who, plain);
		g_free (plain);
		/* it can't be replied to any more (line is freed here) */
		if (g_hash_table_lookup_extended (s->byid, msgid, &idkey, NULL))
		{
			g_queue_remove (&s->order, idkey);
			g_hash_table_remove (s->byid, msgid);
		}
		line = NULL;
		/* if it was their last line, the one before it is now */
		if (last && !strcmp (last, msgid))
		{
			g_hash_table_remove (s->lastby, key);
			for (l = s->order.tail; l; l = l->prev)
			{
				const char *ln = g_hash_table_lookup (s->byid, l->data);
				if (ln && strlen (who) == strcspn (ln, "\t") && !strncmp (ln, who, strlen (who)))
				{
					g_hash_table_insert (s->lastby, g_strdup (key), g_strdup (l->data));
					break;
				}
			}
		}
		g_free (key);
		g_free (who);
	}
	if (reason && *reason)
		PrintTextTimeStampf (sess, when, _("\00314*\t%s deleted %s (%s)\n"), nick,
									quote ? quote : _("a message"), reason);
	else
		PrintTextTimeStampf (sess, when, _("\00314*\t%s deleted %s\n"), nick, quote ? quote : _("a message"));
	g_free (quote);
}

void
reply_free (session *sess)
{
	if (stores)
		g_hash_table_remove (stores, sess);
}
