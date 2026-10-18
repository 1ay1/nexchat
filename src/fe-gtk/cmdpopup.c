/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Typing "/" in the input box opens a list right on top of the input row:
   the commands that match, each with what to type after it and what it
   does. Once the command is typed, the same list offers values for the
   argument the cursor is on: people for <nick>, your channels for
   <channel>, setting names for /set, networks for /server, commands for
   /help; above it a line with the command's usage, the current argument in
   bold. Up and Down pick, Tab or Enter fill in, Escape closes. Built-ins,
   user commands and plugin commands come from common/cmdinfo.c. Classic
   look: a plain bordered list. gui_input_cmdhelp turns it off. */

#include <string.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cmdinfo.h"
#include "../common/userlist.h"
#include "../common/servlist.h"
#include "gtkcompat.h"
#include "gtkutil.h"
#include "cmdpopup.h"
#include "../common/cmddocs.h"

/* rows shown at once; the rest scroll. Enough for every command */
#define MAX_ROWS 400
#define VISIBLE_ROWS 9

/* one row: a command, or a value for an argument */
typedef struct
{
	char *name;		/* shown in bold: "/JOIN", "alice", "#nexchat" */
	char *middle;	/* second column: a command's arguments, a person's mode */
	char *desc;		/* dim, last: what a command does, a setting's value... */
	char *tip;		/* tooltip */
	char *insert;	/* what replaces the word being typed */
	char *heading;	/* a group heading drawn above this row ("Channels"), or NULL */
	char *cmd;		/* the command a row is about, for the details below */
} item;

typedef struct
{
	GtkWidget *entry;
	GtkWidget *host;		/* the GtkOverlay it's drawn in */
	GtkWidget *popover;	/* the frame holding it all (an overlay child) */
	GtkWidget *list;		/* GtkListBox */
	GtkWidget *hint;		/* the usage line while typing arguments */
	GtkWidget *scroll;		/* the list's scrolled window */
	GtkWidget *details;	/* what the selected command does, an example, its options */
	GtkWidget *keys;		/* the keys line at the bottom */
	gboolean want_open;	/* we showed it and haven't closed it */
	char *query;			/* what the list was built for (see popup_update) */
	int last_width, last_x, last_bottom;	/* where the input row was when last placed */
	GPtrArray *items;		/* item *, in list order */
	gboolean args;			/* the items are values for an argument */
	int word_start;		/* byte offset in the text of the word they replace */
	gboolean suppress;	/* Escape: stay closed for this command */
	char *suppressed_for;	/* ...the command it was closed on */
} cmdpopup;

static void
item_free (item *it)
{
	g_free (it->name);
	g_free (it->middle);
	g_free (it->desc);
	g_free (it->tip);
	g_free (it->insert);
	g_free (it->heading);
	g_free (it->cmd);
	g_free (it);
}

static item *
item_new (const char *name, const char *middle, const char *desc, const char *tip, const char *insert)
{
	item *it = g_new0 (item, 1);

	it->name = g_strdup (name);
	it->middle = g_strdup (middle ? middle : "");
	it->desc = g_strdup (desc ? desc : "");
	it->tip = g_strdup (tip ? tip : "");
	it->insert = g_strdup (insert);
	return it;
}

static gboolean
is_cmd_char (char c)
{
	return c && c == prefs.hex_input_command_char[0];
}

/* "/word ..." -> the command word, else NULL; has_space when the command
   is typed and arguments follow */
static char *
typed_command (const char *text, int cursor, gboolean *has_space)
{
	const char *sp;

	*has_space = FALSE;
	if (!is_cmd_char (text[0]) || is_cmd_char (text[1]))	/* "//" sends a plain "/" */
		return NULL;
	sp = strchr (text, ' ');
	if (sp)
	{
		*has_space = TRUE;
		return g_strndup (text + 1, sp - text - 1);
	}
	if (cursor < g_utf8_strlen (text, -1))
		return NULL;	/* editing in the middle of a bare word: leave it be */
	return g_strdup (text + 1);
}

/* with the cursor after the command: which argument it's on (1 = first)
   and the word typed so far, from *start (byte offset) to the cursor */
static int
current_arg (const char *text, int cursor_chars, int *start, char **word)
{
	const char *cur = g_utf8_offset_to_pointer (text, cursor_chars);
	const char *p, *ws;
	int n = 0;

	p = strchr (text, ' ');
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
	*start = ws - text;
	*word = g_strndup (ws, cur - ws);
	return n;
}

/* ---- the values an argument can take ---- */

static void
add_nicks (GPtrArray *out, const char *prefix)
{
	GList *users, *l;
	gsize plen = strlen (prefix);

	if (!current_sess || current_sess->type != SESS_CHANNEL)
	{
		/* a query: the other person */
		if (current_sess && current_sess->type == SESS_DIALOG &&
			 !g_ascii_strncasecmp (current_sess->channel, prefix, plen))
			g_ptr_array_add (out, item_new (current_sess->channel, NULL, NULL, NULL, current_sess->channel));
		return;
	}
	/* most recently talking first, then the rest by name (as Tab completes) */
	users = userlist_double_list (current_sess);
	for (l = users; l; l = l->next)
	{
		struct User *u = l->data;
		char mode[2] = { u->prefix[0], 0 };
		if (u->me || g_ascii_strncasecmp (u->nick, prefix, plen))
			continue;
		g_ptr_array_add (out, item_new (u->nick, mode, u->away ? _("away") : NULL,
												  u->hostname, u->nick));
	}
	g_list_free (users);
}

