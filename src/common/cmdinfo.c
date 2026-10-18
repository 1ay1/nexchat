/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* The commands the input box offers when you type "/": built-ins
   (xc_cmds), the user's commands (command_list) and plugin commands, with
   their help. One name wins the way running it does: a plugin over a
   user command over a built-in, as help() and handle_command() decide. */

#include <string.h>

#include "hexchat.h"
#include "hexchatc.h"
#include "outbound.h"
#include "plugin.h"
#include "cfgfiles.h"
#include "cmddocs.h"
#include "cmdinfo.h"

extern const struct prefs vars[];	/* cfgfiles.c */

void
cmdinfo_free (cmdinfo *c)
{
	if (!c)
		return;
	g_free (c->name);
	g_free (c->usage);
	g_free (c->desc);
	g_free (c->help);
	g_free (c);
}

void
cmdinfo_list_free (GPtrArray *list)
{
	if (list)
		g_ptr_array_free (list, TRUE);
}

/* 0: no match. A name starting with prefix beats one only containing it;
   shorter names first among equals (so /j shows JOIN before JUPE). */
static int
match_score (const char *name, const char *prefix)
{
	gsize plen = strlen (prefix);
	gsize nlen = strlen (name);
	const char *p;

	if (!plen)
		return 1000 - (int) MIN (nlen, 999);
	if (!g_ascii_strncasecmp (name, prefix, plen))
		return (nlen == plen ? 3000 : 2000) - (int) MIN (nlen, 999);
	for (p = name + 1; *p; p++)
		if (!g_ascii_strncasecmp (p, prefix, plen))
			return 500 - (int) MIN (nlen, 499);
	return 0;
}

/* "JOIN <channel>, joins the channel\n  more..." -> usage "JOIN <channel>",
   desc "joins the channel". Help that doesn't start with the name (plugin
   help is free text) is all description. */
static void
split_help (const char *name, const char *help, char **usage, char **desc)
{
	const char *nl, *comma;
	char *line;

	*usage = *desc = NULL;
	if (!help || !*help)
	{
		*usage = g_strdup ("");
		*desc = g_strdup ("");
		return;
	}
	if (!g_ascii_strncasecmp (help, "Usage: ", 7))
		help += 7;
	nl = strchr (help, '\n');
	line = nl ? g_strndup (help, nl - help) : g_strdup (help);
	g_strstrip (line);
	comma = strchr (line, ',');
	if (!g_ascii_strncasecmp (line, name, strlen (name)) && comma)
	{
		*usage = g_strndup (line, comma - line);
		*desc = g_strstrip (g_strdup (comma + 1));
	}
	else if (!g_ascii_strncasecmp (line, name, strlen (name)))
	{
		*usage = g_strdup (line);
		*desc = g_strdup ("");
	}
	else
	{
		*usage = g_strdup ("");
		*desc = g_strdup (line);
	}
	g_free (line);
}

static cmdinfo *
cmdinfo_new (const char *name, const char *help, cmdinfo_kind kind)
{
	cmdinfo *c = g_new0 (cmdinfo, 1);

	c->name = g_ascii_strup (name, -1);
	c->help = g_strdup (help ? help : "");
	split_help (name, help, &c->usage, &c->desc);
	c->kind = kind;
	/* plain-English docs replace the terse built-in line where there are some */
	c->doc = kind == CMD_PLUGIN ? NULL : cmddoc_find (name);
	if (c->doc)
	{
		g_free (c->usage);
		g_free (c->desc);
		c->usage = *c->doc->args ? g_strdup_printf ("%s %s", c->name, c->doc->args) : g_strdup (c->name);
		c->desc = g_strdup (_(c->doc->summary));
		if (!*c->help)
		{
			g_free (c->help);
			c->help = g_strdup_printf ("%s. %s", _(c->doc->summary), c->doc->more ? _(c->doc->more) : "");
		}
	}
	return c;
}

typedef struct
{
	GHashTable *seen;	/* upper-case name -> 1 */
	GPtrArray *out;
	const char *prefix;
} collect;

