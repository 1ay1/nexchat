/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_CMDINFO_H
#define NEXCHAT_CMDINFO_H

#include <glib.h>

/* where a command comes from */
typedef enum
{
	CMD_BUILTIN,
	CMD_USER,		/* Settings > User Commands (an alias) */
	CMD_PLUGIN
} cmdinfo_kind;

/* one command as the input box shows it */
typedef struct
{
	char *name;			/* "JOIN" */
	char *usage;		/* "JOIN <channel>" (what to type), or "" */
	char *desc;			/* "joins the channel" (what it does), or "" */
	char *help;			/* the whole help text, or "" */
	cmdinfo_kind kind;
	int score;			/* how well it matched */
	const struct _cmddoc *doc;	/* plain-English docs (cmddocs.c), or NULL */
} cmdinfo;

/* every command whose name starts with or contains prefix (case doesn't
   matter), best matches first: built-ins, the user's commands and plugin
   commands, each named once. With an empty prefix the everyday ones come
   first, then by group. Free with cmdinfo_list_free. */
GPtrArray *cmdinfo_find (const char *prefix);
void cmdinfo_list_free (GPtrArray *list);
/* the one command named exactly name, or NULL; free with cmdinfo_free */
cmdinfo *cmdinfo_lookup (const char *name);
/* a command one or two typos away from name (/jion -> JOIN), or NULL */
cmdinfo *cmdinfo_nearest (const char *name);
void cmdinfo_free (cmdinfo *c);

/* what kind of value argument n (1 = the first after the name) of c takes,
   read off its usage ("KICK <nick> [reason]" -> 1 is a nick) */
typedef enum
{
	ARG_NONE,
	ARG_NICK,
	ARG_CHANNEL,
	ARG_SETTING,	/* /set */
	ARG_NETWORK,	/* /server, /connect: from the network list */
	ARG_COMMAND		/* /help */
} cmdinfo_arg;
cmdinfo_arg cmdinfo_arg_kind (cmdinfo *c, int n);
/* argument n takes one of the command's listed options/choices ("<what>"
   of /ignore, "<SEND|GET...>" of /dcc) */
gboolean cmdinfo_arg_is_choice (cmdinfo *c, int n);
/* the place of argument n in c's usage as byte offsets into c->usage
   (for showing it bold); FALSE if the usage doesn't have one */
gboolean cmdinfo_arg_span (cmdinfo *c, int n, int *start, int *end);
/* setting names (from /set's table) starting with prefix, sorted; free
   with g_ptr_array_unref */
GPtrArray *cmdinfo_settings (const char *prefix);

#endif