static int
by_lasttalk (gconstpointer a, gconstpointer b)
{
	const item *x = *(item **) a, *y = *(item **) b;
	struct User *ux = current_sess ? userlist_find (current_sess, x->name) : NULL;
	struct User *uy = current_sess ? userlist_find (current_sess, y->name) : NULL;
	time_t tx = ux ? ux->lasttalk : 0, ty = uy ? uy->lasttalk : 0;

	if (tx != ty)
		return tx > ty ? -1 : 1;
	return g_ascii_strcasecmp (x->name, y->name);
}

static void
add_channels (GPtrArray *out, const char *prefix)
{
	GSList *l;
	gsize plen = strlen (prefix);
	GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* open channels on this network, then its saved favourites */
	for (l = sess_list; l; l = l->next)
	{
		session *s = l->data;
		if (s->type != SESS_CHANNEL || !s->channel[0] || !current_sess ||
			 s->server != current_sess->server || g_ascii_strncasecmp (s->channel, prefix, plen))
			continue;
		g_hash_table_add (seen, g_ascii_strdown (s->channel, -1));
		g_ptr_array_add (out, item_new (s->channel, NULL, s->topic ? s->topic : "", s->topic, s->channel));
	}
	if (current_sess && current_sess->server->network)
	{
		ircnet *net = current_sess->server->network;
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
			g_ptr_array_add (out, item_new (f->name, NULL, _("favorite"), NULL, f->name));
		}
	}
	g_hash_table_destroy (seen);
}

static void
add_settings (GPtrArray *out, const char *prefix)
{
	GPtrArray *names = cmdinfo_settings (prefix);
	guint i;

	for (i = 0; i < names->len; i++)
		g_ptr_array_add (out, item_new (g_ptr_array_index (names, i), NULL, NULL, NULL,
												  g_ptr_array_index (names, i)));
	g_ptr_array_unref (names);
}

static void
add_networks (GPtrArray *out, const char *prefix)
{
	GSList *l;
	gsize plen = strlen (prefix);

	for (l = network_list; l; l = l->next)
	{
		ircnet *net = l->data;
		if (!g_ascii_strncasecmp (net->name, prefix, plen))
			g_ptr_array_add (out, item_new (net->name, NULL, NULL, NULL, net->name));
	}
}

static void
add_commands (GPtrArray *out, const char *prefix)
{
	GPtrArray *cmds = cmdinfo_find (prefix);
	guint i;

	for (i = 0; i < cmds->len; i++)
	{
		cmdinfo *c = g_ptr_array_index (cmds, i);
		char *lower = g_ascii_strdown (c->name, -1);
		g_ptr_array_add (out, item_new (c->name, NULL, c->desc, c->help, lower));
		g_free (lower);
	}
	cmdinfo_list_free (cmds);
}

/* ---- the list ---- */

/* name | middle | desc; the first two columns line up down the list */
static GtkWidget *
row_new (item *it, GtkSizeGroup *names, GtkSizeGroup *middles)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	GtkWidget *name, *middle, *desc;
	char *m;

	m = g_markup_printf_escaped ("<b>%s</b>", it->name);
	name = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (name), m);
	g_free (m);
	gtk_label_set_xalign (GTK_LABEL (name), 0);
	gtk_size_group_add_widget (names, name);
	gtk_box_append (GTK_BOX (box), name);

	middle = gtk_label_new (it->middle);
	gtk_label_set_xalign (GTK_LABEL (middle), 0);
	gtk_label_set_ellipsize (GTK_LABEL (middle), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (middle), 30);
	gtk_widget_add_css_class (middle, "nx-cmd-usage");
	gtk_size_group_add_widget (middles, middle);
	gtk_box_append (GTK_BOX (box), middle);

	desc = gtk_label_new (it->desc);
	gtk_label_set_xalign (GTK_LABEL (desc), 0);
	gtk_label_set_ellipsize (GTK_LABEL (desc), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars (GTK_LABEL (desc), 1);	/* takes what's left, "..." */
	gtk_widget_set_hexpand (desc, TRUE);
	gtk_widget_add_css_class (desc, "nx-cmd-desc");
	gtk_widget_add_css_class (desc, "dim-label");
	gtk_box_append (GTK_BOX (box), desc);

	if (*it->tip)
		gtk_widget_set_tooltip_text (box, it->tip);
	return box;
}

static void
popup_hide (cmdpopup *p)
{
	p->want_open = FALSE;
	/* when it opens again it's rebuilt for whatever is typed then */
	if (!p->suppress)
		g_clear_pointer (&p->query, g_free);
	if (p->popover && gtk_widget_get_visible (p->popover))
		gtk_widget_set_visible (p->popover, FALSE);
}

/* it sits right on top of the box that holds the composer (nick button +
   entry), exactly as wide as it: an overlay child placed by its margins,
   bottom-aligned on the row's top edge. It's part of the window, so
   nothing (focus, keys, a resize) can make it close on its own. */
