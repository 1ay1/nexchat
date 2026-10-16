/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
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
#define _FILE_OFFSET_BITS 64 /* allow selection of large files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fe-gtk.h"

#include <gdk/gdkkeysyms.h>
#if defined (WIN32) || defined (__APPLE__)
#include <pango/pangocairo.h>
#endif

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/cfgfiles.h"
#include "../common/hexchatc.h"
#include "../common/typedef.h"
#include "gtkutil.h"
#include "pixmaps.h"

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/* gtkutil.c, just some gtk wrappers */

extern void path_part (char *file, char *path, int pathlen);

GtkWidget *
gtkutil_box_new (GtkOrientation orientation, gboolean homogeneous, int spacing)
{
	GtkWidget *box = gtk_box_new (orientation, spacing);
	gtk_box_set_homogeneous (GTK_BOX (box), homogeneous);
	return box;
}

/* gtk_container_add() into a GTK2 GtkHBox/GtkVBox packed the child with
   expand=TRUE, fill=TRUE. A GTK3 gtk_box_new() box packs it with
   expand=FALSE, so whole panes collapse. Keep the GTK2 behaviour for every
   box, and plain gtk_container_add for everything else. */
void
gtkutil_container_add (GtkWidget *container, GtkWidget *child)
{
	if (GTK_IS_BOX (container))
		gtk_box_pack_start (GTK_BOX (container), child, TRUE, TRUE, 0);
	else
		gtk_container_add (GTK_CONTAINER (container), child);
}

typedef struct
{
	GMainLoop *loop;
	int response;
	gboolean destroyed;
} run_info;

static void
run_response_cb (GtkDialog *dialog, int response, run_info *ri)
{
	ri->response = response;
	if (g_main_loop_is_running (ri->loop))
		g_main_loop_quit (ri->loop);
}

static void
run_destroy_cb (GtkWidget *dialog, run_info *ri)
{
	ri->destroyed = TRUE;
	if (g_main_loop_is_running (ri->loop))
		g_main_loop_quit (ri->loop);
}

static gboolean
run_delete_cb (GtkWidget *dialog, GdkEvent *ev, run_info *ri)
{
	/* like gtk_dialog_run: closing the window answers DELETE_EVENT and the
	   dialog stays alive for the caller to destroy */
	ri->response = GTK_RESPONSE_DELETE_EVENT;
	if (g_main_loop_is_running (ri->loop))
		g_main_loop_quit (ri->loop);
	return TRUE;
}

int
gtkutil_dialog_run_blocking (GtkDialog *dialog)
{
	run_info ri = { NULL, GTK_RESPONSE_NONE, FALSE };
	gulong h1, h2, h3;

	g_object_ref (dialog);
	h1 = g_signal_connect (dialog, "response", G_CALLBACK (run_response_cb), &ri);
	h2 = g_signal_connect (dialog, "destroy", G_CALLBACK (run_destroy_cb), &ri);
	h3 = g_signal_connect (dialog, "delete-event", G_CALLBACK (run_delete_cb), &ri);

	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_widget_show (GTK_WIDGET (dialog));

	ri.loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (ri.loop);
	g_main_loop_unref (ri.loop);

	if (!ri.destroyed)
	{
		g_signal_handler_disconnect (dialog, h1);
		g_signal_handler_disconnect (dialog, h2);
		g_signal_handler_disconnect (dialog, h3);
	}
	g_object_unref (dialog);
	return ri.response;
}

gboolean
gtkutil_window_can_position (GtkWindow *win)
{
#ifdef GDK_WINDOWING_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
		return FALSE;
#endif
	return TRUE;
}

void
gtkutil_window_move (GtkWindow *win, int x, int y)
{
	if (gtkutil_window_can_position (win))
		gtk_window_move (win, x, y);
}

gboolean
gtkutil_window_get_position (GtkWindow *win, int *x, int *y)
{
	if (!gtkutil_window_can_position (win))
		return FALSE;
	gtk_window_get_position (win, x, y);
	return TRUE;
}

void
gtkutil_window_set_urgent (GtkWindow *win, gboolean urgent)
{
	gtk_window_set_urgency_hint (win, urgent);
}

