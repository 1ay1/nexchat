/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

#ifndef HEXCHAT_GTKUTIL_H
#define HEXCHAT_GTKUTIL_H

#include <gtk/gtk.h>
#include "gtkcompat.h"
#include "gtkevents.h"
#include "nxdialog.h"
#include "../common/fe.h"

typedef void (*filereqcallback) (void *, char *file);

/* gtk_hbox_new/gtk_vbox_new (homogeneous, spacing) for GTK3+ */
GtkWidget *gtkutil_box_new (GtkOrientation orientation, gboolean homogeneous, int spacing);
/* gtk_container_add with GTK2 semantics for boxes (expand + fill) */
void gtkutil_container_add (GtkWidget *container, GtkWidget *child);

/* gtk_dialog_run() replacement (it's gone in GTK4): show the dialog, spin a
   nested main loop until it responds or is destroyed, return the response
   id (GTK_RESPONSE_NONE if destroyed). Only for the few callers that must
   block - startup messages before the main loop, and FE_MSG_WAIT. */
int gtkutil_dialog_run_blocking (NxDialog *dialog);

/* Window position and attention. On X11 and Windows these do what HexChat
   always did. Wayland doesn't let clients place windows or read their
   position, so there move is a no-op and get_position returns FALSE (the
   saved prefs are left alone). GTK4 has no gtk_window_move at all; phase 4
   implements these per backend (ROADMAP section 9). */
gboolean gtkutil_window_can_position (GtkWindow *win);
void gtkutil_window_move (GtkWindow *win, int x, int y);
gboolean gtkutil_window_get_position (GtkWindow *win, int *x, int *y);
/* flash the taskbar entry / set urgency (TRUE) or clear it (FALSE) */
void gtkutil_window_set_urgent (GtkWindow *win, gboolean urgent);

/* GtkImageMenuItem (deprecated in GTK3, gone in GTK4): a GtkMenuItem whose
   child is [image][label] laid out like GtkImageMenuItem did (16px icon
   column, 6px gap). image may be NULL; label is a mnemonic, or markup if
   is_markup. Use gtkutil_menu_item_get_label() to reach the label. */
GtkWidget *gtkutil_menu_item_new_with_image (const char *label, gboolean is_markup, GtkWidget *image);
GtkWidget *gtkutil_menu_item_get_label (GtkWidget *item);

/* GTK3 hides images on buttons and menu items unless the theme/setting
   asks for them (gtk-button-images / gtk-menu-images default to FALSE);
   HexChat on GTK2 showed them everywhere. Call once at startup. */
void gtkutil_force_images (void);

/* GtkStock is deprecated in GTK3 and gone in GTK4. These take the old
   GTK_STOCK_* id (or "gtk-*" string) and use the icon name GTK3's own stock
   table maps it to (gtk/deprecated/gtkiconfactory.c), so every icon is the
   same image it was. Unknown ids pass through unchanged as icon names. */
/* GTK's GTK_STOCK_* macros carry deprecation attributes and are gone in
   GTK4; ours are plain strings with the same values. */
#define NX_STOCK_ABOUT "gtk-about"
#define NX_STOCK_ADD "gtk-add"
#define NX_STOCK_APPLY "gtk-apply"
#define NX_STOCK_CANCEL "gtk-cancel"
#define NX_STOCK_CLEAR "gtk-clear"
#define NX_STOCK_CLOSE "gtk-close"
#define NX_STOCK_CONNECT "gtk-connect"
#define NX_STOCK_COPY "gtk-copy"
#define NX_STOCK_DELETE "gtk-delete"
#define NX_STOCK_DIALOG_ERROR "gtk-dialog-error"
#define NX_STOCK_DIALOG_WARNING "gtk-dialog-warning"
#define NX_STOCK_DISCONNECT "gtk-disconnect"
#define NX_STOCK_FIND "gtk-find"
#define NX_STOCK_GO_BACK "gtk-go-back"
#define NX_STOCK_GO_DOWN "gtk-go-down"
#define NX_STOCK_GO_FORWARD "gtk-go-forward"
#define NX_STOCK_GO_UP "gtk-go-up"
#define NX_STOCK_HELP "gtk-help"
#define NX_STOCK_INDEX "gtk-index"
#define NX_STOCK_JUMP_TO "gtk-jump-to"
#define NX_STOCK_JUSTIFY_LEFT "gtk-justify-left"
#define NX_STOCK_MEDIA_PLAY "gtk-media-play"
#define NX_STOCK_NETWORK "gtk-network"
#define NX_STOCK_NEW "gtk-new"
#define NX_STOCK_NO "gtk-no"
#define NX_STOCK_OK "gtk-ok"
#define NX_STOCK_OPEN "gtk-open"
#define NX_STOCK_PREFERENCES "gtk-preferences"
#define NX_STOCK_QUIT "gtk-quit"
#define NX_STOCK_REDO "gtk-redo"
#define NX_STOCK_REFRESH "gtk-refresh"
#define NX_STOCK_REMOVE "gtk-remove"
#define NX_STOCK_REVERT_TO_SAVED "gtk-revert-to-saved"
#define NX_STOCK_SAVE "gtk-save"
#define NX_STOCK_SAVE_AS "gtk-save-as"
#define NX_STOCK_SPELL_CHECK "gtk-spell-check"
#define NX_STOCK_YES "gtk-yes"

