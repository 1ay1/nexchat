/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Lists and trees without GtkTreeView.
 *
 * nx_store holds rows of typed values (like GtkListStore/GtkTreeStore);
 * an nx_row stays valid until it's removed, so it can be kept around
 * (GtkTreeIter couldn't be, safely). A view shows a store in columns.
 *
 * GTK3: the store is a GtkListStore/GtkTreeStore and the view a
 * GtkTreeView, set up with the same calls nexchat always made.
 * GTK4: rows are objects in a GListStore (GtkTreeListModel for trees),
 * shown in a GtkColumnView.
 */

#ifndef NEXCHAT_NXLIST_H
#define NEXCHAT_NXLIST_H

#include <gtk/gtk.h>

typedef struct _NxStore nx_store;
typedef struct _nx_row nx_row;
typedef struct _nx_col nx_col;

/* ---- stores ---- */

/* n_values GTypes follow: G_TYPE_STRING, INT, BOOLEAN, POINTER,
   GDK_TYPE_PIXBUF, GDK_TYPE_RGBA, PANGO_TYPE_ATTR_LIST. tree: rows can
   have children. The store is a GObject (g_object_unref it). */
nx_store *nx_store_new (gboolean tree, int n_values, ...);

/* parent NULL: top level */
nx_row *nx_store_append (nx_store *store, nx_row *parent);
nx_row *nx_store_prepend (nx_store *store, nx_row *parent);
nx_row *nx_store_insert (nx_store *store, nx_row *parent, int pos);
/* before sibling (NULL: at the end) */
nx_row *nx_store_insert_before (nx_store *store, nx_row *parent, nx_row *sibling);
void nx_store_clear (nx_store *store);
int nx_store_n_rows (nx_store *store, nx_row *parent);
nx_row *nx_store_nth (nx_store *store, nx_row *parent, int n);
#define nx_store_first(s) nx_store_nth ((s), NULL, 0)

/* column/value pairs ending with -1, as gtk_list_store_set */
void nx_row_set (nx_row *row, ...);
/* column/&out pairs ending with -1, as gtk_tree_model_get: strings come
   back g_strdup'd, objects and boxed values reffed/copied */
void nx_row_get (nx_row *row, ...);
/* no copies: valid while the row's value stays */
const char *nx_row_peek_string (nx_row *row, int col);
gpointer nx_row_get_pointer (nx_row *row, int col);
int nx_row_get_int (nx_row *row, int col);
gboolean nx_row_get_bool (nx_row *row, int col);

nx_store *nx_row_get_store (nx_row *row);
nx_row *nx_row_next (nx_row *row);
nx_row *nx_row_prev (nx_row *row);
nx_row *nx_row_parent (nx_row *row);
/* position among its siblings */
int nx_row_index (nx_row *row);
/* move to position pos among its siblings */
void nx_row_move (nx_row *row, int pos);
void nx_row_swap (nx_row *a, nx_row *b);
/* removes its children too; the row (and theirs) is freed */
void nx_row_remove (nx_row *row);

/* first row whose pointer column col is ptr, at any depth; NULL if none */
nx_row *nx_store_find_pointer (nx_store *store, int col, gconstpointer ptr);

/* ---- views ---- */

/* the view widget, not packed anywhere */
GtkWidget *nx_view_new (nx_store *store);
/* the view in a scrolled window, added to box like gtkutil_treeview_new did */
GtkWidget *nx_view_new_scrolled (GtkWidget *box, nx_store *store);
void nx_view_set_store (GtkWidget *view, nx_store *store);
nx_store *nx_view_get_store (GtkWidget *view);
void nx_view_set_headers_visible (GtkWidget *view, gboolean visible);

enum
{
	NX_COL_TEXT,		/* string */
	NX_COL_MARKUP,		/* string with pango markup */
	NX_COL_TOGGLE,		/* boolean, a check box */
	NX_COL_ICON,			/* GdkPixbuf */
	NX_COL_ACCEL,		/* string; editing grabs a key combo (nx_col_on_accel) */
	NX_COL_COMBO		/* string; editing picks from nx_col_set_choices */
};

/* value_col: the store column shown */
nx_col *nx_view_add_column (GtkWidget *view, const char *title, int kind, int value_col);
/* an icon (GdkPixbuf column) before the column's text, in the same column */
void nx_col_add_icon (nx_col *col, int pixbuf_col);

enum
{
	NX_BIND_FOREGROUND,	/* GdkRGBA * column: text colour */
	NX_BIND_ATTRS,			/* PangoAttrList * column */
	NX_BIND_WEIGHT,		/* int column (PangoWeight) */
	NX_BIND_EDITABLE,		/* boolean column: this row's cell is editable */
	NX_BIND_FAMILY			/* string column: font family */
};
void nx_col_bind (nx_col *col, int what, int value_col);

void nx_col_set_editable (nx_col *col, gboolean editable);
void nx_col_set_expand (nx_col *col, gboolean expand);
void nx_col_set_resizable (nx_col *col, gboolean resizable);
void nx_col_set_min_width (nx_col *col, int width);
void nx_col_set_fixed_width (nx_col *col, int width);
/* header title alignment, 0.0 left .. 1.0 right */
void nx_col_set_title_align (nx_col *col, float xalign);
/* the cells' text alignment, 0.0 left .. 1.0 right */
void nx_col_set_xalign (nx_col *col, float xalign);
/* clicking the header sorts by value_col */
void nx_col_set_sort_column (nx_col *col, int value_col);
/* ... or with a compare function */
typedef int (*nx_row_compare_fn) (nx_row *a, nx_row *b, gpointer userdata);
void nx_col_set_sort_func (nx_col *col, nx_row_compare_fn fn, gpointer userdata);
nx_col *nx_view_get_column (GtkWidget *view, int n);

typedef void (*nx_edited_cb) (nx_row *row, const char *text, gpointer userdata);
typedef void (*nx_toggled_cb) (nx_row *row, gpointer userdata);
void nx_col_on_edited (nx_col *col, nx_edited_cb cb, gpointer userdata);
void nx_col_on_toggled (nx_col *col, nx_toggled_cb cb, gpointer userdata);
/* NX_COL_ACCEL: the key pressed while editing */
typedef void (*nx_accel_cb) (nx_row *row, guint key, GdkModifierType mods, gpointer userdata);
void nx_col_on_accel (nx_col *col, nx_accel_cb cb, gpointer userdata);
/* NX_COL_COMBO: the choices (NULL-terminated, copied); the choice comes
   to nx_col_on_edited */
void nx_col_set_choices (nx_col *col, const char * const *choices);
void nx_col_set_visible (nx_col *col, gboolean visible);

/* selection: GTK_SELECTION_SINGLE (default), BROWSE, MULTIPLE, NONE */
void nx_view_set_select_mode (GtkWidget *view, GtkSelectionMode mode);
nx_row *nx_view_get_selected (GtkWidget *view);
/* selected rows, in view order; g_list_free the list only */
GList *nx_view_get_selected_rows (GtkWidget *view);
int nx_view_count_selected (GtkWidget *view);
gboolean nx_view_is_selected (GtkWidget *view, nx_row *row);
void nx_view_select (GtkWidget *view, nx_row *row);
void nx_view_unselect (GtkWidget *view, nx_row *row);
void nx_view_select_all (GtkWidget *view);
void nx_view_unselect_all (GtkWidget *view);
typedef void (*nx_view_cb) (GtkWidget *view, gpointer userdata);
void nx_view_on_select (GtkWidget *view, nx_view_cb cb, gpointer userdata);
/* rows the user can't select (category rows in Preferences) */
typedef gboolean (*nx_row_filter_fn) (nx_row *row, gpointer userdata);
void nx_view_set_select_filter (GtkWidget *view, nx_row_filter_fn fn, gpointer userdata);

/* the row at x/y as given to an nx_on_click handler on the view */
nx_row *nx_view_row_at (GtkWidget *view, double x, double y);
/* double click / Enter on a row */
typedef void (*nx_row_cb) (GtkWidget *view, nx_row *row, gpointer userdata);
void nx_view_on_activate (GtkWidget *view, nx_row_cb cb, gpointer userdata);

/* make row the cursor (selecting it); edit_col non-NULL starts editing it */
void nx_view_set_cursor (GtkWidget *view, nx_row *row, nx_col *edit_col);
enum
{
	NX_SCROLL_MIN,			/* just enough to show it */
	NX_SCROLL_CENTER,
	NX_SCROLL_TOP,
	NX_SCROLL_BOTTOM
};
void nx_view_scroll_to (GtkWidget *view, nx_row *row, int how);

/* trees */
void nx_view_expand_all (GtkWidget *view);
void nx_view_expand (GtkWidget *view, nx_row *row);
void nx_view_collapse (GtkWidget *view, nx_row *row);
gboolean nx_view_is_expanded (GtkWidget *view, nx_row *row);

/* type-ahead find on a string column; -1 turns it off */
void nx_view_set_search_column (GtkWidget *view, int col);
/* rows can be dragged to a new place */
void nx_view_set_reorderable (GtkWidget *view, gboolean reorderable);
/* sort by col now, as if its header was clicked */
void nx_view_sort_by (GtkWidget *view, nx_col *col, GtkSortType order);
/* rows without the usual padding (prefs.hex_gui_compact) */
void nx_view_set_compact (GtkWidget *view, gboolean compact);

#if !GTK_CHECK_VERSION(4, 0, 0)
/* GTK3 only, for the tweaks only GtkTreeView had */
GtkTreeModel *nx_store_get_model (nx_store *store);
GtkTreeViewColumn *nx_col_get_column (nx_col *col);
GtkCellRenderer *nx_col_get_renderer (nx_col *col);
nx_row *nx_store_row_from_iter (nx_store *store, GtkTreeIter *iter);
#endif

#endif
