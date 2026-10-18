/* X-Chat
 * Copyright (C) 1998-2005 Peter Zelezny.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>


#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/server.h"
#include "../common/hexchatc.h"
#include "../common/outbound.h"
#include "../common/inbound.h"
#include "../common/plugin.h"
#include "../common/modes.h"
#include "../common/url.h"
#include "../common/util.h"
#include "../common/text.h"
#include "../common/chanopt.h"
#include "../common/cfgfiles.h"

#include "fe-gtk.h"
#include "banlist.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "joind.h"
#include "palette.h"
#include "maingui.h"
#include "menu.h"
#include "fkeys.h"
#include "userlistgui.h"
#include "chanview.h"
#include "pixmaps.h"
#include "plugin-tray.h"
#include "xtext.h"
#include "sexy-spell-entry.h"
#include "cmdpopup.h"
#include "gtkutil.h"

#ifdef G_OS_WIN32
#include <windows.h>
#include <gdk/win32/gdkwin32.h>
#endif

#define GUI_SPACING (3)
#define GUI_BORDER (0)

/* The main window used to be a 3x4 GtkTable with per-row and per-column
   spacing: row 0 (menu) has GUI_SPACING under it, columns 0 and 1 each a
   1px gap after them, and rows 1/2 get a gap depending on where the
   channel switcher sits. GtkGrid only has one spacing for all rows, so the
   gap is stored on the grid and applied as the bottom/end margin of every
   child that ends on that row/column, which is how GtkTable laid it out. */

#define MG_ROW_GAP "mg-row-gap-%d"
#define MG_COL_GAP "mg-col-gap-%d"

static int
mg_grid_gap (GtkWidget *grid, const char *fmt, int index)
{
	char key[32];
	g_snprintf (key, sizeof key, fmt, index);
	return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (grid), key));
}

static void
mg_grid_apply_gaps (GtkWidget *grid)
{
	GList *children = nx_widget_get_children (grid), *l;

	for (l = children; l; l = l->next)
	{
		GtkWidget *child = l->data;
		int left, top, width, height;

		nx_grid_child_get (grid, child, &left, &top, &width, &height);
		/* the gap after the last row/column the child covers, unless that's
		   the edge of the table (GtkTable ignored spacing there too) */
		gtk_widget_set_margin_bottom (child, top + height < 4 ? mg_grid_gap (grid, MG_ROW_GAP, top + height - 1) : 0);
		gtk_widget_set_margin_end (child, left + width < 3 ? mg_grid_gap (grid, MG_COL_GAP, left + width - 1) : 0);
	}
	g_list_free (children);
}

static void
mg_grid_set_row_spacing (GtkWidget *grid, int row, int spacing)
{
	char key[32];
	g_snprintf (key, sizeof key, MG_ROW_GAP, row);
	g_object_set_data (G_OBJECT (grid), key, GINT_TO_POINTER (spacing));
	mg_grid_apply_gaps (grid);
}

static void
mg_grid_set_col_spacing (GtkWidget *grid, int col, int spacing)
{
	char key[32];
	g_snprintf (key, sizeof key, MG_COL_GAP, col);
	g_object_set_data (G_OBJECT (grid), key, GINT_TO_POINTER (spacing));
	mg_grid_apply_gaps (grid);
}

static void
mg_grid_attach (GtkWidget *grid, GtkWidget *child, int l, int r, int t, int b,
					 int xo, int yo)
{
	gtkutil_grid_attach (grid, child, l, r, t, b, xo, yo, 0, 0);
	mg_grid_apply_gaps (grid);
}

enum
{
	POS_INVALID = 0,
	POS_TOPLEFT = 1,
	POS_BOTTOMLEFT = 2,
	POS_TOPRIGHT = 3,
	POS_BOTTOMRIGHT = 4,
	POS_TOP = 5,	/* for tabs only */
	POS_BOTTOM = 6,
	POS_HIDDEN = 7
};

/* two different types of tabs */
#define TAG_IRC 0		/* server, channel, dialog */
#define TAG_UTIL 1	/* dcc, notify, chanlist */

static void mg_create_entry (session *sess, GtkWidget *box);
static void mg_create_search (session *sess, GtkWidget *box);
static void mg_link_irctab (session *sess, int focus);

static session_gui static_mg_gui;
static session_gui *mg_gui = NULL;	/* the shared irc tab */
static int ignore_chanmode = FALSE;
static const char chan_flags[] = { 'c', 'n', 't', 'i', 'm', 'l', 'k' };

static chan *active_tab = NULL;	/* active tab */
GtkWidget *parent_window = NULL;			/* the master window */


static PangoAttrList *away_list;
static PangoAttrList *newdata_list;
static PangoAttrList *nickseen_list;
static PangoAttrList *newmsg_list;
static PangoAttrList *plain_list = NULL;

static PangoAttrList *
mg_attr_list_create (GdkRGBA *col, int size)
{
	PangoAttribute *attr;
	PangoAttrList *list;

	list = pango_attr_list_new ();

	if (col)
	{
		attr = pango_attr_foreground_new (C16R (*col), C16G (*col), C16B (*col));
		attr->start_index = 0;
		attr->end_index = 0xffff;
		pango_attr_list_insert (list, attr);
	}

	if (size > 0)
	{
		attr = pango_attr_scale_new (size == 1 ? PANGO_SCALE_SMALL : PANGO_SCALE_X_SMALL);
		attr->start_index = 0;
		attr->end_index = 0xffff;
		pango_attr_list_insert (list, attr);
	}

	return list;
}

static void
mg_create_tab_colors (void)
{
	if (plain_list)
	{
		pango_attr_list_unref (plain_list);
		pango_attr_list_unref (newmsg_list);
		pango_attr_list_unref (newdata_list);
		pango_attr_list_unref (nickseen_list);
		pango_attr_list_unref (away_list);
	}

	plain_list = mg_attr_list_create (NULL, prefs.hex_gui_tab_small);
	newdata_list = mg_attr_list_create (&colors[COL_NEW_DATA], prefs.hex_gui_tab_small);
	nickseen_list = mg_attr_list_create (&colors[COL_HILIGHT], prefs.hex_gui_tab_small);
	newmsg_list = mg_attr_list_create (&colors[COL_NEW_MSG], prefs.hex_gui_tab_small);
	away_list = mg_attr_list_create (&colors[COL_AWAY], FALSE);
}

static void
set_window_urgency (GtkWidget *win, gboolean set)
{
	gtkutil_window_set_urgent (GTK_WINDOW (win), set);
}

static void
flash_window (GtkWidget *win)
{
	set_window_urgency (win, TRUE);
}

static void
unflash_window (GtkWidget *win)
{
	set_window_urgency (win, FALSE);
}

/* flash the taskbar button */

void
fe_flash_window (session *sess)
{
	if (fe_gui_info (sess, 0) != 1)	/* only do it if not focused */
		flash_window (sess->gui->window);
}

/* set a tab plain, red, light-red, or blue */

void
fe_set_tab_color (struct session *sess, tabcolor col)
{
	struct session *server_sess = sess->server->server_session;
	int col_noflags = (col & ~FE_COLOR_ALLFLAGS);
	int col_shouldoverride = !(col & FE_COLOR_FLAG_NOOVERRIDE);

	if (sess->res->tab && sess->gui->is_tab && (col == 0 || sess != current_tab))
	{
		switch (col_noflags)
		{
		case 0:	/* no particular color (theme default) */
			sess->tab_state = TAB_STATE_NONE;
			sess->unread = sess->unread_hilight = 0;
			chan_set_color (sess->res->tab, plain_list);
			break;
		case 1:	/* new data has been displayed (dark red) */
			if (col_shouldoverride || !((sess->tab_state & TAB_STATE_NEW_MSG)
										|| (sess->tab_state & TAB_STATE_NEW_HILIGHT))) {
				sess->tab_state = TAB_STATE_NEW_DATA;
				chan_set_color (sess->res->tab, newdata_list);
			}

			if (chan_is_collapsed (sess->res->tab)
				&& !((server_sess->tab_state & TAB_STATE_NEW_MSG)
					 || (server_sess->tab_state & TAB_STATE_NEW_HILIGHT))
				&& !(server_sess == current_tab))
			{
				server_sess->tab_state = TAB_STATE_NEW_DATA;
				chan_set_color (chan_get_parent (sess->res->tab), newdata_list);
			}

			break;
		case 2:	/* new message arrived in channel (light red) */
			if (col_shouldoverride || !(sess->tab_state & TAB_STATE_NEW_HILIGHT)) {
				sess->tab_state = TAB_STATE_NEW_MSG;
				chan_set_color (sess->res->tab, newmsg_list);
			}

			if (chan_is_collapsed (sess->res->tab)
				&& !(server_sess->tab_state & TAB_STATE_NEW_HILIGHT)
				&& !(server_sess == current_tab))
			{
				server_sess->tab_state = TAB_STATE_NEW_MSG;
				chan_set_color (chan_get_parent (sess->res->tab), newmsg_list);
			}

			break;
		case 3:	/* your nick has been seen (blue) */
			sess->tab_state = TAB_STATE_NEW_HILIGHT;
			chan_set_color (sess->res->tab, nickseen_list);

			if (chan_is_collapsed (sess->res->tab) && !(server_sess == current_tab))
			{
				server_sess->tab_state = TAB_STATE_NEW_MSG;
				chan_set_color (chan_get_parent (sess->res->tab), nickseen_list);
			}

			break;
		}
		/* the count after the name changes with the colour */
		mg_update_tab_label (sess);
		lastact_update (sess);
		sess->last_tab_state = sess->tab_state; /* For plugins handling future prints */
	}
}

static void
mg_set_myself_away (session_gui *gui, gboolean away)
{
	gtk_label_set_attributes (GTK_LABEL (nx_bin_get_child (gui->nick_label)),
									  away ? away_list : NULL);
}

/* change the little icon to the left of your nickname */

void
mg_set_access_icon (session_gui *gui, GdkPixbuf *pix, gboolean away)
{
	if (gui->op_xpm)
	{
		if (pix == nx_image_get_pixbuf (gui->op_xpm)) /* no change? */
		{
			mg_set_myself_away (gui, away);
			return;
		}

		nx_widget_destroy (gui->op_xpm);
		gui->op_xpm = NULL;
	}

	if (pix && prefs.hex_gui_input_icon)
	{
		gui->op_xpm = nx_image_new_from_pixbuf (pix);
		nx_box_pack_start (gui->nick_box, gui->op_xpm, 0, 0, 0);
		nx_widget_show (gui->op_xpm);
	}

	mg_set_myself_away (gui, away);
}

static void
mg_inputbox_focus (GtkWidget *widget, gpointer userdata)
{
	session_gui *gui = userdata;
	GSList *list;
	session *sess;

	if (gui->is_tab)
		return;

	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (sess->gui == gui)
		{
			current_sess = sess;
			if (!sess->server->server_session)
				sess->server->server_session = sess;
			break;
		}
		list = list->next;
	}
}

void
mg_inputbox_cb (GtkWidget *igad, session_gui *gui)
{
	char *cmd;
	static int ignore = FALSE;
	GSList *list;
	session *sess = NULL;

	if (ignore)
		return;

	cmd = SPELL_ENTRY_GET_TEXT (igad);
	if (cmd[0] == 0)
		return;

	cmd = g_strdup (cmd);

	/* avoid recursive loop */
	ignore = TRUE;
	SPELL_ENTRY_SET_TEXT (igad, "");
	ignore = FALSE;

	/* where did this event come from? */
	if (gui->is_tab)
	{
		sess = current_tab;
	} else
	{
		list = sess_list;
		while (list)
		{
			sess = list->data;
			if (sess->gui == gui)
				break;
			list = list->next;
		}
		if (!list)
			sess = NULL;
	}

	if (sess)
		mg_send_input (sess, igad, cmd);

	g_free (cmd);
}

/* the text a paste confirmation is about, and where it goes */
typedef struct
{
	session *sess;
	GtkWidget *entry;
	char *text;
} paste_ask;

static void
mg_paste_ask_cb (GtkWidget *dialog, int response, paste_ask *p)
{
	nx_widget_destroy (dialog);
	if (is_session (p->sess))
	{
		if (response == GTK_RESPONSE_OK)
			handle_multiline (p->sess, p->text, TRUE, FALSE);
		else if (p->entry)
			/* cancelled: back in the box, to edit or send */
			SPELL_ENTRY_SET_TEXT (p->entry, p->text);
	}
	g_free (p->text);
	g_free (p);
}

/* send what was typed or pasted; more than gui_input_paste_max lines of
   text (not a command) asks first, so a slip of the mouse can't flood */
void
mg_send_input (session *sess, GtkWidget *entry, char *cmd)
{
	int lines = 1;
	char *p, *msg;
	paste_ask *ask;
	GtkWidget *dialog;

	for (p = cmd; *p; p++)
		if (*p == '\n' && p[1] && p[1] != '\n')
			lines++;
	if (!prefs.hex_gui_input_paste_max || lines <= prefs.hex_gui_input_paste_max
		 || cmd[0] == prefs.hex_input_command_char[0])
	{
		handle_multiline (sess, cmd, TRUE, FALSE);
		return;
	}

	ask = g_new0 (paste_ask, 1);
	ask->sess = sess;
	ask->entry = entry;
	ask->text = g_strdup (cmd);
	msg = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE, "Send %d line to %s?", "Send %d lines to %s?", lines),
								  lines, sess->channel[0] ? sess->channel : _("the server"));
	dialog = nx_message_dialog_new (GTK_WINDOW (sess->gui->window), TRUE, NX_MSG_QUESTION,
											  NX_BUTTONS_OK_CANCEL, FALSE, msg);
	g_free (msg);
	g_signal_connect (dialog, "response", G_CALLBACK (mg_paste_ask_cb), ask);
	nx_widget_show (dialog);
	/* Enter answers OK (Escape cancels): focus it, not Cancel */
	if (gtk_window_get_default_widget (GTK_WINDOW (dialog)))
		gtk_widget_grab_focus (gtk_window_get_default_widget (GTK_WINDOW (dialog)));
}

static gboolean
mg_spellcheck_cb (SexySpellEntry *entry, gchar *word, gpointer data)
{
	/* This can cause freezes on long words, nicks arn't very long anyway. */
	if (strlen (word) > 20)
		return TRUE;

	/* Ignore anything we think is a valid url */
	if (url_check_word (word) != 0)
		return FALSE;

	return TRUE;
}

#if 0
static gboolean
has_key (char *modes)
{
	if (!modes)
		return FALSE;
	/* this is a crude check, but "-k" can't exist, so it works. */
	while (*modes)
	{
		if (*modes == 'k')
			return TRUE;
		if (*modes == ' ')
			return FALSE;
		modes++;
	}
	return FALSE;
}
#endif

void
fe_set_title (session *sess)
{
	char tbuf[512];
	int type;

	if (sess->gui->is_tab && sess != current_tab)
		return;

	type = sess->type;

	if (sess->server->connected == FALSE && sess->type != SESS_DIALOG)
		goto def;

	switch (type)
	{
	case SESS_DIALOG:
		g_snprintf (tbuf, sizeof (tbuf), "%s %s @ %s - %s",
					 _("Dialog with"), sess->channel, server_get_network (sess->server, TRUE),
					 _(DISPLAY_NAME));
		break;
	case SESS_SERVER:
		g_snprintf (tbuf, sizeof (tbuf), "%s%s%s - %s",
					 prefs.hex_gui_win_nick ? sess->server->nick : "",
					 prefs.hex_gui_win_nick ? " @ " : "", server_get_network (sess->server, TRUE),
					 _(DISPLAY_NAME));
		break;
	case SESS_CHANNEL:
		/* don't display keys in the titlebar */
			g_snprintf (tbuf, sizeof (tbuf),
					 "%s%s%s / %s%s%s%s - %s",
					 prefs.hex_gui_win_nick ? sess->server->nick : "",
					 prefs.hex_gui_win_nick ? " @ " : "",
					 server_get_network (sess->server, TRUE), sess->channel,
					 prefs.hex_gui_win_modes && sess->current_modes ? " (" : "",
					 prefs.hex_gui_win_modes && sess->current_modes ? sess->current_modes : "",
					 prefs.hex_gui_win_modes && sess->current_modes ? ")" : "",
					 _(DISPLAY_NAME));
		if (prefs.hex_gui_win_ucount)
		{
			g_snprintf (tbuf + strlen (tbuf), 9, " (%d)", sess->total);
		}
		break;
	case SESS_NOTICES:
	case SESS_SNOTICES:
		g_snprintf (tbuf, sizeof (tbuf), "%s%s%s (notices) - %s",
					 prefs.hex_gui_win_nick ? sess->server->nick : "",
					 prefs.hex_gui_win_nick ? " @ " : "", server_get_network (sess->server, TRUE),
					 _(DISPLAY_NAME));
		break;
	default:
	def:
		g_snprintf (tbuf, sizeof (tbuf), _(DISPLAY_NAME));
		gtk_window_set_title (GTK_WINDOW (sess->gui->window), tbuf);
		return;
	}

	gtk_window_set_title (GTK_WINDOW (sess->gui->window), tbuf);
}