GtkWidget *
gtkutil_menu_item_new_with_image (const char *label, gboolean is_markup, GtkWidget *image)
{
	GtkWidget *item = gtk_menu_item_new ();
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	/* an accel label, like GtkMenuItem's own child, so the item's shortcut
	   (gtk_widget_add_accelerator) is drawn on the right: Ctrl+S, Ctrl+Q... */
	GtkWidget *lbl = gtk_accel_label_new ("");

	if (is_markup)
		gtk_label_set_markup_with_mnemonic (GTK_LABEL (lbl), label);
	else
		gtk_label_set_text_with_mnemonic (GTK_LABEL (lbl), label);
	gtk_label_set_xalign (GTK_LABEL (lbl), 0.0);
	gtk_label_set_mnemonic_widget (GTK_LABEL (lbl), item);
	gtk_accel_label_set_accel_widget (GTK_ACCEL_LABEL (lbl), item);

	/* keep a 16px icon column even without an image, like GtkImageMenuItem */
	if (!image)
		image = gtk_image_new ();
	gtk_widget_set_size_request (image, 16, -1);
	gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), lbl, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (item), box);
	g_object_set_data (G_OBJECT (item), "nx-label", lbl);
	gtk_widget_show_all (box);
	return item;
}

/* the GtkLabel of a menu item: ours (box), or a plain GtkMenuItem's child */
GtkWidget *
gtkutil_menu_item_get_label (GtkWidget *item)
{
	GtkWidget *lbl = g_object_get_data (G_OBJECT (item), "nx-label");
	GtkWidget *child;

	if (lbl)
		return lbl;
	child = gtk_bin_get_child (GTK_BIN (item));
	return (child && GTK_IS_LABEL (child)) ? child : NULL;
}

/* Every GtkButton / GtkImageMenuItem keeps its image visible. A hook on
   GtkWidget::realize catches all of them: our own gtk_button_set_image
   calls, gtk_button_new_from_stock, dialog buttons and plugins' menu items,
   without touching each call site. (GObject::notify can't take hooks.) */
static gboolean
gtkutil_realize_hook (GSignalInvocationHint *ihint, guint n_params,
							 const GValue *params, gpointer data)
{
	GObject *obj = g_value_get_object (&params[0]);

	if (GTK_IS_BUTTON (obj))
	{
		if (!gtk_button_get_always_show_image (GTK_BUTTON (obj)))
			gtk_button_set_always_show_image (GTK_BUTTON (obj), TRUE);
	}
	/* menu items: gtkutil_menu_item_new_with_image() always shows its icon */
	return TRUE;	/* stay connected */
}