static void
add (collect *col, const char *name, const char *help, cmdinfo_kind kind)
{
	cmdinfo *c;
	char *key;
	int score;

	if (!name || !*name)
		return;
	score = match_score (name, col->prefix);
	if (!score)
		return;
	key = g_ascii_strup (name, -1);
	if (g_hash_table_contains (col->seen, key))
	{
		g_free (key);
		return;
	}
	g_hash_table_add (col->seen, key);
	c = cmdinfo_new (name, help, kind);
	c->score = score;
	/* nothing typed yet: most used first, then each documented group in
	   its turn (so every group is one block under one heading), then your
	   commands, plugins, and the rest; by name inside a block */
	if (!*col->prefix)
	{
		int block = c->doc && c->doc->common ? 0
			: c->doc ? 1 + cmddoc_group_rank (c->doc->group)
			: c->kind == CMD_USER ? 90 : c->kind == CMD_PLUGIN ? 91 : 92;
		c->score = 100000 - block * 1000;
	}
	/* typed part of a name: an everyday command wins a tie */
	else if (c->doc && c->doc->common)
		c->score += 50;
	g_ptr_array_add (col->out, c);
}

static void
add_plugin (session *sess, void *data, char *name, char *help)
{
	add (data, name, help, CMD_PLUGIN);
}

static int
by_score (gconstpointer a, gconstpointer b)
{
	const cmdinfo *x = *(cmdinfo **) a, *y = *(cmdinfo **) b;

	if (x->score != y->score)
		return y->score - x->score;
	return strcmp (x->name, y->name);
}

GPtrArray *
cmdinfo_find (const char *prefix)
{
	collect col;
	GSList *l;
	int i;

	col.seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	col.out = g_ptr_array_new_with_free_func ((GDestroyNotify) cmdinfo_free);
	col.prefix = prefix ? prefix : "";

	/* in the order a name is looked up when it runs */
	plugin_command_foreach (NULL, &col, add_plugin);
	/* user commands: one name can have several lines (all run); one row */
	for (l = command_list; l; l = l->next)
	{
		struct popup *pop = l->data;
		char *key = g_ascii_strup (pop->name, -1);
		gboolean had = g_hash_table_contains (col.seen, key);
		g_free (key);
		if (had)
		{
			/* a further line of a user command already listed: add what it runs */
			guint k;
			for (k = 0; k < col.out->len; k++)
			{
				cmdinfo *c = g_ptr_array_index (col.out, k);
				if (c->kind == CMD_USER && !g_ascii_strcasecmp (c->name, pop->name))
				{
					char *d = g_strdup_printf ("%s; %s", c->desc, pop->cmd);
					g_free (c->desc);
					c->desc = d;
					break;
				}
			}
			continue;
		}
		add (&col, pop->name, NULL, CMD_USER);
		/* add () kept it only if it matched: fill in what it runs */
		if (col.out->len)
		{
			cmdinfo *c = g_ptr_array_index (col.out, col.out->len - 1);
			if (c->kind == CMD_USER && !g_ascii_strcasecmp (c->name, pop->name) && !c->doc)
			{
				g_free (c->desc);
				c->desc = g_strdup_printf (_("runs %s"), pop->cmd);
			}
		}
	}
	for (i = 0; xc_cmds[i].name; i++)
		add (&col, xc_cmds[i].name, xc_cmds[i].help ? _(xc_cmds[i].help) : NULL, CMD_BUILTIN);
	/* documented commands that go to the server as they are (WHOIS) */
	{
		const cmddoc *d;
		for (d = cmddoc_all (); d->name; d++)
			add (&col, d->name, NULL, CMD_BUILTIN);
	}

	g_hash_table_destroy (col.seen);
	g_ptr_array_sort (col.out, by_score);
	return col.out;
}

/* Damerau-Levenshtein, optimal string alignment (a swap is one edit:
   "jion" is one from "join"), case folded. Command names are short. */
static int
edit_distance (const char *a, const char *b, int limit)
{
	int la = strlen (a), lb = strlen (b), i, j, d;
	int *m;

	if (ABS (la - lb) > limit)
		return limit + 1;
	m = g_new (int, (la + 1) * (lb + 1));
#define M(x, y) m[(x) * (lb + 1) + (y)]
	for (i = 0; i <= la; i++)
		M (i, 0) = i;
	for (j = 0; j <= lb; j++)
		M (0, j) = j;
	for (i = 1; i <= la; i++)
		for (j = 1; j <= lb; j++)
		{
			char ca = g_ascii_tolower (a[i - 1]), cb = g_ascii_tolower (b[j - 1]);
			d = MIN (MIN (M (i - 1, j) + 1, M (i, j - 1) + 1), M (i - 1, j - 1) + (ca != cb));
			if (i > 1 && j > 1 && ca == g_ascii_tolower (b[j - 2]) &&
				 g_ascii_tolower (a[i - 2]) == cb)
				d = MIN (d, M (i - 2, j - 2) + 1);
			M (i, j) = d;
		}
	d = M (la, lb);
#undef M
	g_free (m);
	return d;
}