static void
mg_windowstate_cb (GtkWindow *wid, nx_window_state changed, nx_window_state now, gpointer userdata)
{
	if ((changed & NX_WIN_ICONIFIED) &&
		 (now & NX_WIN_ICONIFIED) &&
		 prefs.hex_gui_tray_minimize && prefs.hex_gui_tray &&
		 gtkutil_tray_icon_supported (wid))
	{
		tray_toggle_visibility (TRUE);
		nx_window_deiconify (wid);
	}

	prefs.hex_gui_win_state = 0;
	if (now & NX_WIN_MAXIMIZED)
		prefs.hex_gui_win_state = 1;

	prefs.hex_gui_win_fullscreen = 0;
	if (now & NX_WIN_FULLSCREEN)
		prefs.hex_gui_win_fullscreen = 1;

	menu_set_fullscreen (current_sess->gui, prefs.hex_gui_win_fullscreen);
}

static void
mg_configure_cb (GtkWindow *wid, gpointer data)
{
	session *sess = data;

	if (sess == NULL)			/* for the main_window */
	{
		if (mg_gui)
		{
			if (prefs.hex_gui_win_save && !prefs.hex_gui_win_state && !prefs.hex_gui_win_fullscreen)
			{
				sess = current_sess;
				gtkutil_window_get_position (GTK_WINDOW (wid), &prefs.hex_gui_win_left,
												 &prefs.hex_gui_win_top);
				nx_window_get_size (wid, &prefs.hex_gui_win_width,
											&prefs.hex_gui_win_height);
			}
		}
	}

	if (sess)
	{
		if (sess->type == SESS_DIALOG && prefs.hex_gui_win_save)
		{
			gtkutil_window_get_position (GTK_WINDOW (wid), &prefs.hex_gui_dialog_left,
											 &prefs.hex_gui_dialog_top);
			nx_window_get_size (wid, &prefs.hex_gui_dialog_width,
										&prefs.hex_gui_dialog_height);
		}
	}
}

/* move to a non-irc tab */

static void
mg_show_generic_tab (GtkWidget *box)
{
	int num;
	GtkWidget *f = NULL;

	if (current_sess && gtk_widget_has_focus (current_sess->gui->input_box))
		f = current_sess->gui->input_box;

	num = gtk_notebook_page_num (GTK_NOTEBOOK (mg_gui->note_book), box);
	gtk_notebook_set_current_page (GTK_NOTEBOOK (mg_gui->note_book), num);
	nx_view_set_store (mg_gui->user_tree, NULL);
	gtk_window_set_title (GTK_WINDOW (mg_gui->window),
								 g_object_get_data (G_OBJECT (box), "title"));
	gtk_widget_set_sensitive (mg_gui->menu, FALSE);

	if (f)
		gtk_widget_grab_focus (f);
}

/* a channel has been focused */

static void
mg_focus (session *sess)
{
	if (sess->gui->is_tab)
		current_tab = sess;
	current_sess = sess;

	/* dirty trick to avoid auto-selection */
	SPELL_ENTRY_SET_EDITABLE (sess->gui->input_box, FALSE);
	gtk_widget_grab_focus (sess->gui->input_box);
	SPELL_ENTRY_SET_EDITABLE (sess->gui->input_box, TRUE);

	sess->server->front_session = sess;

	if (sess->server->server_session != NULL)
	{
		if (sess->server->server_session->type != SESS_SERVER)
			sess->server->server_session = sess;
	} else
	{
		sess->server->server_session = sess;
	}

	/* when called via mg_changui_new, is_tab might be true, but
		sess->res->tab is still NULL. */
	if (sess->res->tab)
		fe_set_tab_color (sess, FE_COLOR_NONE);
}

/* the connecting indicator: a block bouncing in a framed trough, square
   like the classic progress bars, in the theme's selection colour (CSS
   color on #nexchat-connecting) */
static void
mg_progressbar_paint (GtkWidget *bar, cairo_t *cr, int w, int h)
{
	double pos = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (bar), "nx-pos")) / 1000.0;
	double bw = (w - 4) * 0.25, x;
	GdkRGBA accent, frame;

	gtk_widget_get_color (bar, &accent);
	frame = accent;
	frame.alpha *= 0.5;

	/* 1px frame */
	gdk_cairo_set_source_rgba (cr, &frame);
	cairo_set_line_width (cr, 1);
	cairo_rectangle (cr, 0.5, 0.5, w - 1, h - 1);
	cairo_stroke (cr);

	/* the block, 1px inside the frame */
	x = 2 + pos * (w - 4 - bw);
	gdk_cairo_set_source_rgba (cr, &accent);
	cairo_rectangle (cr, x, 2, bw, h - 4);
	cairo_fill (cr);
}

static void
mg_progressbar_draw (GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer unused)
{
	mg_progressbar_paint (GTK_WIDGET (da), cr, w, h);
}

static int
mg_progressbar_update (GtkWidget *bar)
{
	/* 0..1000 forward, then 1000..2000 back, like GTK2's activity mode */
	int step = (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (bar), "nx-step")) + 50) % 2000;

	g_object_set_data (G_OBJECT (bar), "nx-step", GINT_TO_POINTER (step));
	g_object_set_data (G_OBJECT (bar), "nx-pos", GINT_TO_POINTER (step < 1000 ? step : 2000 - step));
	gtk_widget_queue_draw (bar);
	return 1;
}

void
mg_progressbar_create (session_gui *gui)
{
	/* right end of the input row, as tall as the entry */
	gui->bar = gtk_drawing_area_new ();
	gtk_widget_set_name (gui->bar, "nexchat-connecting");
	gtk_widget_set_size_request (gui->bar, 120, -1);
	gtk_widget_set_valign (gui->bar, GTK_ALIGN_FILL);
	gtk_widget_set_margin_start (gui->bar, 4);
	gtk_widget_set_margin_top (gui->bar, 2);
	gtk_widget_set_margin_bottom (gui->bar, 2);
	gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (gui->bar), mg_progressbar_draw, NULL, NULL);
	gtk_widget_set_tooltip_text (gui->bar, _("Connecting" ELLIPSIS));
	nx_box_pack_end (gtk_widget_get_parent (gui->input_box), gui->bar, 0, 0, 0);
	nx_widget_show (gui->bar);
	gui->bartag = fe_timeout_add (50, mg_progressbar_update, gui->bar);
}

void
mg_progressbar_destroy (session_gui *gui)
{
	fe_timeout_remove (gui->bartag);
	nx_widget_destroy (gui->bar);
	gui->bar = 0;
	gui->bartag = 0;
}

/* switching tabs away from this one, so remember some info about it! */

static void
mg_unpopulate (session *sess)
{
	restore_gui *res;
	session_gui *gui;
	int i;

	gui = sess->gui;
	res = sess->res;

	res->input_text = g_strdup (SPELL_ENTRY_GET_TEXT (gui->input_box));
	res->topic_text = g_strdup (nx_entry_get_text (gui->topic_entry));
	res->limit_text = g_strdup (nx_entry_get_text (gui->limit_entry));
	res->key_text = g_strdup (nx_entry_get_text (gui->key_entry));
	if (gui->laginfo)
		res->lag_text = g_strdup (gtk_label_get_text (GTK_LABEL (gui->laginfo)));
	if (gui->throttleinfo)
		res->queue_text = g_strdup (gtk_label_get_text (GTK_LABEL (gui->throttleinfo)));

	for (i = 0; i < NUM_FLAG_WIDS - 1; i++)
		res->flag_wid_state[i] = nx_toggle_get_active ((GtkWidget *) (gui->flag_wid[i]));

	res->old_ul_value = userlist_get_value (gui->user_tree);
	if (gui->lagometer)
		res->lag_value = gtk_progress_bar_get_fraction (
													GTK_PROGRESS_BAR (gui->lagometer));
	if (gui->throttlemeter)
		res->queue_value = gtk_progress_bar_get_fraction (
													GTK_PROGRESS_BAR (gui->throttlemeter));

	if (gui->bar)
	{
		res->c_graph = TRUE;	/* still have a graph, just not visible now */
		mg_progressbar_destroy (gui);
	}
}

static void
mg_restore_label (GtkWidget *label, char **text)
{
	if (!label)
		return;

	if (*text)
	{
		gtk_label_set_text (GTK_LABEL (label), *text);
		g_free (*text);
		*text = NULL;
	} else
	{
		gtk_label_set_text (GTK_LABEL (label), "");
	}
}

static void
mg_restore_entry (GtkWidget *entry, char **text)
{
	if (*text)
	{
		nx_entry_set_text (entry, *text);
		g_free (*text);
		*text = NULL;
	} else
	{
		nx_entry_set_text (entry, "");
	}
	gtk_editable_set_position (GTK_EDITABLE (entry), -1);
}

static void
mg_restore_speller (GtkWidget *entry, char **text)
{
	if (*text)
	{
		SPELL_ENTRY_SET_TEXT (entry, *text);
		g_free (*text);
		*text = NULL;
	} else
	{
		SPELL_ENTRY_SET_TEXT (entry, "");
	}
	SPELL_ENTRY_SET_POS (entry, -1);
}

void
mg_set_topic_tip (session *sess)
{
	char *text;

	switch (sess->type)
	{
	case SESS_CHANNEL:
		if (sess->topic)
		{
			text = g_strdup_printf (_("Topic for %s is: %s"), sess->channel,
						 sess->topic);
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, text);
			g_free (text);
		} else
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, _("No topic is set"));
		break;
	default:
		if (nx_entry_get_text (sess->gui->topic_entry) &&
			 nx_entry_get_text (sess->gui->topic_entry)[0])
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, (char *)nx_entry_get_text (sess->gui->topic_entry));
		else
			gtk_widget_set_tooltip_text (sess->gui->topic_entry, NULL);
	}
}

static void
mg_hide_empty_pane (GtkPaned *pane)
{
	if ((nx_paned_get_child1 (pane) == NULL || !gtk_widget_get_visible (nx_paned_get_child1 (pane))) &&
		(nx_paned_get_child2 (pane) == NULL || !gtk_widget_get_visible (nx_paned_get_child2 (pane))))
	{
		nx_widget_hide (GTK_WIDGET (pane));
		return;
	}

	nx_widget_show (GTK_WIDGET (pane));
}

static void
mg_hide_empty_boxes (session_gui *gui)
{
	/* hide empty vpanes - so the handle is not shown */
	mg_hide_empty_pane ((GtkPaned*)gui->vpane_right);
	mg_hide_empty_pane ((GtkPaned*)gui->vpane_left);
}

static void
mg_userlist_showhide (session *sess, int show)
{
	session_gui *gui = sess->gui;
	int handle_size;
	int right_size;

	right_size = MAX (prefs.hex_gui_pane_right_size, prefs.hex_gui_pane_right_size_min);

	if (show)
	{
		nx_widget_show (gui->user_box);
		gui->ul_hidden = 0;

		handle_size = nx_paned_handle_size (GTK_WIDGET (gui->hpane_right));
		gtk_paned_set_position (GTK_PANED (gui->hpane_right), nx_widget_get_allocated_width (gui->hpane_right) - (right_size + handle_size));
	}
	else
	{
		nx_widget_hide (gui->user_box);
		gui->ul_hidden = 1;
	}

	mg_hide_empty_boxes (gui);
}

static gboolean
mg_is_userlist_and_tree_combined (void)
{
	if (prefs.hex_gui_tab_pos == POS_TOPLEFT && prefs.hex_gui_ulist_pos == POS_BOTTOMLEFT)
		return TRUE;
	if (prefs.hex_gui_tab_pos == POS_BOTTOMLEFT && prefs.hex_gui_ulist_pos == POS_TOPLEFT)
		return TRUE;

	if (prefs.hex_gui_tab_pos == POS_TOPRIGHT && prefs.hex_gui_ulist_pos == POS_BOTTOMRIGHT)
		return TRUE;
	if (prefs.hex_gui_tab_pos == POS_BOTTOMRIGHT && prefs.hex_gui_ulist_pos == POS_TOPRIGHT)
		return TRUE;

	return FALSE;
}

/* decide if the userlist should be shown or hidden for this tab */

void
mg_decide_userlist (session *sess, gboolean switch_to_current)
{
	/* when called from menu.c we need this */
	if (sess->gui == mg_gui && switch_to_current)
		sess = current_tab;

	if (prefs.hex_gui_ulist_hide)
	{
		mg_userlist_showhide (sess, FALSE);
		return;
	}

	switch (sess->type)
	{
	case SESS_SERVER:
	case SESS_DIALOG:
	case SESS_NOTICES:
	case SESS_SNOTICES:
		if (mg_is_userlist_and_tree_combined ())
			mg_userlist_showhide (sess, TRUE);	/* show */
		else
			mg_userlist_showhide (sess, FALSE);	/* hide */
		break;
	default:		
		mg_userlist_showhide (sess, TRUE);	/* show */
	}
}

static int ul_tag = 0;

static gboolean
mg_populate_userlist (session *sess)
{
	if (!sess)
		sess = current_tab;

	if (is_session (sess))
	{
		if (sess->type == SESS_DIALOG)
			mg_set_access_icon (sess->gui, NULL, sess->server->is_away);
		else
			mg_set_access_icon (sess->gui, get_user_icon (sess->server, sess->me), sess->server->is_away);
		userlist_show (sess);
		userlist_set_value (sess->gui->user_tree, sess->res->old_ul_value);
	}

	ul_tag = 0;
	return 0;
}

/* fill the irc tab with a new channel */

