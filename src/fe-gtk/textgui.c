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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/fe.h"
#include "../common/text.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "xtext.h"
#include "maingui.h"
#include "palette.h"
#include "textgui.h"

extern struct text_event te[];
extern char *pntevts_text[];
extern char *pntevts[];

static GtkWidget *pevent_dialog = NULL, *pevent_dialog_twid,
	*pevent_dialog_list, *pevent_dialog_hlist;

enum
{
	EVENT_COLUMN,
	TEXT_COLUMN,
	ROW_COLUMN,
	N_COLUMNS
};


/* this is only used in xtext.c for indented timestamping */
int
xtext_get_stamp_str (time_t tim, char **ret)
{
	return get_stamp_str (prefs.hex_stamp_text_format, tim, ret);
}

static void
PrintTextLine (xtext_buffer *xtbuf, unsigned char *text, int len, int indent, time_t timet)
{
	unsigned char *tab, *new_text;
	int leftlen;

	if (len == 0)
		len = 1;

	if (!indent)
	{
		if (prefs.hex_stamp_text)
		{
			int stamp_size;
			char *stamp;

			if (timet == 0)
				timet = time (0);

			stamp_size = get_stamp_str (prefs.hex_stamp_text_format, timet, &stamp);
			new_text = g_malloc (len + stamp_size + 1);
			memcpy (new_text, stamp, stamp_size);
			g_free (stamp);
			memcpy (new_text + stamp_size, text, len);
			gtk_xtext_append (xtbuf, new_text, len + stamp_size, timet);
			g_free (new_text);
		} else
			gtk_xtext_append (xtbuf, text, len, timet);
		return;
	}

	tab = strchr (text, '\t');
	if (tab && tab < (text + len))
	{
		leftlen = tab - text;
		gtk_xtext_append_indent (xtbuf,
										 text, leftlen, tab + 1, len - (leftlen + 1), timet);
	} else
		gtk_xtext_append_indent (xtbuf, 0, 0, text, len, timet);
}

/* a "── Tuesday, 14 October ──" line before the first line of a new day
   (or of the first day in a fresh tab that has history from before
   today), so you can tell when things were said */
static void
day_separator (xtext_buffer *buf, time_t stamp)
{
	GDateTime *dt, *now;
	int day;
	char *date, *line;

	if (!prefs.hex_text_day_sep || !stamp)
		return;
	dt = g_date_time_new_from_unix_local (stamp);
	if (!dt)
		return;
	day = g_date_time_get_year (dt) * 10000 + g_date_time_get_month (dt) * 100 +
			g_date_time_get_day_of_month (dt);
	if (day == buf->last_day)
	{
		g_date_time_unref (dt);
		return;
	}
	/* a brand new buffer only says so when it starts in the past */
	now = g_date_time_new_now_local ();
	if (buf->last_day || g_date_time_get_year (now) * 10000 + g_date_time_get_month (now) * 100 +
							   g_date_time_get_day_of_month (now) != day)
	{
		date = g_date_time_format (dt, g_date_time_get_year (dt) == g_date_time_get_year (now)
											? _("%A, %e %B") : _("%A, %e %B %Y"));
		g_strstrip (date);
		/* grey, centred feel in the text column: dashes either side */
		line = g_strdup_printf ("\00314\342\224\200\342\224\200\342\224\200 %s \342\224\200\342\224\200\342\224\200", date);
		gtk_xtext_append_indent (buf, 0, 0, (unsigned char *) line, strlen (line), stamp);
		gtk_xtext_last_no_stamp (buf);
		g_free (line);
		g_free (date);
	}
	buf->last_day = day;
	g_date_time_unref (now);
	g_date_time_unref (dt);
}

void
PrintTextRaw (void *xtbuf, unsigned char *text, int indent, time_t stamp)
{
	char *last_text = text;
	int len = 0;
	int beep_done = FALSE;

	day_separator (xtbuf, stamp ? stamp : time (NULL));

	/* split the text into separate lines */
	while (1)
	{
		switch (*text)
		{
		case 0:
			PrintTextLine (xtbuf, last_text, len, indent, stamp);
			return;
		case '\n':
			PrintTextLine (xtbuf, last_text, len, indent, stamp);
			text++;
			if (*text == 0)
				return;
			last_text = text;
			len = 0;
			break;
		case ATTR_BEEP:
			*text = ' ';
			if (!beep_done) /* beeps may be slow, so only do 1 per line */
			{
				beep_done = TRUE;
				if (!prefs.hex_input_filter_beep)
					gdk_display_beep (gdk_display_get_default ());
			}
		default:
			text++;
			len++;
		}
	}
}