void
gtkutil_force_images (void)
{
	static gboolean done;
	guint sig;

	if (done)
		return;
	done = TRUE;

	g_type_class_unref (g_type_class_ref (GTK_TYPE_WIDGET));
	sig = g_signal_lookup ("realize", GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (sig, 0, gtkutil_realize_hook, NULL, NULL);
}

/* GTK 3.24's own stock table: stock id -> icon name (gtkiconfactory.c) and
   label (gtkstock.c). Ids whose image only exists under the stock name
   (gtk-ok, gtk-cancel, gtk-connect, ...) map to themselves: GTK3 ships
   those PNGs as named icons, checked with tests/ux/iconprobe.c. */
static const struct
{
	const char *stock;
	const char *icon;
	const char *context;	/* gettext context in GTK's gtk30 domain */
	const char *label;
} stock_table[] = {
	{ "gtk-about", "help-about", "Stock label", "_About" },
	{ "gtk-add", "list-add", "Stock label", "_Add" },
	{ "gtk-apply", "gtk-apply", "Stock label", "_Apply" },
	{ "gtk-cancel", "gtk-cancel", "Stock label", "_Cancel" },
	{ "gtk-clear", "edit-clear", "Stock label", "_Clear" },
	{ "gtk-close", "window-close", "Stock label", "_Close" },
	{ "gtk-connect", "gtk-connect", "Stock label", "C_onnect" },
	{ "gtk-copy", "edit-copy", "Stock label", "_Copy" },
	{ "gtk-delete", "edit-delete", "Stock label", "_Delete" },
	{ "gtk-dialog-error", "dialog-error", "Stock label", "Error" },
	{ "gtk-dialog-warning", "dialog-warning", "Stock label", "Warning" },
	{ "gtk-disconnect", "gtk-disconnect", "Stock label", "_Disconnect" },
	{ "gtk-find", "edit-find", "Stock label", "_Find" },
	{ "gtk-go-back", "go-previous", "Stock label, navigation", "_Back" },
	{ "gtk-go-down", "go-down", "Stock label, navigation", "_Down" },
	{ "gtk-go-forward", "go-next", "Stock label, navigation", "_Forward" },
	{ "gtk-go-up", "go-up", "Stock label, navigation", "_Up" },
	{ "gtk-help", "help-contents", "Stock label", "_Help" },
	{ "gtk-index", "gtk-index", "Stock label", "_Index" },
	{ "gtk-jump-to", "go-jump", "Stock label", "_Jump to" },
	{ "gtk-justify-left", "format-justify-left", "Stock label", "_Left" },
	{ "gtk-media-play", "media-playback-start", "Stock label, media", "_Play" },
	{ "gtk-network", "network-idle", "Stock label", "_Network" },
	{ "gtk-new", "document-new", "Stock label", "_New" },
	{ "gtk-no", "gtk-no", "Stock label", "_No" },
	{ "gtk-ok", "gtk-ok", "Stock label", "_OK" },
	{ "gtk-open", "document-open", "Stock label", "_Open" },
	{ "gtk-preferences", "gtk-preferences", "Stock label", "_Preferences" },
	{ "gtk-quit", "application-exit", "Stock label", "_Quit" },
	{ "gtk-redo", "edit-redo", "Stock label", "_Redo" },
	{ "gtk-refresh", "view-refresh", "Stock label", "_Refresh" },
	{ "gtk-remove", "list-remove", "Stock label", "_Remove" },
	{ "gtk-revert-to-saved", "document-revert", "Stock label", "_Revert" },
	{ "gtk-save", "document-save", "Stock label", "_Save" },
	{ "gtk-save-as", "document-save-as", "Stock label", "Save _As" },
	{ "gtk-spell-check", "tools-check-spelling", "Stock label", "_Spell Check" },
	{ "gtk-yes", "gtk-yes", "Stock label", "_Yes" },
};

static int
stock_find (const char *stock_id)
{
	guint i;

	if (!stock_id)
		return -1;
	for (i = 0; i < G_N_ELEMENTS (stock_table); i++)
		if (strcmp (stock_table[i].stock, stock_id) == 0)
			return i;
	return -1;
}

const char *
gtkutil_stock_icon_name (const char *stock_id)
{
	int i = stock_find (stock_id);
	return i >= 0 ? stock_table[i].icon : stock_id;
}

GtkWidget *
gtkutil_image_new_from_stock (const char *stock_id, GtkIconSize size)
{
	return gtk_image_new_from_icon_name (gtkutil_stock_icon_name (stock_id), size);
}

const char *
gtkutil_stock_label (const char *stock_id)
{
	int i = stock_find (stock_id);

	if (i < 0)
		return stock_id;
	/* GTK translated these in its own catalogue; use the same strings so
	   every language keeps its existing button text */
	return g_dpgettext2 ("gtk30", stock_table[i].context, stock_table[i].label);
}

GtkWidget *
gtkutil_button_new_from_stock (const char *stock_id)
{
	GtkWidget *button = gtk_button_new_with_mnemonic (gtkutil_stock_label (stock_id));
	gtk_button_set_image (GTK_BUTTON (button),
								 gtkutil_image_new_from_stock (stock_id, GTK_ICON_SIZE_BUTTON));
	return button;
}

GtkWidget *
gtkutil_dialog_add_stock_button (GtkDialog *dialog, const char *stock_id, int response)
{
	GtkWidget *button = gtkutil_button_new_from_stock (stock_id);

	gtk_widget_set_can_default (button, TRUE);
	gtk_widget_show (button);
	gtk_dialog_add_action_widget (dialog, button, response);
	return button;
}

GdkPixbuf *
gtkutil_render_icon (GtkWidget *widget, const char *stock_id, GtkIconSize size, const char *unused)
{
	int w = 16, h = 16;
	GtkIconTheme *theme = widget ? gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget))
								  : gtk_icon_theme_get_default ();

	gtk_icon_size_lookup (size, &w, &h);
	return gtk_icon_theme_load_icon (theme, gtkutil_stock_icon_name (stock_id), MIN (w, h),
												GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
}

