/* nexchat TUI — the command popup's side of the bridge (bridge.h): what to
 * offer while "/..." is typed, the logic of fe-gtk/cmdpopup.c without its
 * widgets. Commands (built-in, the user's, plugins') and their docs come
 * from common/cmdinfo.c and cmddocs.c; values for the argument under the
 * caret from the session (people, channels, settings, networks, commands).
 */

#include "config.h"

#include <string.h>

#include <glib.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cmdinfo.h"
#include "../common/cmddocs.h"
#include "../common/userlist.h"
#include "../common/servlist.h"

#include "bridge.h"

#define MAX_ITEMS 400

char
nx_core_cmd_char (void)
{
	return prefs.hex_input_command_char[0] ? prefs.hex_input_command_char[0] : '/';
}

static int
is_cmd_char (char c)
{
	return c && c == nx_core_cmd_char ();
}

static void
add (GPtrArray *out, const char *name, const char *middle, const char *desc, const char *insert,
	  const char *heading, const char *cmd)
{
	nx_cmd_item *it = g_new0 (nx_cmd_item, 1);
	it->name = g_strdup (name ? name : "");
	it->middle = g_strdup (middle ? middle : "");
	it->desc = g_strdup (desc ? desc : "");
	it->insert = g_strdup (insert ? insert : "");
	it->heading = g_strdup (heading ? heading : "");
	it->cmd = g_strdup (cmd ? cmd : "");
	g_ptr_array_add (out, it);
}

static void
item_free (nx_cmd_item *it)
{
	g_free (it->name);
	g_free (it->middle);
	g_free (it->desc);
	g_free (it->insert);
	g_free (it->heading);
	g_free (it->cmd);
	g_free (it);
}

/* cmdpopup.c command_items: the commands matching word; with nothing
   typed, the everyday ones first, then the rest in their groups */
static void
add_command_rows (GPtrArray *out, const char *word)
{
	GPtrArray *cmds = cmdinfo_find (word);
	const char *last_group = NULL;
	guint i;

	for (i = 0; i < cmds->len && out->len < MAX_ITEMS; i++)
	{
		cmdinfo *c = g_ptr_array_index (cmds, i);
		const char *args = c->usage;
		const char *group = NULL;
		char *name = g_strdup_printf ("%c%s", nx_core_cmd_char (), c->name);
		char *lower = g_ascii_strdown (name, -1);
		if (!g_ascii_strncasecmp (args, c->name, strlen (c->name)))
			args += strlen (c->name);
		while (*args == ' ')
			args++;
		if (!*word)
		{
			group = c->doc && c->doc->common ? _("Most used")
				: c->doc ? _(c->doc->group) : c->kind == CMD_PLUGIN ? _("Plugins")
				: c->kind == CMD_USER ? _("Your commands") : _("More");
			if (last_group && !strcmp (group, last_group))
				group = NULL;
			else
				last_group = group;
		}
		add (out, lower, args,
			  c->kind == CMD_PLUGIN && !*c->desc ? _("(plugin)") : (c->doc ? _(c->doc->summary) : c->desc),
			  c->name, group, c->name);
		g_free (name);
		g_free (lower);
	}
	cmdinfo_list_free (cmds);
}

/* the argument the caret is in: 1 = the first; its word from *start */
static int
current_arg (const char *text, int cursor, int *start, char **word)
{
	const char *cur = text + cursor;
	const char *p = strchr (text, ' '), *ws;
	int n = 0;

	if (!p || p >= cur + 1)
		return 0;
	ws = p;
	while (p < cur)
	{
		if (*p == ' ')
		{
			while (p < cur && *p == ' ')
				p++;
			n++;
			ws = p;
		}
		else
			p++;
	}
	*start = (int) (ws - text);
	*word = g_strndup (ws, cur - ws);
	return n;
}

static int
by_lasttalk (gconstpointer a, gconstpointer b)
{
	const nx_cmd_item *x = *(nx_cmd_item **) a, *y = *(nx_cmd_item **) b;
	struct User *ux = current_sess ? userlist_find (current_sess, x->name) : NULL;
	struct User *uy = current_sess ? userlist_find (current_sess, y->name) : NULL;
	time_t tx = ux ? ux->lasttalk : 0, ty = uy ? uy->lasttalk : 0;
	if (tx != ty)
		return tx > ty ? -1 : 1;
	return g_ascii_strcasecmp (x->name, y->name);
}

