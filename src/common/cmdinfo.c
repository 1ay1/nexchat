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
#include "cmdinfo.h"

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
	for (l = command_list; l; l = l->next)
	{
		struct popup *pop = l->data;
		add (&col, pop->name, NULL, CMD_USER);
		/* add () kept it only if it matched and wasn't taken: fill in what
		   it runs */
		if (col.out->len)
		{
			cmdinfo *c = g_ptr_array_index (col.out, col.out->len - 1);
			if (c->kind == CMD_USER && !g_ascii_strcasecmp (c->name, pop->name) && !*c->desc)
			{
				g_free (c->desc);
				c->desc = g_strdup_printf (_("runs %s"), pop->cmd);
			}
		}
	}
	for (i = 0; xc_cmds[i].name; i++)
		add (&col, xc_cmds[i].name, xc_cmds[i].help ? _(xc_cmds[i].help) : NULL, CMD_BUILTIN);

	g_hash_table_destroy (col.seen);
	g_ptr_array_sort (col.out, by_score);
	return col.out;
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