static void
mg_populate (session *sess)
{
	session_gui *gui = sess->gui;
	restore_gui *res = sess->res;
	int i, render = TRUE;
	guint16 vis = gui->ul_hidden;

	switch (sess->type)
	{
	case SESS_DIALOG:
		/* show the dialog buttons */
		nx_widget_show (gui->dialogbutton_box);
		/* hide the chan-mode buttons */
		nx_widget_hide (gui->topicbutton_box);
		/* hide the userlist */
		mg_decide_userlist (sess, FALSE);
		/* shouldn't edit the topic */
		gtk_editable_set_editable (GTK_EDITABLE (gui->topic_entry), FALSE);
		/* might be hidden from server tab */
		if (prefs.hex_gui_topicbar)
			nx_widget_show (gui->topic_bar);
		break;
	case SESS_SERVER:
		if (prefs.hex_gui_mode_buttons)
			nx_widget_show (gui->topicbutton_box);
		/* hide the dialog buttons */
		nx_widget_hide (gui->dialogbutton_box);
		/* hide the userlist */
		mg_decide_userlist (sess, FALSE);
		/* servers don't have topics */
		nx_widget_hide (gui->topic_bar);
		break;
	default:
		/* hide the dialog buttons */
		nx_widget_hide (gui->dialogbutton_box);
		if (prefs.hex_gui_mode_buttons)
			nx_widget_show (gui->topicbutton_box);
		/* show the userlist */
		mg_decide_userlist (sess, FALSE);
		/* let the topic be editted */
		gtk_editable_set_editable (GTK_EDITABLE (gui->topic_entry), TRUE);
		if (prefs.hex_gui_topicbar)
			nx_widget_show (gui->topic_bar);
	}

	/* move to THE irc tab */
	if (gui->is_tab)
		gtk_notebook_set_current_page (GTK_NOTEBOOK (gui->note_book), 0);

	/* xtext size change? Then don't render, wait for the expose caused
      by showing/hidding the userlist */
	if (vis != gui->ul_hidden && nx_widget_get_allocated_width (gui->user_box) > 1)
		render = FALSE;

	gtk_xtext_buffer_show (GTK_XTEXT (gui->xtext), res->buffer, render);

	if (gui->is_tab)
		gtk_widget_set_sensitive (gui->menu, TRUE);

	/* restore all the GtkEntry's */
	mg_restore_entry (gui->topic_entry, &res->topic_text);
	mg_restore_speller (gui->input_box, &res->input_text);
	mg_restore_entry (gui->key_entry, &res->key_text);
	mg_restore_entry (gui->limit_entry, &res->limit_text);
	mg_restore_label (gui->laginfo, &res->lag_text);
	mg_restore_label (gui->throttleinfo, &res->queue_text);

	mg_focus (sess);
	fe_set_title (sess);

	/* this one flickers, so only change if necessary */
	if (strcmp (sess->server->nick, gtk_button_get_label (GTK_BUTTON (gui->nick_label))) != 0)
		gtk_button_set_label (GTK_BUTTON (gui->nick_label), sess->server->nick);

	/* this is slow, so make it a timeout event */
	if (!gui->is_tab)
	{
		mg_populate_userlist (sess);
	} else
	{
		if (ul_tag == 0)
			ul_tag = g_idle_add ((GSourceFunc)mg_populate_userlist, NULL);
	}

	fe_userlist_numbers (sess);

	/* restore all the channel mode buttons */
	ignore_chanmode = TRUE;
	for (i = 0; i < NUM_FLAG_WIDS - 1; i++)
	{
		/* Hide if mode not supported */
		if (sess->server && strchr (sess->server->chanmodes, chan_flags[i]) == NULL)
			nx_widget_hide (sess->gui->flag_wid[i]);
		else
			nx_widget_show (sess->gui->flag_wid[i]);

		/* Update state */
		nx_toggle_set_active ((GtkWidget *) (gui->flag_wid[i]),
									res->flag_wid_state[i]);
	}
	ignore_chanmode = FALSE;

	if (gui->lagometer)
	{
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (gui->lagometer),
												 res->lag_value);
		if (res->lag_tip)
			gtk_widget_set_tooltip_text (gtk_widget_get_parent (sess->gui->lagometer), res->lag_tip);
	}
	if (gui->throttlemeter)
	{
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (gui->throttlemeter),
												 res->queue_value);
		if (res->queue_tip)
			gtk_widget_set_tooltip_text (gtk_widget_get_parent (sess->gui->throttlemeter), res->queue_tip);
	}

	/* did this tab have a connecting graph? restore it.. */
	if (res->c_graph)
	{
		res->c_graph = FALSE;
		mg_progressbar_create (gui);
	}

	/* menu items */
	menu_set_away (gui, sess->server->is_away);
	nx_item_set_sensitive (gui->menu_item[MENU_ID_AWAY], sess->server->connected);
	nx_item_set_sensitive (gui->menu_item[MENU_ID_JOIN], sess->server->end_of_motd);
	nx_item_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT],
									  sess->server->connected || sess->server->recondelay_tag);
	menu_debug_dump (gui->main_menu);

	mg_set_topic_tip (sess);

	plugin_emit_dummy_print (sess, "Focus Tab");
}

void
mg_bring_tofront_sess (session *sess)	/* IRC tab or window */
{
	if (sess->gui->is_tab)
		chan_focus (sess->res->tab);
	else
		gtk_window_present (GTK_WINDOW (sess->gui->window));
}

void
mg_bring_tofront (GtkWidget *vbox)	/* non-IRC tab or window */
{
	chan *ch;

	ch = g_object_get_data (G_OBJECT (vbox), "ch");
	if (ch)
		chan_focus (ch);
	else
		gtk_window_present (GTK_WINDOW (nx_widget_get_toplevel (vbox)));
}

void
mg_switch_page (int relative, int num)
{
	if (mg_gui)
		chanview_move_focus (mg_gui->chanview, relative, num);
}

/* a toplevel IRC window was destroyed */

static void
mg_topdestroy_cb (GtkWidget *win, session *sess)
{
/*	printf("enter mg_topdestroy. sess %p was destroyed\n", sess);*/
	session_free (sess);	/* tell hexchat.c about it */
}

/* cleanup an IRC tab */

static void
mg_ircdestroy (session *sess)
{
	GSList *list;

	session_free (sess);	/* tell hexchat.c about it */

	if (mg_gui == NULL)
	{
/*		puts("-> mg_gui is already NULL");*/
		return;
	}

	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (sess->gui->is_tab)
		{
/*			puts("-> some tabs still remain");*/
			return;
		}
		list = list->next;
	}

/*	puts("-> no tabs left, killing main tabwindow");*/
	nx_widget_destroy (mg_gui->window);
	active_tab = NULL;
	mg_gui = NULL;
	parent_window = NULL;
}

static void
mg_tab_close_cb (GtkWidget *dialog, gint arg1, session *sess)
{
	GSList *list, *next;

	nx_widget_destroy (dialog);
	if (arg1 == GTK_RESPONSE_OK && is_session (sess))
	{
		/* force it NOT to send individual PARTs */
		sess->server->sent_quit = TRUE;

		for (list = sess_list; list;)
		{
			next = list->next;
			if (((session *)list->data)->server == sess->server &&
				 ((session *)list->data) != sess)
				fe_close_window ((session *)list->data);
			list = next;
		}

		/* just send one QUIT - better for BNCs */
		sess->server->sent_quit = FALSE;
		fe_close_window (sess);
	}
}

void
mg_tab_close (session *sess)
{
	GtkWidget *dialog;
	GSList *list;
	int i;

	if (chan_remove (sess->res->tab, FALSE))
	{
		sess->res->tab = NULL;
		mg_ircdestroy (sess);
	}
	else
	{
		for (i = 0, list = sess_list; list; list = list->next)
		{
			session *s = (session*)list->data;
			if (s->server == sess->server && (s->type == SESS_CHANNEL || s->type == SESS_DIALOG))
				i++;
		}
		{
			char *q = g_strdup_printf (_("This server still has %d channels or dialogs associated with it. "
												  "Close them all?"), i);
			dialog = nx_message_dialog_new (GTK_WINDOW (parent_window), FALSE,
													  NX_MSG_WARNING, NX_BUTTONS_OK_CANCEL, FALSE, q);
			g_free (q);
		}
		g_signal_connect (G_OBJECT (dialog), "response",
								G_CALLBACK (mg_tab_close_cb), sess);
		if (prefs.hex_gui_tab_layout)
		{
			nx_window_set_position_mouse (dialog);
		}
		else
		{
			nx_window_set_position_center (dialog);		
		}
		nx_widget_show (dialog);
	}
}

/* an item with a stock icon (its label is a mnemonic) */
void
mg_create_icon_item (char *label, char *stock, nx_menu *menu,
							nx_item_cb callback, void *userdata)
{
	nx_menu_add_item (menu, NX_MENU_APPEND, label, NX_ITEM_MNEMONIC, stock,
							callback, userdata);
}

static int
mg_count_networks (void)
{
	int cons = 0;
	GSList *list;

	for (list = serv_list; list; list = list->next)
	{
		if (((server *)list->data)->connected)
			cons++;
	}
	return cons;
}

static int
mg_count_dccs (void)
{
	GSList *list;
	struct DCC *dcc;
	int dccs = 0;

	list = dcc_list;
	while (list)
	{
		dcc = list->data;
		if ((dcc->type == TYPE_SEND || dcc->type == TYPE_RECV) &&
			 dcc->dccstat == STAT_ACTIVE)
			dccs++;
		list = list->next;
	}

	return dccs;
}

/* response handler for the quit dialog; same outcomes the gtk_dialog_run
   switch had, but the dialog no longer blocks the main loop */
static void
mg_quit_dialog_response (GtkWidget *dlg, int response, GtkWidget *checkbutton1)
{
	gboolean dont_ask = nx_toggle_get_active ((GtkWidget *) (checkbutton1));

	nx_widget_destroy (GTK_WIDGET (dlg));	/* clears the static via "destroy" */

	switch (response)
	{
	case 0:
		if (dont_ask)
			prefs.hex_gui_quit_dialog = 0;
		hexchat_exit ();
		break;
	case 1: /* minimize to tray */
		if (dont_ask)
		{
			prefs.hex_gui_tray_close = 1;
			/*prefs.hex_gui_quit_dialog = 0;*/
		}
		/* force tray icon ON, if not already */
		if (!prefs.hex_gui_tray)
		{
			prefs.hex_gui_tray = 1;
			tray_apply_setup ();
		}
		tray_toggle_visibility (TRUE);
		break;
	/* Cancel, Escape, window closed: nothing, as before */
	}
}

void
mg_open_quit_dialog (gboolean minimize_button)
{
	static GtkWidget *dialog = NULL;
	GtkWidget *dialog_vbox1;
	GtkWidget *table1;
	GtkWidget *image;
	GtkWidget *checkbutton1;
	GtkWidget *label;
	GtkWidget *button;
	char *text, *connecttext;
	int cons;
	int dccs;

	if (dialog)
	{
		gtk_window_present (GTK_WINDOW (dialog));
		return;
	}

	dccs = mg_count_dccs ();
	cons = mg_count_networks ();
	if (dccs + cons == 0 || !prefs.hex_gui_quit_dialog)
	{
		hexchat_exit ();
		return;
	}

	dialog = nx_dialog_new (NULL, NULL, FALSE);
	nx_set_border_width (dialog, 6);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Quit nexchat?"));
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	dialog_vbox1 = nx_dialog_get_content_area (NX_DIALOG_CAST (dialog));
	nx_widget_show (dialog_vbox1);

	table1 = gtk_grid_new ();
	nx_widget_show (table1);
	nx_box_pack_start (dialog_vbox1, table1, TRUE, TRUE, 0);
	nx_set_border_width (table1, 6);
	gtk_grid_set_row_spacing (GTK_GRID (table1), 12);
	gtk_grid_set_column_spacing (GTK_GRID (table1), 12);

	image = gtkutil_image_new_from_stock ("gtk-dialog-warning", NX_ICON_DIALOG);
	nx_widget_show (image);
	gtkutil_grid_attach (table1, image, 0, 1, 0, 1,
							(GU_FILL),
							(GU_FILL), 0, 0);

	checkbutton1 = gtk_check_button_new_with_mnemonic (_("Don't ask next time."));
	nx_widget_show (checkbutton1);
	gtkutil_grid_attach (table1, checkbutton1, 0, 2, 1, 2,
							(GU_EXPAND | GU_FILL),
							(0), 0, 4);

	connecttext = g_strdup_printf (_("You are connected to %i IRC networks."), cons);
	text = g_strdup_printf ("<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s\n%s",
								_("Are you sure you want to quit?"),
								cons ? connecttext : "",
								dccs ? _("Some file transfers are still active.") : "");
	g_free (connecttext);
	label = gtk_label_new (text);
	g_free (text);
	nx_widget_show (label);
	gtkutil_grid_attach (table1, label, 1, 2, 0, 1,
							(GU_EXPAND | GU_SHRINK | GU_FILL),
							(GU_EXPAND | GU_SHRINK), 0, 0);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);

	if (minimize_button && gtkutil_tray_icon_supported (GTK_WINDOW(dialog)))
	{
		button = gtk_button_new_with_mnemonic (_("_Minimize to Tray"));
		nx_widget_show (button);
		nx_dialog_add_action_widget (NX_DIALOG_CAST (dialog), button, 1);
	}

	button = gtkutil_button_new_from_stock ("gtk-cancel");
	nx_widget_show (button);
	nx_dialog_add_action_widget (NX_DIALOG_CAST (dialog), button,
										  GTK_RESPONSE_CANCEL);
	gtk_widget_grab_focus (button);

	button = gtkutil_button_new_from_stock ("gtk-quit");
	nx_widget_show (button);
	nx_dialog_add_action_widget (NX_DIALOG_CAST (dialog), button, 0);

	/* gtk_dialog_run made it modal for its duration */
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (mg_quit_dialog_response), checkbutton1);
	nx_widget_show (dialog);
}

void
mg_close_sess (session *sess)
{
	if (sess_list->next == NULL)
	{
		mg_open_quit_dialog (FALSE);
		return;
	}

	fe_close_window (sess);
}

static int
mg_chan_remove (chan *ch)
{
	/* remove the tab from chanview */
	chan_remove (ch, TRUE);
	/* any tabs left? */
	if (chanview_get_size (mg_gui->chanview) < 1)
	{
		/* if not, destroy the main tab window */
		nx_widget_destroy (mg_gui->window);
		current_tab = NULL;
		active_tab = NULL;
		mg_gui = NULL;
		parent_window = NULL;
		return TRUE;
	}
	return FALSE;
}

/* destroy non-irc tab/window */

static void
mg_close_gen (chan *ch, GtkWidget *box)
{
	if (!ch)
		ch = g_object_get_data (G_OBJECT (box), "ch");
	if (ch)
	{
		/* remove from notebook */
		nx_widget_destroy (box);
		/* remove the tab from chanview */
		mg_chan_remove (ch);
	} else
	{
		nx_widget_destroy (nx_widget_get_toplevel (box));
	}
}

/* the "X" close button has been pressed (tab-view) */

static void
mg_xbutton_cb (chanview *cv, chan *ch, int tag, gpointer userdata)
{
	if (tag == TAG_IRC)	/* irc tab */
		mg_close_sess (userdata);
	else						/* non-irc utility tab */
		mg_close_gen (ch, userdata);
}

static void
mg_link_gentab (chan *ch, GtkWidget *box)
{
	int num;
	GtkWidget *win;

	g_object_ref (box);

	num = gtk_notebook_page_num (GTK_NOTEBOOK (mg_gui->note_book), box);
	gtk_notebook_remove_page (GTK_NOTEBOOK (mg_gui->note_book), num);
	mg_chan_remove (ch);

	win = gtkutil_window_new (g_object_get_data (G_OBJECT (box), "title"), "",
									  GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), "w")),
									  GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), "h")),
									  2);
	/* so it doesn't try to chan_remove (there's no tab anymore) */
	g_object_steal_data (G_OBJECT (box), "ch");
	nx_set_border_width (box, 0);
	gtkutil_container_add (GTK_WIDGET (win), box);
	nx_widget_show (win);

	g_object_unref (box);
}

static void
mg_detach_tab_cb (nx_item *item, gpointer ch)
{
	if (chan_get_tag (ch) == TAG_IRC)	/* IRC tab */
	{
		/* userdata is session * */
		mg_link_irctab (chan_get_userdata (ch), 1);
		return;
	}

	/* userdata is GtkWidget * */
	mg_link_gentab (ch, chan_get_userdata (ch));	/* non-IRC tab */
}

static void
mg_destroy_tab_cb (nx_item *item, gpointer ch)
{
	/* treat it just like the X button press */
	mg_xbutton_cb (mg_gui->chanview, ch, chan_get_tag (ch), chan_get_userdata (ch));
}

static void
mg_color_insert (nx_item *item, gpointer userdata)
{
	char buf[32];
	char *text;
	int num = GPOINTER_TO_INT (userdata);

	if (num > 99)
	{
		switch (num)
		{
		case 100:
			text = "\002"; break;
		case 101:
			text = "\037"; break;
		case 102:
			text = "\035"; break;
		case 103:
			text = "\036"; break;
		default:
			text = "\017"; break;
		}
		key_action_insert (current_sess->gui->input_box, 0, text, 0, 0);
	} else
	{
		sprintf (buf, "\003%02d", num);
		key_action_insert (current_sess->gui->input_box, 0, buf, 0, 0);
	}
}

static void
mg_markup_item (nx_menu *menu, char *text, int arg)
{
	nx_menu_add_item (menu, NX_MENU_APPEND, text, NX_ITEM_MARKUP, NULL,
							mg_color_insert, GINT_TO_POINTER (arg));
}

nx_menu *
mg_submenu (nx_menu *menu, char *text)
{
	return nx_menu_add_submenu (menu, NX_MENU_APPEND, text, NX_ITEM_MNEMONIC, NULL);
}

