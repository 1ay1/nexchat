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
#include "../common/fe.h"

typedef void (*filereqcallback) (void *, char *file);

/* gtk_hbox_new/gtk_vbox_new (homogeneous, spacing) for GTK3+ */
GtkWidget *gtkutil_box_new (GtkOrientation orientation, gboolean homogeneous, int spacing);
/* gtk_container_add with GTK2 semantics for boxes (expand + fill) */
void gtkutil_container_add (GtkWidget *container, GtkWidget *child);

/* GTK3 hides images on buttons and menu items unless the theme/setting
   asks for them (gtk-button-images / gtk-menu-images default to FALSE);
   HexChat on GTK2 showed them everywhere. Call once at startup. */
void gtkutil_force_images (void);

/* GtkStock is deprecated in GTK3 and gone in GTK4. These take the old
   GTK_STOCK_* id (or "gtk-*" string) and use the icon name GTK3's own stock
   table maps it to (gtk/deprecated/gtkiconfactory.c), so every icon is the
   same image it was. Unknown ids pass through unchanged as icon names. */
/* GTK's GTK_STOCK_* macros carry deprecation attributes; ours are plain
   strings with the same values, so call sites read the same. */
#ifdef GTK_DISABLE_DEPRECATED
#define GTK_STOCK_ABOUT "gtk-about"
#define GTK_STOCK_ADD "gtk-add"
#define GTK_STOCK_APPLY "gtk-apply"
#define GTK_STOCK_CANCEL "gtk-cancel"
#define GTK_STOCK_CLEAR "gtk-clear"
#define GTK_STOCK_CLOSE "gtk-close"
#define GTK_STOCK_CONNECT "gtk-connect"
#define GTK_STOCK_COPY "gtk-copy"
#define GTK_STOCK_DELETE "gtk-delete"
#define GTK_STOCK_DIALOG_ERROR "gtk-dialog-error"
#define GTK_STOCK_DIALOG_WARNING "gtk-dialog-warning"
#define GTK_STOCK_DISCONNECT "gtk-disconnect"
#define GTK_STOCK_FIND "gtk-find"
#define GTK_STOCK_GO_BACK "gtk-go-back"
#define GTK_STOCK_GO_DOWN "gtk-go-down"
#define GTK_STOCK_GO_FORWARD "gtk-go-forward"
#define GTK_STOCK_GO_UP "gtk-go-up"
#define GTK_STOCK_HELP "gtk-help"
#define GTK_STOCK_INDEX "gtk-index"
#define GTK_STOCK_JUMP_TO "gtk-jump-to"
#define GTK_STOCK_JUSTIFY_LEFT "gtk-justify-left"
#define GTK_STOCK_MEDIA_PLAY "gtk-media-play"
#define GTK_STOCK_NETWORK "gtk-network"
#define GTK_STOCK_NEW "gtk-new"
#define GTK_STOCK_NO "gtk-no"
#define GTK_STOCK_OK "gtk-ok"
#define GTK_STOCK_OPEN "gtk-open"
#define GTK_STOCK_PREFERENCES "gtk-preferences"
#define GTK_STOCK_QUIT "gtk-quit"
#define GTK_STOCK_REDO "gtk-redo"
#define GTK_STOCK_REFRESH "gtk-refresh"
#define GTK_STOCK_REMOVE "gtk-remove"
#define GTK_STOCK_REVERT_TO_SAVED "gtk-revert-to-saved"
#define GTK_STOCK_SAVE "gtk-save"
#define GTK_STOCK_SAVE_AS "gtk-save-as"
#define GTK_STOCK_SPELL_CHECK "gtk-spell-check"
#define GTK_STOCK_YES "gtk-yes"
#endif
const char *gtkutil_stock_icon_name (const char *stock_id);
GtkWidget *gtkutil_image_new_from_stock (const char *stock_id, GtkIconSize size);
/* label + mnemonic GTK used for a stock button (_OK, _Cancel, ...), translated */
const char *gtkutil_stock_label (const char *stock_id);
GtkWidget *gtkutil_button_new_from_stock (const char *stock_id);
/* gtk_dialog_add_button (dialog, GTK_STOCK_X, resp) without GtkStock: same
   label and icon the stock button had */
GtkWidget *gtkutil_dialog_add_stock_button (GtkDialog *dialog, const char *stock_id, int response);
/* gtk_widget_render_icon (w, stock, size, NULL): a new pixbuf, from the theme */
GdkPixbuf *gtkutil_render_icon (GtkWidget *widget, const char *stock_id, GtkIconSize size, const char *unused);

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
void gtkutil_copy_to_clipboard (GtkWidget *widget, GdkAtom selection,
                                const gchar *str);
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
