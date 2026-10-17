/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_CMDPOPUP_H
#define NEXCHAT_CMDPOPUP_H

#include <gtk/gtk.h>

/* the "/" command list right on top of the box holding the input box (nick
   button + entry), as wide as that box */
void cmdpopup_attach (GtkWidget *entry);
/* a key in the entry: TRUE if the list used it (Up/Down/Tab/Enter/Escape) */
gboolean cmdpopup_key (GtkWidget *entry, guint keyval, GdkModifierType state);
/* tests: what it shows now, "" when closed (g_free it) */
char *cmdpopup_describe (GtkWidget *entry);
/* tests: the popover itself, for a screenshot */
GtkWidget *cmdpopup_widget (GtkWidget *entry);
/* tests: where it sits next to the input row */
char *cmdpopup_geometry (GtkWidget *entry);

#endif
