/* X-Chat
 * Copyright (C) 2005 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* joind.c - The Join Dialog.

   Popups up when you connect without any autojoin channels and helps you
   to find or join a channel.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

#ifndef WIN32
#include <unistd.h>
#endif

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/server.h"
#include "../common/servlist.h"
#include "../common/fe.h"
#include "fe-gtk.h"
#include "gtkutil.h"
#include "chanlist.h"


static void
joind_radio2_cb (GtkWidget *radio, server *serv)
{
	if (nx_toggle_get_active ((GtkWidget *) (radio)))
	{
		gtk_widget_grab_focus (serv->gui->joind_entry);
		gtk_editable_set_position (GTK_EDITABLE (serv->gui->joind_entry), 999);
	}
}

static void
joind_entryenter_cb (GtkWidget *entry, GtkWidget *ok)
{
	gtk_widget_grab_focus (ok);
}

static void
joind_entryfocus_cb (GtkWidget *entry, gpointer userdata)
{
	server *serv = userdata;

	nx_toggle_set_active ((GtkWidget *) (serv->gui->joind_radio2), TRUE);
}

static void
joind_destroy_cb (GtkWidget *win, server *serv)
{
	if (is_server (serv))
		serv->gui->joind_win = NULL;
}

static void
joind_ok_cb (GtkWidget *ok, server *serv)
{
	if (!is_server (serv))
	{
		nx_widget_destroy (nx_widget_get_toplevel (ok));
		return;
	}

	/* do nothing */
	if (nx_toggle_get_active ((GtkWidget *) (serv->gui->joind_radio1)))
		goto xit;

	/* join specific channel */
	if (nx_toggle_get_active ((GtkWidget *) (serv->gui->joind_radio2)))
	{
		char *text = (char *)nx_entry_get_text (serv->gui->joind_entry);
		if (strlen (text) < 1)
		{
			fe_message (_("Channel name too short, try again."), FE_MSG_ERROR);
			return;
		}
		serv->p_join (serv, text, "");
		goto xit;
	}

	/* channel list */
	chanlist_opengui (serv, TRUE);

xit:
	prefs.hex_gui_join_dialog = 0;
	if (nx_toggle_get_active ((GtkWidget *) (serv->gui->joind_check)))
		prefs.hex_gui_join_dialog = 1;

	nx_widget_destroy (serv->gui->joind_win);
	serv->gui->joind_win = NULL;
}