void
gtkutil_grid_attach (GtkWidget *grid, GtkWidget *child,
							int left, int right, int top, int bottom,
							int xoptions, int yoptions, int xpadding, int ypadding)
{
	/* explicit FALSE too: GtkTable never let a child's own wish to expand
	   leak into a column that wasn't attached with EXPAND */
	gtk_widget_set_hexpand (child, (xoptions & GU_EXPAND) != 0);
	gtk_widget_set_vexpand (child, (yoptions & GU_EXPAND) != 0);
	gtk_widget_set_halign (child, (xoptions & GU_FILL) ? GTK_ALIGN_FILL : GTK_ALIGN_CENTER);
	gtk_widget_set_valign (child, (yoptions & GU_FILL) ? GTK_ALIGN_FILL : GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start (child, xpadding);
	gtk_widget_set_margin_end (child, xpadding);
	gtk_widget_set_margin_top (child, ypadding);
	gtk_widget_set_margin_bottom (child, ypadding);
	gtk_grid_attach (GTK_GRID (grid), child, left, top, right - left, bottom - top);
}

struct file_req
{
	GtkWidget *dialog;
	void *userdata;
	filereqcallback callback;
	int flags;		/* FRF_* flags */
};

static void
gtkutil_file_req_destroy (GtkWidget * wid, struct file_req *freq)
{
	freq->callback (freq->userdata, NULL);
	g_free (freq);
}

static void
gtkutil_check_file (char *filename, struct file_req *freq)
{
	int axs = FALSE;

	GFile *file = g_file_new_for_path (filename);

	if (freq->flags & FRF_WRITE)
	{
		GFile *parent = g_file_get_parent (file);

		GFileInfo *fi = g_file_query_info (parent, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (fi != NULL)
		{
			if (g_file_info_get_attribute_boolean (fi, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE))
			{
				axs = TRUE;
			}

			g_object_unref (fi);
		}

		g_object_unref (parent);
	}
	else
	{
		GFileInfo *fi = g_file_query_info (file, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, G_FILE_QUERY_INFO_NONE, NULL, NULL);

		if (fi != NULL)
		{
			if (g_file_info_get_file_type (fi) != G_FILE_TYPE_DIRECTORY || (freq->flags & FRF_CHOOSEFOLDER))
			{
				axs = TRUE;
			}

			g_object_unref (fi);
		}
	}

	g_object_unref (file);

	if (axs)
	{
		char *filename_utf8 = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
		if (filename_utf8 != NULL)
		{
			freq->callback (freq->userdata, filename_utf8);
			g_free (filename_utf8);
		}
		else
		{
			fe_message ("Filename encoding is corrupt.", FE_MSG_ERROR);
		}
	}
	else
	{
		if (freq->flags & FRF_WRITE)
		{
			fe_message (_("Cannot write to that file."), FE_MSG_ERROR);
		}
		else
		{
			fe_message (_("Cannot read that file."), FE_MSG_ERROR);
		}
	}
}

static void
gtkutil_file_req_done (GtkWidget * wid, struct file_req *freq)
{
	GSList *files, *cur;
	GtkFileChooser *fs = GTK_FILE_CHOOSER (freq->dialog);

	if (freq->flags & FRF_MULTIPLE)
	{
		files = cur = gtk_file_chooser_get_filenames (fs);
		while (cur)
		{
			gtkutil_check_file (cur->data, freq);
			g_free (cur->data);
			cur = cur->next;
		}
		if (files)
			g_slist_free (files);
	}
	else
	{
		if (freq->flags & FRF_CHOOSEFOLDER)
		{
			gchar *filename = gtk_file_chooser_get_current_folder (fs);
			gtkutil_check_file (filename, freq);
			g_free (filename);
		}
		else
		{
			gchar *filename = gtk_file_chooser_get_filename (fs);
			gtkutil_check_file (gtk_file_chooser_get_filename (fs), freq);
			g_free (filename);
		}
	}

	/* this should call the "destroy" cb, where we free(freq) */
	gtk_widget_destroy (freq->dialog);
}

static void
gtkutil_file_req_response (GtkWidget *dialog, gint res, struct file_req *freq)
{
	switch (res)
	{
	case GTK_RESPONSE_ACCEPT:
		gtkutil_file_req_done (dialog, freq);
		break;

	case GTK_RESPONSE_CANCEL:
		/* this should call the "destroy" cb, where we free(freq) */
		gtk_widget_destroy (freq->dialog);
	}
}

void
gtkutil_file_req (GtkWindow *parent, const char *title, void *callback, void *userdata, char *filter, char *extensions,
						int flags)
{
	struct file_req *freq;
	GtkWidget *dialog;
	GtkFileFilter *filefilter;
	extern char *get_xdir_fs (void);
	char *token;
	char *tokenbuffer;

	if (flags & FRF_WRITE)
	{
		dialog = gtk_file_chooser_dialog_new (title, NULL,
												GTK_FILE_CHOOSER_ACTION_SAVE,
												NULL, NULL);
		gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_SAVE, GTK_RESPONSE_ACCEPT);

		if (!(flags & FRF_NOASKOVERWRITE))
			gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
	}
	else
	{
		dialog = gtk_file_chooser_dialog_new (title, NULL,
												GTK_FILE_CHOOSER_ACTION_OPEN,
												NULL, NULL);
		gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_OK, GTK_RESPONSE_ACCEPT);
	}

	if (filter && filter[0] && (flags & FRF_FILTERISINITIAL))
	{
		if (flags & FRF_WRITE)
		{
			char temp[1024];
			path_part (filter, temp, sizeof (temp));
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), temp);
			gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), file_part (filter));
		}
		else
			gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), filter);
	}
	else if (!(flags & FRF_RECENTLYUSED))
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), get_xdir ());

	if (flags & FRF_MULTIPLE)
		gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), TRUE);
	if (flags & FRF_CHOOSEFOLDER)
		gtk_file_chooser_set_action (GTK_FILE_CHOOSER (dialog), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

	if ((flags & FRF_EXTENSIONS || flags & FRF_MIMETYPES) && extensions != NULL)
	{
		filefilter = gtk_file_filter_new ();
		tokenbuffer = g_strdup (extensions);
		token = strtok (tokenbuffer, ";");

		while (token != NULL)
		{
			if (flags & FRF_EXTENSIONS)
				gtk_file_filter_add_pattern (filefilter, token);
			else
				gtk_file_filter_add_mime_type (filefilter, token);
			token = strtok (NULL, ";");
		}

		g_free (tokenbuffer);
		gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filefilter);
	}

	gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (dialog), get_xdir (), NULL);

	freq = g_new (struct file_req, 1);
	freq->dialog = dialog;
	freq->flags = flags;
	freq->callback = callback;
	freq->userdata = userdata;

	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (gtkutil_file_req_response), freq);
	g_signal_connect (G_OBJECT (dialog), "destroy",
						   G_CALLBACK (gtkutil_file_req_destroy), (gpointer) freq);

	if (parent)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

	if (flags & FRF_MODAL)
	{
		g_assert (parent);
		gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	}

	gtk_widget_show (dialog);
}