static void
popup_show (cmdpopup *p)
{
	graphene_rect_t rb;
	GtkWidget *row = gtk_widget_get_parent (p->entry);	/* nick box + entry */
	int host_h = gtk_widget_get_height (p->host);
	int x, w, bottom;

	if (!row || !gtk_widget_compute_bounds (row, p->host, &rb))
	{
		if (!gtk_widget_compute_bounds (p->entry, p->host, &rb))
			return;
	}
	x = (int) rb.origin.x;
	w = MAX ((int) rb.size.width, 1);
	bottom = host_h - (int) rb.origin.y;	/* from the overlay's bottom to the row's top */
	p->last_width = w;
	p->last_x = x;
	p->last_bottom = bottom;

	gtk_widget_set_margin_start (p->popover, x);
	gtk_widget_set_margin_bottom (p->popover, MAX (bottom, 0));
	gtk_widget_set_size_request (p->popover, w, -1);
	p->want_open = TRUE;
	if (!gtk_widget_get_visible (p->popover))
		gtk_widget_set_visible (p->popover, TRUE);
}

/* group headings above the rows that start a group (GtkListBox header) */
static void
header_func (GtkListBoxRow *row, GtkListBoxRow *before, gpointer data)
{
	cmdpopup *p = data;
	int i = gtk_list_box_row_get_index (row);
	item *it = p->items && i >= 0 && i < (int) p->items->len ? g_ptr_array_index (p->items, i) : NULL;
	GtkWidget *h;

	if (!it || !it->heading)
	{
		gtk_list_box_row_set_header (row, NULL);
		return;
	}
	h = gtk_label_new (it->heading);
	gtk_label_set_xalign (GTK_LABEL (h), 0);
	gtk_widget_add_css_class (h, "nx-cmd-group");
	gtk_list_box_row_set_header (row, h);
}

/* the list is at most VISIBLE_ROWS rows tall; more scroll */
static void
size_list (cmdpopup *p, guint n)
{
	GtkListBoxRow *r0 = gtk_list_box_get_row_at_index (GTK_LIST_BOX (p->list), 0);
	int row_h = 0;

	if (r0)
		gtk_widget_measure (GTK_WIDGET (r0), GTK_ORIENTATION_VERTICAL, -1, NULL, &row_h, NULL, NULL);
	if (row_h <= 0)
		row_h = 22;
	if (n > VISIBLE_ROWS)
	{
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (p->scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (p->scroll), FALSE);
		/* a little over N rows, so it's clear there's more below */
		gtk_widget_set_size_request (p->scroll, -1, row_h * VISIBLE_ROWS + row_h / 2);
	}
	else
	{
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (p->scroll), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
		gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (p->scroll), TRUE);
		gtk_widget_set_size_request (p->scroll, -1, -1);
	}
}

/* show these items (taken over), the first selected */
static void
popup_set_items (cmdpopup *p, GPtrArray *items)
{
	GtkWidget *child;
	GtkSizeGroup *names = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	GtkSizeGroup *middles = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	guint i, n;

	while ((child = gtk_widget_get_first_child (p->list)))
		gtk_list_box_remove (GTK_LIST_BOX (p->list), child);
	if (p->items)
		g_ptr_array_unref (p->items);
	p->items = items;
	n = MIN (items->len, MAX_ROWS);
	for (i = 0; i < n; i++)
	{
		GtkWidget *r;
		gtk_list_box_append (GTK_LIST_BOX (p->list), row_new (g_ptr_array_index (items, i), names, middles));
		/* the keys stay in the input box: rows never take the focus */
		r = GTK_WIDGET (gtk_list_box_get_row_at_index (GTK_LIST_BOX (p->list), i));
		gtk_widget_set_focusable (r, FALSE);
	}
	g_object_unref (names);	/* the rows keep the groups alive */
	g_object_unref (middles);
	gtk_list_box_invalidate_headers (GTK_LIST_BOX (p->list));
	size_list (p, n);
	gtk_widget_set_visible (p->scroll, n > 0);
	if (n)
	{
		gtk_list_box_select_row (GTK_LIST_BOX (p->list),
										 gtk_list_box_get_row_at_index (GTK_LIST_BOX (p->list), 0));
		gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (p->scroll)), 0);
	}
	else if (!p->args)
		gtk_widget_set_visible (p->details, FALSE);
}

/* the commands matching word; with nothing typed, the everyday ones first
   under a heading, then the rest in their groups */
static GPtrArray *
command_items (const char *word)
{
	GPtrArray *cmds = cmdinfo_find (word);
	GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) item_free);
	const char *last_group = NULL;
	guint i;

	for (i = 0; i < cmds->len; i++)
	{
		cmdinfo *c = g_ptr_array_index (cmds, i);
		const char *args = c->usage;
		char *name = g_strdup_printf ("/%s", c->name);
		item *it;

		/* the usage repeats the name: show just what comes after it */
		if (!g_ascii_strncasecmp (args, c->name, strlen (c->name)))
			args += strlen (c->name);
		while (*args == ' ')
			args++;
		it = item_new (name, args,
			c->kind == CMD_PLUGIN && !*c->desc ? _("(plugin)") : c->desc, c->help, c->name);
		it->cmd = g_strdup (c->name);
		if (!*word)
		{
			/* headings: \"Most used\", then each group, then \"More\" */
			const char *group = c->doc && c->doc->common ? _("Most used")
				: c->doc ? _(c->doc->group) : c->kind == CMD_PLUGIN ? _("Plugins")
				: c->kind == CMD_USER ? _("Your commands") : _("More");
			if (!last_group || strcmp (group, last_group))
				it->heading = g_strdup (group);
			last_group = group;
		}
		g_ptr_array_add (out, it);
		g_free (name);
	}
	cmdinfo_list_free (cmds);
	return out;
}

