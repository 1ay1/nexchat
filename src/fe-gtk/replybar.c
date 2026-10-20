/* nexchat
 * Copyright (C) 2026 nexchat contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Replying, the way chat apps do it:

   - right-click a line > Reply, or Cmd/Ctrl+Shift+R, or /reply on its own:
     choose the message (Cmd+Shift+R / /reply list the recent ones in a
     popup, newest first, type to narrow it by nick or words)
   - a bar over the input box says "Replying to alice: what she said",
     with an x to cancel (Esc does too)
   - Enter sends the line as a reply to that message (+draft/reply where
   the server carries it, "alice: ..." where it doesn't), and the bar goes

   The target is per tab and kept while you switch tabs. */

#include <string.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/util.h"
#include "../common/proto-irc.h"
#include "../common/batch.h"
#include "gtkutil.h"
#include "maingui.h"
#include "replybar.h"

#define PICK_MAX 30

/* "alice\tthe text" -> pieces */
static void
split_line (const char *line, char **nick, char **text)
{
	const char *tab = strchr (line, '\t');

	*nick = g_strndup (line, tab ? (gsize) (tab - line) : strlen (line));
	*text = strip_color (tab ? tab + 1 : "", -1, STRIP_ALL);
}

/* the bar for the front tab, from its res->reply_to */
gboolean
fe_reply_pick (session *sess, const char *msgid)
{
	if (msgid)
		replybar_set (sess, msgid);
	else
		replybar_pick (sess);
	return TRUE;
}

void
replybar_update (session *sess)
{
	session_gui *gui;
	GtkWidget *bar, *label;
	const char *line;

	if (!sess || !sess->gui || !(bar = sess->gui->reply_bar))
		return;
	gui = sess->gui;
	if (sess != current_sess && gui->is_tab)
		return;
	label = g_object_get_data (G_OBJECT (bar), "label");
	line = sess->res->reply_to ? reply_lookup (sess, sess->res->reply_to) : NULL;
	if (!line)
	{
		/* gone (too old) or nothing chosen */
		g_clear_pointer (&sess->res->reply_to, g_free);
		gtk_widget_set_visible (bar, FALSE);
		return;
	}
	{
		char *nick, *text, *markup;

		split_line (line, &nick, &text);
		markup = g_markup_printf_escaped (_("Replying to <b>%s</b>: %s"), nick, text);
		gtk_label_set_markup (GTK_LABEL (label), markup);
		g_free (markup);
		g_free (nick);
		g_free (text);
	}
	gtk_widget_set_visible (bar, TRUE);
	if (g_getenv ("NEXCHAT_REPLY_DEBUG"))
		g_printerr ("reply-bar: %s\n", gtk_label_get_text (GTK_LABEL (label)));
}

void
replybar_set (session *sess, const char *msgid)
{
	g_free (sess->res->reply_to);
	sess->res->reply_to = msgid ? g_strdup (msgid) : NULL;
	replybar_update (sess);
	if (msgid && sess->gui && sess->gui->input_box)
		gtk_widget_grab_focus (sess->gui->input_box);
}

gboolean
replybar_active (session *sess)
{
	return sess && sess->res && sess->res->reply_to;
}

/* Enter with a reply chosen: TRUE if it was sent that way */
gboolean
replybar_send (session *sess, const char *text)
{
	/* a command (but "//text" is text that starts with a slash) goes on as
	   a command; the reply stays chosen */
	if (!replybar_active (sess) || !*text ||
		 (text[0] == '/' && text[1] != '/'))
		return FALSE;
	/* "//text" is text that starts with a slash */
	reply_send (sess, sess->res->reply_to, *text == '/' ? text + 1 : text);
	replybar_set (sess, NULL);
	return TRUE;
}

static void
cancel_clicked (GtkButton *b, gpointer d)
{
	if (current_sess)
		replybar_set (current_sess, NULL);
}