const char *gtkutil_stock_icon_name (const char *stock_id);
GtkWidget *gtkutil_image_new_from_stock (const char *stock_id, nx_icon_size size);
/* label + mnemonic GTK used for a stock button (_OK, _Cancel, ...), translated */
const char *gtkutil_stock_label (const char *stock_id);
GtkWidget *gtkutil_button_new_from_stock (const char *stock_id);
/* gtk_dialog_add_button (dialog, GTK_STOCK_X, resp) without GtkStock: same
   label and icon the stock button had */
GtkWidget *gtkutil_dialog_add_stock_button (NxDialog *dialog, const char *stock_id, int response);
/* gtk_widget_render_icon (w, stock, size, NULL): a new pixbuf, from the theme */
GdkPixbuf *gtkutil_render_icon (GtkWidget *widget, const char *stock_id, nx_icon_size size, const char *unused);

/* GtkTable's attach options. GtkAttachOptions lives in GTK3's deprecated
   gtktable.h and is gone in GTK4, so we carry our own copy. */
#define GU_EXPAND (1 << 0)
#define GU_SHRINK (1 << 1)
#define GU_FILL   (1 << 2)

/* gtk_table_attach() on a GtkGrid: same cell coordinates (left/right/top/
   bottom edges), EXPAND -> h/vexpand, FILL -> align FILL else CENTER,
   padding -> margins on both sides. SHRINK has no GTK3 meaning. */
void gtkutil_grid_attach (GtkWidget *grid, GtkWidget *child,
								  int left, int right, int top, int bottom,
								  int xoptions, int yoptions, int xpadding, int ypadding);

void gtkutil_file_req (GtkWindow *parent, const char *title, void *callback, void *userdata, char *filter, char *extensions, int flags);
void gtkutil_destroy (GtkWidget * igad, GtkWidget * dgad);
void gtkutil_destroy_on_esc (GtkWidget *win);
GtkWidget *gtkutil_button (GtkWidget *box, char *stock, char *tip, void *callback,
				 void *userdata, char *labeltext);
void gtkutil_label_new (char *text, GtkWidget * box);
GtkWidget *gtkutil_entry_new (int max, GtkWidget * box, void *callback,
										gpointer userdata);
void show_and_unfocus (GtkWidget * wid);
void gtkutil_set_icon (GtkWidget *win);
GtkWidget *gtkutil_window_new (char *title, char *role, int width, int height, int flags);
void gtkutil_copy_to_clipboard (GtkWidget *widget, const gchar *str);
GtkWidget *gtkutil_treeview_new (GtkWidget *box, GtkTreeModel *model,
                                 GtkTreeCellDataFunc mapper, ...);
gboolean gtkutil_treemodel_string_to_iter (GtkTreeModel *model, gchar *pathstr, GtkTreeIter *iter_ret);
gboolean gtkutil_treeview_get_selected_iter (GtkTreeView *view, GtkTreeIter *iter_ret);
gboolean gtkutil_treeview_get_selected (GtkTreeView *view, GtkTreeIter *iter_ret, ...);
gboolean gtkutil_tray_icon_supported (GtkWindow *window);

#if defined (WIN32) || defined (__APPLE__)
gboolean gtkutil_find_font (const char *fontname);
#endif

#endif