/* the options a command takes, from its docs or its usage ([ALL|HISTORY],
   [-m]), as suggestions when the word being typed could be one */
static void
add_options (GPtrArray *out, cmdinfo *c, const char *prefix)
{
	gsize plen = strlen (prefix);
	const cmddoc_opt *o;

	if (c->doc && c->doc->opts)
	{
		for (o = c->doc->opts; o->flag; o++)
			if (!g_ascii_strncasecmp (o->flag, prefix, plen))
				g_ptr_array_add (out, item_new (o->flag, NULL, _(o->what), NULL, o->flag));
		return;
	}
	/* no docs: the words in [...] or <A|B|C> of the usage that look like
	   options or choices */
	{
		const char *u = c->usage;
		while ((u = strpbrk (u, "[<")))
		{
			const char *e = strchr (u, *u == '[' ? ']' : '>');
			char *inner, **alts;
			int k;
			if (!e)
				break;
			inner = g_strndup (u + 1, e - u - 1);
			alts = g_strsplit (inner, "|", 0);
			for (k = 0; alts[k]; k++)
			{
				const char *a = alts[k];
				/* -flag or a word in capitals (ALL, HISTORY), not <placeholders> */
				if ((a[0] == '-' && g_ascii_isalpha (a[1])) ||
					 (g_ascii_isupper (a[0]) && !strchr (a, '<') && !strchr (a, ' ')))
					if (!g_ascii_strncasecmp (a, prefix, plen))
						g_ptr_array_add (out, item_new (a, NULL, NULL, NULL, a));
			}
			g_strfreev (alts);
			g_free (inner);
			u = e + 1;
		}
	}
}

/* the selected command explained below the list: what it does, an
   example, and its options */
static void
show_details (cmdpopup *p, const char *cmd)
{
	cmdinfo *c = cmd ? cmdinfo_lookup (cmd) : NULL;
	GString *m;
	char *t;

	if (!c)
	{
		gtk_widget_set_visible (p->details, FALSE);
		return;
	}
	m = g_string_new (NULL);
	if (c->doc)
	{
		t = g_markup_printf_escaped ("<b>/%s</b> %s  %s", c->name, c->doc->args, _(c->doc->summary));
		g_string_append (m, t);
		g_free (t);
		if (c->doc->more)
		{
			t = g_markup_printf_escaped ("\n%s", _(c->doc->more));
			g_string_append (m, t);
			g_free (t);
		}
		if (c->doc->example)
		{
			t = g_markup_printf_escaped ("\n%s  <tt>%s</tt>", _("Example:"), c->doc->example);
			g_string_append (m, t);
			g_free (t);
		}
		if (c->doc->opts)
		{
			const cmddoc_opt *o;
			g_string_append_printf (m, "\n%s", _("Options:"));
			for (o = c->doc->opts; o->flag; o++)
			{
				t = g_markup_printf_escaped ("  <tt>%s</tt> %s", o->flag, _(o->what));
				g_string_append (m, t);
				g_free (t);
				if (o[1].flag)
					g_string_append (m, ",");
			}
		}
	}
	else
	{
		/* only the terse built-in help: show it whole */
		t = g_markup_printf_escaped ("<b>/%s</b>  %s", c->name, *c->help ? c->help : c->desc);
		g_string_append (m, t);
		g_free (t);
	}
	gtk_label_set_markup (GTK_LABEL (p->details), m->str);
	gtk_widget_set_visible (p->details, TRUE);
	g_string_free (m, TRUE);
	cmdinfo_free (c);
}

/* the selected row changed */
static void
row_selected_cb (GtkListBox *box, GtkListBoxRow *row, cmdpopup *p)
{
	item *it;

	if (!row || !p->items || p->args)
		return;
	it = g_ptr_array_index (p->items, gtk_list_box_row_get_index (row));
	show_details (p, it->cmd);
}

/* the usage line, argument n in bold */
static void
set_hint (cmdpopup *p, cmdinfo *c, int n)
{
	GString *m = g_string_new (NULL);
	const char *u = c->usage;
	int s, e;
	char *t;

	t = g_markup_printf_escaped ("<b>/%s</b>", c->name);
	g_string_append (m, t);
	g_free (t);
	if (*u)
	{
		const char *after = u;
		if (!g_ascii_strncasecmp (u, c->name, strlen (c->name)))
			after = u + strlen (c->name);
		if (n > 0 && cmdinfo_arg_span (c, n, &s, &e) && s >= after - u)
		{
			char *a = g_markup_escape_text (after, s - (after - u));
			char *b = g_markup_escape_text (u + s, e - s);
			char *z = g_markup_escape_text (u + e, -1);
			g_string_append_printf (m, "%s<u><b>%s</b></u>%s", a, b, z);
			g_free (a); g_free (b); g_free (z);
		}
		else
		{
			char *a = g_markup_escape_text (after, -1);
			g_string_append (m, a);
			g_free (a);
		}
	}
	if (*c->desc)
	{
		t = g_markup_printf_escaped ("  <span alpha=\"60%%\">%s</span>", c->desc);
		g_string_append (m, t);
		g_free (t);
	}
	gtk_label_set_markup (GTK_LABEL (p->hint), m->str);
	g_string_free (m, TRUE);
}

