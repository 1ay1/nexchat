/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Typing "/" in the input box opens a list of commands above it: the name,
   what to type after it and what it does, narrowed with each letter. Up and
   Down pick, Tab or Enter fill it in, Escape closes. Once the command is
   typed (after the space) the list gives way to a one-line hint with its
   usage. Built-ins, user commands and plugin commands all show, from
   common/cmdinfo.c. Classic look: a plain bordered list, no rounding.
   gui_input_cmdhelp turns it off. */

#include <string.h>

#include "fe-gtk.h"
#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cmdinfo.h"
#include "gtkcompat.h"
#include "gtkutil.h"
#include "cmdpopup.h"

#define MAX_ROWS 10

typedef struct
{
	GtkWidget *entry;
	GtkWidget *popover;
	GtkWidget *list;		/* GtkListBox */
	GtkWidget *hint;		/* the usage line while typing arguments */
	GPtrArray *cmds;		/* the cmdinfo shown, in list order */
	gboolean suppress;	/* Escape: stay closed until the text changes shape */
	char *suppressed_for;	/* ...the word it was closed on */
} cmdpopup;

static gboolean
is_cmd_char (char c)
{
	return c && c == prefs.hex_input_command_char[0];
}

/* the text is "/word" with the cursor in word: the word, else NULL */
static char *
typed_command (const char *text, int cursor, gboolean *has_space)
{
	const char *sp;
	glong chars;

	*has_space = FALSE;
	if (!is_cmd_char (text[0]) || is_cmd_char (text[1]))	/* "//" sends a plain "/" */
		return NULL;
	sp = strchr (text, ' ');
	if (sp)
	{
		*has_space = TRUE;
		return g_strndup (text + 1, sp - text - 1);
	}
	chars = g_utf8_strlen (text, -1);
	if (cursor < chars)
		return NULL;	/* editing in the middle of a bare word: leave it be */
	return g_strdup (text + 1);
}

static GtkWidget *
row_new (cmdinfo *c)
{
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *name, *usage, *desc;
	char *m;
	const char *args = c->usage;

	/* the usage repeats the name: show just what comes after it */
	if (!g_ascii_strncasecmp (args, c->name, strlen (c->name)))
		args += strlen (c->name);
	while (*args == ' ')
		args++;

	m = g_markup_printf_escaped ("<b>/%s</b>", c->name);
	name = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (name), m);
	g_free (m);
	gtk_label_set_xalign (GTK_LABEL (name), 0);
	gtk_box_append (GTK_BOX (box), name);

	usage = gtk_label_new (args);
	gtk_label_set_xalign (GTK_LABEL (usage), 0);
	gtk_label_set_ellipsize (GTK_LABEL (usage), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (usage), 28);
	gtk_widget_add_css_class (usage, "nx-cmd-usage");
	gtk_box_append (GTK_BOX (box), usage);

	desc = gtk_label_new (c->kind == CMD_PLUGIN && !*c->desc ? _("(plugin)") : c->desc);
	gtk_label_set_xalign (GTK_LABEL (desc), 0);
	gtk_label_set_ellipsize (GTK_LABEL (desc), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (desc), 48);
	gtk_widget_set_hexpand (desc, TRUE);
	gtk_widget_add_css_class (desc, "nx-cmd-desc");
	gtk_widget_add_css_class (desc, "dim-label");
	gtk_box_append (GTK_BOX (box), desc);

	gtk_widget_set_tooltip_text (box, c->help);
	return box;
}

static void
popup_hide (cmdpopup *p)
{
	if (p->popover && gtk_widget_get_visible (p->popover))
		gtk_popover_popdown (GTK_POPOVER (p->popover));
}

/* put the popover just above the entry, its left edge on the text */
static void
popup_show (cmdpopup *p)
{
	GdkRectangle r = { 0, 0, 1, 1 };

	gtk_popover_set_pointing_to (GTK_POPOVER (p->popover), &r);
	if (!gtk_widget_get_visible (p->popover))
		gtk_popover_popup (GTK_POPOVER (p->popover));
}

static void
popup_fill (cmdpopup *p, const char *word)
{
	GtkWidget *child;
	guint i, n;

	while ((child = gtk_widget_get_first_child (p->list)))
		gtk_list_box_remove (GTK_LIST_BOX (p->list), child);
	cmdinfo_list_free (p->cmds);
	p->cmds = cmdinfo_find (word);
	n = MIN (p->cmds->len, MAX_ROWS);
	for (i = 0; i < n; i++)
		gtk_list_box_append (GTK_LIST_BOX (p->list), row_new (g_ptr_array_index (p->cmds, i)));
	if (n)
		gtk_list_box_select_row (GTK_LIST_BOX (p->list),
										 gtk_list_box_get_row_at_index (GTK_LIST_BOX (p->list), 0));
}

