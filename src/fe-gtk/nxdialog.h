/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* GtkDialog and GtkMessageDialog are deprecated in GTK4. Their GTK4
   replacements (GtkAlertDialog, ...) look and behave differently: a
   different layout, no content area, async only. nexchat's dialogs are a
   content area above a row of buttons answering with a response id, so
   NxDialog is exactly that on a plain GtkWindow: same "response" signal,
   same ids, same Escape and close button behaviour.

   GTK3: these are the GtkDialog calls themselves. */

#ifndef NEXCHAT_NXDIALOG_H
#define NEXCHAT_NXDIALOG_H

#include <gtk/gtk.h>


#define NX_TYPE_DIALOG (nx_dialog_get_type ())
G_DECLARE_FINAL_TYPE (NxDialog, nx_dialog, NX, DIALOG, GtkWindow)

GtkWidget *nx_dialog_new (const char *title, GtkWindow *parent, gboolean modal);
GtkWidget *nx_dialog_get_content_area (NxDialog *d);
GtkWidget *nx_dialog_get_action_area (NxDialog *d);
/* add a button that answers response_id; returns it */
GtkWidget *nx_dialog_add_button (NxDialog *d, const char *mnemonic_label, int response_id);
void nx_dialog_add_action_widget (NxDialog *d, GtkWidget *child, int response_id);
/* emit "response" (as a button press would) */
void nx_dialog_response (NxDialog *d, int response_id);
/* the button that Enter activates */
void nx_dialog_set_default_response (NxDialog *d, int response_id);

/* GtkMessageDialog: an icon, a message (plain or markup), buttons */
typedef enum { NX_MSG_INFO, NX_MSG_WARNING, NX_MSG_QUESTION, NX_MSG_ERROR } nx_msg_type;
typedef enum { NX_BUTTONS_NONE, NX_BUTTONS_OK, NX_BUTTONS_CLOSE, NX_BUTTONS_OK_CANCEL } nx_msg_buttons;
GtkWidget *nx_message_dialog_new (GtkWindow *parent, gboolean modal, nx_msg_type type,
											 nx_msg_buttons buttons, gboolean markup, const char *text);

#define NX_DIALOG_CAST(d) NX_DIALOG (d)


#endif
