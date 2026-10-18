/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_BATCH_H
#define NEXCHAT_BATCH_H

/* BATCH +id type params / BATCH -id */
void batch_command (server *serv, char *word[], char *word_eol[]);
/* a QUIT/JOIN in a netsplit/netjoin batch: TRUE if it went into the
   summary printed when the batch ends */
gboolean batch_collect (server *serv, const char *id, session *sess, const char *nick);
/* forget open batches (disconnect) */
void batch_reset (server *serv);
/* the line belongs to a chathistory batch */
gboolean batch_is_history (server *serv, const char *id);

/* IRCv3 draft/chathistory: ask for the latest lines of a channel just
   joined (newer than what scrollback already shows) */
void chathistory_request (session *sess);
/* a chathistory batch for target starts / ends */
void chathistory_begin (server *serv, const char *target);
void chathistory_end (server *serv, const char *target);

/* IRCv3 draft/read-marker: the server says target was read up to t (on
   another device, or what it remembers from before): clear its unread
   count when nothing newer has come in */
void readmarker_from_server (server *serv, const char *target, time_t t);
/* we looked at sess: tell the server it's read up to its newest line */
void readmarker_send (session *sess);
/* the same in a few seconds (a busy channel in front isn't one MARKREAD
   per line) */
void readmarker_soon (session *sess);

/* +typing (IRCv3 typing, behind gui_typing_show): nick in target says
   active/paused/done; the window shows who's typing */
void typing_from (server *serv, const char *nick, const char *target, const char *state);
/* what to show for sess ("alice is typing..."), or NULL; g_free it */
char *typing_text (session *sess);
/* someone stopped being a typer by sending their line */
void typing_clear (session *sess, const char *nick);
/* we're typing in sess (gui_typing_send): tell the server, throttled;
   active FALSE when the line is sent or emptied */
void typing_send (session *sess, gboolean active);

/* +draft/reply (behind gui_replies): recent lines by msgid, so a reply
   can show what it answers */
void reply_remember (session *sess, const char *msgid, const char *nick, const char *text);
/* print "replying to alice: ..." above a line that replies to msgid */
void reply_show (session *sess, const char *msgid, time_t stamp);
/* the msgid of nick's last line here, or NULL */
const char *reply_last_from (session *sess, const char *nick);
/* forget everything for sess (closed) */
void reply_free (session *sess);

#endif
