/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_STS_H
#define NEXCHAT_STS_H

#include <glib.h>

/* the TLS port a connection to host must use (0: no policy) */
int sts_port_for (const char *host);
/* the server sent sts=value on host:port; returns the TLS port to
   reconnect to when the connection is plain text and must upgrade */
int sts_handle (const char *host, int port, gboolean tls, const char *value);

#endif