GtkWidget *
replybar_new (session_gui *gui)
{
	GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *label = gtk_label_new (NULL);
	GtkWidget *x = gtk_button_new_from_icon_name ("window-close-symbolic");

	gtk_widget_add_css_class (bar, "nx-replybar");
	gtk_label_set_xalign (GTK_LABEL (label), 0);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_button_set_has_frame (GTK_BUTTON (x), FALSE);
	gtk_widget_set_tooltip_text (x, _("Don't reply (Esc)"));
	gtk_widget_set_focusable (x, FALSE);
	g_signal_connect (x, "clicked", G_CALLBACK (cancel_clicked), NULL);
	gtk_box_append (GTK_BOX (bar), label);
	gtk_box_append (GTK_BOX (bar), x);
	g_object_set_data (G_OBJECT (bar), "label", label);
	gtk_widget_set_visible (bar, FALSE);
	gui->reply_bar = bar;
	return bar;
}

/* tests: NEXCHAT_REPLY_CMD="cmd;cmd;..." runs each, a second apart, in the
   front channel once there is one (typed text when it doesn't start with
   "/": goes through Enter, as typed) */
static gboolean
test_step (gpointer data)
{
	char **cmds = data;
	static int i;
	GtkWidget *e;

	if (!current_sess || current_sess->type != SESS_CHANNEL)
		return G_SOURCE_CONTINUE;
	if (!cmds[i])
	{
		g_strfreev (cmds);
		return G_SOURCE_REMOVE;
	}
	e = current_sess->gui->input_box;
	SPELL_ENTRY_SET_TEXT (e, cmds[i]);
	g_signal_emit_by_name (e, "activate");
	g_printerr ("reply-test: ran %s (bar %s)\n", cmds[i],
					replybar_active (current_sess) ? "up" : "down");
	i++;
	return G_SOURCE_CONTINUE;
}

void
replybar_test_init (void)
{
	if (g_getenv ("NEXCHAT_REPLY_CMD"))
		g_timeout_add (1000, test_step, g_strsplit (g_getenv ("NEXCHAT_REPLY_CMD"), ";", -1));
}

/* ---- choosing the message: a popup of the recent ones ---- */

typedef struct
{
	session *sess;
	GtkWidget *pop, *entry, *list;
	GPtrArray *ids;	/* msgid per row, as listed */
} picker;

static void
picker_fill (picker *pk)
{
	GList *recent = reply_recent (pk->sess, 200), *l;
	const char *want = gtk_editable_get_text (GTK_EDITABLE (pk->entry));
	GtkWidget *child;
	int n = 0;

	while ((child = gtk_widget_get_first_child (pk->list)))
		gtk_list_box_remove (GTK_LIST_BOX (pk->list), child);
	g_ptr_array_set_size (pk->ids, 0);
	for (l = recent; l && n < PICK_MAX; l = l->next)
	{
		const char *line = reply_lookup (pk->sess, l->data);
		char *nick, *text, *markup;
		GtkWidget *row;

		if (!line)
			continue;
		split_line (line, &nick, &text);
		/* our own lines aren't what one replies to */
		if (!pk->sess->server->p_cmp (nick, pk->sess->server->nick) ||
			 (*want && !nocasestrstr (nick, want) && !nocasestrstr (text, want)))
		{
			g_free (nick);
			g_free (text);
			continue;
		}
		markup = g_markup_printf_escaped ("<b>%s</b>  %s", nick, text);
		row = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (row), markup);
		gtk_label_set_xalign (GTK_LABEL (row), 0);
		gtk_label_set_ellipsize (GTK_LABEL (row), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars (GTK_LABEL (row), 60);
		gtk_widget_set_margin_start (row, 6);
		gtk_widget_set_margin_end (row, 6);
		gtk_widget_set_margin_top (row, 3);
		gtk_widget_set_margin_bottom (row, 3);
		gtk_list_box_append (GTK_LIST_BOX (pk->list), row);
		g_ptr_array_add (pk->ids, g_strdup (l->data));
		g_free (markup);
		g_free (nick);
		g_free (text);
		n++;
	}
	g_list_free (recent);
	if (n)
		gtk_list_box_select_row (GTK_LIST_BOX (pk->list),
										 gtk_list_box_get_row_at_index (GTK_LIST_BOX (pk->list), 0));
	else
	{
		GtkWidget *none = gtk_label_new (*want ? _("Nothing matches") : _("Nothing to reply to yet"));
		gtk_widget_add_css_class (none, "dim-label");
		gtk_widget_set_margin_top (none, 8);
		gtk_widget_set_margin_bottom (none, 8);
		gtk_list_box_append (GTK_LIST_BOX (pk->list), none);
		gtk_list_box_row_set_selectable (gtk_list_box_get_row_at_index (GTK_LIST_BOX (pk->list), 0), FALSE);
	}
}

