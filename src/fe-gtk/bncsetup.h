/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Network List > "Stay online...": set up a bouncer (soju, ZNC) or a
   network's own always-on, without knowing how either works. One small
   window: say what you have, "Check" logs in once to find out what it is
   and whether the password works, "Save and connect" writes the network
   entry in the form that bouncer wants and connects. */

#ifndef NEXCHAT_BNCSETUP_H
#define NEXCHAT_BNCSETUP_H

#include <gtk/gtk.h>
#include "../common/servlist.h"

/* net: fill in that network (NULL: a new one). done(net) runs after
   saving, before connecting, so the list can show it */
void bncsetup_open (GtkWindow *parent, ircnet *net, void (*done) (ircnet *net));

/* the kind found when logging in: "soju", "znc", "persist", "plain", or an
   error. Exposed for the tests (NEXCHAT_BNC_PROBE). */
typedef struct
{
	char *kind;		/* "soju", "znc", "persist", "plain"; NULL on error */
	char *seen;		/* what it looked like even when the login failed */
	char *error;	/* why not, in plain words */
	char *networks;	/* ZNC: its network names, comma separated */
	gboolean cert_untrusted;	/* TLS, but the certificate wouldn't be trusted */
	char *cert_why;	/* why, in plain words */
} bnc_probe_result;

typedef void (*bnc_probe_cb) (bnc_probe_result *r, gpointer data);
void bncsetup_probe (const char *host, int port, gboolean tls, const char *user,
							const char *pass, bnc_probe_cb cb, gpointer data);
void bnc_probe_result_free (bnc_probe_result *r);

/* tests: the open window's choice and status line, or NULL */
char *bncsetup_describe (void);
/* tests: "bncfound: ..." and "bncnets: ..." lines */
char *bncsetup_describe_more (void);

#endif
