/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* GTK4 NxDialog, see nxdialog.h. Layout follows GTK3's GtkDialog and
   GtkMessageDialog templates (and Adwaita's spacing for them). */

#include <gtk/gtk.h>
#include "nxdialog.h"
#include "gtkcompat.h"

#if GTK_CHECK_VERSION(4, 0, 0)

struct _NxDialog
{
	GtkWindow parent;
	GtkWidget *content;		/* the caller's area */
	GtkWidget *action;		/* the button row */
	int default_response;
};

G_DEFINE_FINAL_TYPE (NxDialog, nx_dialog, GTK_TYPE_WINDOW)

enum { RESPONSE, N_SIGNALS };
static guint signals[N_SIGNALS];

static gboolean
nx_dialog_close_request (GtkWindow *w)
{
	/* closing answers DELETE_EVENT, as GtkDialog did; the window stays
	   for the response handler to destroy */
	nx_dialog_response (NX_DIALOG (w), GTK_RESPONSE_DELETE_EVENT);
	return TRUE;
}

static void
nx_dialog_class_init (NxDialogClass *klass)
{
	GtkWindowClass *window_class = GTK_WINDOW_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	window_class->close_request = nx_dialog_close_request;
	/* same signature as GtkDialog::response */
	signals[RESPONSE] = g_signal_new ("response", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
												 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
	/* Escape closes, as in GtkDialog */
	gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "window.close", NULL);
	gtk_widget_class_set_css_name (widget_class, "window");
}

static void
nx_dialog_init (NxDialog *d)
{
	GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	d->default_response = GTK_RESPONSE_NONE;
	gtk_widget_add_css_class (GTK_WIDGET (d), "dialog");
	d->content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_vexpand (d->content, TRUE);
	gtk_widget_add_css_class (d->content, "dialog-vbox");
	/* GTK3: buttons at the end, 6px apart, a 6px border round the row */
	d->action = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (d->action, GTK_ALIGN_END);
	gtk_widget_set_margin_start (d->action, 6);
	gtk_widget_set_margin_end (d->action, 6);
	gtk_widget_set_margin_top (d->action, 6);
	gtk_widget_set_margin_bottom (d->action, 6);
	gtk_widget_add_css_class (d->action, "dialog-action-area");
	gtk_box_append (GTK_BOX (vbox), d->content);
	gtk_box_append (GTK_BOX (vbox), d->action);
	gtk_window_set_child (GTK_WINDOW (d), vbox);
}

GtkWidget *
nx_dialog_new (const char *title, GtkWindow *parent, gboolean modal)
{
	NxDialog *d = g_object_new (NX_TYPE_DIALOG, NULL);

	if (title)
		gtk_window_set_title (GTK_WINDOW (d), title);
	if (parent)
	{
		gtk_window_set_transient_for (GTK_WINDOW (d), parent);
		gtk_window_set_destroy_with_parent (GTK_WINDOW (d), TRUE);
		/* GTK3 put a dialog over the middle of its parent */
		gtkutil_window_place (GTK_WIDGET (d), 2);
	}
	gtk_window_set_modal (GTK_WINDOW (d), modal);
	return GTK_WIDGET (d);
}

GtkWidget *
nx_dialog_get_content_area (NxDialog *d)
{
	return d->content;
}

GtkWidget *
nx_dialog_get_action_area (NxDialog *d)
{
	return d->action;
}

static void
button_clicked (GtkWidget *b, NxDialog *d)
{
	nx_dialog_response (d, GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "nx-response")));
}

void
nx_dialog_add_action_widget (NxDialog *d, GtkWidget *child, int response_id)
{
	g_object_set_data (G_OBJECT (child), "nx-response", GINT_TO_POINTER (response_id));
	if (GTK_IS_BUTTON (child))
		g_signal_connect (child, "clicked", G_CALLBACK (button_clicked), d);
	/* GtkButtonBox gave every button the same, at least 85px, width */
	if (GTK_IS_BUTTON (child))
		gtk_widget_set_size_request (child, 85, -1);
	/* HELP sat at the other end */
	if (response_id == GTK_RESPONSE_HELP)
		gtk_box_insert_child_after (GTK_BOX (d->action), child, NULL);
	else
		gtk_box_append (GTK_BOX (d->action), child);
	if (response_id == d->default_response)
		gtk_window_set_default_widget (GTK_WINDOW (d), child);
}