static void
picker_choose (picker *pk, int i)
{
	if (i >= 0 && i < (int) pk->ids->len && is_session (pk->sess))
		replybar_set (pk->sess, g_ptr_array_index (pk->ids, i));
	gtk_popover_popdown (GTK_POPOVER (pk->pop));
}

static void
picker_row_activated (GtkListBox *lb, GtkListBoxRow *row, picker *pk)
{
	picker_choose (pk, gtk_list_box_row_get_index (row));
}

static void
picker_entry_activate (GtkEntry *e, picker *pk)
{
	GtkListBoxRow *r = gtk_list_box_get_selected_row (GTK_LIST_BOX (pk->list));
	if (r)
		picker_choose (pk, gtk_list_box_row_get_index (r));
}

static void
picker_changed (GtkEditable *e, picker *pk)
{
	picker_fill (pk);
}

/* Up/Down in the filter move the selection */
static gboolean
picker_key (GtkEventControllerKey *k, guint keyval, guint code, GdkModifierType st, picker *pk)
{
	GtkListBoxRow *r = gtk_list_box_get_selected_row (GTK_LIST_BOX (pk->list));
	int i = r ? gtk_list_box_row_get_index (r) : -1;

	if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up)
	{
		GtkListBoxRow *to = gtk_list_box_get_row_at_index (GTK_LIST_BOX (pk->list),
																		  i + (keyval == GDK_KEY_Down ? 1 : -1));
		if (to && gtk_list_box_row_get_selectable (to))
		{
			gtk_list_box_select_row (GTK_LIST_BOX (pk->list), to);
			gtk_widget_grab_focus (pk->entry);
		}
		return TRUE;
	}
	return FALSE;
}

static void
picker_closed (GtkPopover *pop, picker *pk)
{
	GtkWidget *p = pk->pop;

	g_ptr_array_unref (pk->ids);
	g_free (pk);
	/* after this signal: GTK still holds the popover now */
	g_idle_add_once ((GSourceOnceFunc) gtk_widget_unparent, p);
}

void
replybar_pick (session *sess)
{
	picker *pk;
	GtkWidget *box, *sw;
	GtkEventController *keys;

	if (!sess || !sess->gui || !sess->gui->input_box)
		return;
	pk = g_new0 (picker, 1);
	pk->sess = sess;
	pk->ids = g_ptr_array_new_with_free_func (g_free);

	pk->pop = gtk_popover_new ();
	gtk_popover_set_position (GTK_POPOVER (pk->pop), GTK_POS_TOP);
	gtk_widget_set_parent (pk->pop, sess->gui->input_box);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	pk->entry = gtk_search_entry_new ();
	gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (pk->entry), _("Reply to... (nick or words)"));
	gtk_box_append (GTK_BOX (box), pk->entry);
	sw = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (sw), TRUE);
	gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (sw), 320);
	pk->list = gtk_list_box_new ();
	gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (pk->list), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (sw), pk->list);
	gtk_box_append (GTK_BOX (box), sw);
	gtk_popover_set_child (GTK_POPOVER (pk->pop), box);

	g_signal_connect (pk->list, "row-activated", G_CALLBACK (picker_row_activated), pk);
	g_signal_connect (pk->entry, "activate", G_CALLBACK (picker_entry_activate), pk);
	g_signal_connect (pk->entry, "search-changed", G_CALLBACK (picker_changed), pk);
	keys = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
	g_signal_connect (keys, "key-pressed", G_CALLBACK (picker_key), pk);
	gtk_widget_add_controller (pk->entry, keys);
	g_signal_connect (pk->pop, "closed", G_CALLBACK (picker_closed), pk);

	picker_fill (pk);
	gtk_popover_popup (GTK_POPOVER (pk->pop));
	gtk_widget_grab_focus (pk->entry);
	if (g_getenv ("NEXCHAT_REPLY_DEBUG"))
		g_printerr ("reply-pick: %u listed\n", pk->ids->len);
}
