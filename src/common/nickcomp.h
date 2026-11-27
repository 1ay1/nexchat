/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Which person a few typed letters mean. One ranking for everything that
   completes nicks: Tab, the "@" list and <nick> arguments in the input
   box (both front ends) and the "ni: hi" completion on send.

   How well the letters fit comes first, then (in "last spoke" order, the
   default) who's in the conversation with you: someone who just said your
   name, someone you just spoke to, someone who just talked. Away people
   after present ones, then A-Z. Your own nick only if nothing else fits. */

#ifndef NEXCHAT_NICKCOMP_H
#define NEXCHAT_NICKCOMP_H

#include <glib.h>
#include <time.h>

typedef enum
{
	NICKCOMP_NONE = -1,
	NICKCOMP_EXACT,		/* "bob" -> bob */
	NICKCOMP_PREFIX,		/* "bo" -> bob */
	NICKCOMP_LOOSE,		/* "kiki" -> _kiki_, "exbob" -> [Ex]Bob: the letters only */
	NICKCOMP_WORD,			/* "cat" -> CrazyCat, "64" -> Nemu64: a word inside it */
	NICKCOMP_LETTERS		/* "cc" -> CrazyCat: its letters in order, from the first */
} nickcomp_match_t;

typedef struct
{
	const char *nick;
	time_t lasttalk;		/* said anything */
	time_t lastmention;	/* said your name */
	time_t lastaddressed;	/* you said theirs */
	unsigned int away:1;
	unsigned int me:1;
	int match;				/* set by nickcomp_rank */
	gpointer data;
} nickcomp_cand;

/* how word fits nick (IRC case: [ is {, ...) */
nickcomp_match_t nickcomp_match (const char *nick, const char *word);

/* drop the candidates (nickcomp_cand *) that word doesn't fit or that fit
   worse than worst, sort the rest best first; smart: by who's in the
   conversation, else A-Z within each fit */
void nickcomp_rank (GPtrArray *cands, const char *word, nickcomp_match_t worst, gboolean smart);

/* the first word of a line is addressed to someone ("bob: hi", "bob, hi",
   "@bob hi"): that word without its punctuation, or NULL (g_free it) */
char *nickcomp_addressee (const char *line, const char *suffix);

#endif