/* the text or cursor changed: decide what to show. The list is only rebuilt
   when what it's for changes (the command typed, the argument and its word);
   a cursor move that leaves those alone keeps the list, its selection and
   where it's scrolled to. */
static void
popup_update (cmdpopup *p)
{
	const char *text = gtk_editable_get_text (GTK_EDITABLE (p->entry));
	int cursor = gtk_editable_get_position (GTK_EDITABLE (p->entry));
	gboolean has_space;
	char *cmd, *key;

	if (!prefs.hex_gui_input_cmdhelp || !(cmd = typed_command (text, cursor, &has_space)))
	{
		p->suppress = FALSE;
		g_clear_pointer (&p->query, g_free);
		popup_hide (p);
		return;
	}
	/* what this state shows: command name, or command + argument + word */
	if (!has_space)
		key = g_strdup_printf ("c:%s", cmd);
	else
	{
		char *w = NULL;
		int st = 0, n = current_arg (text, cursor, &st, &w);
		key = g_strdup_printf ("a:%s:%d:%d:%s", cmd, n, st, w ? w : "");
		g_free (w);
	}
	if (p->query && !strcmp (p->query, key) && (p->want_open || p->suppress))
	{
		/* nothing it depends on changed */
		g_free (key);
		g_free (cmd);
		return;
	}
	g_free (p->query);
	p->query = key;
	/* Escape closed it: stay closed while the same command is being typed
	   or given its arguments; a different command opens it again */
	if (p->suppress)
	{
		const char *was = p->suppressed_for ? p->suppressed_for : "";
		if ((has_space && !g_ascii_strcasecmp (cmd, was)) || (!has_space && g_str_has_prefix (cmd, was)))
		{
			g_free (cmd);
			return;
		}
		p->suppress = FALSE;
	}

	if (!has_space)
	{
		/* the command name: the commands that match */
		GPtrArray *items = command_items (cmd);
		p->args = FALSE;
		p->word_start = 1;
		gtk_widget_set_visible (p->hint, FALSE);
		popup_set_items (p, items);
		if (items->len)
		{
			show_details (p, ((item *) items->pdata[0])->cmd);
			gtk_label_set_text (GTK_LABEL (p->keys),
				_("↑↓ choose   Tab fill in   Enter run   Esc close"));
			popup_show (p);
		}
		else
			popup_hide (p);
	}
	else
	{
		/* its arguments: the usage line, and values for the one under the cursor */
		cmdinfo *c = cmdinfo_lookup (cmd);
		char *word = NULL;
		int start = 0, n = current_arg (text, cursor, &start, &word);
		GPtrArray *items = g_ptr_array_new_with_free_func ((GDestroyNotify) item_free);

		p->args = TRUE;
		p->word_start = start;
		if (c && n > 0)
		{
			/* options and choices (-a, ALL, PRIV) are offered where the word
			   could be one: it starts with -, or this argument takes a choice */
			if (word[0] == '-' || n == 1 || cmdinfo_arg_is_choice (c, n))
				add_options (items, c, word);
			if (word[0] != '-')
			switch (cmdinfo_arg_kind (c, n))
			{
			case ARG_NICK:
				add_nicks (items, word);
				g_ptr_array_sort (items, by_lasttalk);
				break;
			case ARG_CHANNEL:
				add_channels (items, word);
				break;
			case ARG_SETTING:
				add_settings (items, word);
				break;
			case ARG_NETWORK:
				add_networks (items, word);
				break;
			case ARG_COMMAND:
				add_commands (items, word);
				break;
			default:
				break;
			}
			/* the whole value typed already: nothing to offer */
			if (items->len == 1 && !g_ascii_strcasecmp (((item *) items->pdata[0])->insert, word))
				g_ptr_array_set_size (items, 0);
		}
		if (c && (*c->usage || *c->desc))
		{
			set_hint (p, c, n);
			gtk_widget_set_visible (p->hint, TRUE);
		}
		else
			gtk_widget_set_visible (p->hint, FALSE);
		popup_set_items (p, items);
		/* the example, so it's clear what goes where */
		if (c && c->doc && c->doc->example)
		{
			char *m = g_markup_printf_escaped ("%s  <tt>%s</tt>", _("Example:"), c->doc->example);
			gtk_label_set_markup (GTK_LABEL (p->details), m);
			gtk_widget_set_visible (p->details, TRUE);
			g_free (m);
		}
		else
			gtk_widget_set_visible (p->details, FALSE);
		gtk_label_set_text (GTK_LABEL (p->keys), items->len
			? _("↑↓ choose   Tab fill in   Enter send   Esc close")
			: _("Enter send   Esc close"));
		if (gtk_widget_get_visible (p->hint) || items->len)
			popup_show (p);
		else
			popup_hide (p);
		cmdinfo_free (c);
		g_free (word);
	}
	g_free (cmd);
}

