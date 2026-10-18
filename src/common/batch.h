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

#endif
