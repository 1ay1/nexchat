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
   entry in the form that bouncer wants and connects. The probe itself is
   common/bncprobe.c (the TUI uses it too). */

#ifndef NEXCHAT_BNCSETUP_H
#define NEXCHAT_BNCSETUP_H

#include <gtk/gtk.h>
#include "../common/servlist.h"
#include "../common/bncprobe.h"

/* net: fill in that network (NULL: a new one). done(net) runs after
   saving, before connecting, so the list can show it */
void bncsetup_open (GtkWindow *parent, ircnet *net, void (*done) (ircnet *net));

/* tests: the open window's choice and status line, or NULL */
char *bncsetup_describe (void);
/* tests: "bncfound: ..." and "bncnets: ..." lines */
char *bncsetup_describe_more (void);

#endif
