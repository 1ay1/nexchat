/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Server > Bouncer...: the one place for the bouncer nexchat runs for you
   (a soju of its own). Shows it and every network on it, live; adds and
   takes off networks, starts and stops it, sets it up the first time.
   A bouncer elsewhere goes through "Use another bouncer..." (bncsetup). */

#ifndef NEXCHAT_BNCWIN_H
#define NEXCHAT_BNCWIN_H

#include <gtk/gtk.h>
#include "../common/servlist.h"

/* from: the network of the tab it was opened from (moved first when
   setting up), or NULL */
void bncwin_open (GtkWindow *parent, ircnet *from);

/* tests: one line of what it shows */
char *bncwin_describe (void);
/* a bouncer's state changed: show it now */
void bncwin_poke (void);

#endif