static void
add_nicks (GPtrArray *out, session *sess, const char *prefix)
{
	gsize plen = strlen (prefix);
	GList *users, *l;
	guint first = out->len;

	if (!sess || sess->type != SESS_CHANNEL)
	{
		if (sess && sess->type == SESS_DIALOG && !g_ascii_strncasecmp (sess->channel, prefix, plen))
			add (out, sess->channel, NULL, NULL, sess->channel, NULL, NULL);
		return;
	}
	users = userlist_double_list (sess);
	for (l = users; l; l = l->next)
	{
		struct User *u = l->data;
		char mode[2] = { u->prefix[0], 0 };
		if (u->me || g_ascii_strncasecmp (u->nick, prefix, plen))
			continue;
		add (out, u->nick, mode, u->away ? _("away") : NULL, u->nick, NULL, NULL);
	}
	g_list_free (users);
	/* most recently talking first */
	if (out->len > first)
	{
		session *was = current_sess;
		current_sess = sess;
		qsort (out->pdata + first, out->len - first, sizeof (gpointer),
				 (int (*) (const void *, const void *)) by_lasttalk);
		current_sess = was;
	}
}

static void
add_channels (GPtrArray *out, session *sess, const char *prefix)
{
	gsize plen = strlen (prefix);
	GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GSList *l;

	for (l = sess_list; l; l = l->next)
	{
		session *s = l->data;
		if (s->type != SESS_CHANNEL || !s->channel[0] || !sess || s->server != sess->server ||
			 g_ascii_strncasecmp (s->channel, prefix, plen))
			continue;
		g_hash_table_add (seen, g_ascii_strdown (s->channel, -1));
		add (out, s->channel, NULL, s->topic ? s->topic : "", s->channel, NULL, NULL);
	}
	if (sess && sess->server->network)
	{
		ircnet *net = sess->server->network;
		for (l = net->favchanlist; l; l = l->next)
		{
			favchannel *f = l->data;
			char *k = g_ascii_strdown (f->name, -1);
			if (g_ascii_strncasecmp (f->name, prefix, plen) || g_hash_table_contains (seen, k))
			{
				g_free (k);
				continue;
			}
			g_hash_table_add (seen, k);
			add (out, f->name, NULL, _("favorite"), f->name, NULL, NULL);
		}
	}
	g_hash_table_destroy (seen);
}

static void
add_settings (GPtrArray *out, const char *prefix)
{
	GPtrArray *names = cmdinfo_settings (prefix);
	guint i;
	for (i = 0; i < names->len && out->len < MAX_ITEMS; i++)
		add (out, g_ptr_array_index (names, i), NULL, NULL, g_ptr_array_index (names, i), NULL, NULL);
	g_ptr_array_unref (names);
}

static void
add_networks (GPtrArray *out, const char *prefix)
{
	gsize plen = strlen (prefix);
	GSList *l;
	for (l = network_list; l; l = l->next)
	{
		ircnet *net = l->data;
		if (!g_ascii_strncasecmp (net->name, prefix, plen))
			add (out, net->name, NULL, NULL, net->name, NULL, NULL);
	}
}

static void
add_commands (GPtrArray *out, const char *prefix)
{
	GPtrArray *cmds = cmdinfo_find (prefix);
	guint i;
	for (i = 0; i < cmds->len && out->len < MAX_ITEMS; i++)
	{
		cmdinfo *c = g_ptr_array_index (cmds, i);
		char *lower = g_ascii_strdown (c->name, -1);
		add (out, c->name, NULL, c->desc, lower, NULL, NULL);
		g_free (lower);
	}
	cmdinfo_list_free (cmds);
}

/* cmdpopup.c add_options: a command's flags and choices */
static void
add_options (GPtrArray *out, cmdinfo *c, const char *prefix)
{
	gsize plen = strlen (prefix);
	if (c->doc && c->doc->opts)
	{
		const cmddoc_opt *o;
		for (o = c->doc->opts; o->flag; o++)
			if (!g_ascii_strncasecmp (o->flag, prefix, plen))
				add (out, o->flag, NULL, _(o->what), o->flag, NULL, NULL);
	}
}