static void
mg_create_color_menu (nx_menu *menu, session *sess)
{
	nx_menu *submenu;
	nx_menu *subsubmenu;
	char buf[256];
	int i;

	submenu = mg_submenu (menu, _("Insert Attribute or Color Code"));

	mg_markup_item (submenu, _("<b>Bold</b>"), 100);
	mg_markup_item (submenu, _("<u>Underline</u>"), 101);
	mg_markup_item (submenu, _("<i>Italic</i>"), 102);
	mg_markup_item (submenu, _("<s>Strikethrough</s>"), 103);
	mg_markup_item (submenu, _("Normal"), 999);

	subsubmenu = mg_submenu (submenu, _("Colors 0-7"));

	for (i = 0; i < 8; i++)
	{
		sprintf (buf, "<tt><sup>%02d</sup> <span background=\"#%02x%02x%02x\">"
					"   </span></tt>",
				i, C8R (colors[i]), C8G (colors[i]), C8B (colors[i]));
		mg_markup_item (subsubmenu, buf, i);
	}

	subsubmenu = mg_submenu (submenu, _("Colors 8-15"));

	for (i = 8; i < 16; i++)
	{
		sprintf (buf, "<tt><sup>%02d</sup> <span background=\"#%02x%02x%02x\">"
					"   </span></tt>",
				i, C8R (colors[i]), C8G (colors[i]), C8B (colors[i]));
		mg_markup_item (subsubmenu, buf, i);
	}
}

static void
mg_set_guint8 (nx_item *item, gpointer setting)
{
	session *sess = current_sess;
	guint8 logging = sess->text_logging;

	*(guint8 *) setting = SET_OFF;
	if (nx_item_get_active (item))
		*(guint8 *) setting = SET_ON;

	/* has the logging setting changed? */
	if (logging != sess->text_logging)
		log_open_or_close (sess);

	chanopt_save (sess);
	chanopt_save_all (FALSE);
}

static void
mg_perchan_menu_item (char *label, nx_menu *menu, guint8 *setting, guint global)
{
	guint8 initial_value = *setting;

	/* if it's using global value, use that as initial state */
	if (initial_value == SET_DEFAULT)
		initial_value = global;

	menu_toggle_item (label, menu, mg_set_guint8, setting, initial_value);
}

static void
mg_create_perchannelmenu (session *sess, nx_menu *menu)
{
	nx_menu *submenu;

	submenu = menu_quick_sub (_("_Settings"), menu, NULL, XCMENU_MNEMONIC, -1);

	mg_perchan_menu_item (_("_Log to Disk"), submenu, &sess->text_logging, prefs.hex_irc_logging);
	mg_perchan_menu_item (_("_Reload Scrollback"), submenu, &sess->text_scrollback, prefs.hex_text_replay);
	if (sess->type == SESS_CHANNEL)
	{
		mg_perchan_menu_item (_("Strip _Colors"), submenu, &sess->text_strip, prefs.hex_text_stripcolor_msg);
		mg_perchan_menu_item (_("_Hide Join/Part Messages"), submenu, &sess->text_hidejoinpart, prefs.hex_irc_conf_mode);
	}
}

static void
mg_create_alertmenu (session *sess, nx_menu *menu)
{
	nx_menu *submenu;
	int hex_balloon, hex_beep, hex_tray, hex_flash;


	switch (sess->type) {
		case SESS_DIALOG:
			hex_balloon = prefs.hex_input_balloon_priv;
			hex_beep = prefs.hex_input_beep_priv;
			hex_tray = prefs.hex_input_tray_priv;
			hex_flash = prefs.hex_input_flash_priv;
			break;
		default:
			hex_balloon = prefs.hex_input_balloon_chans;
			hex_beep = prefs.hex_input_beep_chans;
			hex_tray = prefs.hex_input_tray_chans;
			hex_flash = prefs.hex_input_flash_chans;
	}

	submenu = menu_quick_sub(_("_Extra Alerts"), menu, NULL, XCMENU_MNEMONIC, -1);

	mg_perchan_menu_item(_("Show Notifications"), submenu, &sess->alert_balloon, hex_balloon);

	mg_perchan_menu_item(_("Beep on _Message"), submenu, &sess->alert_beep, hex_beep);

	mg_perchan_menu_item(_("Blink Tray _Icon"), submenu, &sess->alert_tray, hex_tray);

	mg_perchan_menu_item(_("Blink Task _Bar"), submenu, &sess->alert_taskbar, hex_flash);
}

static void
mg_create_tabmenu (session *sess, GdkEvent *event, chan *ch)
{
	nx_menu *menu;
	char buf[256];

	menu = nx_menu_new ();

	if (sess)
	{
		char *name = g_markup_escape_text (sess->channel[0] ? sess->channel : _("<none>"), -1);
		g_snprintf (buf, sizeof (buf), "<span foreground=\"#3344cc\"><b>%s</b></span>", name);
		g_free (name);

		nx_menu_add_item (menu, NX_MENU_APPEND, buf, NX_ITEM_MARKUP, NULL, NULL, NULL);

		/* separator */
		menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);

		/* per-channel alerts */
		mg_create_alertmenu (sess, menu);

		/* per-channel settings */
		mg_create_perchannelmenu (sess, menu);

		/* separator */
		menu_quick_item (0, 0, menu, XCMENU_SHADED, 0, 0);

		if (sess->type == SESS_CHANNEL)
			menu_addfavoritemenu (sess->server, menu, sess->channel, TRUE);
		else if (sess->type == SESS_SERVER)
			menu_addconnectmenu (sess->server, menu);
	}

	nx_menu_add_item (menu, NX_MENU_APPEND, _("_Detach"), NX_ITEM_MNEMONIC, NX_STOCK_REDO,
							mg_detach_tab_cb, ch);
	nx_menu_add_item (menu, NX_MENU_APPEND, _("_Close"), NX_ITEM_MNEMONIC, NX_STOCK_CLOSE,
							mg_destroy_tab_cb, ch);
	if (sess && tabmenu_list)
		menu_create (menu, tabmenu_list, sess->channel, FALSE);
	if (sess)
		menu_add_plugin_items (menu, "\x4$TAB", sess->channel);

	{
		GtkWidget *w = nx_current_event_widget ();
		nx_menu_popup (menu, w ? w : parent_window, event, NULL, NULL);
	}
}

static gboolean
mg_tab_contextmenu_cb (chanview *cv, chan *ch, int tag, gpointer ud, guint button, int n_press)
{
	/* middle-click to close a tab */
	if (((prefs.hex_gui_tab_middleclose && button == 2))
		&& n_press == 1)
	{
		mg_xbutton_cb (cv, ch, tag, ud);
		return TRUE;
	}

	if (button != 3)
		return FALSE;

	if (tag == TAG_IRC)
		mg_create_tabmenu (ud, nx_current_event (), ch);
	else
		mg_create_tabmenu (NULL, nx_current_event (), ch);

	return TRUE;
}

void
mg_dnd_drop_file (session *sess, char *target, char *uri)
{
	char *p, *data, *next, *fname;

	p = data = g_strdup (uri);
	while (*p)
	{
		next = strchr (p, '\r');
		if (g_ascii_strncasecmp ("file:", p, 5) == 0)
		{
			if (next)
				*next = 0;
			fname = g_filename_from_uri (p, NULL, NULL);
			if (fname)
			{
				/* dcc_send() expects utf-8 */
				p = g_filename_from_utf8 (fname, -1, 0, 0, 0);
				if (p)
				{
					dcc_send (sess, target, p, prefs.hex_dcc_max_send_cps, 0);
					g_free (p);
				}
				g_free (fname);
			}
		}
		if (!next)
			break;
		p = next + 1;
		if (*p == '\n')
			p++;
	}
	g_free (data);

}

static void
mg_dialog_dnd_drop (GtkWidget * widget, double x, double y, const char *uris,
						  gpointer ud)
{
	if (current_sess->type == SESS_DIALOG)
		/* sess->channel is really the nickname of dialogs */
		mg_dnd_drop_file (current_sess, current_sess->channel, (char *) uris);
}

/* add a tabbed channel */

static void
mg_add_chan (session *sess)
{
	GdkPixbuf *icon;
	char *name = _("<none>");

	if (sess->channel[0])
		name = sess->channel;

	switch (sess->type)
	{
	case SESS_CHANNEL:
		icon = pix_tree_channel;
		break;
	case SESS_SERVER:
		icon = pix_tree_server;
		break;
	default:
		icon = pix_tree_dialog;
	}

	sess->res->tab = chanview_add (sess->gui->chanview, name, sess->server, sess,
											 sess->type == SESS_SERVER ? FALSE : TRUE,
											 TAG_IRC, icon);
	if (plain_list == NULL)
		mg_create_tab_colors ();

	chan_set_color (sess->res->tab, plain_list);

	if (sess->res->buffer == NULL)
	{
		sess->res->buffer = gtk_xtext_buffer_new (GTK_XTEXT (sess->gui->xtext));
		gtk_xtext_set_time_stamp (sess->res->buffer, prefs.hex_stamp_text);
		sess->res->user_model = userlist_create_model (sess);
	}
}

static void
mg_userlist_button (GtkWidget * box, char *label, char *cmd,
						  int a, int b, int c, int d)
{
	GtkWidget *wid = gtk_button_new_with_label (label);
	g_signal_connect (G_OBJECT (wid), "clicked",
							G_CALLBACK (userlist_button_cb), cmd);
	gtkutil_grid_attach (box, wid, a, b, c, d, GU_EXPAND | GU_FILL, GU_EXPAND | GU_FILL, 0, 0);
	show_and_unfocus (wid);
}

static GtkWidget *
mg_create_userlistbuttons (GtkWidget *box)
{
	struct popup *pop;
	GSList *list = button_list;
	int a = 0, b = 0;
	GtkWidget *tab;

	tab = gtk_grid_new ();
	nx_box_pack_end (box, tab, FALSE, FALSE, 0);

	while (list)
	{
		pop = list->data;
		if (pop->cmd[0])
		{
			mg_userlist_button (tab, pop->name, pop->cmd, a, a + 1, b, b + 1);
			a++;
			if (a == 2)
			{
				a = 0;
				b++;
			}
		}
		list = list->next;
	}

	return tab;
}

static void
mg_topic_cb (GtkWidget *entry, gpointer userdata)
{
	session *sess = current_sess;
	char *text;

	if (sess->channel[0] && sess->server->connected && sess->type == SESS_CHANNEL)
	{
		text = (char *)nx_entry_get_text (entry);
		if (text[0] == 0)
			text = NULL;
		sess->server->p_topic (sess->server, sess->channel, text);
	} else
		nx_entry_set_text (entry, "");
	/* restore focus to the input widget, where the next input will most
likely be */
	gtk_widget_grab_focus (sess->gui->input_box);
}

static void
mg_tabwindow_kill_cb (GtkWidget *win, gpointer userdata)
{
	GSList *list, *next;
	session *sess;

/*	puts("enter mg_tabwindow_kill_cb");*/
	hexchat_is_quitting = TRUE;

	/* see if there's any non-tab windows left */
	list = sess_list;
	while (list)
	{
		sess = list->data;
		next = list->next;
		if (!sess->gui->is_tab)
		{
			hexchat_is_quitting = FALSE;
/*			puts("-> will not exit, some toplevel windows left");*/
		} else
		{
			mg_ircdestroy (sess);
		}
		list = next;
	}

	current_tab = NULL;
	active_tab = NULL;
	mg_gui = NULL;
	parent_window = NULL;
}

static GtkWidget *
mg_changui_destroy (session *sess)
{
	GtkWidget *ret = NULL;

	if (sess->gui->is_tab)
	{
		/* avoid calling the "destroy" callback */
		g_signal_handlers_disconnect_by_func (G_OBJECT (sess->gui->window),
														  mg_tabwindow_kill_cb, 0);
		/* remove the tab from the chanview */
		if (!mg_chan_remove (sess->res->tab))
			/* if the window still exists, restore the signal handler */
			g_signal_connect (G_OBJECT (sess->gui->window), "destroy",
									G_CALLBACK (mg_tabwindow_kill_cb), 0);
	} else
	{
		/* avoid calling the "destroy" callback */
		g_signal_handlers_disconnect_by_func (G_OBJECT (sess->gui->window),
														  mg_topdestroy_cb, sess);
		/*nx_widget_destroy (sess->gui->window);*/
		/* don't destroy until the new one is created. Not sure why, but */
		/* it fixes: Gdk-CRITICAL **: gdk_colormap_get_screen: */
		/*           assertion `GDK_IS_COLORMAP (cmap)' failed */
		ret = sess->gui->window;
		g_free (sess->gui);
		sess->gui = NULL;
	}
	return ret;
}

static void
mg_link_irctab (session *sess, int focus)
{
	GtkWidget *win;

	if (sess->gui->is_tab)
	{
		win = mg_changui_destroy (sess);
		mg_changui_new (sess, sess->res, 0, focus);
		mg_populate (sess);
		hexchat_is_quitting = FALSE;
		if (win)
			nx_widget_destroy (win);
		return;
	}

	mg_unpopulate (sess);
	win = mg_changui_destroy (sess);
	mg_changui_new (sess, sess->res, 1, focus);
	/* the buffer is now attached to a different widget */
	((xtext_buffer *)sess->res->buffer)->xtext = (GtkXText *)sess->gui->xtext;
	if (win)
		nx_widget_destroy (win);
}

void
mg_detach (session *sess, int mode)
{
	switch (mode)
	{
	/* detach only */
	case 1:
		if (sess->gui->is_tab)
			mg_link_irctab (sess, 1);
		break;
	/* attach only */
	case 2:
		if (!sess->gui->is_tab)
			mg_link_irctab (sess, 1);
		break;
	/* toggle */
	default:
		mg_link_irctab (sess, 1);
	}
}

static int
check_is_number (char *t)
{
	while (*t)
	{
		if (*t < '0' || *t > '9')
			return FALSE;
		t++;
	}
	return TRUE;
}

static void
mg_change_flag (GtkWidget * wid, session *sess, char flag)
{
	server *serv = sess->server;
	char mode[3];

	mode[1] = flag;
	mode[2] = '\0';
	if (serv->connected && sess->channel[0])
	{
		if (nx_toggle_get_active ((GtkWidget *) (wid)))
			mode[0] = '+';
		else
			mode[0] = '-';
		serv->p_mode (serv, sess->channel, mode);
		serv->p_join_info (serv, sess->channel);
		sess->ignore_mode = TRUE;
		sess->ignore_date = TRUE;
	}
}

static void
flagl_hit (GtkWidget * wid, struct session *sess)
{
	char modes[512];
	const char *limit_str;
	server *serv = sess->server;

	if (nx_toggle_get_active ((GtkWidget *) (wid)))
	{
		if (serv->connected && sess->channel[0])
		{
			limit_str = nx_entry_get_text (sess->gui->limit_entry);
			if (check_is_number ((char *)limit_str) == FALSE)
			{
				fe_message (_("User limit must be a number!\n"), FE_MSG_ERROR);
				nx_entry_set_text (sess->gui->limit_entry, "");
				nx_toggle_set_active ((GtkWidget *) (wid), FALSE);
				return;
			}
			g_snprintf (modes, sizeof (modes), "+l %d", atoi (limit_str));
			serv->p_mode (serv, sess->channel, modes);
			serv->p_join_info (serv, sess->channel);
		}
	} else
		mg_change_flag (wid, sess, 'l');
}

static void
flagk_hit (GtkWidget * wid, struct session *sess)
{
	char modes[512];
	server *serv = sess->server;

	if (serv->connected && sess->channel[0])
	{
		g_snprintf (modes, sizeof (modes), "-k %s", 
			  nx_entry_get_text (sess->gui->key_entry));

		if (nx_toggle_get_active ((GtkWidget *) (wid)))
			modes[0] = '+';

		serv->p_mode (serv, sess->channel, modes);
	}
}

static void
mg_flagbutton_cb (GtkWidget *but, char *flag)
{
	session *sess;
	char mode;

	if (ignore_chanmode)
		return;

	sess = current_sess;
	mode = tolower ((unsigned char) flag[0]);

	switch (mode)
	{
	case 'l':
		flagl_hit (but, sess);
		break;
	case 'k':
		flagk_hit (but, sess);
		break;
	case 'b':
		ignore_chanmode = TRUE;
		nx_toggle_set_active ((GtkWidget *) (sess->gui->flag_b), FALSE);
		ignore_chanmode = FALSE;
		banlist_opengui (sess);
		break;
	default:
		mg_change_flag (but, sess, mode);
	}
}

static GtkWidget *
mg_create_flagbutton (char *tip, GtkWidget *box, char *face)
{
	GtkWidget *btn, *lbl;
	char label_markup[16];

	g_snprintf (label_markup, sizeof(label_markup), "<tt>%s</tt>", face);
	lbl = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL(lbl), label_markup);

	btn = gtk_toggle_button_new ();
	gtk_widget_set_size_request (btn, -1, 0);
	gtk_widget_set_tooltip_text (btn, tip);
	gtkutil_container_add (GTK_WIDGET (btn), lbl);

	nx_box_pack_start (box, btn, 0, 0, 0);
	g_signal_connect (G_OBJECT (btn), "toggled",
							G_CALLBACK (mg_flagbutton_cb), face);
	show_and_unfocus (btn);

	return btn;
}