static void
joind_show_dialog (server *serv)
{
	GtkWidget *dialog1;
	GtkWidget *dialog_vbox1;
	GtkWidget *vbox1;
	GtkWidget *hbox1;
	GtkWidget *image1;
	GtkWidget *vbox2;
	GtkWidget *label;
	GtkWidget *radiobutton1;
	GtkWidget *radiobutton2;
	GtkWidget *radiobutton3;
	GtkWidget *hbox2;
	GtkWidget *entry1;
	GtkWidget *checkbutton1;
	GtkWidget *okbutton1;
	char buf[256];
	char buf2[256];

	serv->gui->joind_win = dialog1 = nx_dialog_new (NULL, NULL, FALSE);
	g_snprintf(buf, sizeof(buf), _("Connection Complete - %s"), _(DISPLAY_NAME));
	gtk_window_set_title (GTK_WINDOW (dialog1), buf);
	nx_window_set_dialog_hint (dialog1);
	nx_window_set_position_center (dialog1);
	gtk_window_set_transient_for (GTK_WINDOW(dialog1), GTK_WINDOW(serv->front_session->gui->window));
	gtk_window_set_modal (GTK_WINDOW (dialog1), TRUE);
	gtk_window_set_resizable (GTK_WINDOW (dialog1), FALSE);

	dialog_vbox1 = nx_dialog_get_content_area (NX_DIALOG_CAST (dialog1));
	nx_widget_show (dialog_vbox1);

	vbox1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	nx_widget_show (vbox1);
	nx_box_pack_start (dialog_vbox1, vbox1, TRUE, TRUE, 0);

	hbox1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_widget_show (hbox1);
	nx_box_pack_start (vbox1, hbox1, TRUE, TRUE, 0);

	image1 = gtkutil_image_new_from_stock (NX_STOCK_NETWORK, NX_ICON_LARGE_TOOLBAR);
	nx_widget_show (image1);
	nx_box_pack_start (hbox1, image1, FALSE, TRUE, 24);
	/* was gtk_misc_set_alignment (0.5, 0.06): centred, pinned near the top */
	gtk_widget_set_halign (image1, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (image1, GTK_ALIGN_START);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	nx_set_border_width (vbox2, 6);
	nx_widget_show (vbox2);
	nx_box_pack_start (hbox1, vbox2, TRUE, TRUE, 0);

	g_snprintf (buf2, sizeof (buf2), _("Connection to %s complete."),
				 server_get_network (serv, TRUE));
	g_snprintf (buf, sizeof (buf), "\n<b>%s</b>", buf2);
	label = gtk_label_new (buf);
	nx_widget_show (label);
	nx_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	label = gtk_label_new (_("In the server list window, no channel (chat room) has been entered to be automatically joined for this network."));
	nx_widget_show (label);
	nx_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	nx_label_set_wrap (label, TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	label = gtk_label_new (_("What would you like to do next?"));
	nx_widget_show (label);
	nx_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	serv->gui->joind_radio1 = radiobutton1 = nx_radio_button_new (NULL, _("_Nothing, I'll join a channel later."));
	nx_widget_show (radiobutton1);
	nx_box_pack_start (vbox2, radiobutton1, FALSE, FALSE, 0);

	hbox2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_widget_show (hbox2);
	nx_box_pack_start (vbox2, hbox2, FALSE, FALSE, 0);

	serv->gui->joind_radio2 = radiobutton2 = nx_radio_button_new (radiobutton1, _("_Join this channel:"));
	nx_widget_show (radiobutton2);
	nx_box_pack_start (hbox2, radiobutton2, FALSE, FALSE, 0);

	serv->gui->joind_entry = entry1 = gtk_entry_new ();
	nx_entry_set_text (entry1, "#");
	nx_widget_show (entry1);
	nx_box_pack_start (hbox2, entry1, TRUE, TRUE, 8);

	g_snprintf (buf, sizeof (buf), "<small>     %s</small>",
				 _("If you know the name of the channel you want to join, enter it here."));
	label = gtk_label_new (buf);
	nx_widget_show (label);
	nx_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	radiobutton3 = nx_radio_button_new (radiobutton1, _("O_pen the channel list."));
	nx_widget_show (radiobutton3);
	nx_box_pack_start (vbox2, radiobutton3, FALSE, FALSE, 0);

	g_snprintf (buf, sizeof (buf), "<small>     %s</small>",
				 _("Retrieving the channel list may take a minute or two."));
	label = gtk_label_new (buf);
	nx_widget_show (label);
	nx_box_pack_start (vbox2, label, FALSE, FALSE, 0);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	serv->gui->joind_check = checkbutton1 = gtk_check_button_new_with_mnemonic (_("_Always show this dialog after connecting."));
	if (prefs.hex_gui_join_dialog)
		nx_toggle_set_active ((GtkWidget *) (checkbutton1), TRUE);
	nx_widget_show (checkbutton1);
	nx_box_pack_start (vbox1, checkbutton1, FALSE, FALSE, 0);

	okbutton1 = gtkutil_button_new_from_stock ("gtk-ok");
	nx_widget_show (okbutton1);
	nx_dialog_add_action_widget (NX_DIALOG_CAST (dialog1), okbutton1, GTK_RESPONSE_OK);

	g_signal_connect (G_OBJECT (dialog1), "destroy",
							G_CALLBACK (joind_destroy_cb), serv);
	nx_on_focus_in (entry1, joind_entryfocus_cb, serv);
	g_signal_connect (G_OBJECT (entry1), "activate",
							G_CALLBACK (joind_entryenter_cb), okbutton1);
	g_signal_connect (G_OBJECT (radiobutton2), "toggled",
							G_CALLBACK (joind_radio2_cb), serv);
	g_signal_connect (G_OBJECT (okbutton1), "clicked",
							G_CALLBACK (joind_ok_cb), serv);
							
	if (serv->network)
		if (g_ascii_strcasecmp(((ircnet*)serv->network)->name, "Libera.Chat") == 0)
		{
			nx_entry_set_text (entry1, "#hexchat");
		}

	gtk_widget_grab_focus (okbutton1);
	nx_widget_show_all (dialog1);
}

void
joind_open (server *serv)
{
	if (prefs.hex_gui_join_dialog)
		joind_show_dialog (serv);
}

void
joind_close (server *serv)
{
	if (serv->gui->joind_win)
	{
		nx_widget_destroy (serv->gui->joind_win);
		serv->gui->joind_win = NULL;
	}
}
