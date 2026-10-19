/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Staying online through a bouncer (soju, ZNC) or a network that keeps
   you connected itself (Ergo's always-on, IRCv3 draft/persistence).
   What kind of server we're on is worked out from what it offers at
   connect, and each kind is driven the way it understands. */

#ifndef NEXCHAT_BOUNCER_H
#define NEXCHAT_BOUNCER_H

#include "hexchat.h"
#include "servlist.h"

typedef enum
{
	BNC_NONE = 0,		/* a plain IRC server */
	BNC_SOJU,			/* soju.im/bouncer-networks (soju, also KiwiBNC) */
	BNC_ZNC,			/* znc.in/... caps or the irc.znc.in name */
	BNC_PERSIST		/* the network keeps us on: draft/persistence (Ergo) */
} bnc_kind;

/* a network the bouncer is connected to for us */
typedef struct
{
	char *id;		/* soju netid; ZNC: the network name */
	char *name;
	char *host;
	int port;
	gboolean tls;
	char *nick;
	char *state;	/* "connected", "connecting", "disconnected" */
	char *error;	/* why it's disconnected, if the bouncer says */
} bnc_network;

/* CAP LS: the caps we want for this (added to the REQ in buffer) */
void bouncer_cap_ls (server *serv, const char *cap, const char *value, char *buffer, int buflen, gboolean *want);
/* CAP ACK/NEW/DEL */
void bouncer_cap_toggle (server *serv, const char *cap, gboolean enable);
/* who answered CAP LS, and the 005 tokens: a ZNC answers CAP as irc.znc.in */
void bouncer_server_name (server *serv, const char *name);
void bouncer_isupport (server *serv, const char *token);
/* end of MOTD: ask what we need (soju's networks, persistence status) */
void bouncer_login_end (server *serv);
/* BOUNCER and PERSISTENCE from the server */
/* sub, then its first argument, then the rest (":" already stripped) */
void bouncer_command (server *serv, const char *sub, const char *arg, const char *rest);
void bouncer_persistence (server *serv, const char *sub, const char *client, const char *effective);
/* FAIL BOUNCER / FAIL PERSISTENCE: TRUE if it was ours */
gboolean bouncer_fail (server *serv, char *word[], const char *text);
/* a reply from ZNC's *status / *controlpanel: TRUE if it was taken in */
gboolean bouncer_znc_reply (server *serv, const char *from, const char *text);
/* disconnected */
void bouncer_reset (server *serv);

bnc_kind bouncer_kind (server *serv);
const char *bouncer_kind_name (bnc_kind k);
/* the networks the bouncer has (soju, ZNC), or NULL */
GPtrArray *bouncer_networks (server *serv);
/* -1 unknown, 0 off, 1 on (persistence) */
int bouncer_persist_state (server *serv);
/* one line for a tooltip or status: "Online 24/7 via soju (3 networks)" */
char *bouncer_describe (server *serv);

/* ask the bouncer to change things. FALSE (with *err set) if it can't */
gboolean bouncer_add_network (server *serv, const char *name, const char *host, int port,
										gboolean tls, const char *nick, char **err);
gboolean bouncer_del_network (server *serv, const char *id, char **err);
gboolean bouncer_set_persist (server *serv, gboolean on, char **err);
/* list again (ZNC has no notifications) */
void bouncer_refresh (server *serv);

/* networks to put on the bouncer behind net the next time it's connected
   ("name host[:port]" lines; ":-port" is plain text). Ones it already
   has are left alone. */
void bouncer_queue_networks (ircnet *net, const char *lines);

/* soju.im/search: search the bouncer's history of each target for text,
   results printed in out as they come. FALSE (with *err) if it can't */
gboolean bouncer_search (server *serv, session *out, char **targets, const char *text, char **err);
/* a line of a soju.im/search batch: TRUE if it was taken (printed in out) */
gboolean bouncer_search_take (server *serv, const char *batch, const char *line, time_t when);
/* BATCH +id soju.im/search / BATCH -id */
void bouncer_search_batch (server *serv, const char *id, gboolean start);

/* /BOUNCER */
int bouncer_cmd (session *sess, char *word[], char *word_eol[]);

#endif