GtkWidget *
nx_dialog_add_button (NxDialog *d, const char *label, int response_id)
{
	GtkWidget *b = gtk_button_new_with_mnemonic (label);

	nx_dialog_add_action_widget (d, b, response_id);
	return b;
}

void
nx_dialog_response (NxDialog *d, int response_id)
{
	g_signal_emit (d, signals[RESPONSE], 0, response_id);
}

void
nx_dialog_set_default_response (NxDialog *d, int response_id)
{
	GtkWidget *c;

	d->default_response = response_id;
	for (c = gtk_widget_get_first_child (d->action); c; c = gtk_widget_get_next_sibling (c))
		if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (c), "nx-response")) == response_id)
			gtk_window_set_default_widget (GTK_WINDOW (d), c);
}

/* ---- message dialog ---- */

GtkWidget *
nx_message_dialog_new (GtkWindow *parent, gboolean modal, nx_msg_type type,
							  nx_msg_buttons buttons, gboolean markup, const char *text)
{
	NxDialog *d = NX_DIALOG (nx_dialog_new (NULL, parent, modal));
	GtkWidget *label;
	/* GtkMessageDialog's buttons came from GTK's catalogue */
	const char *ok = g_dgettext ("gtk30", "_OK");
	const char *cancel = g_dgettext ("gtk30", "_Cancel");
	const char *close = g_dgettext ("gtk30", "_Close");

	(void) type;	/* GTK3.10+ message dialogs showed no icon */
	gtk_window_set_title (GTK_WINDOW (d), "");	/* and no title */
	gtk_window_set_resizable (GTK_WINDOW (d), FALSE);
	gtk_widget_add_css_class (GTK_WIDGET (d), "message");
	/* GTK3 messagedialog.ui: the text centred, 30px in from the sides,
	   at most 60 characters wide */
	label = gtk_label_new (NULL);
	if (markup)
		gtk_label_set_markup (GTK_LABEL (label), text);
	else
		gtk_label_set_text (GTK_LABEL (label), text);
	gtk_label_set_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (label), 60);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
	gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start (label, 30);
	gtk_widget_set_margin_end (label, 30);
	gtk_widget_set_margin_top (label, 6);
	gtk_widget_set_margin_bottom (label, 30);
	gtk_box_append (GTK_BOX (d->content), label);

	/* and its buttons fill the bottom row in equal parts, 2px in */
	gtk_widget_set_halign (d->action, GTK_ALIGN_FILL);
	gtk_box_set_homogeneous (GTK_BOX (d->action), TRUE);
	gtk_box_set_spacing (GTK_BOX (d->action), 2);
	gtk_widget_set_margin_start (d->action, 2);
	gtk_widget_set_margin_end (d->action, 2);
	gtk_widget_set_margin_top (d->action, 0);
	gtk_widget_set_margin_bottom (d->action, 2);
	gtk_widget_add_css_class (d->action, "message-buttons");

	switch (buttons)
	{
	case NX_BUTTONS_OK:
		nx_dialog_add_button (d, ok, GTK_RESPONSE_OK);
		nx_dialog_set_default_response (d, GTK_RESPONSE_OK);
		break;
	case NX_BUTTONS_CLOSE:
		nx_dialog_add_button (d, close, GTK_RESPONSE_CLOSE);
		nx_dialog_set_default_response (d, GTK_RESPONSE_CLOSE);
		break;
	case NX_BUTTONS_OK_CANCEL:
		nx_dialog_add_button (d, cancel, GTK_RESPONSE_CANCEL);
		nx_dialog_add_button (d, ok, GTK_RESPONSE_OK);
		nx_dialog_set_default_response (d, GTK_RESPONSE_OK);
		break;
	default:
		break;
	}
	return GTK_WIDGET (d);
}

#endif
