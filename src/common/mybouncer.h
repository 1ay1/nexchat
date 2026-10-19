/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* A bouncer nexchat runs for you: if soju is installed, nexchat sets up
   its own private soju (its own folder, its own config, loopback only,
   nothing system-wide touched), keeps it running (a systemd user service
   where there is one, so it stays up when nexchat is closed and the
   computer is on; otherwise started with nexchat), and moves your
   networks onto it: the address, your nick, your NickServ/SASL login and
   your channels. Each network entry then connects through it. */

#ifndef NEXCHAT_MYBOUNCER_H
#define NEXCHAT_MYBOUNCER_H

#include "hexchat.h"
#include "servlist.h"

typedef enum
{
	MYBNC_NONE,			/* no soju on this computer */
	MYBNC_AVAILABLE,	/* soju is installed, not set up */
	MYBNC_SET_UP		/* ours is set up (running or not) */
} mybnc_state;

mybnc_state mybouncer_state (void);
/* where soju is, or NULL */
const char *mybouncer_soju_path (void);
/* is it answering right now */
gboolean mybouncer_running (void);
/* "systemd" (stays up when nexchat is closed) or "nexchat" (only while
   nexchat runs) */
const char *mybouncer_kept_by (void);

/* set it up and start it; the networks are moved by mybouncer_adopt.
   FALSE with *err (in plain words) if it can't */
gboolean mybouncer_setup (char **err);
/* move nets (ircnet*) onto it: each is added to soju with its login and
   channels, and its entry changed to connect through soju. Done in the
   background; progress goes to cb (text, done-flag) */
typedef void (*mybnc_progress) (const char *text, gboolean done, gboolean ok, gpointer data);
void mybouncer_adopt (GSList *nets, mybnc_progress cb, gpointer data);
/* back to connecting directly (the entries), and stop and remove ours */
gboolean mybouncer_remove (char **err);
/* just this network: off soju, its entry as it was before */
gboolean mybouncer_release (ircnet *net, char **err);
/* start / stop ours (kept set up) */
gboolean mybouncer_start (char **err);
void mybouncer_stop (void);

/* what soju says about each of its networks: "name\tstate\tdetails"
   lines ("connected as dee" / "3 channels" / the last error), or NULL */
char *mybouncer_status (void);

/* at startup: if ours is set up and kept by nexchat, start it */
void mybouncer_init (void);
/* logged in through our soju with nick: if your settings say another,
   it's changed (and soju keeps the new one) */
void mybouncer_logged_in (server *serv, const char *nick);
/* the nick your settings give net (its own, or the global one) */
const char *mybouncer_wanted_nick (ircnet *net);
/* what net uses on its network, through our soju: the nick and where it
   comes from, how it logs in (\"SASL as alice\", \"NickServ\", \"none\")
   and the account. g_free each */
void mybouncer_identity (ircnet *net, char **nick, char **nick_from, char **login, char **account);
/* change how net logs in to its network (on soju and in the entry):
   account and password for SASL; account NULL or \"\" for none. FALSE with *err */
gboolean mybouncer_set_login (ircnet *net, const char *account, const char *password, char **err);
/* the network's own password kept for net (g_free), or NULL */
char *mybouncer_own_password (ircnet *net);
/* entries we moved: this one goes through our soju */
gboolean mybouncer_owns (ircnet *net);

#endif