static gboolean
gtkutil_esc_destroy (GtkWidget * win, GdkEventKey * key, gpointer userdata)
{
	GtkWidget *wid;

	/* Destroy the window of detached utils */
	if (!gtk_widget_is_toplevel (win))
	{
		if (gdk_window_get_type_hint (gtk_widget_get_window (win)) == GDK_WINDOW_TYPE_HINT_DIALOG)
			wid = gtk_widget_get_parent (win);
		else
			return FALSE;
	}
	else
		wid = win;

	if (key->keyval == GDK_KEY_Escape)
		gtk_widget_destroy (wid);
			
	return FALSE;
}

void
gtkutil_destroy_on_esc (GtkWidget *win)
{
	g_signal_connect (G_OBJECT (win), "key_press_event", G_CALLBACK (gtkutil_esc_destroy), win);
}

void
gtkutil_destroy (GtkWidget * igad, GtkWidget * dgad)
{
	gtk_widget_destroy (dgad);
}

static void
gtkutil_get_str_response (GtkDialog *dialog, gint arg1, gpointer entry)
{
	void (*callback) (int cancel, char *text, void *user_data);
	char *text;
	void *user_data;

	text = (char *) gtk_entry_get_text (GTK_ENTRY (entry));
	callback = g_object_get_data (G_OBJECT (dialog), "cb");
	user_data = g_object_get_data (G_OBJECT (dialog), "ud");

	switch (arg1)
	{
	case GTK_RESPONSE_REJECT:
		callback (TRUE, text, user_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_ACCEPT:
		callback (FALSE, text, user_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	}
}

static void
gtkutil_str_enter (GtkWidget *entry, GtkWidget *dialog)
{
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
}

void
fe_get_str (char *msg, char *def, void *callback, void *userdata)
{
	GtkWidget *dialog;
	GtkWidget *entry;
	GtkWidget *hbox;
	GtkWidget *label;
	extern GtkWidget *parent_window;

	dialog = gtk_dialog_new_with_buttons (msg, NULL, 0, NULL, NULL);
	gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_OK, GTK_RESPONSE_ACCEPT);

	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));
	gtk_box_set_homogeneous (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), TRUE);

	if (userdata == (void *)1)	/* nick box is usually on the very bottom, make it centered */
	{
		gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
	}
	else
	{
		gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	}

	hbox = gtkutil_box_new (GTK_ORIENTATION_HORIZONTAL, TRUE, 0);

	g_object_set_data (G_OBJECT (dialog), "cb", callback);
	g_object_set_data (G_OBJECT (dialog), "ud", userdata);

	entry = gtk_entry_new ();
	g_signal_connect (G_OBJECT (entry), "activate",
						 	G_CALLBACK (gtkutil_str_enter), dialog);
	gtk_entry_set_text (GTK_ENTRY (entry), def);
	gtk_box_pack_end (GTK_BOX (hbox), entry, 0, 0, 0);

	label = gtk_label_new (msg);
	gtk_box_pack_end (GTK_BOX (hbox), label, 0, 0, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (gtkutil_get_str_response), entry);

	gtkutil_container_add (gtk_dialog_get_content_area (GTK_DIALOG (dialog)), hbox);

	gtk_widget_show_all (dialog);
}