static void
popup_changed_cb (GtkEditable *e, cmdpopup *p)
{
	popup_update (p);
}

/* while it's up, keep it on the input row whatever moves it (window
   resized, user list shown/hidden, a pane dragged); a frame clock tick
   costs nothing when nothing changed */
static gboolean
refit_tick (GtkWidget *w, GdkFrameClock *clock, gpointer data)
{
	cmdpopup *p = data;
	graphene_rect_t b;
	GtkWidget *row = gtk_widget_get_parent (p->entry);

	if (row && gtk_widget_compute_bounds (row, p->host, &b) &&
		 ((int) b.size.width != p->last_width || (int) b.origin.x != p->last_x ||
		  gtk_widget_get_height (p->host) - (int) b.origin.y != p->last_bottom))
		popup_show (p);
	return G_SOURCE_CONTINUE;
}

/* put it in the box: a command as "/join " (lower case if that's how it
   was typed), a value in place of the word being typed, then a space */
static void
popup_complete (cmdpopup *p, item *it)
{
	/* a copy: setting the text frees the entry's own buffer */
	char *text = g_strdup (gtk_editable_get_text (GTK_EDITABLE (p->entry)));
	int cursor = gtk_editable_get_position (GTK_EDITABLE (p->entry));
	const char *cur = g_utf8_offset_to_pointer (text, cursor);
	char *t, *ins;

	if (!p->args)
	{
		gboolean lower = text[1] && g_ascii_islower (text[1]);
		ins = lower ? g_ascii_strdown (it->insert, -1) : g_strdup (it->insert);
		t = g_strdup_printf ("%c%s %s", prefs.hex_input_command_char[0], ins,
									strchr (text, ' ') ? strchr (text, ' ') + 1 : "");
		gtk_editable_set_text (GTK_EDITABLE (p->entry), t);
		gtk_editable_set_position (GTK_EDITABLE (p->entry), g_utf8_strlen (ins, -1) + 2);
	}
	else
	{
		/* keep what's after the cursor; add a space unless one follows. The
		   item goes away when setting the text rebuilds the list: copy it */
		gboolean space_after = *cur == ' ';
		char *before = g_strndup (text, p->word_start);
		int pos;
		ins = g_strdup (it->insert);
		pos = g_utf8_strlen (before, -1) + g_utf8_strlen (ins, -1) + 1;
		t = g_strdup_printf ("%s%s%s%s", before, ins, space_after ? "" : " ", cur);
		gtk_editable_set_text (GTK_EDITABLE (p->entry), t);
		gtk_editable_set_position (GTK_EDITABLE (p->entry), pos);
		g_free (before);
	}
	g_free (t);
	g_free (ins);
	g_free (text);
	popup_update (p);
}

static item *
selected (cmdpopup *p)
{
	GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (p->list));

	if (!row || !p->items || !gtk_widget_get_visible (p->scroll))
		return NULL;
	return g_ptr_array_index (p->items, gtk_list_box_row_get_index (row));
}

static void
move (cmdpopup *p, int by)
{
	GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (p->list));
	int i = row ? gtk_list_box_row_get_index (row) : -1;
	int n = MIN ((int) p->items->len, MAX_ROWS);

	if (!n)
		return;
	if (by == 1 || by == -1)
		i = (i + by + n) % n;	/* single steps wrap around */
	else
		i = CLAMP (i + by, 0, n - 1);
	row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (p->list), i);
	gtk_list_box_select_row (GTK_LIST_BOX (p->list), row);
	if (!p->args)
		show_details (p, ((item *) g_ptr_array_index (p->items, i))->cmd);
	/* keep it in sight: scroll the list to the row (only when the list
	   scrolls at all; adjusting a list that fits does nothing useful) */
	if (gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (p->scroll)) &&
		 gtk_widget_get_visible (gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (p->scroll))))
	{
		graphene_rect_t b;
		GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (p->scroll));
		if (gtk_widget_compute_bounds (GTK_WIDGET (row), p->list, &b))
		{
			double top = gtk_adjustment_get_value (adj), page = gtk_adjustment_get_page_size (adj);
			if (b.origin.y < top)
				gtk_adjustment_set_value (adj, b.origin.y);
			else if (b.origin.y + b.size.height > top + page)
				gtk_adjustment_set_value (adj, b.origin.y + b.size.height - page);
		}
	}
}

