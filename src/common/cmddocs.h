/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEXCHAT_CMDDOCS_H
#define NEXCHAT_CMDDOCS_H

/* Plain-English help for the commands people use, for the "/" list:
   what it does in words anyone gets, which group it's in, an example to
   copy, and its options/choices explained. The old one-line help in
   outbound.c stays for /HELP and for commands not described here. */

typedef struct
{
	const char *flag;		/* "-a", "ALL", "-ssl" */
	const char *what;		/* "search every open window" */
} cmddoc_opt;

typedef struct _cmddoc
{
	const char *name;		/* "JOIN" */
	const char *group;		/* "Channels" */
	const char *summary;	/* "Go into a channel" */
	const char *args;		/* "<channel> [password]" (shown after the name) */
	const char *example;	/* "/join #linux" */
	const char *more;		/* a sentence of detail, or NULL */
	const cmddoc_opt *opts;	/* NULL-terminated, or NULL */
	int common;				/* shown first when nothing is typed after "/" */
} cmddoc;

const cmddoc *cmddoc_find (const char *name);
/* every entry, NULL-terminated, in the order the groups are shown */
const cmddoc *cmddoc_all (void);
/* where a group goes in the list (0 first); the same for every command in it */
int cmddoc_group_rank (const char *group);

#endif