static void
gtkutil_get_number_response (GtkDialog *dialog, gint arg1, gpointer spin)
{
	void (*callback) (int cancel, int value, void *user_data);
	int num;
	void *user_data;

	num = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));
	callback = g_object_get_data (G_OBJECT (dialog), "cb");
	user_data = g_object_get_data (G_OBJECT (dialog), "ud");

	switch (arg1)
	{
	case GTK_RESPONSE_REJECT:
		callback (TRUE, num, user_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_ACCEPT:
		callback (FALSE, num, user_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	}
}

static void
gtkutil_get_bool_response (GtkDialog *dialog, gint arg1, gpointer spin)
{
	void (*callback) (int value, void *user_data);
	void *user_data;

	callback = g_object_get_data (G_OBJECT (dialog), "cb");
	user_data = g_object_get_data (G_OBJECT (dialog), "ud");

	switch (arg1)
	{
	case GTK_RESPONSE_REJECT:
		callback (0, user_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_ACCEPT:
		callback (1, user_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		break;
	}
}

void
fe_get_int (char *msg, int def, void *callback, void *userdata)
{
	GtkWidget *dialog;
	GtkWidget *spin;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkAdjustment *adj;
	extern GtkWidget *parent_window;

	dialog = gtk_dialog_new_with_buttons (msg, NULL, 0, NULL, NULL);
	gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_OK, GTK_RESPONSE_ACCEPT);
	gtk_box_set_homogeneous (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), TRUE);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));

	hbox = gtkutil_box_new (GTK_ORIENTATION_HORIZONTAL, TRUE, 0);

	g_object_set_data (G_OBJECT (dialog), "cb", callback);
	g_object_set_data (G_OBJECT (dialog), "ud", userdata);

	spin = gtk_spin_button_new (NULL, 1, 0);
	adj = gtk_spin_button_get_adjustment ((GtkSpinButton*)spin);
	gtk_adjustment_set_lower (adj, 0);
	gtk_adjustment_set_upper (adj, 1024);
	gtk_adjustment_set_step_increment (adj, 1);
	gtk_spin_button_set_value ((GtkSpinButton*)spin, def);
	gtk_box_pack_end (GTK_BOX (hbox), spin, 0, 0, 0);

	label = gtk_label_new (msg);
	gtk_box_pack_end (GTK_BOX (hbox), label, 0, 0, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (gtkutil_get_number_response), spin);

	gtkutil_container_add (gtk_dialog_get_content_area (GTK_DIALOG (dialog)), hbox);

	gtk_widget_show_all (dialog);
}

void
fe_get_bool (char *title, char *prompt, void *callback, void *userdata)
{
	GtkWidget *dialog;
	GtkWidget *prompt_label;
	extern GtkWidget *parent_window;

	dialog = gtk_dialog_new_with_buttons (title, NULL, 0, NULL, NULL);
	gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_NO, GTK_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (GTK_DIALOG (dialog), NX_STOCK_YES, GTK_RESPONSE_ACCEPT);
	gtk_box_set_homogeneous (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), TRUE);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));


	g_object_set_data (G_OBJECT (dialog), "cb", callback);
	g_object_set_data (G_OBJECT (dialog), "ud", userdata);

	prompt_label = gtk_label_new (prompt);

	g_signal_connect (G_OBJECT (dialog), "response",
		G_CALLBACK (gtkutil_get_bool_response), NULL);

	gtkutil_container_add (gtk_dialog_get_content_area (GTK_DIALOG (dialog)), prompt_label);

	gtk_widget_show_all (dialog);
}