static void
pevent_dialog_close (GtkWidget *wid, gpointer arg)
{
	pevent_dialog = NULL;
	pevent_save (NULL);
}

static void
pevent_edited (nx_row *row, const char *new_text, gpointer data)
{
	GtkXText *xtext = GTK_XTEXT (pevent_dialog_twid);
	int len, m;
	const char *text;
	char *out;
	int sig;

	/* the row edited, as GTK3's handler read the selected one */
	sig = nx_row_get_int (row, ROW_COLUMN);

	text = new_text;
	len = strlen (new_text);

	if (pevt_build_string (text, &out, &m) != 0)
	{
		fe_message (_("There was an error parsing the string"), FE_MSG_ERROR);
		return;
	}
	if (m > (te[sig].num_args & 0x7f))
	{
		g_free (out);
		out = g_strdup_printf (
			_("This signal is only passed %d args, $%d is invalid"),
			te[sig].num_args & 0x7f, m);
		fe_message (out, FE_MSG_WARN);
		g_free (out);
		return;
	}

	nx_row_set (row, TEXT_COLUMN, new_text, -1);

	g_free (pntevts_text[sig]);
	g_free (pntevts[sig]);

	pntevts_text[sig] = g_strdup (text);
	pntevts[sig] = out;

	out = g_malloc (len + 2);
	memcpy (out, text, len + 1);
	out[len] = '\n';
	out[len + 1] = 0;
	check_special_chars (out, TRUE);

	PrintTextRaw (xtext->buffer, out, 0, 0);
	g_free (out);

	/* Scroll to bottom */
	gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj));

	/* save this when we exit */
	prefs.save_pevents = 1;
}

static void
pevent_dialog_hfill (GtkWidget *list, int e)
{
	int i = 0;
	char *text;
	char num[16];
	nx_store *store = nx_view_get_store (list);

	nx_store_clear (store);

	while (i < (te[e].num_args & 0x7f))
	{
		text = _(te[e].help[i]);
		i++;
		if (text[0] == '\001')
			text++;
		g_snprintf (num, sizeof num, "%d", i);
		nx_row_set (nx_store_append (store, NULL), 0, num, 1, text, -1);
	}
}

static void
pevent_selection_changed (GtkWidget *view, gpointer userdata)
{
	nx_row *row = nx_view_get_selected (view);

	if (!row)
	{
		nx_store_clear (nx_view_get_store (pevent_dialog_hlist));
		return;
	}

	pevent_dialog_hfill (pevent_dialog_hlist, nx_row_get_int (row, ROW_COLUMN));
}

static void
pevent_dialog_fill (GtkWidget *list)
{
	int i;
	nx_store *store = nx_view_get_store (list);

	nx_store_clear (store);

	i = NUM_XP;
	do
	{
		i--;
		nx_row_set (nx_store_prepend (store, NULL),
		            EVENT_COLUMN, te[i].name,
		            TEXT_COLUMN, pntevts_text[i],
		            ROW_COLUMN, i, -1);
	}
	while (i != 0);
}

static void
pevent_save_req_cb (void *arg1, char *file)
{
	if (file)
		pevent_save (file);
}

static void
pevent_save_cb (GtkWidget * wid, void *data)
{
	if (data)
	{
		gtkutil_file_req (NULL, _("Print Texts File"), pevent_save_req_cb, NULL,
								NULL, NULL, FRF_WRITE);
		return;
	}
	pevent_save (NULL);
}

static void
pevent_load_req_cb (void *arg1, char *file)
{
	if (file)
	{
		pevent_load (file);
		pevent_make_pntevts ();
		pevent_dialog_fill (pevent_dialog_list);
		prefs.save_pevents = 1;
	}
}

static void
pevent_load_cb (GtkWidget * wid, void *data)
{
	gtkutil_file_req (NULL, _("Print Texts File"), pevent_load_req_cb, NULL, NULL, NULL, 0);
}

static void
pevent_ok_cb (GtkWidget * wid, void *data)
{
	nx_widget_destroy (pevent_dialog);
}