static void
mg_key_entry_cb (GtkWidget * igad, gpointer userdata)
{
	char modes[512];
	session *sess = current_sess;
	server *serv = sess->server;

	if (serv->connected && sess->channel[0])
	{
		g_snprintf (modes, sizeof (modes), "+k %s",
				nx_entry_get_text (igad));
		serv->p_mode (serv, sess->channel, modes);
		serv->p_join_info (serv, sess->channel);
	}
}

static void
mg_limit_entry_cb (GtkWidget * igad, gpointer userdata)
{
	char modes[512];
	session *sess = current_sess;
	server *serv = sess->server;

	if (serv->connected && sess->channel[0])
	{
		if (check_is_number ((char *)nx_entry_get_text (igad)) == FALSE)
		{
			nx_entry_set_text (igad, "");
			fe_message (_("User limit must be a number!\n"), FE_MSG_ERROR);
			nx_toggle_set_active ((GtkWidget *) (sess->gui->flag_l), FALSE);
			return;
		}
		g_snprintf (modes, sizeof(modes), "+l %d", 
				atoi (nx_entry_get_text (igad)));
		serv->p_mode (serv, sess->channel, modes);
		serv->p_join_info (serv, sess->channel);
	}
}

static void
mg_create_chanmodebuttons (session_gui *gui, GtkWidget *box)
{
	gui->flag_c = mg_create_flagbutton (_("Filter Colors"), box, "c");
	gui->flag_n = mg_create_flagbutton (_("No outside messages"), box, "n");
	gui->flag_t = mg_create_flagbutton (_("Topic Protection"), box, "t");
	gui->flag_i = mg_create_flagbutton (_("Invite Only"), box, "i");
	gui->flag_m = mg_create_flagbutton (_("Moderated"), box, "m");
	gui->flag_b = mg_create_flagbutton (_("Ban List"), box, "b");

	gui->flag_k = mg_create_flagbutton (_("Keyword"), box, "k");
	gui->key_entry = gtk_entry_new ();
	gtk_widget_set_name (gui->key_entry, "hexchat-inputbox");
	gtk_entry_set_max_length (GTK_ENTRY (gui->key_entry), 23);
	gtk_widget_set_size_request (gui->key_entry, 115, -1);
	nx_box_pack_start (box, gui->key_entry, 0, 0, 0);
	g_signal_connect (G_OBJECT (gui->key_entry), "activate",
							G_CALLBACK (mg_key_entry_cb), NULL);

	gui->flag_l = mg_create_flagbutton (_("User Limit"), box, "l");
	gui->limit_entry = gtk_entry_new ();
	gtk_widget_set_name (gui->limit_entry, "hexchat-inputbox");
	gtk_entry_set_max_length (GTK_ENTRY (gui->limit_entry), 10);
	gtk_widget_set_size_request (gui->limit_entry, 30, -1);
	nx_box_pack_start (box, gui->limit_entry, 0, 0, 0);
	g_signal_connect (G_OBJECT (gui->limit_entry), "activate",
							G_CALLBACK (mg_limit_entry_cb), NULL);
}

/*static void
mg_create_link_buttons (GtkWidget *box, gpointer userdata)
{
	gtkutil_button (box, NX_STOCK_CLOSE, _("Close this tab/window"),
						 mg_x_click_cb, userdata, 0);

	if (!userdata)
	gtkutil_button (box, NX_STOCK_REDO, _("Attach/Detach this tab"),
						 mg_link_cb, userdata, 0);
}*/

static void
mg_dialog_button_cb (GtkWidget *wid, char *cmd)
{
	/* the longest cmd is 12, and the longest nickname is 64 */
	char buf[128];
	char *host = "";
	char *topic;

	if (!current_sess)
		return;

	topic = (char *)(nx_entry_get_text (current_sess->gui->topic_entry));
	topic = strrchr (topic, '@');
	if (topic)
		host = topic + 1;

	auto_insert (buf, sizeof (buf), cmd, 0, 0, "", "", "",
					 server_get_network (current_sess->server, TRUE), host, "",
					 current_sess->channel, "");

	handle_command (current_sess, buf, TRUE);

	/* dirty trick to avoid auto-selection */
	SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
	gtk_widget_grab_focus (current_sess->gui->input_box);
	SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
}

static void
mg_dialog_button (GtkWidget *box, char *name, char *cmd)
{
	GtkWidget *wid;

	wid = gtk_button_new_with_label (name);
	nx_box_pack_start (box, wid, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (wid), "clicked",
							G_CALLBACK (mg_dialog_button_cb), cmd);
	gtk_widget_set_size_request (wid, -1, 0);
}

static void
mg_create_dialogbuttons (GtkWidget *box)
{
	struct popup *pop;
	GSList *list = dlgbutton_list;

	while (list)
	{
		pop = list->data;
		if (pop->cmd[0])
			mg_dialog_button (box, pop->name, pop->cmd);
		list = list->next;
	}
}

static void
mg_create_topicbar (session *sess, GtkWidget *box)
{
	GtkWidget *hbox, *topic, *bbox;
	session_gui *gui = sess->gui;

	gui->topic_bar = hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (box, hbox, 0, 0, 0);

	if (!gui->is_tab)
		sess->res->tab = NULL;

	gui->topic_entry = topic = sexy_spell_entry_new ();
	gtk_widget_set_name (topic, "hexchat-inputbox");
	sexy_spell_entry_set_checked (SEXY_SPELL_ENTRY (topic), FALSE);
	gtkutil_container_add (GTK_WIDGET (hbox), topic);
	g_signal_connect (G_OBJECT (topic), "activate",
							G_CALLBACK (mg_topic_cb), 0);

	gui->topicbutton_box = bbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (hbox, bbox, 0, 0, 0);
	mg_create_chanmodebuttons (gui, bbox);

	gui->dialogbutton_box = bbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (hbox, bbox, 0, 0, 0);
	mg_create_dialogbuttons (bbox);
}

/* check if a word is clickable */

static int
mg_word_check (GtkWidget * xtext, char *word)
{
	session *sess = current_sess;
	int ret;

	ret = url_check_word (word);
	if (ret == 0 && sess->type == SESS_DIALOG)
		return WORD_DIALOG;

	return ret;
}

/* mouse click inside text area */

static void
mg_word_clicked (GtkWidget *xtext, char *word, GtkXTextClick *click)
{
	session *sess = current_sess;
	/* menus still position from the event being handled */
	GdkEvent *even = nx_current_event ();
	int word_type = 0, start, end;
	char *tmp;

	if (word)
	{
		word_type = mg_word_check (xtext, word);
		url_last (&start, &end);
	}

	if (click->button == 1)			/* left button */
	{
		if (word == NULL)
		{
			mg_focus (sess);
			return;
		}

		if ((click->state & 13) == prefs.hex_gui_url_mod)
		{
			switch (word_type)
			{
			case WORD_URL:
			case WORD_HOST6:
			case WORD_HOST:
				word[end] = 0;
				fe_open_url (word + start);
			}
		}
		return;
	}

	if (click->button == 2)
	{
		if (sess->type == SESS_DIALOG)
			menu_middlemenu (sess, even);
		else if (click->n_press == 2)
			userlist_select (sess, word);
		return;
	}
	if (word == NULL)
		return;

	switch (word_type)
	{
	case 0:
	case WORD_PATH:
		menu_textmenu (sess, even, click->x, click->y);
		break;
	case WORD_URL:
	case WORD_HOST6:
	case WORD_HOST:
		word[end] = 0;
		word += start;
		menu_urlmenu (even, word);
		break;
	case WORD_NICK:
		word[end] = 0;
		word += start;
		menu_nickmenu (sess, even, word, FALSE);
		break;
	case WORD_CHANNEL:
		word[end] = 0;
		word += start;
		menu_chanmenu (sess, even, word);
		break;
	case WORD_EMAIL:
		word[end] = 0;
		word += start;
		tmp = g_strdup_printf ("mailto:%s", word + (ispunct (*word) ? 1 : 0));
		menu_urlmenu (even, tmp);
		g_free (tmp);
		break;
	case WORD_DIALOG:
		menu_nickmenu (sess, even, sess->channel, FALSE);
		break;
	}
}

void
mg_update_xtext (GtkWidget *wid)
{
	GtkXText *xtext = GTK_XTEXT (wid);

	gtk_xtext_set_palette (xtext, colors);
	gtk_xtext_set_max_lines (xtext, prefs.hex_text_max_lines);
	gtk_xtext_set_background (xtext, channelwin_pix);
	gtk_xtext_set_wordwrap (xtext, prefs.hex_text_wordwrap);
	gtk_xtext_set_show_marker (xtext, prefs.hex_text_show_marker);
	gtk_xtext_set_show_separator (xtext, prefs.hex_text_indent ? prefs.hex_text_show_sep : 0);
	gtk_xtext_set_indent (xtext, prefs.hex_text_indent);
	gtk_xtext_set_jump_button (xtext, prefs.hex_gui_jump_button);
	if (prefs.hex_text_zoom < 25 || prefs.hex_text_zoom > 400)
		prefs.hex_text_zoom = 100;
	xtext->zoom = prefs.hex_text_zoom;
	if (!gtk_xtext_set_font (xtext, prefs.hex_text_font))
	{
		fe_message ("Failed to open any font. I'm out of here!", FE_MSG_WAIT | FE_MSG_ERROR);
		exit (1);
	}

	gtk_xtext_refresh (xtext);
}

/* text zoom steps, in percent, like a browser */
static const int zoom_steps[] = { 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300 };

/* a short message floating over the chat, gone after a second */
void
mg_toast (session_gui *gui, const char *text)
{
	if (gui && gui->xtext)
		gtk_xtext_show_toast (GTK_XTEXT (gui->xtext), text, 1200);
}

/* step: +1 bigger, -1 smaller, 0 back to 100% */
void
mg_zoom (int step)
{
	int i, cur = prefs.hex_text_zoom ? prefs.hex_text_zoom : 100, n = G_N_ELEMENTS (zoom_steps);
	int z = 100;
	GSList *list;
	GSList *done = NULL;
	char buf[64];

	if (step > 0)
	{
		z = zoom_steps[n - 1];
		for (i = 0; i < n; i++)
			if (zoom_steps[i] > cur) { z = zoom_steps[i]; break; }
	}
	else if (step < 0)
	{
		z = zoom_steps[0];
		for (i = n - 1; i >= 0; i--)
			if (zoom_steps[i] < cur) { z = zoom_steps[i]; break; }
	}
	if (z == cur)
		return;
	prefs.hex_text_zoom = z;

	/* tabs share one text widget: each widget once. Every other buffer
	   shown in it was measured with the old font: re-measure when shown,
	   or switching to another channel showed the old wrapping and nick
	   column */
	for (list = sess_list; list; list = list->next)
	{
		session *sess = list->data;
		if (sess->res && sess->res->buffer)
			((xtext_buffer *) sess->res->buffer)->needs_recalc = TRUE;
	}
	for (list = sess_list; list; list = list->next)
	{
		session *sess = list->data;
		GtkWidget *x = sess->gui ? sess->gui->xtext : NULL;

		if (!x || g_slist_find (done, x))
			continue;
		done = g_slist_prepend (done, x);
		GTK_XTEXT (x)->zoom = z;
		gtk_xtext_set_font (GTK_XTEXT (x), prefs.hex_text_font);
		/* the visible buffer was just re-measured by set_font */
		GTK_XTEXT (x)->buffer->needs_recalc = FALSE;
		gtk_xtext_refresh (GTK_XTEXT (x));
	}
	g_slist_free (done);

	if (current_sess && current_sess->gui)
	{
		g_snprintf (buf, sizeof buf, _("Zoom %d%%"), z);
		mg_toast (current_sess->gui, buf);
	}
}

static void
mg_xtext_zoom_cb (GtkWidget *xtext, int step)
{
	if (prefs.hex_gui_zoom_keys)
		mg_zoom (step);
}

static void
mg_create_textarea (session *sess, GtkWidget *box)
{
	GtkWidget *inbox, *vbox, *frame;
	GtkXText *xtext;
	session_gui *gui = sess->gui;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtkutil_container_add (GTK_WIDGET (box), vbox);
	inbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
	gtkutil_container_add (GTK_WIDGET (vbox), inbox);

	frame = gtk_frame_new (NULL);
	nx_frame_set_shadow (frame, GTK_SHADOW_IN);
	gtkutil_container_add (GTK_WIDGET (inbox), frame);

	gui->xtext = gtk_xtext_new (colors, TRUE);
	xtext = GTK_XTEXT (gui->xtext);
	gtk_xtext_set_max_indent (xtext, prefs.hex_text_max_indent);
	gtk_xtext_set_thin_separator (xtext, prefs.hex_text_thin_sep);
	gtk_xtext_set_urlcheck_function (xtext, mg_word_check);
	xtext->zoom_cb = mg_xtext_zoom_cb;
	gtk_xtext_set_max_lines (xtext, prefs.hex_text_max_lines);
	gtkutil_container_add (GTK_WIDGET (frame), GTK_WIDGET (xtext));

	mg_update_xtext (GTK_WIDGET (xtext));

	g_signal_connect (G_OBJECT (xtext), "word_click",
							G_CALLBACK (mg_word_clicked), NULL);

	gui->vscrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, GTK_XTEXT (xtext)->adj);
	gtk_widget_set_name (gui->vscrollbar, "hexchat-scrollbar");
	nx_box_pack_start (inbox, mg_drop_overlay (gui->vscrollbar), FALSE, TRUE, 0);

	/* flags 5 (motion|drop) as before: no theme highlight, we paint our own */
	mg_layout_drop_dest (gui->vscrollbar,
								NX_DND_CHANVIEW | NX_DND_USERLIST | NX_DND_NO_THEME_HIGHLIGHT);

	nx_dnd_file_dest (gui->xtext, mg_dialog_dnd_drop, NULL, NULL, NULL);
}

static GtkWidget *
mg_create_infoframe (GtkWidget *box)
{
	GtkWidget *frame, *label, *hbox;

	frame = gtk_frame_new (0);
	nx_frame_set_shadow (frame, GTK_SHADOW_OUT);
	gtkutil_container_add (GTK_WIDGET (box), frame);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtkutil_container_add (GTK_WIDGET (frame), hbox);

	label = gtk_label_new (NULL);
	gtkutil_container_add (GTK_WIDGET (hbox), label);

	return label;
}

static void
mg_create_meters (session_gui *gui, GtkWidget *parent_box)
{
	GtkWidget *infbox, *wid, *box;

	gui->meter_box = infbox = box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	nx_box_pack_start (parent_box, box, 0, 0, 0);

	if ((prefs.hex_gui_lagometer & 2) || (prefs.hex_gui_throttlemeter & 2))
	{
		infbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		nx_box_pack_start (box, infbox, 0, 0, 0);
	}

	if (prefs.hex_gui_lagometer & 1)
	{
		gui->lagometer = wid = gtk_progress_bar_new ();
#ifdef WIN32
		gtk_widget_set_size_request (wid, 1, 10);
#else
		gtk_widget_set_size_request (wid, 1, 8);
#endif

		wid = nx_event_box_new ();
		gtkutil_container_add (GTK_WIDGET (wid), gui->lagometer);
		nx_box_pack_start (box, wid, 0, 0, 0);
	}
	if (prefs.hex_gui_lagometer & 2)
	{
		gui->laginfo = wid = mg_create_infoframe (infbox);
		gtk_label_set_text ((GtkLabel *) wid, "Lag");
	}

	if (prefs.hex_gui_throttlemeter & 1)
	{
		gui->throttlemeter = wid = gtk_progress_bar_new ();
#ifdef WIN32
		gtk_widget_set_size_request (wid, 1, 10);
#else
		gtk_widget_set_size_request (wid, 1, 8);
#endif

		wid = nx_event_box_new ();
		gtkutil_container_add (GTK_WIDGET (wid), gui->throttlemeter);
		nx_box_pack_start (box, wid, 0, 0, 0);
	}
	if (prefs.hex_gui_throttlemeter & 2)
	{
		gui->throttleinfo = wid = mg_create_infoframe (infbox);
		gtk_label_set_text ((GtkLabel *) wid, "Throttle");
	}
}