nx_cmd_list *
nx_core_cmd_suggest (nx_key buf, const char *text, int cursor)
{
	session *sess = (session *) buf;
	nx_cmd_list *l;
	GPtrArray *items;
	const char *sp;
	char *cmd;

	if (!prefs.hex_gui_input_cmdhelp || !text || !is_cmd_char (text[0]) || is_cmd_char (text[1]))
		return NULL;
	if (sess && !is_session (sess))
		sess = NULL;
	sp = strchr (text, ' ');
	/* editing in the middle of a bare word: leave it be */
	if (!sp && cursor < (int) strlen (text))
		return NULL;
	cmd = sp ? g_strndup (text + 1, sp - text - 1) : g_strdup (text + 1);

	l = g_new0 (nx_cmd_list, 1);
	l->arg_start = l->arg_end = -1;
	items = g_ptr_array_new ();
	if (!sp)
	{
		add_command_rows (items, cmd);
		l->usage = g_strdup ("");
		l->desc = g_strdup ("");
		l->example = g_strdup ("");
	}
	else
	{
		cmdinfo *c = cmdinfo_lookup (cmd);
		char *word = NULL;
		int start = 0, n = current_arg (text, cursor, &start, &word);
		l->args = 1;
		l->word_start = start;
		if (c && n > 0)
		{
			if (word[0] == '-' || n == 1 || cmdinfo_arg_is_choice (c, n))
				add_options (items, c, word);
			if (word[0] != '-')
				switch (cmdinfo_arg_kind (c, n))
				{
				case ARG_NICK: add_nicks (items, sess, word); break;
				case ARG_CHANNEL: add_channels (items, sess, word); break;
				case ARG_SETTING: add_settings (items, word); break;
				case ARG_NETWORK: add_networks (items, word); break;
				case ARG_COMMAND: add_commands (items, word); break;
				default: break;
				}
			/* the whole value typed already: nothing to offer */
			if (items->len == 1 && !g_ascii_strcasecmp (((nx_cmd_item *) items->pdata[0])->insert, word))
			{
				item_free (items->pdata[0]);
				g_ptr_array_set_size (items, 0);
			}
		}
		if (c)
		{
			const char *u = c->usage, *after = u;
			int s, e;
			if (!g_ascii_strncasecmp (u, c->name, strlen (c->name)))
				after = u + strlen (c->name);
			l->usage = g_strdup_printf ("%c%s%s", nx_core_cmd_char (), c->name, after);
			if (n > 0 && cmdinfo_arg_span (c, n, &s, &e) && s >= after - u)
			{
				/* positions in l->usage: the char, the name, then `after` */
				int off = 1 + (int) strlen (c->name) - (int) (after - u);
				l->arg_start = s + off;
				l->arg_end = e + off;
			}
			l->desc = g_strdup (c->doc ? _(c->doc->summary) : c->desc);
			l->example = g_strdup (c->doc && c->doc->example ? c->doc->example : "");
			cmdinfo_free (c);
		}
		else
		{
			l->usage = g_strdup ("");
			l->desc = g_strdup ("");
			l->example = g_strdup ("");
		}
		g_free (word);
	}
	g_free (cmd);

	l->n = (int) items->len;
	l->items = g_new0 (nx_cmd_item, items->len ? items->len : 1);
	{
		guint i;
		for (i = 0; i < items->len; i++)
		{
			l->items[i] = *(nx_cmd_item *) items->pdata[i];
			g_free (items->pdata[i]);	/* the strings moved into l */
		}
	}
	g_ptr_array_free (items, TRUE);
	if (!l->n && !l->usage[0])
	{
		nx_cmd_list_free (l);
		return NULL;
	}
	return l;
}

void
nx_cmd_list_free (nx_cmd_list *l)
{
	int i;
	if (!l)
		return;
	for (i = 0; i < l->n; i++)
	{
		g_free (l->items[i].name);
		g_free (l->items[i].middle);
		g_free (l->items[i].desc);
		g_free (l->items[i].insert);
		g_free (l->items[i].heading);
		g_free (l->items[i].cmd);
	}
	g_free (l->items);
	g_free (l->usage);
	g_free (l->desc);
	g_free (l->example);
	g_free (l);
}

char *
nx_core_cmd_details (const char *name)
{
	cmdinfo *c = name && *name ? cmdinfo_lookup (name) : NULL;
	GString *s;
	if (!c)
		return NULL;
	s = g_string_new (NULL);
	if (c->doc)
	{
		if (c->doc->more)
			g_string_append (s, _(c->doc->more));
		if (c->doc->example)
			g_string_append_printf (s, "%sExample: %s", s->len ? "  \xc2\xb7  " : "", c->doc->example);
	}
	else if (*c->desc)
		g_string_append (s, c->desc);
	cmdinfo_free (c);
	return g_string_free (s, FALSE);
}
