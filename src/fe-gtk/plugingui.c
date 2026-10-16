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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#define PLUGIN_C
typedef struct session hexchat_context;
#include "../common/hexchat-plugin.h"
#include "../common/plugin.h"
#include "../common/util.h"
#include "../common/outbound.h"
#include "../common/fe.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "gtkutil.h"
#include "maingui.h"
#include "nxlist.h"

/* model for the plugin treeview */
enum
{
	NAME_COLUMN,
	VERSION_COLUMN,
	FILE_COLUMN,
	DESC_COLUMN,
	FILEPATH_COLUMN,
	N_COLUMNS
};

static GtkWidget *plugin_window = NULL;


static GtkWidget *
plugingui_treeview_new (GtkWidget *box)
{
	nx_store *store;
	GtkWidget *view;
	static const char *const titles[] = { N_("Name"), N_("Version"), N_("File"), N_("Description") };
	int i;

	store = nx_store_new (FALSE, N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
	                      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);
	for (i = 0; i < 4; i++)
		nx_col_set_title_align (nx_view_add_column (view, _(titles[i]), NX_COL_TEXT, i), 0.5);
	/* GTK3 had the path as a 5th, empty-titled column */
	nx_col_set_title_align (nx_view_add_column (view, NULL, NX_COL_TEXT, FILEPATH_COLUMN), 0.5);
	/* GTK3's default: type-ahead on the first text column */
	nx_view_set_search_column (view, NAME_COLUMN);
	nx_widget_show (view);

	return view;
}

static char *
plugingui_getfilename (GtkWidget *view)
{
	nx_row *row = nx_view_get_selected (view);
	char *str = NULL;

	if (row)
		nx_row_get (row, FILEPATH_COLUMN, &str, -1);
	return str;
}

static void
plugingui_close (GtkWidget * wid, gpointer a)
{
	plugin_window = NULL;
}

extern GSList *plugin_list;

void
fe_pluginlist_update (void)
{
	hexchat_plugin *pl;
	GSList *list;
	GtkWidget *view;
	nx_store *store;

	if (!plugin_window)
		return;

	view = g_object_get_data (G_OBJECT (plugin_window), "view");
	store = nx_view_get_store (view);
	nx_store_clear (store);

	list = plugin_list;
	while (list)
	{
		pl = list->data;
		if (pl->version[0] != 0)
		{
			nx_row_set (nx_store_append (store, NULL), NAME_COLUMN, pl->name,
			            VERSION_COLUMN, pl->version,
			            FILE_COLUMN, file_part (pl->filename),
			            DESC_COLUMN, pl->desc,
			            FILEPATH_COLUMN, pl->filename, -1);
		}
		list = list->next;
	}
}

static void
plugingui_load_cb (session *sess, char *file)
{
	if (file)
	{
		char *buf;

		if (strchr (file, ' '))
			buf = g_strdup_printf ("LOAD \"%s\"", file);
		else
			buf = g_strdup_printf ("LOAD %s", file);
		handle_command (sess, buf, FALSE);
		g_free (buf);
	}
}

void
plugingui_load (void)
{
	char *sub_dir = g_build_filename (get_xdir(), "addons", NULL);

	gtkutil_file_req (NULL, _("Select a Plugin or Script to load"), plugingui_load_cb, current_sess,
							sub_dir, "*."PLUGIN_SUFFIX";*.lua;*.pl;*.py;*.tcl;*.js", FRF_FILTERISINITIAL|FRF_EXTENSIONS);

	g_free (sub_dir);
}

static void
plugingui_loadbutton_cb (GtkWidget * wid, gpointer unused)
{
	plugingui_load ();
}

static void
plugingui_unload (GtkWidget * wid, gpointer unused)
{
	char *modname, *file;
	GtkWidget *view;
	nx_row *row;
	
	view = g_object_get_data (G_OBJECT (plugin_window), "view");
	row = nx_view_get_selected (view);
	if (!row)
		return;
	nx_row_get (row, NAME_COLUMN, &modname, FILEPATH_COLUMN, &file, -1);

	if (g_str_has_suffix (file, "."PLUGIN_SUFFIX))
	{
		if (plugin_kill (modname, FALSE) == 2)
			fe_message (_("That plugin is refusing to unload.\n"), FE_MSG_ERROR);
	}
	else
	{
		char *buf;
		/* let python.so or perl.so handle it */
		if (strchr (file, ' '))
			buf = g_strdup_printf ("UNLOAD \"%s\"", file);
		else
			buf = g_strdup_printf ("UNLOAD %s", file);
		handle_command (current_sess, buf, FALSE);
		g_free (buf);
	}

	g_free (modname);
	g_free (file);
}

static void
plugingui_reloadbutton_cb (GtkWidget *wid, GtkWidget *view)
{
	char *file = plugingui_getfilename(view);

	if (file)
	{
		char *buf;

		if (strchr (file, ' '))
			buf = g_strdup_printf ("RELOAD \"%s\"", file);
		else
			buf = g_strdup_printf ("RELOAD %s", file);
		handle_command (current_sess, buf, FALSE);
		g_free (buf);
		g_free (file);
	}
}

void
plugingui_open (void)
{
	GtkWidget *view;
	GtkWidget *vbox, *hbox;
	char buf[128];

	if (plugin_window)
	{
		mg_bring_tofront (plugin_window);
		return;
	}

	g_snprintf(buf, sizeof(buf), _("Plugins and Scripts - %s"), _(DISPLAY_NAME));
	plugin_window = mg_create_generic_tab ("Addons", buf, FALSE, TRUE, plugingui_close, NULL,
														 700, 300, &vbox, 0);
	gtkutil_destroy_on_esc (plugin_window);

	view = plugingui_treeview_new (vbox);
	g_object_set_data (G_OBJECT (plugin_window), "view", view);


	hbox = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		nx_set_border_width (hbox, 5);
	nx_box_pack_end (vbox, hbox, 0, 0, 0);

	gtkutil_button (hbox, NX_STOCK_REVERT_TO_SAVED, NULL,
	                plugingui_loadbutton_cb, NULL, _("_Load..."));

	gtkutil_button (hbox, NX_STOCK_DELETE, NULL,
	                plugingui_unload, NULL, _("_Unload"));

	gtkutil_button (hbox, NX_STOCK_REFRESH, NULL,
	                plugingui_reloadbutton_cb, view, _("_Reload"));

	fe_pluginlist_update ();

	nx_widget_show_all (plugin_window);
}