static void
pevent_test_cb (GtkWidget * wid, GtkWidget * twid)
{
	int len, n;
	char *out, *text;

	for (n = 0; n < NUM_XP; n++)
	{
		text = _(pntevts_text[n]);
		len = strlen (text);

		out = g_malloc (len + 2);
		memcpy (out, text, len + 1);
		out[len] = '\n';
		out[len + 1] = 0;
		check_special_chars (out, TRUE);

		PrintTextRaw (GTK_XTEXT (twid)->buffer, out, 0, 0);
		g_free (out);
	}
}

static GtkWidget *
pevent_treeview_new (GtkWidget *box)
{
	nx_store *store;
	GtkWidget *view;
	nx_col *col;

	store = nx_store_new (FALSE, N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);
	nx_scrolled_window_set_shadow_in (gtk_widget_get_parent (view), TRUE);
	gtk_widget_set_size_request (gtk_widget_get_parent (view), -1, 250);
	nx_view_set_search_column (view, EVENT_COLUMN);
	nx_view_on_select (view, pevent_selection_changed, NULL);

	col = nx_view_add_column (view, _("Event"), NX_COL_TEXT, EVENT_COLUMN);
	nx_col_set_resizable (col, TRUE);
	nx_col_set_min_width (col, 100);

	col = nx_view_add_column (view, _("Text"), NX_COL_TEXT, TEXT_COLUMN);
	nx_col_set_editable (col, TRUE);
	nx_col_on_edited (col, pevent_edited, NULL);

	return view;
}

static GtkWidget *
pevent_hlist_treeview_new (GtkWidget *box)
{
	nx_store *store;
	GtkWidget *view;

	/* the number as text: a GTK3 int column showed it the same */
	store = nx_store_new (FALSE, 2, G_TYPE_STRING, G_TYPE_STRING);
	view = nx_view_new_scrolled (box, store);
	g_object_unref (store);
	nx_scrolled_window_set_shadow_in (gtk_widget_get_parent (view), TRUE);
	gtk_widget_set_can_focus (view, FALSE);

	nx_view_add_column (view, _("$ Number"), NX_COL_TEXT, 0);
	nx_view_add_column (view, _("Description"), NX_COL_TEXT, 1);

	return view;
}

void
pevent_dialog_show (void)
{
	GtkWidget *vbox, *hbox, *wid, *pane;

	if (pevent_dialog)
	{
		mg_bring_tofront (pevent_dialog);
		return;
	}

	pevent_dialog =
			  mg_create_generic_tab ("edit events", _("Edit Events"),
											 TRUE, FALSE, pevent_dialog_close, NULL,
											 600, 455, &vbox, 0);

	pane = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
	nx_box_pack_start (vbox, pane, TRUE, TRUE, 0);
	
	pevent_dialog_list = pevent_treeview_new (pane);
	pevent_dialog_fill (pevent_dialog_list);

	pevent_dialog_hlist = pevent_hlist_treeview_new (pane);

	/* GTK2's treeview selected the cursor row when it took focus, so the
	   dialog opened with the first event selected and its $-number list and
	   preview filled in. GTK3 only moves the cursor; select it ourselves. */
	nx_view_select (pevent_dialog_list, nx_store_first (nx_view_get_store (pevent_dialog_list)));

	wid = nx_scrolled_window_new ();
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (wid), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	nx_box_pack_start (vbox, wid, FALSE, TRUE, 0);

	pevent_dialog_twid = gtk_xtext_new (colors, 0);
	gtk_widget_set_sensitive (pevent_dialog_twid, FALSE);
	gtk_widget_set_size_request (pevent_dialog_twid, -1, 75);
	gtkutil_container_add (GTK_WIDGET (wid), pevent_dialog_twid);
	gtk_xtext_set_font (GTK_XTEXT (pevent_dialog_twid), prefs.hex_text_font);

	hbox = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		nx_box_pack_start (vbox, hbox, FALSE, FALSE, 4);
	gtkutil_button (hbox, NX_STOCK_SAVE_AS, NULL, pevent_save_cb,
						 (void *) 1, _("Save As..."));
	gtkutil_button (hbox, NX_STOCK_OPEN, NULL, pevent_load_cb,
						 NULL, _("Load From..."));
	gtkutil_button (hbox, NULL, NULL, pevent_test_cb,
						pevent_dialog_twid, _("Test All"));
	gtkutil_button (hbox, NX_STOCK_OK, NULL, pevent_ok_cb,
						NULL, _("OK"));

	nx_widget_show_all (pevent_dialog);
}