/* the text changed: decide what to show */
static void
popup_update (cmdpopup *p)
{
	const char *text = gtk_editable_get_text (GTK_EDITABLE (p->entry));
	int cursor = gtk_editable_get_position (GTK_EDITABLE (p->entry));
	gboolean has_space;
	char *word;

	if (!prefs.hex_gui_input_cmdhelp)
	{
		popup_hide (p);
		return;
	}
	word = typed_command (text, cursor, &has_space);
	if (!word)
	{
		p->suppress = FALSE;
		popup_hide (p);
		return;
	}
	/* Escape closed it: stay closed while the same command is being typed
	   or given its arguments; a different command opens it again */
	if (p->suppress)
	{
		const char *was = p->suppressed_for ? p->suppressed_for : "";
		if ((has_space && !g_ascii_strcasecmp (word, was)) ||
			 (!has_space && g_str_has_prefix (word, was)))
		{
			g_free (word);
			return;
		}
		p->suppress = FALSE;
	}

	if (has_space)
	{
		/* arguments: one line with the usage of the command typed */
		cmdinfo *c = cmdinfo_lookup (word);
		if (c && (*c->usage || *c->desc))
		{
			char *m = g_markup_printf_escaped ("<b>/%s</b>%s%s  <span alpha=\"60%%\">%s</span>",
				c->name, *c->usage ? " " : "",
				*c->usage && !g_ascii_strncasecmp (c->usage, c->name, strlen (c->name))
					? c->usage + strlen (c->name) + (c->usage[strlen (c->name)] == ' ')
					: c->usage,
				c->desc);
			gtk_label_set_markup (GTK_LABEL (p->hint), m);
			g_free (m);
			gtk_widget_set_visible (p->hint, TRUE);
			gtk_widget_set_visible (gtk_widget_get_parent (p->list), FALSE);
			popup_show (p);
		}
		else
			popup_hide (p);
		cmdinfo_free (c);
		g_free (word);
		return;
	}

	popup_fill (p, word);
	gtk_widget_set_visible (p->hint, FALSE);
	gtk_widget_set_visible (gtk_widget_get_parent (p->list), TRUE);
	if (p->cmds->len)
		popup_show (p);
	else
		popup_hide (p);
	g_free (word);
}

static void
popup_changed_cb (GtkEditable *e, cmdpopup *p)
{
	popup_update (p);
}

/* fill in the chosen command: "/JOIN " (or "/join " if typed lower case) */
static void
popup_complete (cmdpopup *p, cmdinfo *c)
{
	const char *text = gtk_editable_get_text (GTK_EDITABLE (p->entry));
	gboolean lower = text[1] && g_ascii_islower (text[1]);
	char *name = lower ? g_ascii_strdown (c->name, -1) : g_strdup (c->name);
	char *t = g_strdup_printf ("%c%s ", prefs.hex_input_command_char[0], name);

	gtk_editable_set_text (GTK_EDITABLE (p->entry), t);
	gtk_editable_set_position (GTK_EDITABLE (p->entry), -1);
	g_free (t);
	g_free (name);
	popup_update (p);
}

static cmdinfo *
selected (cmdpopup *p)
{
	GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (p->list));

	if (!row || !p->cmds)
		return NULL;
	return g_ptr_array_index (p->cmds, gtk_list_box_row_get_index (row));
}

static void
move (cmdpopup *p, int by)
{
	GtkListBoxRow *row = gtk_list_box_get_selected_row (GTK_LIST_BOX (p->list));
	int i = row ? gtk_list_box_row_get_index (row) : -1;
	int n = MIN ((int) p->cmds->len, MAX_ROWS);

	if (!n)
		return;
	i = (i + by + n) % n;
	gtk_list_box_select_row (GTK_LIST_BOX (p->list),
									 gtk_list_box_get_row_at_index (GTK_LIST_BOX (p->list), i));
}

/* keys while the list is up; FALSE lets the entry (and fkeys.c) have them */
gboolean
cmdpopup_key (GtkWidget *entry, guint keyval, GdkModifierType state)
{
	cmdpopup *p = g_object_get_data (G_OBJECT (entry), "nx-cmdpopup");
	gboolean list_up;
	cmdinfo *c;

	if (!p || !p->popover || !gtk_widget_get_visible (p->popover))
		return FALSE;
	list_up = gtk_widget_get_visible (gtk_widget_get_parent (p->list));
	if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK))
		return FALSE;

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
		if (!list_up)
			return FALSE;
		move (p, -1);
		return TRUE;
	case GDK_KEY_Down:
	case GDK_KEY_KP_Down:
		if (!list_up)
			return FALSE;
		move (p, 1);
		return TRUE;
	case GDK_KEY_Tab:
	case GDK_KEY_ISO_Left_Tab:
		if (!list_up || !(c = selected (p)))
			return FALSE;
		popup_complete (p, c);
		return TRUE;
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
		/* Enter on a full name sends it as it is (/away, /quit); on a part of
		   one it fills in the pick first */
		if (!list_up || !(c = selected (p)))
			return FALSE;
		{
			gboolean sp;
			char *word = typed_command (gtk_editable_get_text (GTK_EDITABLE (entry)),
												 gtk_editable_get_position (GTK_EDITABLE (entry)), &sp);
			gboolean exact = word && !g_ascii_strcasecmp (word, c->name);
			g_free (word);
			if (exact)
			{
				popup_hide (p);
				return FALSE;
			}
		}
		popup_complete (p, c);
		return TRUE;
	}
	return FALSE;
}