cmdinfo *
cmdinfo_nearest (const char *name)
{
	GPtrArray *all = cmdinfo_find ("");
	cmdinfo *best = NULL;
	int best_d, limit;
	guint i;

	/* short names only get one typo, or everything is "near" */
	limit = strlen (name) <= 4 ? 1 : 2;
	best_d = limit + 1;
	for (i = 0; i < all->len; i++)
	{
		cmdinfo *c = g_ptr_array_index (all, i);
		int d = edit_distance (name, c->name, limit);
		if (d < best_d)
		{
			best_d = d;
			best = c;
		}
	}
	if (best)
	{
		g_ptr_array_find (all, best, &i);
		best = g_ptr_array_steal_index (all, i);
	}
	cmdinfo_list_free (all);
	return best;
}

cmdinfo *
cmdinfo_lookup (const char *name)
{
	GPtrArray *all = cmdinfo_find (name);
	cmdinfo *hit = NULL;
	guint i;

	for (i = 0; i < all->len; i++)
	{
		cmdinfo *c = g_ptr_array_index (all, i);
		if (!g_ascii_strcasecmp (c->name, name))
		{
			hit = g_ptr_array_steal_index (all, i);
			break;
		}
	}
	cmdinfo_list_free (all);
	return hit;
}

/* where argument n sits in usage: the n-th <...> or [...] after the name */
gboolean
cmdinfo_arg_span (cmdinfo *c, int n, int *start, int *end)
{
	const char *u = c->usage, *p;
	int i = 0;

	if (!u || !*u || n < 1)
		return FALSE;
	p = u;
	if (!g_ascii_strncasecmp (p, c->name, strlen (c->name)))
		p += strlen (c->name);
	while (*p)
	{
		if (*p == '<' || *p == '[')
		{
			char close = *p == '<' ? '>' : ']';
			const char *q = strchr (p, close);
			if (!q)
				return FALSE;
			if (++i == n)
			{
				*start = p - u;
				*end = q - u + 1;
				return TRUE;
			}
			p = q + 1;
		}
		else
			p++;
	}
	return FALSE;
}

cmdinfo_arg
cmdinfo_arg_kind (cmdinfo *c, int n)
{
	int s, e;
	char *word;
	cmdinfo_arg kind = ARG_NONE;

	/* commands whose first argument is known whatever the help says */
	if (n == 1)
	{
		if (!strcmp (c->name, "SET"))
			return ARG_SETTING;
		if (!strcmp (c->name, "HELP"))
			return ARG_COMMAND;
		/* /server freenode-style: a network from the list (or a host) */
		if (!strcmp (c->name, "SERVER") || !strcmp (c->name, "NEWSERVER"))
			return ARG_NETWORK;
	}
	if (!cmdinfo_arg_span (c, n, &s, &e))
		return ARG_NONE;
	word = g_ascii_strdown (c->usage + s + 1, e - s - 2);
	if (strstr (word, "nick"))
		kind = ARG_NICK;
	else if (strstr (word, "channel") || !strcmp (word, "#chan"))
		kind = ARG_CHANNEL;
	g_free (word);
	return kind;
}

gboolean
cmdinfo_arg_is_choice (cmdinfo *c, int n)
{
	int s, e;
	char *word;
	gboolean choice;

	if (!cmdinfo_arg_span (c, n, &s, &e))
		return FALSE;
	word = g_strndup (c->usage + s + 1, e - s - 2);
	/* <SEND|GET|CHAT>, <VERSION|TIME|PING>, or a slot the docs fill with
	   choices: <what>, [options] */
	choice = strchr (word, '|') != NULL ||
		(c->doc && c->doc->opts && (!g_ascii_strcasecmp (word, "what") ||
											 !g_ascii_strcasecmp (word, "options")));
	g_free (word);
	return choice;
}

static int
strcmp_p (gconstpointer a, gconstpointer b)
{
	return strcmp (*(char **) a, *(char **) b);
}

GPtrArray *
cmdinfo_settings (const char *prefix)
{
	GPtrArray *out = g_ptr_array_new ();
	gsize plen = strlen (prefix);
	int i;

	for (i = 0; vars[i].name; i++)
		if (!g_ascii_strncasecmp (vars[i].name, prefix, plen))
			g_ptr_array_add (out, vars[i].name);
	g_ptr_array_sort (out, strcmp_p);
	return out;
}