GtkWidget *
gtkutil_button (GtkWidget *box, char *stock, char *tip, void *callback,
					 void *userdata, char *labeltext)
{
	GtkWidget *wid, *img, *bbox;

	wid = gtk_button_new ();

	if (labeltext)
	{
		gtk_button_set_label (GTK_BUTTON (wid), labeltext);
		gtk_button_set_image (GTK_BUTTON (wid), gtkutil_image_new_from_stock (stock, GTK_ICON_SIZE_MENU));
		gtk_button_set_use_underline (GTK_BUTTON (wid), TRUE);
		if (box)
			gtkutil_container_add (GTK_WIDGET (box), wid);
	}
	else
	{
		bbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		gtkutil_container_add (GTK_WIDGET (wid), bbox);
		gtk_widget_show (bbox);

		img = gtkutil_image_new_from_stock (stock, GTK_ICON_SIZE_MENU);
		gtkutil_container_add (GTK_WIDGET (bbox), img);
		gtk_widget_show (img);
		gtk_box_pack_start (GTK_BOX (box), wid, 0, 0, 0);
	}

	g_signal_connect (G_OBJECT (wid), "clicked",
							G_CALLBACK (callback), userdata);
	gtk_widget_show (wid);
	if (tip)
		gtk_widget_set_tooltip_text (wid, tip);

	return wid;
}

void
gtkutil_label_new (char *text, GtkWidget * box)
{
	GtkWidget *label = gtk_label_new (text);
	gtkutil_container_add (GTK_WIDGET (box), label);
	gtk_widget_show (label);
}

GtkWidget *
gtkutil_entry_new (int max, GtkWidget * box, void *callback,
						 gpointer userdata)
{
	GtkWidget *entry = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY (entry), max);
	gtkutil_container_add (GTK_WIDGET (box), entry);
	if (callback)
		g_signal_connect (G_OBJECT (entry), "changed",
								G_CALLBACK (callback), userdata);
	gtk_widget_show (entry);
	return entry;
}

void
show_and_unfocus (GtkWidget * wid)
{
	gtk_widget_set_can_focus (wid, FALSE);
	gtk_widget_show (wid);
}

void
gtkutil_set_icon (GtkWidget *win)
{
#ifndef WIN32
	/* FIXME: Magically breaks icon rendering in most
	 * (sub)windows, but OFC only on Windows. GTK <3
	 */
	gtk_window_set_icon (GTK_WINDOW (win), pix_hexchat);
#endif
}

extern GtkWidget *parent_window;	/* maingui.c */

GtkWidget *
gtkutil_window_new (char *title, char *role, int width, int height, int flags)
{
	GtkWidget *win;

	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtkutil_set_icon (win);
#ifdef WIN32
	gtk_window_set_wmclass (GTK_WINDOW (win), "nexchat", "nexchat");
#endif
	gtk_window_set_title (GTK_WINDOW (win), title);
	gtk_window_set_default_size (GTK_WINDOW (win), width, height);
	gtk_window_set_role (GTK_WINDOW (win), role);
	if (flags & 1)
		gtk_window_set_position (GTK_WINDOW (win), GTK_WIN_POS_MOUSE);
	if ((flags & 2) && parent_window)
	{
		gtk_window_set_type_hint (GTK_WINDOW (win), GDK_WINDOW_TYPE_HINT_DIALOG);
		gtk_window_set_transient_for (GTK_WINDOW (win), GTK_WINDOW (parent_window));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (win), TRUE);
	}

	return win;
}

/* pass NULL as selection to paste to both clipboard & X11 text */
void
gtkutil_copy_to_clipboard (GtkWidget *widget, GdkAtom selection,
                           const gchar *str)
{
	GtkWidget *win;
	GtkClipboard *clip, *clip2;

	win = gtk_widget_get_toplevel (GTK_WIDGET (widget));
	if (gtk_widget_is_toplevel (win))
	{
		int len = strlen (str);

		if (selection)
		{
			clip = gtk_widget_get_clipboard (win, selection);
			gtk_clipboard_set_text (clip, str, len);
		} else
		{
			/* copy to both primary X selection and clipboard */
			clip = gtk_widget_get_clipboard (win, GDK_SELECTION_PRIMARY);
			clip2 = gtk_widget_get_clipboard (win, GDK_SELECTION_CLIPBOARD);
			gtk_clipboard_set_text (clip, str, len);
			gtk_clipboard_set_text (clip2, str, len);
		}
	}
}

/* Treeview util functions */