/* keys while it's up; FALSE lets the entry (and fkeys.c) have them */
gboolean
cmdpopup_key (GtkWidget *entry, guint keyval, GdkModifierType state)
{
	cmdpopup *p = g_object_get_data (G_OBJECT (entry), "nx-cmdpopup");
	item *it;

	if (!p || !p->popover || !gtk_widget_get_visible (p->popover))
		return FALSE;
	if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK))
		return FALSE;
	it = selected (p);

	switch (keyval)
	{
	case GDK_KEY_Escape:
		p->suppress = TRUE;
		g_free (p->suppressed_for);
		{
			gboolean sp;
			p->suppressed_for = typed_command (gtk_editable_get_text (GTK_EDITABLE (entry)),
														  gtk_editable_get_position (GTK_EDITABLE (entry)), &sp);
		}
		popup_hide (p);
		return TRUE;
	case GDK_KEY_Up:
	case GDK_KEY_KP_Up:
	case GDK_KEY_Down:
	case GDK_KEY_KP_Down:
		if (!it)
			return FALSE;	/* just the hint: Up/Down stay history */
		move (p, keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up ? -1 : 1);
		return TRUE;
	case GDK_KEY_Page_Up:
	case GDK_KEY_Page_Down:
		if (!it)
			return FALSE;
		move (p, keyval == GDK_KEY_Page_Up ? -VISIBLE_ROWS : VISIBLE_ROWS);
		return TRUE;
	case GDK_KEY_Tab:
	case GDK_KEY_ISO_Left_Tab:
		if (!it)
			return FALSE;
		popup_complete (p, it);
		return TRUE;
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
		/* Enter sends the line as it is once it's whole: a full command name
		   (/away), or arguments being typed; on part of a command name it
		   fills in the pick first */
		if (!it || p->args)
		{
			popup_hide (p);
			return FALSE;
		}
		{
			gboolean sp;
			char *word = typed_command (gtk_editable_get_text (GTK_EDITABLE (entry)),
												 gtk_editable_get_position (GTK_EDITABLE (entry)), &sp);
			gboolean exact = word && !g_ascii_strcasecmp (word, it->insert);
			g_free (word);
			if (exact)
			{
				popup_hide (p);
				return FALSE;
			}
		}
		popup_complete (p, it);
		return TRUE;
	}
	return FALSE;
}

/* the input box (or its text inside) has the keyboard */
static gboolean
entry_focused (cmdpopup *p)
{
	GtkRoot *root = gtk_widget_get_root (p->entry);
	GtkWidget *f = root ? gtk_root_get_focus (root) : NULL;

	return f && (f == p->entry || gtk_widget_is_ancestor (f, p->entry));
}

static void
row_activated_cb (GtkListBox *box, GtkListBoxRow *row, cmdpopup *p)
{
	popup_complete (p, g_ptr_array_index (p->items, gtk_list_box_row_get_index (row)));
	gtk_widget_grab_focus (p->entry);
}

/* the input lost focus: close, unless it went to the list itself (a click
   on a row) or the change is only passing (a row being rebuilt) */
static gboolean
focus_out_idle (gpointer data)
{
	cmdpopup *p = data;

	if (!entry_focused (p))
		popup_hide (p);
	return G_SOURCE_REMOVE;
}

static void
focus_out_cb (GtkEventControllerFocus *f, cmdpopup *p)
{
	g_timeout_add (150, focus_out_idle, p);
}

static void
cmdpopup_free (cmdpopup *p)
{
	if (p->items)
		g_ptr_array_unref (p->items);
	g_free (p->suppressed_for);
	g_free (p->query);
	g_free (p);
}

void
cmdpopup_attach (GtkWidget *entry, GtkWidget *host)
{
	cmdpopup *p = g_new0 (cmdpopup, 1);
	GtkWidget *box, *sw;
	GtkEventController *f;

	p->entry = entry;
	p->host = host;
	/* a plain framed box laid over the chat, not a GtkPopover: a popup
	   surface closes on its own (keys, focus, resizes); this can't */
	box = p->popover = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class (box, "nx-cmdpopup");
	gtk_widget_set_halign (box, GTK_ALIGN_START);
	gtk_widget_set_valign (box, GTK_ALIGN_END);
	gtk_widget_set_focusable (box, FALSE);
	gtk_widget_set_can_target (box, TRUE);
	gtk_widget_set_visible (box, FALSE);
	gtk_overlay_add_overlay (GTK_OVERLAY (host), box);
	/* the chat underneath keeps its size: the list only covers it */
	gtk_overlay_set_measure_overlay (GTK_OVERLAY (host), box, FALSE);
	gtk_overlay_set_clip_overlay (GTK_OVERLAY (host), box, TRUE);

	/* the usage line on top, the list of values under it (nearest the box) */
	p->hint = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (p->hint), 0);
	gtk_label_set_ellipsize (GTK_LABEL (p->hint), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars (GTK_LABEL (p->hint), 1);
	gtk_widget_add_css_class (p->hint, "nx-cmd-hint");
	gtk_box_append (GTK_BOX (box), p->hint);

	p->list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (p->list), GTK_SELECTION_BROWSE);
	gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (p->list), TRUE);
	gtk_list_box_set_header_func (GTK_LIST_BOX (p->list), header_func, p, NULL);
	gtk_widget_set_can_focus (p->list, FALSE);
	g_signal_connect (p->list, "row-activated", G_CALLBACK (row_activated_cb), p);
	g_signal_connect (p->list, "row-selected", G_CALLBACK (row_selected_cb), p);
	p->scroll = sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), p->list);
	gtk_box_append (GTK_BOX (box), sw);

	/* the selected command explained: what it does, an example, options */
	p->details = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (p->details), 0);
	gtk_label_set_wrap (GTK_LABEL (p->details), TRUE);
	gtk_label_set_wrap_mode (GTK_LABEL (p->details), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_width_chars (GTK_LABEL (p->details), 1);
	/* always 3 lines tall: moving through the list doesn't resize the popup */
	gtk_label_set_lines (GTK_LABEL (p->details), 3);
	gtk_label_set_ellipsize (GTK_LABEL (p->details), PANGO_ELLIPSIZE_END);
	gtk_widget_add_css_class (p->details, "nx-cmd-details");
	gtk_box_append (GTK_BOX (box), p->details);

	/* which keys do what, always in sight */
	p->keys = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (p->keys), 0);
	gtk_label_set_ellipsize (GTK_LABEL (p->keys), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars (GTK_LABEL (p->keys), 1);
	gtk_widget_add_css_class (p->keys, "nx-cmd-keys");
	gtk_widget_add_css_class (p->keys, "dim-label");
	gtk_box_append (GTK_BOX (box), p->keys);

	g_signal_connect (entry, "changed", G_CALLBACK (popup_changed_cb), p);
	/* moving the cursor to another argument changes what's offered */
	g_signal_connect_swapped (entry, "notify::cursor-position", G_CALLBACK (popup_update), p);
	gtk_widget_add_tick_callback (p->popover, refit_tick, p, NULL);	/* only while mapped */
	f = gtk_event_controller_focus_new ();
	g_signal_connect (f, "leave", G_CALLBACK (focus_out_cb), p);
	gtk_widget_add_controller (entry, f);
	g_object_set_data_full (G_OBJECT (entry), "nx-cmdpopup", p, (GDestroyNotify) cmdpopup_free);
}

