/* nexchat
 * Copyright (C) 2026 nexchat contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_HTTPGET_H
#define NEXCHAT_HTTPGET_H

#include <glib.h>

/* A small HTTP(S) GET for link previews: GIO for the connection, the
   OpenSSL nexchat already uses for TLS (verified, with SNI), HTTP/1.0 so
   nothing is chunked. Runs in a thread; the callback comes back on the
   main loop.

   Only http/https, only public addresses (a link can't make us talk to
   the router, localhost or the LAN), at most max_bytes of body, at most 3
   redirects, 8 seconds in all. */

typedef struct
{
	int status;				/* HTTP status, 0 if it never got that far */
	char *content_type;		/* lower case, no parameters ("image/png") */
	GBytes *body;			/* up to max_bytes; NULL on failure */
	gboolean truncated;		/* there was more */
	char *error;			/* why it failed, for the curious */
	char *final_url;		/* after redirects */
} httpget_result;

typedef void (*httpget_cb) (httpget_result *res, gpointer data);

/* res is freed after the callback returns */
void httpget (const char *url, gsize max_bytes, GCancellable *cancel,
				  httpget_cb cb, gpointer data);

/* POST body to url (file uploads): content_type and filename describe
   it, auth is an "Authorization:" value or NULL. Redirects aren't
   followed (the spec asks for 201 + Location; res->final_url carries the
   Location, resolved against url). Public addresses only, as above. */
void httppost (const char *url, GBytes *body, const char *content_type,
					const char *filename, const char *auth, GCancellable *cancel,
					httpget_cb cb, gpointer data);

#endif