void
mg_update_meters (session_gui *gui)
{
	nx_widget_destroy (gui->meter_box);
	gui->lagometer = NULL;
	gui->laginfo = NULL;
	gui->throttlemeter = NULL;
	gui->throttleinfo = NULL;

	mg_create_meters (gui, gui->button_box_parent);
	nx_widget_show_all (gui->meter_box);
}

static void
mg_create_userlist (session_gui *gui, GtkWidget *box)
{
	GtkWidget *frame, *ulist, *vbox;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	gtkutil_container_add (GTK_WIDGET (box), vbox);

	frame = gtk_frame_new (NULL);
	gtk_widget_set_name (frame, "hexchat-ulistinfo");
	if (prefs.hex_gui_ulist_count)
		nx_box_pack_start (vbox, frame, 0, 0, GUI_SPACING);

	gui->namelistinfo = gtk_label_new (NULL);
	gtkutil_container_add (GTK_WIDGET (frame), gui->namelistinfo);

	gui->user_tree = ulist = userlist_create (vbox);

	mg_create_meters (gui, vbox);

	gui->button_box_parent = vbox;
	gui->button_box = mg_create_userlistbuttons (vbox);
}

static void
mg_vpane_cb (GtkPaned *pane, GParamSpec *param, session_gui *gui)
{
	prefs.hex_gui_pane_divider_position = gtk_paned_get_position (pane);
}

static void
mg_leftpane_cb (GtkPaned *pane, GParamSpec *param, session_gui *gui)
{
	prefs.hex_gui_pane_left_size = gtk_paned_get_position (pane);
}

static void
mg_rightpane_cb (GtkPaned *pane, GParamSpec *param, session_gui *gui)
{
	int handle_size;

	handle_size = nx_paned_handle_size (GTK_WIDGET (pane));
	/* record the position from the RIGHT side */
	prefs.hex_gui_pane_right_size = nx_widget_get_allocated_width (pane) - gtk_paned_get_position (pane) - handle_size;
}

static gboolean
mg_add_pane_signals (session_gui *gui)
{
	g_signal_connect (G_OBJECT (gui->hpane_right), "notify::position",
							G_CALLBACK (mg_rightpane_cb), gui);
	g_signal_connect (G_OBJECT (gui->hpane_left), "notify::position",
							G_CALLBACK (mg_leftpane_cb), gui);
	g_signal_connect (G_OBJECT (gui->vpane_left), "notify::position",
							G_CALLBACK (mg_vpane_cb), gui);
	g_signal_connect (G_OBJECT (gui->vpane_right), "notify::position",
							G_CALLBACK (mg_vpane_cb), gui);
	return FALSE;
}

static void
mg_create_center (session *sess, session_gui *gui, GtkWidget *box)
{
	GtkWidget *vbox, *hbox, *book;

	/* sep between top and bottom of left side */
	gui->vpane_left = gtk_paned_new (GTK_ORIENTATION_VERTICAL);

	/* sep between top and bottom of right side */
	gui->vpane_right = gtk_paned_new (GTK_ORIENTATION_VERTICAL);

	/* sep between left and xtext */
	gui->hpane_left = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_position (GTK_PANED (gui->hpane_left), prefs.hex_gui_pane_left_size);

	/* sep between xtext and right side */
	gui->hpane_right = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);

	if (prefs.hex_gui_win_swap)
	{
		nx_paned_pack (gui->hpane_left, gui->vpane_left, TRUE, FALSE, TRUE);
		nx_paned_pack (gui->hpane_left, gui->hpane_right, FALSE, TRUE, TRUE);
	}
	else
	{
		nx_paned_pack (gui->hpane_left, gui->vpane_left, FALSE, FALSE, TRUE);
		nx_paned_pack (gui->hpane_left, gui->hpane_right, TRUE, TRUE, TRUE);
	}
	nx_paned_pack (gui->hpane_right, gui->vpane_right, TRUE, FALSE, TRUE);

	gtkutil_container_add (GTK_WIDGET (box), gui->hpane_left);

	gui->note_book = book = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (book), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (book), FALSE);
	nx_paned_pack (gui->hpane_right, book, FALSE, TRUE, TRUE);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_paned_pack (gui->vpane_right, hbox, FALSE, FALSE, TRUE);
	mg_create_userlist (gui, hbox);

	gui->user_box = hbox;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
	/* the chat column sits in an overlay: the "/" command list is drawn over
	   it, right above the input row (in the window, not a popup surface) */
	gui->chat_overlay = gtk_overlay_new ();
	gtk_overlay_set_child (GTK_OVERLAY (gui->chat_overlay), vbox);
	gtk_notebook_append_page (GTK_NOTEBOOK (book), gui->chat_overlay, NULL);
	mg_create_topicbar (sess, vbox);

	if (prefs.hex_gui_search_pos)
	{
		mg_create_search (sess, vbox);
		mg_create_textarea (sess, vbox);
	}
	else
	{
		mg_create_textarea (sess, vbox);
		mg_create_search (sess, vbox);
	}

	mg_create_entry (sess, vbox);

	mg_add_pane_signals (gui);
}

static void
mg_change_nick (int cancel, char *text, gpointer userdata)
{
	char buf[256];

	if (!cancel)
	{
		g_snprintf (buf, sizeof (buf), "nick %s", text);
		handle_command (current_sess, buf, FALSE);
	}
}

static void
mg_nickclick_cb (GtkWidget *button, gpointer userdata)
{
	fe_get_str (_("Enter new nickname:"), current_sess->server->nick,
					mg_change_nick, (void *) 1);
}

/* make sure chanview and userlist positions are sane */

static void
mg_sanitize_positions (int *cv, int *ul)
{
	if (prefs.hex_gui_tab_layout == 2)
	{
		/* treeview can't be on TOP or BOTTOM */
		if (*cv == POS_TOP || *cv == POS_BOTTOM)
			*cv = POS_TOPLEFT;
	}

	/* userlist can't be on TOP or BOTTOM */
	if (*ul == POS_TOP || *ul == POS_BOTTOM)
		*ul = POS_TOPRIGHT;

	/* can't have both in the same place */
	if (*cv == *ul)
	{
		*cv = POS_TOPRIGHT;
		if (*ul == POS_TOPRIGHT)
			*cv = POS_BOTTOMRIGHT;
	}
}

static void
mg_place_userlist_and_chanview_real (session_gui *gui, GtkWidget *userlist, GtkWidget *chanview)
{
	int unref_userlist = FALSE;
	int unref_chanview = FALSE;

	/* first, remove userlist/treeview from their containers */
	if (userlist && gtk_widget_get_parent (userlist))
	{
		g_object_ref (userlist);
		nx_widget_unparent_child (userlist);
		unref_userlist = TRUE;
	}

	if (chanview && gtk_widget_get_parent (chanview))
	{
		g_object_ref (chanview);
		nx_widget_unparent_child (chanview);
		unref_chanview = TRUE;
	}

	if (chanview)
	{
		/* incase the previous pos was POS_HIDDEN */
		nx_widget_show (chanview);

		mg_grid_set_row_spacing (gui->main_table, 1, 0);
		mg_grid_set_row_spacing (gui->main_table, 2, 2);

		/* then place them back in their new positions */
		switch (prefs.hex_gui_tab_pos)
		{
		case POS_TOPLEFT:
			nx_paned_pack (gui->vpane_left, chanview, FALSE, FALSE, TRUE);
			break;
		case POS_BOTTOMLEFT:
			nx_paned_pack (gui->vpane_left, chanview, TRUE, FALSE, TRUE);
			break;
		case POS_TOPRIGHT:
			nx_paned_pack (gui->vpane_right, chanview, FALSE, FALSE, TRUE);
			break;
		case POS_BOTTOMRIGHT:
			nx_paned_pack (gui->vpane_right, chanview, TRUE, FALSE, TRUE);
			break;
		case POS_TOP:
			mg_grid_set_row_spacing (gui->main_table, 1, GUI_SPACING-1);
			mg_grid_attach (gui->main_table, chanview,
									1, 2, 1, 2, GU_FILL, GU_FILL);
			break;
		case POS_HIDDEN:
			nx_widget_hide (chanview);
			/* always attach it to something to avoid ref_count=0 */
			if (prefs.hex_gui_ulist_pos == POS_TOP)
				mg_grid_attach (gui->main_table, chanview,
										1, 2, 3, 4, GU_FILL, GU_FILL);

			else
				mg_grid_attach (gui->main_table, chanview,
										1, 2, 1, 2, GU_FILL, GU_FILL);
			break;
		default:/* POS_BOTTOM */
			mg_grid_set_row_spacing (gui->main_table, 2, 3);
			mg_grid_attach (gui->main_table, chanview,
									1, 2, 3, 4, GU_FILL, GU_FILL);
		}
	}

	if (userlist)
	{
		switch (prefs.hex_gui_ulist_pos)
		{
		case POS_TOPLEFT:
			nx_paned_pack (gui->vpane_left, userlist, FALSE, FALSE, TRUE);
			break;
		case POS_BOTTOMLEFT:
			nx_paned_pack (gui->vpane_left, userlist, TRUE, FALSE, TRUE);
			break;
		case POS_BOTTOMRIGHT:
			nx_paned_pack (gui->vpane_right, userlist, TRUE, FALSE, TRUE);
			break;
		/*case POS_HIDDEN:
			break;*/	/* Hide using the VIEW menu instead */
		default:/* POS_TOPRIGHT */
			nx_paned_pack (gui->vpane_right, userlist, FALSE, FALSE, TRUE);
		}
	}

	if (mg_is_userlist_and_tree_combined () && prefs.hex_gui_pane_divider_position != 0)
	{
		gtk_paned_set_position (GTK_PANED (gui->vpane_left), prefs.hex_gui_pane_divider_position);
		gtk_paned_set_position (GTK_PANED (gui->vpane_right), prefs.hex_gui_pane_divider_position);
	}

	if (unref_chanview)
		g_object_unref (chanview);
	if (unref_userlist)
		g_object_unref (userlist);

	mg_hide_empty_boxes (gui);
}

static void
mg_place_userlist_and_chanview (session_gui *gui)
{
	GtkOrientation orientation;
	GtkWidget *chanviewbox = NULL;
	int pos;

	mg_sanitize_positions (&prefs.hex_gui_tab_pos, &prefs.hex_gui_ulist_pos);

	if (gui->chanview)
	{
		pos = prefs.hex_gui_tab_pos;

		orientation = chanview_get_orientation (gui->chanview);
		if ((pos == POS_BOTTOM || pos == POS_TOP) && orientation == GTK_ORIENTATION_VERTICAL)
			chanview_set_orientation (gui->chanview, FALSE);
		else if ((pos == POS_TOPLEFT || pos == POS_BOTTOMLEFT || pos == POS_TOPRIGHT || pos == POS_BOTTOMRIGHT) && orientation == GTK_ORIENTATION_HORIZONTAL)
			chanview_set_orientation (gui->chanview, TRUE);
		chanviewbox = chanview_get_box (gui->chanview);
	}

	mg_place_userlist_and_chanview_real (gui, gui->user_box, chanviewbox);
}

void
mg_change_layout (int type)
{
	if (mg_gui)
	{
		/* put tabs at the bottom */
		if (type == 0 && prefs.hex_gui_tab_pos != POS_BOTTOM && prefs.hex_gui_tab_pos != POS_TOP)
			prefs.hex_gui_tab_pos = POS_BOTTOM;

		mg_place_userlist_and_chanview (mg_gui);
		chanview_set_impl (mg_gui->chanview, type);
	}
}

/* the input box's right-click menu: the colour codes go at the end */
static void
mg_inputbox_rightclick (SexySpellEntry *entry, nx_menu *menu, gpointer unused)
{
	mg_create_color_menu (menu, NULL);
}

/* Search bar adapted from Conspire's by William Pitcock */

#define SEARCH_CHANGE		1
#define SEARCH_NEXT			2
#define SEARCH_PREVIOUS		3
#define SEARCH_REFRESH		4

static void
search_handle_event(int search_type, session *sess)
{
	textentry *last;
	const gchar *text = NULL;
	gtk_xtext_search_flags flags;
	GError *err = NULL;
	gboolean backwards = FALSE;

	/* When just typing show most recent first */
	if (search_type == SEARCH_PREVIOUS || search_type == SEARCH_CHANGE)
		backwards = TRUE;

	flags = ((prefs.hex_text_search_case_match == 1? case_match: 0) |
				(backwards? backward: 0) |
				(prefs.hex_text_search_highlight_all == 1? highlight: 0) |
				(prefs.hex_text_search_follow == 1? follow: 0) |
				(prefs.hex_text_search_regexp == 1? regexp: 0));

	if (search_type != SEARCH_REFRESH)
		text = nx_entry_get_text (sess->gui->shentry);
	last = gtk_xtext_search (GTK_XTEXT (sess->gui->xtext), text, flags, &err);

	if (err)
	{
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NX_STOCK_DIALOG_ERROR));
		gtk_entry_set_icon_tooltip_text (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, _(err->message));
		g_error_free (err);
	}
	else if (!last)
	{
		if (text && text[0] == 0) /* empty string, no error */
		{
			gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NULL));
		}
		else
		{
			/* Either end of search or not found, try again to wrap if only end */
			last = gtk_xtext_search (GTK_XTEXT (sess->gui->xtext), text, flags, &err);
			if (!last) /* Not found error */
			{
				gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NX_STOCK_DIALOG_ERROR));
				gtk_entry_set_icon_tooltip_text (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, _("No results found."));
			}
		}
	}
	else
	{
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NULL));
	}
}

static void
search_handle_change(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_CHANGE, sess);
}

static void
search_handle_refresh(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_REFRESH, sess);
}

void
mg_search_handle_previous(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_PREVIOUS, sess);
}

void
mg_search_handle_next(GtkWidget *wid, session *sess)
{
	search_handle_event(SEARCH_NEXT, sess);
}

static void
search_set_option (GtkWidget *but, guint *pref)
{
	*pref = nx_toggle_get_active (but);
	save_config();
}

void
mg_search_toggle(session *sess)
{
	if (gtk_widget_get_visible(sess->gui->shbox))
	{
		nx_widget_hide (sess->gui->shbox);
		gtk_widget_grab_focus(sess->gui->input_box);
		nx_entry_set_text (sess->gui->shentry, "");
	}
	else
	{
		/* Reset search state */
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, gtkutil_stock_icon_name (NULL));

		/* Show and focus */
		nx_widget_show (sess->gui->shbox);
		gtk_widget_grab_focus(sess->gui->shentry);
	}
}

static gboolean
search_handle_esc (GtkWidget *win, guint keyval, GdkModifierType state, gpointer userdata)
{
	session *sess = userdata;

	if (keyval == GDK_KEY_Escape)
		mg_search_toggle(sess);
			
	return FALSE;
}

