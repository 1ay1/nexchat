/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_PLATFORM_MACOS_H
#define NEXCHAT_PLATFORM_MACOS_H

#include <glib.h>

/* bounce the Dock icon until the app is activated (TRUE) or stop (FALSE) */
void macos_request_attention (gboolean urgent);
/* the system is in dark mode */
gboolean macos_is_dark (void);
/* the system accent colour */
gboolean macos_accent (double *r, double *g, double *b);

#endif