/* tests: where it sits next to the input row, in window coordinates:
   "pop x w bottom | row x w top" */
char *
cmdpopup_geometry (GtkWidget *entry)
{
	cmdpopup *p = g_object_get_data (G_OBJECT (entry), "nx-cmdpopup");
	GtkWidget *win, *row;
	graphene_rect_t pop, rb;

	if (!p || !p->popover || !gtk_widget_get_visible (p->popover))
		return g_strdup ("");
	win = GTK_WIDGET (gtk_widget_get_root (entry));
	row = gtk_widget_get_parent (entry);
	if (!gtk_widget_compute_bounds (row, win, &rb) ||
		 !gtk_widget_compute_bounds (p->popover, win, &pop))
		return g_strdup ("");
	return g_strdup_printf ("pop x=%d w=%d bottom=%d | row x=%d w=%d top=%d",
		(int) pop.origin.x, (int) pop.size.width, (int) (pop.origin.y + pop.size.height),
		(int) rb.origin.x, (int) rb.size.width, (int) rb.origin.y);
}

GtkWidget *
cmdpopup_widget (GtkWidget *entry)
{
	cmdpopup *p = g_object_get_data (G_OBJECT (entry), "nx-cmdpopup");
	return p ? p->popover : NULL;
}

/* tests: what it shows ("" when closed): "list: A [B] C" (the selected one
   in []) and/or "hint: text" */
char *
cmdpopup_describe (GtkWidget *entry)
{
	cmdpopup *p = g_object_get_data (G_OBJECT (entry), "nx-cmdpopup");
	GString *s;
	guint i, n;
	GtkListBoxRow *sel;

	if (!p || !p->popover || !gtk_widget_get_visible (p->popover))
		return g_strdup ("");
	s = g_string_new (NULL);
	if (gtk_widget_get_visible (p->scroll) && p->items)
	{
		g_string_append (s, "list:");
		sel = gtk_list_box_get_selected_row (GTK_LIST_BOX (p->list));
		n = g_getenv ("NX_DESCRIBE_ALL") ? p->items->len : MIN (p->items->len, 12);
		for (i = 0; i < n; i++)
		{
			item *it = g_ptr_array_index (p->items, i);
			gboolean is_sel = sel && gtk_list_box_row_get_index (sel) == (int) i;
			/* commands without their slash, as before */
			const char *name = !p->args && it->name[0] == '/' ? it->name + 1 : it->name;
			if (it->heading)
				g_string_append_printf (s, " {%s}", it->heading);
			g_string_append_printf (s, " %s%s%s", is_sel ? "[" : "", name, is_sel ? "]" : "");
		}
		if (p->items->len > n)
			g_string_append_printf (s, " +%u", p->items->len - n);
		if (sel)
		{
			/* the selected row, and whether it's inside the visible part */
			graphene_rect_t b;
			GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (p->scroll));
			int idx = gtk_list_box_row_get_index (sel);
			gboolean shown = FALSE;
			if (gtk_widget_compute_bounds (GTK_WIDGET (sel), p->list, &b))
				shown = b.origin.y >= gtk_adjustment_get_value (adj) - 1 &&
					b.origin.y + b.size.height <= gtk_adjustment_get_value (adj) + gtk_adjustment_get_page_size (adj) + 1;
			g_string_append_printf (s, " sel=%d:%s:%s", idx,
				((item *) g_ptr_array_index (p->items, idx))->name, shown ? "in-view" : "OUT-OF-VIEW");
		}
	}
	if (gtk_widget_get_visible (p->hint))
		g_string_append_printf (s, "%shint: %s", s->len ? " | " : "", gtk_label_get_text (GTK_LABEL (p->hint)));
	if (gtk_widget_get_visible (p->details))
	{
		/* one line for the test dump */
		char *d = g_strdup (gtk_label_get_text (GTK_LABEL (p->details)));
		g_strdelimit (d, "\n", ' ');
		g_string_append_printf (s, " | details: %s", d);
		g_free (d);
	}
	return g_string_free (s, FALSE);
}