static void
mg_create_search(session *sess, GtkWidget *box)
{
	GtkWidget *entry, *label, *next, *previous, *highlight, *matchcase, *regex, *close;
	session_gui *gui = sess->gui;

	gui->shbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
	nx_box_pack_start (box, gui->shbox, FALSE, FALSE, 0);

	close = gtk_button_new ();
	nx_button_set_image (close, gtkutil_image_new_from_stock (NX_STOCK_CLOSE, NX_ICON_MENU));
	nx_button_set_flat (close);
	gtk_widget_set_can_focus (close, FALSE);
	nx_box_pack_start (gui->shbox, close, FALSE, FALSE, 0);
	g_signal_connect_swapped(G_OBJECT(close), "clicked", G_CALLBACK(mg_search_toggle), sess);

	label = gtk_label_new(_("Find:"));
	nx_box_pack_start (gui->shbox, label, FALSE, FALSE, 0);

	gui->shentry = entry = gtk_entry_new();
	nx_box_pack_start (gui->shbox, entry, FALSE, FALSE, 0);
	gtk_widget_set_size_request (gui->shentry, 180, -1);
	gui->search_changed_signal = g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(search_handle_change), sess);
	nx_on_key (entry, search_handle_esc, sess);
	g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(mg_search_handle_next), sess);
	gtk_entry_set_icon_activatable (GTK_ENTRY (entry), GTK_ENTRY_ICON_SECONDARY, FALSE);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (sess->gui->shentry), GTK_ENTRY_ICON_SECONDARY, _("Search hit end or not found."));

	previous = gtk_button_new ();
	nx_button_set_image (previous, gtkutil_image_new_from_stock (NX_STOCK_GO_BACK, NX_ICON_MENU));
	nx_button_set_flat (previous);
	gtk_widget_set_can_focus (previous, FALSE);
	nx_box_pack_start (gui->shbox, previous, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(previous), "clicked", G_CALLBACK(mg_search_handle_previous), sess);

	next = gtk_button_new ();
	nx_button_set_image (next, gtkutil_image_new_from_stock (NX_STOCK_GO_FORWARD, NX_ICON_MENU));
	nx_button_set_flat (next);
	gtk_widget_set_can_focus (next, FALSE);
	nx_box_pack_start (gui->shbox, next, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(next), "clicked", G_CALLBACK(mg_search_handle_next), sess);

	highlight = gtk_check_button_new_with_mnemonic (_("_Highlight all"));
	nx_toggle_set_active ((GtkWidget *) (highlight), prefs.hex_text_search_highlight_all);
	gtk_widget_set_can_focus (highlight, FALSE);
	g_signal_connect (G_OBJECT (highlight), "toggled", G_CALLBACK (search_set_option), &prefs.hex_text_search_highlight_all);
	g_signal_connect (G_OBJECT (highlight), "toggled", G_CALLBACK (search_handle_refresh), sess);
	nx_box_pack_start (gui->shbox, highlight, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text (highlight, _("Highlight all occurrences, and underline the current occurrence."));

	matchcase = gtk_check_button_new_with_mnemonic (_("Mat_ch case"));
	nx_toggle_set_active ((GtkWidget *) (matchcase), prefs.hex_text_search_case_match);
	gtk_widget_set_can_focus (matchcase, FALSE);
	g_signal_connect (G_OBJECT (matchcase), "toggled", G_CALLBACK (search_set_option), &prefs.hex_text_search_case_match);
	nx_box_pack_start (gui->shbox, matchcase, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text (matchcase, _("Perform a case-sensitive search."));

	regex = gtk_check_button_new_with_mnemonic (_("_Regex"));
	nx_toggle_set_active ((GtkWidget *) (regex), prefs.hex_text_search_regexp);
	gtk_widget_set_can_focus (regex, FALSE);
	g_signal_connect (G_OBJECT (regex), "toggled", G_CALLBACK (search_set_option), &prefs.hex_text_search_regexp);
	nx_box_pack_start (gui->shbox, regex, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text (regex, _("Regard search string as a regular expression."));
}

static void
mg_create_entry (session *sess, GtkWidget *box)
{
	GtkWidget *hbox, *but, *entry;
	session_gui *gui = sess->gui;

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (box, hbox, 0, 0, 0);

	gui->nick_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (hbox, gui->nick_box, 0, 0, 0);

	gui->nick_label = but = gtk_button_new_with_label (sess->server->nick);
	gtk_widget_set_name (but, "hexchat-nickbutton");
	nx_button_set_flat (but);
	gtk_widget_set_can_focus (but, FALSE);
	nx_box_pack_end (gui->nick_box, but, 0, 0, 0);
	g_signal_connect (G_OBJECT (but), "clicked",
							G_CALLBACK (mg_nickclick_cb), NULL);

	gui->input_box = entry = sexy_spell_entry_new ();
	sexy_spell_entry_set_checked ((SexySpellEntry *)entry, prefs.hex_gui_input_spell);
	sexy_spell_entry_set_parse_attributes ((SexySpellEntry *)entry, prefs.hex_gui_input_attr);

	gtk_entry_set_max_length (GTK_ENTRY (gui->input_box), 0);
	g_signal_connect (G_OBJECT (entry), "activate",
							G_CALLBACK (mg_inputbox_cb), gui);
	gtkutil_container_add (GTK_WIDGET (hbox), entry);

	gtk_widget_set_name (entry, "hexchat-inputbox");
	nx_on_key (entry, key_handle_input, NULL);
	cmdpopup_attach (entry, gui->chat_overlay);
	nx_on_focus_in (entry, mg_inputbox_focus, gui);
	sexy_spell_entry_set_menu_hook (SEXY_SPELL_ENTRY (entry), mg_inputbox_rightclick, NULL);
	g_signal_connect (G_OBJECT (entry), "word-check",
							G_CALLBACK (mg_spellcheck_cb), NULL);
	gtk_widget_grab_focus (entry);
}

static void
mg_switch_tab_cb (chanview *cv, chan *ch, int tag, gpointer ud)
{
	chan *old;
	session *sess = ud;

	old = active_tab;
	active_tab = ch;

	if (tag == TAG_IRC)
	{
		if (active_tab != old)
		{
			if (old && current_tab)
				mg_unpopulate (current_tab);
			mg_populate (sess);
		}
	} else if (old != active_tab)
	{
		/* userdata for non-irc tabs is actually the GtkBox */
		mg_show_generic_tab (ud);
		if (!mg_is_userlist_and_tree_combined ())
			mg_userlist_showhide (current_sess, FALSE);	/* hide */
	}
}

/* compare two tabs (for tab sorting function) */

static int
mg_tabs_compare (session *a, session *b)
{
	/* server tabs always go first */
	if (a->type == SESS_SERVER)
		return -1;

	/* then channels */
	if (a->type == SESS_CHANNEL && b->type != SESS_CHANNEL)
		return -1;
	if (a->type != SESS_CHANNEL && b->type == SESS_CHANNEL)
		return 1;

	return g_ascii_strcasecmp (a->channel, b->channel);
}

static void
mg_create_tabs (session_gui *gui)
{
	gboolean use_icons = FALSE;

	/* if any one of these PNGs exist, the chanview will create
	 * the extra column for icons. */
	if (prefs.hex_gui_tab_icons && (pix_tree_channel || pix_tree_dialog || pix_tree_server || pix_tree_util))
	{
		use_icons = TRUE;
	}

	gui->chanview = chanview_new (prefs.hex_gui_tab_layout, prefs.hex_gui_tab_trunc,
											prefs.hex_gui_tab_sort, use_icons,
											prefs.hex_gui_ulist_style);
	chanview_set_callbacks (gui->chanview, mg_switch_tab_cb, mg_xbutton_cb,
									mg_tab_contextmenu_cb, (void *)mg_tabs_compare);
	mg_place_userlist_and_chanview (gui);
}

static void
mg_tabwin_focus_cb (GtkWidget * win, gpointer userdata)
{
	current_sess = current_tab;
	if (current_sess)
	{
		gtk_xtext_check_marker_visibility (GTK_XTEXT (current_sess->gui->xtext));
		plugin_emit_dummy_print (current_sess, "Focus Window");
	}
	unflash_window (GTK_WIDGET (win));
}

static void
mg_topwin_focus_cb (GtkWidget * win, gpointer userdata)
{
	session *sess = userdata;

	current_sess = sess;
	if (!sess->server->server_session)
		sess->server->server_session = sess;
	gtk_xtext_check_marker_visibility(GTK_XTEXT (current_sess->gui->xtext));
	unflash_window (GTK_WIDGET (win));
	plugin_emit_dummy_print (sess, "Focus Window");
}

static void
mg_create_menu (session_gui *gui, GtkWidget *table, int away_state)
{
	gui->main_menu = menu_create_main (nx_widget_get_toplevel (table), TRUE, away_state,
												  !gui->is_tab, gui->menu_item);
	gui->menu = nx_menu_bar_get_widget (gui->main_menu);
	menu_debug_dump (gui->main_menu);
	menu_debug_activate (gui->main_menu);
	mg_grid_attach (table, gui->menu, 0, 3, 0, 1,
						 GU_EXPAND | GU_FILL, GU_SHRINK | GU_FILL);
}

static void
mg_create_irctab (session *sess, GtkWidget *table)
{
	GtkWidget *vbox;
	session_gui *gui = sess->gui;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	mg_grid_attach (table, vbox, 1, 2, 2, 3,
						 GU_EXPAND | GU_FILL, GU_EXPAND | GU_FILL);
	mg_create_center (sess, gui, vbox);
}

static void
mg_create_topwindow (session *sess)
{
	GtkWidget *win;
	GtkWidget *table;

	if (sess->type == SESS_DIALOG)
		win = gtkutil_window_new (DISPLAY_NAME, NULL,
										  prefs.hex_gui_dialog_width, prefs.hex_gui_dialog_height, 0);
	else
		win = gtkutil_window_new (DISPLAY_NAME, NULL,
										  prefs.hex_gui_win_width,
										  prefs.hex_gui_win_height, 0);
	sess->gui->window = win;
	nx_set_border_width (win, GUI_BORDER);
	gtk_widget_set_opacity (GTK_WIDGET (win), (prefs.hex_gui_transparency / 255.));

	nx_on_focus_in (win, mg_topwin_focus_cb, sess);
	g_signal_connect (G_OBJECT (win), "destroy",
							G_CALLBACK (mg_topdestroy_cb), sess);
	nx_on_window_configure (win, mg_configure_cb, sess);

	palette_alloc (win);

	table = gtk_grid_new ();
	/* spacing under the menubar */
	mg_grid_set_row_spacing (table, 0, GUI_SPACING);
	/* left and right borders */
	mg_grid_set_col_spacing (table, 0, 1);
	mg_grid_set_col_spacing (table, 1, 1);
	gtkutil_container_add (GTK_WIDGET (win), table);

	mg_create_irctab (sess, table);
	mg_create_menu (sess->gui, table, sess->server->is_away);

	if (sess->res->buffer == NULL)
	{
		sess->res->buffer = gtk_xtext_buffer_new (GTK_XTEXT (sess->gui->xtext));
		gtk_xtext_buffer_show (GTK_XTEXT (sess->gui->xtext), sess->res->buffer, TRUE);
		gtk_xtext_set_time_stamp (sess->res->buffer, prefs.hex_stamp_text);
		sess->res->user_model = userlist_create_model (sess);
	}

	userlist_show (sess);

	nx_widget_show_all (table);

	if (prefs.hex_gui_hide_menu)
		nx_widget_hide (sess->gui->menu);

	/* Will be shown when needed */
	nx_widget_hide (sess->gui->topic_bar);

	if (!prefs.hex_gui_ulist_buttons)
		nx_widget_hide (sess->gui->button_box);

	if (!prefs.hex_gui_input_nick)
		nx_widget_hide (sess->gui->nick_box);

	nx_widget_hide (sess->gui->shbox);

	mg_decide_userlist (sess, FALSE);

	if (sess->type == SESS_DIALOG)
	{
		/* hide the chan-mode buttons */
		nx_widget_hide (sess->gui->topicbutton_box);
	} else
	{
		nx_widget_hide (sess->gui->dialogbutton_box);

		if (!prefs.hex_gui_mode_buttons)
			nx_widget_hide (sess->gui->topicbutton_box);
	}

	mg_place_userlist_and_chanview (sess->gui);

	nx_widget_show (win);
}

static gboolean
mg_tabwindow_de_cb (GtkWidget *widget, gpointer user_data)
{
	GSList *list;
	session *sess;
	GtkWindow *win = GTK_WINDOW(nx_widget_get_toplevel (widget));

	if (prefs.hex_gui_tray_close && gtkutil_tray_icon_supported (win) && tray_toggle_visibility (FALSE))
		return TRUE;

	/* check for remaining toplevel windows */
	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (!sess->gui->is_tab)
			return FALSE;
		list = list->next;
	}

	mg_open_quit_dialog (TRUE);
	return TRUE;
}

#ifdef G_OS_WIN32
/* the user changed the clock or time zone: timestamps follow */
static GdkWin32MessageFilterReturn
mg_time_change (GdkWin32Display *display, MSG *msg, int *ret, gpointer data)
{
	if (msg->message == WM_TIMECHANGE)
		_tzset ();
	return GDK_WIN32_MESSAGE_FILTER_CONTINUE;
}
#endif

static void
mg_create_tabwindow (session *sess)
{
	GtkWidget *win;
	GtkWidget *table;

	win = gtkutil_window_new (DISPLAY_NAME, NULL, prefs.hex_gui_win_width,
									  prefs.hex_gui_win_height, 0);
	sess->gui->window = win;
	gtkutil_window_move (GTK_WINDOW (win), prefs.hex_gui_win_left,
						  prefs.hex_gui_win_top);
	if (prefs.hex_gui_win_state)
		gtk_window_maximize (GTK_WINDOW (win));
	if (prefs.hex_gui_win_fullscreen)
		gtk_window_fullscreen (GTK_WINDOW (win));
	gtk_widget_set_opacity (GTK_WIDGET (win), (prefs.hex_gui_transparency / 255.));
	nx_set_border_width (win, GUI_BORDER);

	nx_on_close_request (win, mg_tabwindow_de_cb, 0);
	g_signal_connect (G_OBJECT (win), "destroy",
						   G_CALLBACK (mg_tabwindow_kill_cb), 0);
	nx_on_focus_in (win, mg_tabwin_focus_cb, NULL);
	nx_on_window_configure (win, mg_configure_cb, NULL);
	nx_on_window_state (win, mg_windowstate_cb, NULL);

	palette_alloc (win);

	sess->gui->main_table = table = gtk_grid_new ();
	/* spacing under the menubar */
	mg_grid_set_row_spacing (table, 0, GUI_SPACING);
	/* left and right borders */
	mg_grid_set_col_spacing (table, 0, 1);
	mg_grid_set_col_spacing (table, 1, 1);
	gtkutil_container_add (GTK_WIDGET (win), table);

	mg_create_irctab (sess, table);
	mg_create_tabs (sess->gui);
	mg_create_menu (sess->gui, table, sess->server->is_away);

	mg_focus (sess);

	nx_widget_show_all (table);

	if (prefs.hex_gui_hide_menu)
		nx_widget_hide (sess->gui->menu);

	mg_decide_userlist (sess, FALSE);

	/* Will be shown when needed */
	nx_widget_hide (sess->gui->topic_bar);

	if (!prefs.hex_gui_mode_buttons)
		nx_widget_hide (sess->gui->topicbutton_box);

	if (!prefs.hex_gui_ulist_buttons)
		nx_widget_hide (sess->gui->button_box);

	if (!prefs.hex_gui_input_nick)
		nx_widget_hide (sess->gui->nick_box);

	nx_widget_hide (sess->gui->shbox);

	mg_place_userlist_and_chanview (sess->gui);

	nx_widget_show (win);

#ifdef G_OS_WIN32
	{
		static gboolean filtered;
		if (!filtered)
		{
			filtered = TRUE;
			gdk_win32_display_add_filter (GDK_WIN32_DISPLAY (gtk_widget_get_display (win)),
												mg_time_change, NULL);
		}
	}
#endif
}

void
mg_apply_setup (void)
{
	GSList *list = sess_list;
	session *sess;
	int done_main = FALSE;

	mg_create_tab_colors ();

	while (list)
	{
		sess = list->data;
		gtk_xtext_set_time_stamp (sess->res->buffer, prefs.hex_stamp_text);
		((xtext_buffer *)sess->res->buffer)->needs_recalc = TRUE;
		if (!sess->gui->is_tab || !done_main)
			mg_place_userlist_and_chanview (sess->gui);
		if (sess->gui->is_tab)
			done_main = TRUE;
		list = list->next;
	}
}

static chan *
mg_add_generic_tab (char *name, char *title, void *family, GtkWidget *box)
{
	chan *ch;

	gtk_notebook_append_page (GTK_NOTEBOOK (mg_gui->note_book), box, NULL);
	nx_widget_show (box);

	ch = chanview_add (mg_gui->chanview, name, NULL, box, TRUE, TAG_UTIL, pix_tree_util);
	chan_set_color (ch, plain_list);

	g_object_set_data_full (G_OBJECT (box), "title", g_strdup (title), g_free);
	g_object_set_data (G_OBJECT (box), "ch", ch);

	if (prefs.hex_gui_tab_newtofront)
		chan_focus (ch);

	return ch;
}

void
fe_buttons_update (session *sess)
{
	session_gui *gui = sess->gui;

	nx_widget_destroy (gui->button_box);
	gui->button_box = mg_create_userlistbuttons (gui->button_box_parent);

	if (prefs.hex_gui_ulist_buttons)
		nx_widget_show (sess->gui->button_box);
	else
		nx_widget_hide (sess->gui->button_box);
}

void
fe_clear_channel (session *sess)
{
	char tbuf[CHANLEN+6];
	session_gui *gui = sess->gui;

	if (sess->gui->is_tab)
	{
		if (sess->waitchannel[0])
		{
			if (prefs.hex_gui_tab_trunc > 2 && g_utf8_strlen (sess->waitchannel, -1) > prefs.hex_gui_tab_trunc)
			{
				/* truncate long channel names */
				tbuf[0] = '(';
				strcpy (tbuf + 1, sess->waitchannel);
				g_utf8_offset_to_pointer(tbuf, prefs.hex_gui_tab_trunc)[0] = 0;
				strcat (tbuf, "..)");
			} else
			{
				sprintf (tbuf, "(%s)", sess->waitchannel);
			}
		}
		else
			strcpy (tbuf, _("<none>"));
		chan_rename (sess->res->tab, tbuf, prefs.hex_gui_tab_trunc);
	}

	if (!sess->gui->is_tab || sess == current_tab)
	{
		nx_entry_set_text (gui->topic_entry, "");

		if (gui->op_xpm)
		{
			nx_widget_destroy (gui->op_xpm);
			gui->op_xpm = 0;
		}
	} else
	{
		if (sess->res->topic_text)
		{
			g_free (sess->res->topic_text);
			sess->res->topic_text = NULL;
		}
	}
}

void
fe_set_nonchannel (session *sess, int state)
{
}

void
fe_dlgbuttons_update (session *sess)
{
	GtkWidget *box;
	session_gui *gui = sess->gui;

	nx_widget_destroy (gui->dialogbutton_box);

	gui->dialogbutton_box = box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (gui->topic_bar, box, 0, 0, 0);
	nx_box_reorder_child (gui->topic_bar, box, 3);
	mg_create_dialogbuttons (box);

	nx_widget_show_all (box);

	if (current_tab && current_tab->type != SESS_DIALOG)
		nx_widget_hide (current_tab->gui->dialogbutton_box);
}

void
fe_update_mode_buttons (session *sess, char mode, char sign)
{
	int state, i;

	if (sign == '+')
		state = TRUE;
	else
		state = FALSE;

	for (i = 0; i < NUM_FLAG_WIDS - 1; i++)
	{
		if (chan_flags[i] == mode)
		{
			if (!sess->gui->is_tab || sess == current_tab)
			{
				ignore_chanmode = TRUE;
				if (nx_toggle_get_active ((GtkWidget *) (sess->gui->flag_wid[i])) != state)
					nx_toggle_set_active ((GtkWidget *) (sess->gui->flag_wid[i]), state);
				ignore_chanmode = FALSE;
			} else
			{
				sess->res->flag_wid_state[i] = state;
			}
			return;
		}
	}
}

void
fe_set_nick (server *serv, char *newnick)
{
	GSList *list = sess_list;
	session *sess;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			if (current_tab == sess || !sess->gui->is_tab)
				gtk_button_set_label (GTK_BUTTON (sess->gui->nick_label), newnick);
		}
		list = list->next;
	}
}

