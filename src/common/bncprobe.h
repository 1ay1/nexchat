/* nexchat
 * Copyright (C) 2026 Ayush Bhat.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* What a bouncer is, without a window (bncprobe.c): the login probe, what
   this computer has, and the address parser. Used by fe-gtk's Stay online
   windows and the TUI's, so both find out the same things the same way. */

#ifndef NEXCHAT_BNCPROBE_H
#define NEXCHAT_BNCPROBE_H

#include <glib.h>

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
/* one connection, CAP LS, PASS/NICK/USER: what is it, is the login right.
   Runs in a thread; cb is called on the main loop. */
void bncsetup_probe (const char *host, int port, gboolean tls, const char *user,
							const char *pass, bnc_probe_cb cb, gpointer data);
void bnc_probe_result_free (bnc_probe_result *r);

/* "host", "host:port", "host:+port", "ircs://host:port", "irc://host" */
typedef struct
{
	char *host;		/* g_free it */
	int port;
	gboolean tls;
} address;
gboolean bnc_parse_address (const char *in, address *a);

/* ---- what this computer has: soju / ZNC installed, who answers on the
   usual ports ---- */
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

/* looks for soju and ZNC; the answer comes once everything is known (the
   ports are tried in parallel) */
typedef void (*bnc_detect_cb) (bnc_found *f, gpointer data);
void bncdetect_run (bnc_detect_cb cb, gpointer data);
void bnc_found_free (bnc_found *f);
char *bnc_found_describe (bnc_found *f);

#endif
