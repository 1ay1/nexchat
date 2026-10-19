/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* What bouncers this computer has: installed programs, their configs and
   system services, and any answering on this computer right now. Run
   when "Stay online" opens, so it can say "soju is installed here" or
   "a soju is running on port 6697" instead of asking for an address. */

#ifndef NEXCHAT_BNCDETECT_H
#define NEXCHAT_BNCDETECT_H

#include <glib.h>

typedef struct
{
	char *kind;		/* "soju", "znc" */
	char *path;		/* the program, or NULL if not installed */
	char *version;	/* "0.11.1", or NULL */
	char *config;	/* a config of it found on this computer, or NULL */
	/* its system service: "running", "failed", "stopped", or NULL (none) */
	char *service;
	char *service_why;	/* failed: the last log line, in its words */
	gboolean ours;	/* the one nexchat runs */
} bnc_installed;

typedef struct
{
	char *kind;		/* "soju", "znc", "persist", "plain" */
	int port;
	gboolean tls;
	gboolean ours;	/* the one nexchat runs */
} bnc_listening;

typedef struct
{
	GPtrArray *installed;	/* bnc_installed* */
	GPtrArray *listening;	/* bnc_listening* */
} bnc_found;

/* looks; cb runs on the main thread when it's done (about a second: the
   ports are tried in parallel) */
typedef void (*bnc_detect_cb) (bnc_found *f, gpointer data);
void bncdetect_run (bnc_detect_cb cb, gpointer data);
void bnc_found_free (bnc_found *f);

/* one line per thing found, for tests and /bouncer detect */
char *bnc_found_describe (bnc_found *f);

#endif