void
fe_set_away (server *serv)
{
	GSList *list = sess_list;
	session *sess;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			if (!sess->gui->is_tab || sess == current_tab)
			{
				menu_set_away (sess->gui, serv->is_away);
				/* gray out my nickname */
				mg_set_myself_away (sess->gui, serv->is_away);
			}
		}
		list = list->next;
	}
}

void
fe_set_channel (session *sess)
{
	mg_update_tab_label (sess);
}

/* the tab's text: the channel, then "(3)" while 3 messages are unread
   (gui_tab_unread), the highlights apart: "(3, 1!)" */
void
mg_update_tab_label (session *sess)
{
	int max = prefs.hex_gui_tab_trunc;
	char *name, *label;

	if (!sess->res->tab || !sess->channel[0])
		return;
	if (!prefs.hex_gui_tab_unread || (!sess->unread && !sess->unread_hilight))
	{
		chan_rename (sess->res->tab, sess->channel, max);
		return;
	}
	/* the name is cut to gui_tab_trunc, the count never is */
	if (max > 2 && g_utf8_strlen (sess->channel, -1) > max)
	{
		char *cut = g_utf8_substring (sess->channel, 0, max);
		name = g_strconcat (cut, "..", NULL);
		g_free (cut);
	}
	else
		name = g_strdup (sess->channel);
	if (sess->unread_hilight)
		label = g_strdup_printf ("%s (%u, %u!)", name, sess->unread, sess->unread_hilight);
	else
		label = g_strdup_printf ("%s (%u)", name, sess->unread);
	chan_rename (sess->res->tab, label, 0);
	g_free (name);
	g_free (label);
}

void
mg_changui_new (session *sess, restore_gui *res, int tab, int focus)
{
	int first_run = FALSE;
	session_gui *gui;

	if (res == NULL)
	{
		res = g_new0 (restore_gui, 1);
	}

	sess->res = res;

	if (sess->server->front_session == NULL)
	{
		sess->server->front_session = sess;
	}

	if (!tab)
	{
		gui = g_new0 (session_gui, 1);
		gui->is_tab = FALSE;
		sess->gui = gui;
		mg_create_topwindow (sess);
		fe_set_title (sess);
		return;
	}

	if (mg_gui == NULL)
	{
		first_run = TRUE;
		gui = &static_mg_gui;
		memset (gui, 0, sizeof (session_gui));
		gui->is_tab = TRUE;
		sess->gui = gui;
		mg_create_tabwindow (sess);
		mg_gui = gui;
		parent_window = gui->window;
	} else
	{
		sess->gui = gui = mg_gui;
		gui->is_tab = TRUE;
	}

	mg_add_chan (sess);

	if (first_run || (prefs.hex_gui_tab_newtofront == FOCUS_NEW_ONLY_ASKED && focus)
			|| prefs.hex_gui_tab_newtofront == FOCUS_NEW_ALL )
		chan_focus (res->tab);
}

GtkWidget *
mg_create_generic_tab (char *name, char *title, int force_toplevel,
							  int link_buttons,
							  void *close_callback, void *userdata,
							  int width, int height, GtkWidget **vbox_ret,
							  void *family)
{
	GtkWidget *vbox, *win;

	if (prefs.hex_gui_tab_pos == POS_HIDDEN && prefs.hex_gui_tab_utils)
		prefs.hex_gui_tab_utils = 0;

	if (force_toplevel || !prefs.hex_gui_tab_utils)
	{
		win = gtkutil_window_new (title, name, width, height, 2);
		vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		*vbox_ret = vbox;
		gtkutil_container_add (GTK_WIDGET (win), vbox);
		nx_widget_show (vbox);
		if (close_callback)
			g_signal_connect (G_OBJECT (win), "destroy",
									G_CALLBACK (close_callback), userdata);
		return win;
	}

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	g_object_set_data (G_OBJECT (vbox), "w", GINT_TO_POINTER (width));
	g_object_set_data (G_OBJECT (vbox), "h", GINT_TO_POINTER (height));
	nx_set_border_width (vbox, 3);
	*vbox_ret = vbox;

	if (close_callback)
		g_signal_connect (G_OBJECT (vbox), "destroy",
								G_CALLBACK (close_callback), userdata);

	mg_add_generic_tab (name, title, family, vbox);

/*	if (link_buttons)
	{
		hbox = gtk_hbox_new (FALSE, 0);
		nx_box_pack_start (vbox, hbox, 0, 0, 0);
		mg_create_link_buttons (hbox, ch);
		nx_widget_show (hbox);
	}*/

	return vbox;
}

void
mg_move_tab (session *sess, int delta)
{
	if (sess->gui->is_tab)
		chan_move (sess->res->tab, delta);
}

void
mg_move_tab_family (session *sess, int delta)
{
	if (sess->gui->is_tab)
		chan_move_family (sess->res->tab, delta);
}

void
mg_set_title (GtkWidget *vbox, char *title) /* for non-irc tab/window only */
{
	char *old;

	old = g_object_get_data (G_OBJECT (vbox), "title");
	if (old)
	{
		g_object_set_data_full (G_OBJECT (vbox), "title", g_strdup (title), g_free);
	} else
	{
		gtk_window_set_title (GTK_WINDOW (vbox), title);
	}
}

void
fe_server_callback (server *serv)
{
	joind_close (serv);

	if (serv->gui->chanlist_window)
		mg_close_gen (NULL, serv->gui->chanlist_window);

	if (serv->gui->rawlog_window)
		mg_close_gen (NULL, serv->gui->rawlog_window);

	g_free (serv->gui);
}

/* called when a session is being killed */

void
fe_session_callback (session *sess)
{
	gtk_xtext_buffer_free (sess->res->buffer);
	g_object_unref (G_OBJECT (sess->res->user_model));

	if (sess->res->banlist && sess->res->banlist->window)
		mg_close_gen (NULL, sess->res->banlist->window);

	g_free (sess->res->input_text);
	g_free (sess->res->topic_text);
	g_free (sess->res->limit_text);
	g_free (sess->res->key_text);
	g_free (sess->res->queue_text);
	g_free (sess->res->queue_tip);
	g_free (sess->res->lag_text);
	g_free (sess->res->lag_tip);

	if (sess->gui->bartag)
		fe_timeout_remove (sess->gui->bartag);

	if (sess->gui != &static_mg_gui)
		g_free (sess->gui);
	g_free (sess->res);
}

/* ===== DRAG AND DROP STUFF ===== */

static gboolean
is_child_of (GtkWidget *widget, GtkWidget *parent)
{
	while (widget)
	{
		if (gtk_widget_get_parent (widget) == parent)
			return TRUE;
		widget = gtk_widget_get_parent (widget);
	}
	return FALSE;
}

static void
mg_handle_drop (GtkWidget *widget, int y, int *pos, int *other_pos)
{
	int height;
	session_gui *gui = current_sess->gui;

	height = nx_widget_get_height (widget);

	if (y < height / 2)
	{
		if (is_child_of (widget, gui->vpane_left))
			*pos = 1;	/* top left */
		else
			*pos = 3;	/* top right */
	}
	else
	{
		if (is_child_of (widget, gui->vpane_left))
			*pos = 2;	/* bottom left */
		else
			*pos = 4;	/* bottom right */
	}

	/* both in the same pos? must move one */
	if (*pos == *other_pos)
	{
		switch (*other_pos)
		{
		case 1:
			*other_pos = 2;
			break;
		case 2:
			*other_pos = 1;
			break;
		case 3:
			*other_pos = 4;
			break;
		case 4:
			*other_pos = 3;
			break;
		}
	}

	mg_place_userlist_and_chanview (gui);
}

/* a layout drag was dropped on widget (the drag icon and the kind checks
   are in gtkdnd.c) */

static void
mg_drag_drop_cb (GtkWidget *widget, double y, nx_dnd_kind kind, gpointer user_data)
{
	switch (kind)
	{
	case NX_DND_USERLIST:
		mg_handle_drop (widget, y, &prefs.hex_gui_ulist_pos, &prefs.hex_gui_tab_pos);
		break;
	case NX_DND_CHANVIEW:
		mg_handle_drop (widget, y, &prefs.hex_gui_tab_pos, &prefs.hex_gui_ulist_pos);
		break;
	default:
		break;
	}
}

/* draw highlight rectangle in the destination */

/* GTK2 XOR-drew the drop-target outline straight onto the window from
   drag-motion. GTK3 only allows drawing inside ::draw, so drag-motion now
   records which half to outline and ::draw paints it. DIFFERENCE is cairo's
   nearest thing to GDK_XOR. */

#define MG_DROP_HALF "mg-drop-half"	/* 0 = none, 1 = top, 2 = bottom */
#define MG_DROP_COL "mg-drop-col"

static void
mg_drag_highlight_paint (GtkWidget *widget, cairo_t *cr)
{
	int which = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), MG_DROP_HALF));
	GdkRGBA *col = g_object_get_data (G_OBJECT (widget), MG_DROP_COL);
	int width, height, half, y;

	if (!which || !col)
		return;

	width = nx_widget_get_allocated_width (widget);
	height = nx_widget_get_allocated_height (widget);
	half = height / 2;
	y = (which == 1) ? 0 : half;

	cairo_save (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
	gdk_cairo_set_source_rgba (cr, col);
	cairo_set_line_width (cr, 1);
	/* same two nested 1px rectangles as gdk_draw_rectangle drew */
	if (which == 1)
	{
		cairo_rectangle (cr, 1 + 0.5, 2 + 0.5, width - 3, half - 4);
		cairo_rectangle (cr, 0 + 0.5, 1 + 0.5, width - 1, half - 2);
	} else
	{
		cairo_rectangle (cr, 0 + 0.5, y + 1 + 0.5, width - 1, half - 2);
		cairo_rectangle (cr, 1 + 0.5, y + 2 + 0.5, width - 3, half - 4);
	}
	cairo_stroke (cr);
	cairo_restore (cr);
}

/* GTK4 has no ::draw on someone else's widget. The outline goes in a
   GtkDrawingArea laid over the drop target (same size, input-transparent,
   inside a GtkOverlay that wraps it) and draws with the same cairo code. */
static void
mg_drag_overlay_draw (GtkDrawingArea *da, cairo_t *cr, int w, int h, gpointer target)
{
	mg_drag_highlight_paint (target, cr);
}

static void
mg_drag_overlay_attach (GtkWidget *widget)
{
	GtkWidget *parent = gtk_widget_get_parent (widget), *ov, *da;
	GtkWidget *outer = widget;

	if (g_object_get_data (G_OBJECT (widget), "mg-drop-da"))
		return;
	/* a tree view is drawn by its scrolled window: outline that */
	if (parent && GTK_IS_VIEWPORT (parent))
		parent = gtk_widget_get_parent (outer = parent);
	if (parent && GTK_IS_SCROLLED_WINDOW (parent))
		outer = parent;
	ov = gtk_widget_get_parent (outer);
	if (!GTK_IS_OVERLAY (ov))
		return;	/* not wrapped (see mg_drop_overlay): no outline */
	da = gtk_drawing_area_new ();
	gtk_widget_set_can_target (da, FALSE);
	gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (da), mg_drag_overlay_draw, widget, NULL);
	gtk_overlay_add_overlay (GTK_OVERLAY (ov), da);
	g_object_set_data (G_OBJECT (widget), "mg-drop-da", da);
}

/* wrap child in an overlay so a drop outline can be drawn over it; returns
   what to pack in its place */
GtkWidget *
mg_drop_overlay (GtkWidget *child)
{
	GtkWidget *ov = gtk_overlay_new ();

	gtk_overlay_set_child (GTK_OVERLAY (ov), child);
	gtk_widget_set_hexpand (ov, gtk_widget_get_hexpand (child));
	gtk_widget_set_vexpand (ov, gtk_widget_get_vexpand (child));
	return ov;
}

static void
mg_drag_highlight_clear_cb (GtkWidget *widget, gpointer userdata)
{
	if (g_object_get_data (G_OBJECT (widget), MG_DROP_HALF))
	{
		g_object_set_data (G_OBJECT (widget), MG_DROP_HALF, NULL);
		gtk_widget_queue_draw (widget);
		if (g_object_get_data (G_OBJECT (widget), "mg-drop-da"))
			gtk_widget_queue_draw (g_object_get_data (G_OBJECT (widget), "mg-drop-da"));
	}
}

static void
mg_drag_motion_cb (GtkWidget *widget, double y, nx_dnd_kind kind, gpointer unused)
{
	GdkRGBA *col;
	int which;

	mg_drag_overlay_attach (widget);

	/* a new random colour on every motion, as before */
	col = g_new (GdkRGBA, 1);
	col->red = (rand() % 0xffff) / 65535.0;
	col->green = (rand() % 0xffff) / 65535.0;
	col->blue = (rand() % 0xffff) / 65535.0;
	col->alpha = 1.0;
	g_object_set_data_full (G_OBJECT (widget), MG_DROP_COL, col, g_free);

	/* y is widget-relative in GTK3 for both cases, scrollbar included, so the
	   ox/oy allocation offset the old code needed for scbar is gone */
	which = (y < nx_widget_get_allocated_height (widget) / 2) ? 1 : 2;
	g_object_set_data (G_OBJECT (widget), MG_DROP_HALF, GINT_TO_POINTER (which));
	gtk_widget_queue_draw (widget);
	if (g_object_get_data (G_OBJECT (widget), "mg-drop-da"))
		gtk_widget_queue_draw (g_object_get_data (G_OBJECT (widget), "mg-drop-da"));
}

/* the channel tree and user list take each other's layout drags (and the
   chat scrollbar takes both): drop on a pane half to move them there */
void
mg_layout_drop_dest (GtkWidget *widget, guint kinds)
{
	nx_dnd_layout_dest (widget, kinds, mg_drag_motion_cb, mg_drag_highlight_clear_cb,
							  mg_drag_drop_cb, NULL);
}