GtkWidget *
gtkutil_treeview_new (GtkWidget *box, GtkTreeModel *model,
                      GtkTreeCellDataFunc mapper, ...)
{
	GtkWidget *win, *view;
	GtkCellRenderer *renderer = NULL;
	GtkTreeViewColumn *col;
	va_list args;
	int col_id = 0;
	GType type;
	char *title, *attr;

	win = gtk_scrolled_window_new (0, 0);
	gtkutil_container_add (GTK_WIDGET (box), win);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (win),
											  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show (win);

	view = gtk_tree_view_new_with_model (model);
	/* the view now has a ref on the model, we can unref it */
	g_object_unref (G_OBJECT (model));
	gtkutil_container_add (GTK_WIDGET (win), view);

	va_start (args, mapper);
	for (col_id = va_arg (args, int); col_id != -1; col_id = va_arg (args, int))
	{
		type = gtk_tree_model_get_column_type (model, col_id);
		switch (type)
		{
			case G_TYPE_BOOLEAN:
				renderer = gtk_cell_renderer_toggle_new ();
				attr = "active";
				break;
			case G_TYPE_STRING:	/* fall through */
			default:
				renderer = gtk_cell_renderer_text_new ();
				attr = "text";
				break;
		}

		title = va_arg (args, char *);
		if (mapper)	/* user-specified function to set renderer attributes */
		{
			col = gtk_tree_view_column_new_with_attributes (title, renderer, NULL);
			gtk_tree_view_column_set_cell_data_func (col, renderer, mapper,
			                                         GINT_TO_POINTER (col_id), NULL);
		} else
		{
			/* just set the typical attribute for this type of renderer */
			col = gtk_tree_view_column_new_with_attributes (title, renderer,
			                                                attr, col_id, NULL);
		}
		gtk_tree_view_append_column (GTK_TREE_VIEW (view), col);
		if (title == NULL)
			gtk_tree_view_column_set_visible (col, FALSE);
	}

	va_end (args);

	return view;
}

gboolean
gtkutil_treemodel_string_to_iter (GtkTreeModel *model, gchar *pathstr, GtkTreeIter *iter_ret)
{
	GtkTreePath *path = gtk_tree_path_new_from_string (pathstr);
	gboolean success;

	success = gtk_tree_model_get_iter (model, iter_ret, path);
	gtk_tree_path_free (path);
	return success;
}

/*gboolean
gtkutil_treeview_get_selected_iter (GtkTreeView *view, GtkTreeIter *iter_ret)
{
	GtkTreeModel *store;
	GtkTreeSelection *select;
	
	select = gtk_tree_view_get_selection (view);
	return gtk_tree_selection_get_selected (select, &store, iter_ret);
}*/

gboolean
gtkutil_treeview_get_selected (GtkTreeView *view, GtkTreeIter *iter_ret, ...)
{
	GtkTreeModel *store;
	GtkTreeSelection *select;
	gboolean has_selected;
	va_list args;
	
	select = gtk_tree_view_get_selection (view);
	has_selected = gtk_tree_selection_get_selected (select, &store, iter_ret);

	if (has_selected) {
		va_start (args, iter_ret);
		gtk_tree_model_get_valist (store, iter_ret, args);
		va_end (args);
	}

	return has_selected;
}

gboolean
gtkutil_tray_icon_supported (GtkWindow *window)
{
#ifndef GDK_WINDOWING_X11
	return TRUE;
#else
	GdkScreen *screen = gtk_window_get_screen (window);
	GdkDisplay *display = gdk_screen_get_display (screen);
	int screen_number;
	Display *xdisplay;
	char *selection_name;
	Atom selection_atom;
	Window tray_window = None;

	/* a GTK3 build with X11 support may still be running on Wayland etc. */
	if (!GDK_IS_X11_DISPLAY (display))
		return TRUE;

	screen_number = gdk_x11_screen_get_screen_number (screen);
	xdisplay = gdk_x11_display_get_xdisplay (display);
	selection_name = g_strdup_printf ("_NET_SYSTEM_TRAY_S%d", screen_number);
	selection_atom = XInternAtom (xdisplay, selection_name, False);

	XGrabServer (xdisplay);

	tray_window = XGetSelectionOwner (xdisplay, selection_atom);

	XUngrabServer (xdisplay);
	XFlush (xdisplay);
	g_free (selection_name);

	return (tray_window != None);
#endif
}

#if defined (WIN32) || defined (__APPLE__)
gboolean
gtkutil_find_font (const char *fontname)
{
	int i;
	int n_families;
	const char *family_name;
	PangoFontMap *fontmap;
	PangoFontFamily *family;
	PangoFontFamily **families;

	fontmap = pango_cairo_font_map_get_default ();
	pango_font_map_list_families (fontmap, &families, &n_families);

	for (i = 0; i < n_families; i++)
	{
		family = families[i];
		family_name = pango_font_family_get_name (family);

		if (!g_ascii_strcasecmp (family_name, fontname))
		{
			g_free (families);
			return TRUE;
		}
	}

	g_free (families);
	return FALSE;
}
#endif