static void
row_activated_cb (GtkListBox *box, GtkListBoxRow *row, cmdpopup *p)
{
	popup_complete (p, g_ptr_array_index (p->cmds, gtk_list_box_row_get_index (row)));
	gtk_widget_grab_focus (p->entry);
}

static void
focus_out_cb (GtkEventControllerFocus *f, cmdpopup *p)
{
	popup_hide (p);
}

static void
cmdpopup_free (cmdpopup *p)
{
	cmdinfo_list_free (p->cmds);
	g_free (p->suppressed_for);
	if (p->popover)
		gtk_widget_unparent (p->popover);
	g_free (p);
}

void
cmdpopup_attach (GtkWidget *entry)
{
	cmdpopup *p = g_new0 (cmdpopup, 1);
	GtkWidget *box, *sw;
	GtkEventController *f;

	p->entry = entry;
	p->popover = gtk_popover_new ();
	gtk_widget_add_css_class (p->popover, "nx-cmdpopup");
	gtk_popover_set_has_arrow (GTK_POPOVER (p->popover), FALSE);
	gtk_popover_set_autohide (GTK_POPOVER (p->popover), FALSE);	/* typing goes on in the entry */
	gtk_popover_set_position (GTK_POPOVER (p->popover), GTK_POS_TOP);
	gtk_widget_set_halign (p->popover, GTK_ALIGN_START);
	gtk_widget_set_can_focus (p->popover, FALSE);
	gtk_widget_set_parent (p->popover, entry);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_popover_set_child (GTK_POPOVER (p->popover), box);

	p->list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (p->list), GTK_SELECTION_BROWSE);
	gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (p->list), TRUE);
	gtk_widget_set_can_focus (p->list, FALSE);
	g_signal_connect (p->list, "row-activated", G_CALLBACK (row_activated_cb), p);
	sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), p->list);
	gtk_box_append (GTK_BOX (box), sw);

	p->hint = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (p->hint), 0);
	gtk_label_set_ellipsize (GTK_LABEL (p->hint), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars (GTK_LABEL (p->hint), 90);
	gtk_widget_add_css_class (p->hint, "nx-cmd-hint");
	gtk_box_append (GTK_BOX (box), p->hint);

	g_signal_connect (entry, "changed", G_CALLBACK (popup_changed_cb), p);
	f = gtk_event_controller_focus_new ();
	g_signal_connect (f, "leave", G_CALLBACK (focus_out_cb), p);
	gtk_widget_add_controller (entry, f);
	g_object_set_data_full (G_OBJECT (entry), "nx-cmdpopup", p, (GDestroyNotify) cmdpopup_free);
}

GtkWidget *
cmdpopup_widget (GtkWidget *entry)
{
	cmdpopup *p = g_object_get_data (G_OBJECT (entry), "nx-cmdpopup");
	return p ? p->popover : NULL;
}

/* tests: what the popup shows ("" when closed): "list: NAME|NAME..." with
   the selected one in [], or "hint: text" */
char *
cmdpopup_describe (GtkWidget *entry)
{
	cmdpopup *p = g_object_get_data (G_OBJECT (entry), "nx-cmdpopup");
	GString *s;
	guint i, n;
	GtkListBoxRow *sel;

	if (!p || !p->popover || !gtk_widget_get_visible (p->popover))
		return g_strdup ("");
	if (gtk_widget_get_visible (p->hint))
		return g_strdup_printf ("hint: %s", gtk_label_get_text (GTK_LABEL (p->hint)));
	s = g_string_new ("list:");
	sel = gtk_list_box_get_selected_row (GTK_LIST_BOX (p->list));
	n = MIN (p->cmds->len, MAX_ROWS);
	for (i = 0; i < n; i++)
	{
		cmdinfo *c = g_ptr_array_index (p->cmds, i);
		gboolean is_sel = sel && gtk_list_box_row_get_index (sel) == (int) i;
		g_string_append_printf (s, " %s%s%s", is_sel ? "[" : "", c->name, is_sel ? "]" : "");
	}
	return g_string_free (s, FALSE);
}
