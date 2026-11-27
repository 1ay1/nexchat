/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "nickcomp.h"

extern const unsigned char rfc_tolowertab[];
#define lc(c) (rfc_tolowertab[(unsigned char) (c)])

/* someone who said your name, or whom you answered, counts as having
   spoken this much more recently: the conversation you're in comes first,
   and between two of those, the latest */
#define MENTION_BONUS (5 * 60)
#define ADDRESSED_BONUS (5 * 60)

static gboolean
alnum (unsigned char c)
{
	return g_ascii_isalnum (c) || c >= 0x80;
}

/* nick folded, with only its letters and digits: "[Ex]Bob" -> "exbob" */
static void
skeleton (const char *s, char *out, gsize len)
{
	gsize n = 0;

	for (; *s && n + 1 < len; s++)
		if (alnum (*s))
			out[n++] = lc (*s);
	out[n] = 0;
}

static gboolean
prefix_fold (const char *s, const char *w)
{
	for (; *w; s++, w++)
		if (!*s || lc (*s) != lc (*w))
			return FALSE;
	return TRUE;
}

/* a word starts at i: after punctuation, at a lower->Upper or a
   letter<->digit change ("CrazyCat", "Nemu64", "dark_star") */
static gboolean
word_start (const char *n, gsize i)
{
	unsigned char a = n[i - 1], b = n[i];

	if (!alnum (a))
		return alnum (b);
	if (g_ascii_islower (a) && g_ascii_isupper (b))
		return TRUE;
	return g_ascii_isdigit (a) != g_ascii_isdigit (b) && a < 0x80 && b < 0x80;
}

nickcomp_match_t
nickcomp_match (const char *nick, const char *word)
{
	char sn[128], sw[128];
	gsize i, wl = strlen (word);
	const char *p;

	if (!*word)
		return NICKCOMP_PREFIX;	/* nothing typed yet: everyone */
	if (prefix_fold (nick, word))
		return nick[wl] ? NICKCOMP_PREFIX : NICKCOMP_EXACT;
	skeleton (nick, sn, sizeof sn);
	skeleton (word, sw, sizeof sw);
	if (*sw && prefix_fold (sn, sw))
		return NICKCOMP_LOOSE;
	if (wl < 2)
		return NICKCOMP_NONE;
	for (i = 1; nick[i]; i++)
		if (word_start (nick, i) && prefix_fold (nick + i, word))
			return NICKCOMP_WORD;
	/* the letters in order, the first of them the nick's first */
	if (!*sw || !*sn || sn[0] != sw[0])
		return NICKCOMP_NONE;
	for (p = sn, i = 0; *p && sw[i]; p++)
		if (*p == sw[i])
			i++;
	return sw[i] ? NICKCOMP_NONE : NICKCOMP_LETTERS;
}

static gboolean rank_smart;

/* how fresh someone is in the conversation */
static time_t
recency (const nickcomp_cand *c)
{
	time_t r = c->lasttalk;

	if (c->lastmention && c->lastmention + MENTION_BONUS > r)
		r = c->lastmention + MENTION_BONUS;
	if (c->lastaddressed && c->lastaddressed + ADDRESSED_BONUS > r)
		r = c->lastaddressed + ADDRESSED_BONUS;
	return r;
}

/* a prefix and a prefix of the letters are as good as each other: which
   of them is in the conversation decides */
static int
tier (int match)
{
	return match == NICKCOMP_LOOSE ? NICKCOMP_PREFIX : match;
}

static int
fold_cmp (const char *a, const char *b)
{
	for (; *a && lc (*a) == lc (*b); a++, b++)
		;
	return (int) lc (*a) - (int) lc (*b);
}

static int
cand_cmp (gconstpointer pa, gconstpointer pb)
{
	const nickcomp_cand *a = *(nickcomp_cand **) pa, *b = *(nickcomp_cand **) pb;

	if (a->me != b->me)
		return a->me ? 1 : -1;
	if (tier (a->match) != tier (b->match))
		return tier (a->match) - tier (b->match);
	if (rank_smart)
	{
		time_t ra = recency (a), rb = recency (b);
		if (ra != rb)
			return ra > rb ? -1 : 1;
		if (a->away != b->away)
			return a->away ? 1 : -1;
	}
	if (a->match != b->match)
		return a->match - b->match;
	return fold_cmp (a->nick, b->nick);
}

void
nickcomp_rank (GPtrArray *cands, const char *word, nickcomp_match_t worst, gboolean smart)
{
	gboolean others = FALSE;
	guint i;

	for (i = 0; i < cands->len;)
	{
		nickcomp_cand *c = cands->pdata[i];

		c->match = nickcomp_match (c->nick, word);
		if (c->match == NICKCOMP_NONE || c->match > (int) worst)
			g_ptr_array_remove_index (cands, i);
		else
		{
			others |= !c->me;
			i++;
		}
	}
	/* your own nick only when it's the only one that fits */
	if (others)
	{
		for (i = 0; i < cands->len;)
		{
			if (((nickcomp_cand *) cands->pdata[i])->me)
				g_ptr_array_remove_index (cands, i);
			else
				i++;
		}
	}
	rank_smart = smart;
	g_ptr_array_sort (cands, cand_cmp);
}

char *
nickcomp_addressee (const char *line, const char *suffix)
{
	const char *e;
	gsize n;
	gboolean at = *line == '@';

	if (at)
		line++;
	for (e = line; *e && *e != ' '; e++)
		;
	n = e - line;
	/* "bob:" "bob," "bob<suffix>", or "@bob" */
	if (n > 1 && (line[n - 1] == ':' || line[n - 1] == ',' || (suffix && *suffix && line[n - 1] == *suffix)))
		n--;
	else if (!at)
		return NULL;
	return n ? g_strndup (line, n) : NULL;
}
