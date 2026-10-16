/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdarg.h>
#include <string.h>
#include "nxlist.h"
#include "gtkcompat.h"

/* like gtkutil_container_add: a box gets it expand+fill, as GTK2 did */
static void
add_to (GtkWidget *box, GtkWidget *child)
{
	if (GTK_IS_BOX (box))
		nx_box_pack_start (box, child, TRUE, TRUE, 0);
#if GTK_CHECK_VERSION(4, 0, 0)
	else if (GTK_IS_FRAME (box))
		gtk_frame_set_child (GTK_FRAME (box), child);
	/* gtk_container_add on a pane filled the first free side */
	else if (GTK_IS_PANED (box) && !gtk_paned_get_start_child (GTK_PANED (box)))
		gtk_paned_set_start_child (GTK_PANED (box), child);
	else if (GTK_IS_PANED (box))
		gtk_paned_set_end_child (GTK_PANED (box), child);
	else
		g_warning ("nxlist: can't add to a %s", G_OBJECT_TYPE_NAME (box));
#else
	else
		gtk_container_add (GTK_CONTAINER (box), child);
#endif
}

#if GTK_CHECK_VERSION(4, 0, 0)

/* ================= GTK4: GListStore rows in a GtkColumnView ============= */

#define NX_TYPE_ROW (nx_row_obj_get_type ())
GType nx_row_obj_get_type (void);
typedef struct _nx_row NxRowObj;
typedef struct { GObjectClass parent_class; } NxRowObjClass;

struct _NxStore
{
	GObject parent;
	gboolean tree;
	int n_values;
	GType *types;
	GListStore *top;			/* of nx_row */
	GPtrArray *pending;		/* frozen: top-level appends not in top yet */
	guint auto_thaw;			/* idle that puts them in */
	gboolean in_auto;
};

struct _nx_row
{
	GObject parent;
	nx_store *store;			/* not reffed: the store outlives its rows */
	nx_row *parent_row;
	GListStore *children;	/* trees only, created on demand */
	GValue *values;
	guint removed : 1;
};

G_DEFINE_TYPE (NxRowObj, nx_row_obj, G_TYPE_OBJECT)

enum { ROW_CHANGED, ROW_LAST };
static guint row_signals[ROW_LAST];

static void
nx_row_obj_finalize (GObject *o)
{
	nx_row *r = (nx_row *) o;
	int i;

	if (r->values)
	{
		for (i = 0; i < r->store->n_values; i++)
			g_value_unset (&r->values[i]);
		g_free (r->values);
	}
	g_clear_object (&r->children);
	G_OBJECT_CLASS (nx_row_obj_parent_class)->finalize (o);
}

static void
nx_row_obj_class_init (NxRowObjClass *k)
{
	G_OBJECT_CLASS (k)->finalize = nx_row_obj_finalize;
	/* a value changed: bound cells refresh */
	row_signals[ROW_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (k), G_SIGNAL_RUN_LAST,
														  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
nx_row_obj_init (NxRowObj *r)
{
}

#define NX_TYPE_STORE (nx_store_get_type ())
G_DECLARE_FINAL_TYPE (NxStore, nx_store, NX, STORE, GObject)
G_DEFINE_TYPE (NxStore, nx_store, G_TYPE_OBJECT)

static void
nx_store_finalize (GObject *o)
{
	nx_store *s = (nx_store *) o;

	nx_store_clear (s);
	if (s->auto_thaw)
		g_source_remove (s->auto_thaw);
	if (s->pending)
		g_ptr_array_free (s->pending, TRUE);
	g_object_unref (s->top);
	g_free (s->types);
	G_OBJECT_CLASS (nx_store_parent_class)->finalize (o);
}

static void
nx_store_class_init (NxStoreClass *k)
{
	G_OBJECT_CLASS (k)->finalize = nx_store_finalize;
}

static void
nx_store_init (NxStore *s)
{
	s->top = g_list_store_new (NX_TYPE_ROW);
}

nx_store *
nx_store_new (gboolean tree, int n_values, ...)
{
	nx_store *s = g_object_new (NX_TYPE_STORE, NULL);
	va_list ap;
	int i;

	s->tree = tree;
	s->n_values = n_values;
	s->types = g_new (GType, n_values);
	va_start (ap, n_values);
	for (i = 0; i < n_values; i++)
		s->types[i] = va_arg (ap, GType);
	va_end (ap);
	return s;
}

/* put queued rows into the list; still frozen (or still waiting for the
   idle) afterwards */
static void
store_flush (nx_store *s)
{
	GPtrArray *p = s->pending;

	if (!p || !p->len)
		return;
	s->pending = g_ptr_array_new_with_free_func (g_object_unref);
	g_list_store_splice (s->top, g_list_model_get_n_items (G_LIST_MODEL (s->top)), 0,
								p->pdata, p->len);
	g_ptr_array_free (p, TRUE);
}

static GListStore *
siblings (nx_store *s, nx_row *parent)
{
	if (!parent)
	{
		/* queued rows count as in the list once anything looks */
		store_flush (s);
		return s->top;
	}
	if (!parent->children)
		parent->children = g_list_store_new (NX_TYPE_ROW);
	return parent->children;
}

static nx_row *
row_new (nx_store *s, nx_row *parent)
{
	nx_row *r = g_object_new (NX_TYPE_ROW, NULL);
	int i;

	r->store = s;
	r->parent_row = parent;
	r->values = g_new0 (GValue, s->n_values);
	for (i = 0; i < s->n_values; i++)
		g_value_init (&r->values[i], s->types[i]);
	return r;
}

static gboolean
store_idle_thaw (gpointer data)
{
	nx_store *s = data;

	s->auto_thaw = 0;
	nx_store_thaw (s);
	return G_SOURCE_REMOVE;
}

nx_row *
nx_store_insert (nx_store *s, nx_row *parent, int pos)
{
	GListStore *l;
	nx_row *r = row_new (s, parent);
	guint n;

	if (s->pending && !parent && pos < 0)
	{
		g_ptr_array_add (s->pending, r);	/* takes the ref */
		return r;
	}
	/* top-level appends go in together at the next idle: one change for
	   the view instead of one per row (thousands, for a channel list) */
	if (!parent && pos < 0 && !s->pending)
	{
		s->in_auto = TRUE;
		nx_store_freeze (s);
		s->in_auto = FALSE;
		s->auto_thaw = g_idle_add_full (G_PRIORITY_HIGH_IDLE, store_idle_thaw, s, NULL);
		g_ptr_array_add (s->pending, r);
		return r;
	}
	l = siblings (s, parent);	/* flushes queued rows, stays frozen */
	n = g_list_model_get_n_items (G_LIST_MODEL (l));
	if (pos < 0 || (guint) pos > n)
		pos = n;
	g_list_store_insert (l, pos, r);
	g_object_unref (r);		/* the list holds it */
	return r;
}

void
nx_store_freeze (nx_store *s)
{
	if (!s->pending)
		s->pending = g_ptr_array_new_with_free_func (g_object_unref);
	/* frozen on purpose: wait for nx_store_thaw, not the idle */
	if (s->auto_thaw && !s->in_auto)
	{
		g_source_remove (s->auto_thaw);
		s->auto_thaw = 0;
	}
}

void
nx_store_thaw (nx_store *s)
{
	GPtrArray *p = s->pending;

	if (!p)
		return;
	s->pending = NULL;
	if (s->auto_thaw)
	{
		g_source_remove (s->auto_thaw);
		s->auto_thaw = 0;
	}
	if (p->len)
		g_list_store_splice (s->top, g_list_model_get_n_items (G_LIST_MODEL (s->top)), 0,
									p->pdata, p->len);
	g_ptr_array_free (p, TRUE);
}

nx_row *
nx_store_append (nx_store *s, nx_row *parent)
{
	return nx_store_insert (s, parent, -1);
}

nx_row *
nx_store_prepend (nx_store *s, nx_row *parent)
{
	return nx_store_insert (s, parent, 0);
}

nx_row *
nx_store_insert_before (nx_store *s, nx_row *parent, nx_row *sibling)
{
	return nx_store_insert (s, parent, sibling ? nx_row_index (sibling) : -1);
}

static void
mark_removed (GListStore *l)
{
	guint i, n = g_list_model_get_n_items (G_LIST_MODEL (l));

	for (i = 0; i < n; i++)
	{
		nx_row *r = g_list_model_get_item (G_LIST_MODEL (l), i);
		r->removed = TRUE;
		if (r->children)
			mark_removed (r->children);
		g_object_unref (r);
	}
}

void
nx_store_clear (nx_store *s)
{
	if (s->pending)
		g_ptr_array_set_size (s->pending, 0);
	mark_removed (s->top);
	g_list_store_remove_all (s->top);
}

int
nx_store_n_rows (nx_store *s, nx_row *parent)
{
	if (parent && !parent->children)
		return 0;
	return g_list_model_get_n_items (G_LIST_MODEL (siblings (s, parent)));
}

nx_row *
nx_store_nth (nx_store *s, nx_row *parent, int n)
{
	nx_row *r;

	if (n < 0 || n >= nx_store_n_rows (s, parent))
		return NULL;
	r = g_list_model_get_item (G_LIST_MODEL (siblings (s, parent)), n);
	g_object_unref (r);		/* still held by the list */
	return r;
}

static void
row_set_valist (nx_row *r, va_list ap)
{
	int col;

	while ((col = va_arg (ap, int)) != -1)
	{
		GValue *v = &r->values[col];
		GType t = r->store->types[col];

		if (t == G_TYPE_STRING)
			g_value_set_string (v, va_arg (ap, const char *));
		else if (t == G_TYPE_INT)
			g_value_set_int (v, va_arg (ap, int));
		else if (t == G_TYPE_UINT)
			g_value_set_uint (v, va_arg (ap, guint));
		else if (t == G_TYPE_BOOLEAN)
			g_value_set_boolean (v, va_arg (ap, gboolean));
		else if (t == G_TYPE_POINTER)
			g_value_set_pointer (v, va_arg (ap, gpointer));
		else if (G_TYPE_IS_OBJECT (t) || G_TYPE_IS_INTERFACE (t))
			g_value_set_object (v, va_arg (ap, gpointer));
		else if (G_TYPE_IS_BOXED (t))
			g_value_set_boxed (v, va_arg (ap, gpointer));
		else
			g_return_if_reached ();
	}
}

void
nx_row_set (nx_row *r, ...)
{
	va_list ap;

	va_start (ap, r);
	row_set_valist (r, ap);
	va_end (ap);
	g_signal_emit (r, row_signals[ROW_CHANGED], 0);
}

void
nx_row_get (nx_row *r, ...)
{
	va_list ap;
	int col;

	va_start (ap, r);
	while ((col = va_arg (ap, int)) != -1)
	{
		GValue *v = &r->values[col];
		GType t = r->store->types[col];
		gpointer out = va_arg (ap, gpointer);

		if (t == G_TYPE_STRING)
			*(char **) out = g_value_dup_string (v);
		else if (t == G_TYPE_INT)
			*(int *) out = g_value_get_int (v);
		else if (t == G_TYPE_UINT)
			*(guint *) out = g_value_get_uint (v);
		else if (t == G_TYPE_BOOLEAN)
			*(gboolean *) out = g_value_get_boolean (v);
		else if (t == G_TYPE_POINTER)
			*(gpointer *) out = g_value_get_pointer (v);
		else if (G_TYPE_IS_OBJECT (t) || G_TYPE_IS_INTERFACE (t))
			*(gpointer *) out = g_value_dup_object (v);
		else if (G_TYPE_IS_BOXED (t))
			*(gpointer *) out = g_value_dup_boxed (v);
	}
	va_end (ap);
}

const char *
nx_row_peek_string (nx_row *r, int col)
{
	return g_value_get_string (&r->values[col]);
}

gpointer
nx_row_get_pointer (nx_row *r, int col)
{
	GValue *v = &r->values[col];

	if (G_VALUE_HOLDS_POINTER (v))
		return g_value_get_pointer (v);
	if (G_VALUE_HOLDS_OBJECT (v))
		return g_value_get_object (v);
	return g_value_get_boxed (v);
}

int
nx_row_get_int (nx_row *r, int col)
{
	return g_value_get_int (&r->values[col]);
}

gboolean
nx_row_get_bool (nx_row *r, int col)
{
	return g_value_get_boolean (&r->values[col]);
}

nx_store *
nx_row_get_store (nx_row *r)
{
	return r->store;
}

nx_row *
nx_row_parent (nx_row *r)
{
	return r->parent_row;
}

int
nx_row_index (nx_row *r)
{
	guint pos;

	if (r->removed || !g_list_store_find (siblings (r->store, r->parent_row), r, &pos))
		return -1;
	return pos;
}

nx_row *
nx_row_next (nx_row *r)
{
	int i = nx_row_index (r);
	return i < 0 ? NULL : nx_store_nth (r->store, r->parent_row, i + 1);
}

nx_row *
nx_row_prev (nx_row *r)
{
	int i = nx_row_index (r);
	return i <= 0 ? NULL : nx_store_nth (r->store, r->parent_row, i - 1);
}

void
nx_row_move (nx_row *r, int pos)
{
	GListStore *l = siblings (r->store, r->parent_row);
	int i = nx_row_index (r);
	int n = g_list_model_get_n_items (G_LIST_MODEL (l));

	if (i < 0)
		return;
	if (pos < 0 || pos >= n)
		pos = n - 1;
	if (pos == i)
		return;
	g_object_ref (r);
	g_list_store_remove (l, i);
	g_list_store_insert (l, pos, r);
	g_object_unref (r);
}

void
nx_row_swap (nx_row *a, nx_row *b)
{
	int ia = nx_row_index (a), ib = nx_row_index (b);

	if (ia < 0 || ib < 0 || a->parent_row != b->parent_row || ia == ib)
		return;
	if (ia > ib)
	{
		nx_row *t = a; int ti = ia;
		a = b; ia = ib; b = t; ib = ti;
	}
	nx_row_move (b, ia);
	nx_row_move (a, ib);
}

void
nx_row_remove (nx_row *r)
{
	int i = nx_row_index (r);

	if (i < 0)
		return;
	r->removed = TRUE;
	if (r->children)
		mark_removed (r->children);
	g_list_store_remove (siblings (r->store, r->parent_row), i);
}

static nx_row *
find_pointer (GListStore *l, int col, gconstpointer ptr)
{
	guint i, n = g_list_model_get_n_items (G_LIST_MODEL (l));

	for (i = 0; i < n; i++)
	{
		nx_row *r = g_list_model_get_item (G_LIST_MODEL (l), i), *found;

		g_object_unref (r);
		if (nx_row_get_pointer (r, col) == ptr)
			return r;
		if (r->children && (found = find_pointer (r->children, col, ptr)))
			return found;
	}
	return NULL;
}

nx_row *
nx_store_find_pointer (nx_store *s, int col, gconstpointer ptr)
{
	return find_pointer (siblings (s, NULL), col, ptr);
}

/* ---- views ---- */

typedef struct
{
	GtkWidget *cv;				/* the GtkColumnView */
	nx_store *store;
	GtkTreeListModel *tree;	/* trees only */
	GtkSortListModel *sorted;
	GtkSelectionModel *sel;
	GtkSelectionMode mode;
	GList *cols;				/* nx_col */
	nx_view_cb select_cb;
	gpointer select_ud;
	nx_row_filter_fn filter;
	gpointer filter_ud;
	nx_row_cb activate_cb;
	gpointer activate_ud;
	int search_col;
	gboolean compact;
	gboolean in_select;		/* our own selection changes */
	gboolean reorderable;
	gboolean had_cursor;		/* got focus once */
	nx_row *last_ok;			/* the last row the select filter allowed */
	GString *typeahead;
	gint64 typeahead_time;
} view_priv;

struct _nx_col
{
	GtkWidget *view;
	GtkColumnViewColumn *col;
	int kind;
	int value_col;
	int icon_col;				/* -1: none */
	int fg_col, attrs_col, weight_col, editable_col, family_col;
	gboolean editable;
	nx_edited_cb edited;
	gpointer edited_ud;
	nx_toggled_cb toggled;
	gpointer toggled_ud;
	int sort_col;
	nx_row_compare_fn sort_fn;
	gpointer sort_ud;
	float title_align;
	gboolean expand_set;
	int min_width;
	float xalign;
	nx_accel_cb accel;
	gpointer accel_ud;
	char **choices;
};

/* GtkTreeView gave the last column the space left over unless some column
   asked to expand; GtkColumnView leaves it empty */
static void
last_fills (view_priv *p)
{
	GList *l;
	gboolean any = FALSE;

	for (l = p->cols; l; l = l->next)
		if (((nx_col *) l->data)->expand_set && gtk_column_view_column_get_expand (((nx_col *) l->data)->col))
			any = TRUE;
	for (l = p->cols; l; l = l->next)
	{
		nx_col *c = l->data;
		if (!c->expand_set)
			gtk_column_view_column_set_expand (c->col, !any && !l->next);
	}
}

static view_priv *
vp (GtkWidget *view)
{
	return g_object_get_data (G_OBJECT (view), "nx-view");
}

/* the model items are GtkTreeListRow for trees, nx_row for lists */
static nx_row *
item_row (gpointer item)
{
	if (GTK_IS_TREE_LIST_ROW (item))
	{
		nx_row *r = gtk_tree_list_row_get_item (item);
		g_object_unref (r);
		return r;
	}
	return item;
}

static nx_row *
pos_row (view_priv *p, guint pos)
{
	gpointer item;
	nx_row *r;

	store_flush (p->store);
	item = g_list_model_get_item (G_LIST_MODEL (p->sel), pos);

	if (!item)
		return NULL;
	r = item_row (item);
	g_object_unref (item);
	return r;
}

/* where the row sits in the view now; G_MAXUINT if it isn't shown */
static guint
row_pos (view_priv *p, nx_row *r)
{
	guint i, n;

	store_flush (p->store);
	n = g_list_model_get_n_items (G_LIST_MODEL (p->sel));

	for (i = 0; i < n; i++)
		if (pos_row (p, i) == r)
			return i;
	return G_MAXUINT;
}

static GListModel *
tree_children (gpointer item, gpointer data)
{
	nx_row *r = item;

	/* NULL: a leaf, no expander (children added later get one when the
	   view is rebuilt; nexchat fills trees before showing them) */
	if (!r->children || g_list_model_get_n_items (G_LIST_MODEL (r->children)) == 0)
		return NULL;
	return g_object_ref (G_LIST_MODEL (r->children));
}

static void
selection_changed (GtkSelectionModel *sm, guint pos, guint n, GtkWidget *view)
{
	view_priv *p = vp (view);

	if (p->filter && !p->in_select)
	{
		/* a row the filter refuses: back to the last allowed one, as
		   GtkTreeSelection's select function never let it change */
		nx_row *r = nx_view_get_selected (view);
		if (r && !p->filter (r, p->filter_ud))
		{
			if (p->last_ok && nx_row_index (p->last_ok) >= 0)
				nx_view_select (view, p->last_ok);
			else
				nx_view_unselect_all (view);
			return;
		}
	}
	if (p->filter)
		p->last_ok = nx_view_get_selected (view);
	if (p->select_cb)
		p->select_cb (view, p->select_ud);
}

static void
build_models (GtkWidget *view)
{
	view_priv *p = vp (view);
	GListModel *base;
	GtkSorter *sorter;

	if (p->store->tree)
	{
		p->tree = gtk_tree_list_model_new (G_LIST_MODEL (g_object_ref (p->store->top)),
													  FALSE, FALSE, tree_children, NULL, NULL);
		base = G_LIST_MODEL (p->tree);
	}
	else
	{
		p->tree = NULL;
		base = G_LIST_MODEL (g_object_ref (p->store->top));
	}
	sorter = gtk_column_view_get_sorter (GTK_COLUMN_VIEW (p->cv));
	p->sorted = gtk_sort_list_model_new (base, sorter ? g_object_ref (sorter) : NULL);
	switch (p->mode)
	{
	case GTK_SELECTION_MULTIPLE:
		p->sel = GTK_SELECTION_MODEL (gtk_multi_selection_new (G_LIST_MODEL (p->sorted)));
		break;
	case GTK_SELECTION_NONE:
		p->sel = GTK_SELECTION_MODEL (gtk_no_selection_new (G_LIST_MODEL (p->sorted)));
		break;
	default:
		p->sel = GTK_SELECTION_MODEL (gtk_single_selection_new (G_LIST_MODEL (p->sorted)));
		gtk_single_selection_set_autoselect (GTK_SINGLE_SELECTION (p->sel), p->mode == GTK_SELECTION_BROWSE);
		gtk_single_selection_set_can_unselect (GTK_SINGLE_SELECTION (p->sel), p->mode != GTK_SELECTION_BROWSE);
		if (p->mode != GTK_SELECTION_BROWSE)
			gtk_single_selection_set_selected (GTK_SINGLE_SELECTION (p->sel), GTK_INVALID_LIST_POSITION);
		break;
	}
	g_signal_connect (p->sel, "selection-changed", G_CALLBACK (selection_changed), view);
	gtk_column_view_set_model (GTK_COLUMN_VIEW (p->cv), p->sel);
	g_object_unref (p->sel);	/* the view holds it */
}

/* cells: one widget tree per column kind, bound to the row's values */

typedef struct
{
	nx_col *col;
	nx_row *row;
	gulong changed_id;
	gboolean selected;
	GtkWidget *icon, *label, *check, *entry, *expander;
} cell;

static void cell_refresh (cell *c);

static void
cell_row_changed (nx_row *r, cell *c)
{
	cell_refresh (c);
}

static void
cell_free (gpointer data)
{
	cell *c = data;

	if (c->row && c->changed_id)
		g_signal_handler_disconnect (c->row, c->changed_id);
	g_free (c);
}

static void
cell_refresh (cell *c)
{
	nx_col *col = c->col;
	nx_row *r = c->row;

	if (!r)
		return;
	if (c->icon)
	{
		GdkPixbuf *pix = col->icon_col >= 0 ? nx_row_get_pointer (r, col->icon_col) : NULL;
		if (pix)
		{
			GdkTexture *t = nx_texture_new_for_pixbuf (pix);
			gtk_image_set_from_paintable (GTK_IMAGE (c->icon), GDK_PAINTABLE (t));
			gtk_image_set_pixel_size (GTK_IMAGE (c->icon), gdk_pixbuf_get_width (pix));
			g_object_unref (t);
			gtk_widget_set_visible (c->icon, TRUE);
		}
		else
		{
			gtk_image_clear (GTK_IMAGE (c->icon));
			gtk_widget_set_visible (c->icon, col->kind == NX_COL_ICON);
		}
	}
	if (c->check)
	{
		g_object_set_data (G_OBJECT (c->check), "nx-quiet", GINT_TO_POINTER (1));
		gtk_check_button_set_active (GTK_CHECK_BUTTON (c->check), nx_row_get_bool (r, col->value_col));
		g_object_set_data (G_OBJECT (c->check), "nx-quiet", NULL);
	}
	if (c->label)
	{
		const char *s = nx_row_peek_string (r, col->value_col);
		PangoAttrList *attrs = NULL;

		if (col->kind == NX_COL_MARKUP)
			gtk_label_set_markup (GTK_LABEL (c->label), s ? s : "");
		else
			gtk_label_set_text (GTK_LABEL (c->label), s ? s : "");

		if (col->attrs_col >= 0 && nx_row_get_pointer (r, col->attrs_col))
			attrs = pango_attr_list_copy (nx_row_get_pointer (r, col->attrs_col));
		if (col->fg_col >= 0 && nx_row_get_pointer (r, col->fg_col) && !c->selected)
		{
			GdkRGBA *rgba = nx_row_get_pointer (r, col->fg_col);
			if (!attrs)
				attrs = pango_attr_list_new ();
			pango_attr_list_insert (attrs, pango_attr_foreground_new (rgba->red * 65535,
											rgba->green * 65535, rgba->blue * 65535));
		}
		if (col->weight_col >= 0)
		{
			if (!attrs)
				attrs = pango_attr_list_new ();
			pango_attr_list_insert (attrs, pango_attr_weight_new (nx_row_get_int (r, col->weight_col)));
		}
		if (col->family_col >= 0 && nx_row_peek_string (r, col->family_col))
		{
			if (!attrs)
				attrs = pango_attr_list_new ();
			pango_attr_list_insert (attrs, pango_attr_family_new (nx_row_peek_string (r, col->family_col)));
		}
		/* markup labels carry their own attributes */
		if (col->kind != NX_COL_MARKUP || attrs)
			gtk_label_set_attributes (GTK_LABEL (c->label), attrs);
		if (attrs)
			pango_attr_list_unref (attrs);
	}
}

static void
check_toggled (GtkCheckButton *b, cell *c)
{
	if (g_object_get_data (G_OBJECT (b), "nx-quiet") || !c->row)
		return;
	/* the store decides: put it back until the handler sets the value */
	g_object_set_data (G_OBJECT (b), "nx-quiet", GINT_TO_POINTER (1));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (b), nx_row_get_bool (c->row, c->col->value_col));
	g_object_set_data (G_OBJECT (b), "nx-quiet", NULL);
	if (c->col->toggled)
		c->col->toggled (c->row, c->col->toggled_ud);
}

static gboolean
cell_is_editable (cell *c)
{
	if (c->col->editable_col >= 0)
		return nx_row_get_bool (c->row, c->col->editable_col);
	return c->col->editable;
}

static void cell_stop_editing (cell *c, gboolean commit);

static void
entry_activate (GtkEntry *e, cell *c)
{
	cell_stop_editing (c, TRUE);
}

static gboolean
entry_key (GtkEventControllerKey *k, guint keyval, guint code, GdkModifierType st, cell *c)
{
	if (keyval == GDK_KEY_Escape)
	{
		cell_stop_editing (c, FALSE);
		return TRUE;
	}
	return FALSE;
}

static void
entry_focus_out (GtkEventControllerFocus *f, cell *c)
{
	/* GTK3 committed an edit when focus went elsewhere */
	if (gtk_widget_get_visible (c->entry))
		cell_stop_editing (c, TRUE);
}

static void
cell_start_editing (cell *c)
{
	const char *s;

	if (!c->entry || !c->row || !cell_is_editable (c))
		return;
	s = nx_row_peek_string (c->row, c->col->value_col);
	if (c->col->kind == NX_COL_ACCEL)
		gtk_editable_set_text (GTK_EDITABLE (c->entry), g_dgettext ("gtk30", "New accelerator…"));
	else if (c->col->kind == NX_COL_COMBO)
	{
		guint i;
		g_object_set_data (G_OBJECT (c->entry), "nx-quiet", GINT_TO_POINTER (1));
		gtk_drop_down_set_selected (GTK_DROP_DOWN (c->entry), GTK_INVALID_LIST_POSITION);
		for (i = 0; c->col->choices && c->col->choices[i]; i++)
			if (s && !strcmp (s, c->col->choices[i]))
				gtk_drop_down_set_selected (GTK_DROP_DOWN (c->entry), i);
		g_object_set_data (G_OBJECT (c->entry), "nx-quiet", NULL);
	}
	else
		gtk_editable_set_text (GTK_EDITABLE (c->entry), s ? s : "");
	gtk_widget_set_visible (c->label, FALSE);
	gtk_widget_set_visible (c->entry, TRUE);
	gtk_widget_grab_focus (c->entry);
	if (c->col->kind == NX_COL_COMBO)
	{
		/* pop the list up, as GTK3 did; the drop-down's first child is its
		   toggle button ("activate" only came in GTK 4.18) */
		GtkWidget *b = gtk_widget_get_first_child (c->entry);
		if (GTK_IS_TOGGLE_BUTTON (b))
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b), TRUE);
	}
	else if (GTK_IS_EDITABLE (c->entry) && c->col->kind != NX_COL_ACCEL)
		gtk_editable_select_region (GTK_EDITABLE (c->entry), 0, -1);
}

/* back to showing the label, without an edit */
static void
cell_end_editing (cell *c)
{
	gtk_widget_set_visible (c->entry, FALSE);
	gtk_widget_set_visible (c->label, TRUE);
	gtk_widget_grab_focus (c->col->view);
}

/* NX_COL_ACCEL: the next key press is the new accelerator, Escape cancels
   and BackSpace clears, as GtkCellRendererAccel did */
static gboolean
accel_key (GtkEventControllerKey *k, guint keyval, guint code, GdkModifierType st, cell *c)
{
	nx_row *r = c->row;

	if (gtk_widget_get_visible (c->entry) == FALSE)
		return FALSE;
	switch (keyval)
	{
	case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: case GDK_KEY_Control_L: case GDK_KEY_Control_R:
	case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: case GDK_KEY_Super_L: case GDK_KEY_Super_R:
	case GDK_KEY_Meta_L: case GDK_KEY_Meta_R: case GDK_KEY_ISO_Level3_Shift:
		return TRUE;	/* a modifier alone: wait for the key */
	case GDK_KEY_Escape:
		cell_end_editing (c);
		return TRUE;
	case GDK_KEY_BackSpace:
		if (!(st & gtk_accelerator_get_default_mod_mask ()))
		{
			cell_end_editing (c);
			if (r && c->col->accel)
				c->col->accel (r, 0, 0, c->col->accel_ud);
			return TRUE;
		}
		break;
	}
	cell_end_editing (c);
	if (r && c->col->accel)
		c->col->accel (r, gdk_keyval_to_lower (keyval), st & gtk_accelerator_get_default_mod_mask (),
							c->col->accel_ud);
	return TRUE;
}

static void
combo_chosen (GtkDropDown *dd, GParamSpec *ps, cell *c)
{
	guint i = gtk_drop_down_get_selected (dd);
	nx_row *r = c->row;

	if (g_object_get_data (G_OBJECT (dd), "nx-quiet") || !gtk_widget_get_visible (c->entry))
		return;
	cell_end_editing (c);
	if (r && i != GTK_INVALID_LIST_POSITION && c->col->edited)
		c->col->edited (r, c->col->choices[i], c->col->edited_ud);
}

static void
combo_focus_out (GtkEventControllerFocus *f, cell *c)
{
	/* focus going into its popup is still choosing */
	if (gtk_widget_get_visible (c->entry) && !gtk_event_controller_focus_contains_focus (f))
		cell_end_editing (c);
}

static void
cell_stop_editing (cell *c, gboolean commit)
{
	char *text;
	nx_row *r = c->row;

	if (!gtk_widget_get_visible (c->entry))
		return;
	if (!GTK_IS_EDITABLE (c->entry) || c->col->kind == NX_COL_ACCEL)
	{
		cell_end_editing (c);
		return;
	}
	text = g_strdup (gtk_editable_get_text (GTK_EDITABLE (c->entry)));
	cell_end_editing (c);
	if (commit && r && c->col->edited)
		c->col->edited (r, text, c->col->edited_ud);
	g_free (text);
}

static void
label_clicked (GtkGestureClick *g, int n, double x, double y, cell *c)
{
	view_priv *p = vp (c->col->view);

	/* GTK3: a click on the cursor row's editable cell starts editing */
	if (n == 1 && c->row && cell_is_editable (c) && nx_view_get_selected (c->col->view) == c->row
		 && g_object_get_data (G_OBJECT (c->label), "nx-was-selected"))
		cell_start_editing (c);
	(void) p;
}

static void
label_pressed (GtkGestureClick *g, int n, double x, double y, cell *c)
{
	/* remember whether the row was already selected before this click */
	g_object_set_data (G_OBJECT (c->label), "nx-was-selected",
							 GINT_TO_POINTER (c->row && nx_view_is_selected (c->col->view, c->row)));
}

/* GTK3 drew a selected row's text in the selection colour, whatever
   foreground the row asked for */
static void
item_selected_changed (GtkListItem *li, GParamSpec *ps, cell *c)
{
	gboolean sel = gtk_list_item_get_selected (li);

	if (sel != c->selected)
	{
		c->selected = sel;
		if (c->col->fg_col >= 0)
			cell_refresh (c);
	}
}

static void
factory_setup (GtkSignalListItemFactory *f, GtkListItem *li, nx_col *col)
{
	view_priv *p = vp (col->view);
	cell *c = g_new0 (cell, 1);
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *top = box;

	c->col = col;
	if (col->min_width > 0)
		gtk_widget_set_size_request (box, col->min_width - 8, -1);	/* less the cell padding */
	if (p->compact)
		gtk_widget_add_css_class (box, "nx-compact");
	if (p->tree && col == g_list_nth_data (p->cols, 0))
	{
		c->expander = gtk_tree_expander_new ();
		gtk_tree_expander_set_child (GTK_TREE_EXPANDER (c->expander), box);
		top = c->expander;
	}
	if (col->kind == NX_COL_TOGGLE)
	{
		c->check = gtk_check_button_new ();
		gtk_widget_set_halign (c->check, GTK_ALIGN_CENTER);
		gtk_widget_set_hexpand (c->check, TRUE);
		g_signal_connect (c->check, "toggled", G_CALLBACK (check_toggled), c);
		gtk_box_append (GTK_BOX (box), c->check);
	}
	else
	{
		if (col->kind == NX_COL_ICON || col->icon_col >= 0)
		{
			c->icon = gtk_image_new ();
			gtk_box_append (GTK_BOX (box), c->icon);
		}
		if (col->kind != NX_COL_ICON)
		{
			GtkGesture *g;
			GtkEventController *k;

			c->label = gtk_label_new (NULL);
			gtk_label_set_xalign (GTK_LABEL (c->label), col->xalign);
			gtk_widget_set_hexpand (c->label, TRUE);
			gtk_box_append (GTK_BOX (box), c->label);
			if (col->editable || col->editable_col >= 0)
			{
				if (col->kind == NX_COL_ACCEL)
				{
					/* an entry only to take focus and keys; it isn't typed in */
					c->entry = gtk_text_new ();
					gtk_editable_set_editable (GTK_EDITABLE (c->entry), FALSE);
					gtk_text_set_propagate_text_width (GTK_TEXT (c->entry), FALSE);
					k = gtk_event_controller_key_new ();
					/* before the window's shortcuts: any key is the new accel */
					gtk_event_controller_set_propagation_phase (k, GTK_PHASE_CAPTURE);
					g_signal_connect (k, "key-pressed", G_CALLBACK (accel_key), c);
					gtk_widget_add_controller (c->entry, k);
					k = gtk_event_controller_focus_new ();
					g_signal_connect_swapped (k, "leave", G_CALLBACK (cell_end_editing), c);
					gtk_widget_add_controller (c->entry, k);
				}
				else if (col->kind == NX_COL_COMBO)
				{
					c->entry = gtk_drop_down_new_from_strings ((const char * const *) col->choices);
					g_signal_connect (c->entry, "notify::selected", G_CALLBACK (combo_chosen), c);
					k = gtk_event_controller_focus_new ();
					g_signal_connect (k, "leave", G_CALLBACK (combo_focus_out), c);
					gtk_widget_add_controller (c->entry, k);
				}
				else
				{
					c->entry = gtk_entry_new ();
					g_signal_connect (c->entry, "activate", G_CALLBACK (entry_activate), c);
					k = gtk_event_controller_key_new ();
					g_signal_connect (k, "key-pressed", G_CALLBACK (entry_key), c);
					gtk_widget_add_controller (c->entry, k);
					k = gtk_event_controller_focus_new ();
					g_signal_connect (k, "leave", G_CALLBACK (entry_focus_out), c);
					gtk_widget_add_controller (c->entry, k);
				}
				gtk_widget_set_hexpand (c->entry, TRUE);
				gtk_widget_set_visible (c->entry, FALSE);
				gtk_box_append (GTK_BOX (box), c->entry);
				g = gtk_gesture_click_new ();
				gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (g), GTK_PHASE_CAPTURE);
				g_signal_connect (g, "pressed", G_CALLBACK (label_pressed), c);
				g_signal_connect (g, "released", G_CALLBACK (label_clicked), c);
				gtk_widget_add_controller (c->label, GTK_EVENT_CONTROLLER (g));
			}
		}
	}
	g_object_set_data_full (G_OBJECT (li), "nx-cell", c, cell_free);
	g_object_set_data (G_OBJECT (top), "nx-cell", c);
	if (col->fg_col >= 0)
		g_signal_connect (li, "notify::selected", G_CALLBACK (item_selected_changed), c);
	gtk_list_item_set_child (li, top);
}

static void
factory_bind (GtkSignalListItemFactory *f, GtkListItem *li, nx_col *col)
{
	cell *c = g_object_get_data (G_OBJECT (li), "nx-cell");
	gpointer item = gtk_list_item_get_item (li);

	c->row = item_row (item);
	if (c->expander)
		gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (c->expander), item);
	c->changed_id = g_signal_connect (c->row, "changed", G_CALLBACK (cell_row_changed), c);
	c->selected = gtk_list_item_get_selected (li);
	cell_refresh (c);
}

static void
factory_unbind (GtkSignalListItemFactory *f, GtkListItem *li, nx_col *col)
{
	cell *c = g_object_get_data (G_OBJECT (li), "nx-cell");

	if (c->entry)
		cell_stop_editing (c, FALSE);
	if (c->row && c->changed_id)
		g_signal_handler_disconnect (c->row, c->changed_id);
	c->changed_id = 0;
	c->row = NULL;
	if (c->expander)
		gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (c->expander), NULL);
}

static void
view_activate (GtkColumnView *cv, guint pos, GtkWidget *view)
{
	view_priv *p = vp (view);
	nx_row *r = pos_row (p, pos);

	if (r && p->activate_cb)
		p->activate_cb (view, r, p->activate_ud);
}

/* type-ahead: GTK3's interactive search jumped to the first row starting
   with what was typed; no popup entry here, just the jump */
static gboolean
view_key (GtkEventControllerKey *k, guint keyval, guint code, GdkModifierType st, GtkWidget *view)
{
	view_priv *p = vp (view);
	gunichar ch = gdk_keyval_to_unicode (keyval);
	guint i, n;
	gint64 now;

	if (p->search_col < 0 || !ch || !g_unichar_isprint (ch) ||
		 (st & (GDK_CONTROL_MASK | GDK_ALT_MASK)))
		return FALSE;
	now = g_get_monotonic_time ();
	if (now - p->typeahead_time > G_USEC_PER_SEC)
		g_string_truncate (p->typeahead, 0);
	p->typeahead_time = now;
	g_string_append_unichar (p->typeahead, ch);
	store_flush (p->store);
	n = g_list_model_get_n_items (G_LIST_MODEL (p->sel));
	for (i = 0; i < n; i++)
	{
		nx_row *r = pos_row (p, i);
		const char *s = nx_row_peek_string (r, p->search_col);
		if (s && !g_ascii_strncasecmp (s, p->typeahead->str, p->typeahead->len))
		{
			nx_view_set_cursor (view, r, NULL);
			nx_view_scroll_to (view, r, NX_SCROLL_CENTER);
			return TRUE;
		}
	}
	return TRUE;
}

/* GtkTreeView put its cursor on the first row when it got focus with no
   cursor yet, and in single/browse mode that selected it */
static void
view_focus_in (GtkEventControllerFocus *f, GtkWidget *view)
{
	view_priv *p = vp (view);
	nx_row *r;

	if (p->had_cursor || p->mode == GTK_SELECTION_NONE)
		return;
	p->had_cursor = TRUE;
	if (nx_view_count_selected (view))
		return;
	r = pos_row (p, 0);
	if (r && (!p->filter || p->filter (r, p->filter_ud)) && p->mode != GTK_SELECTION_MULTIPLE)
		nx_view_select (view, r);
}

/* first shown: at the top, or at the selected row, as GtkTreeView was */
static gboolean
view_scroll_first (gpointer data)
{
	GtkWidget *view = data;
	view_priv *p = vp (view);
	nx_row *r;

	if (p)
	{
		r = nx_view_get_selected (view);
		if (!r)
			r = pos_row (p, 0);
		if (r)
			gtk_column_view_scroll_to (GTK_COLUMN_VIEW (p->cv), row_pos (p, r), NULL,
												GTK_LIST_SCROLL_NONE, NULL);
	}
	g_object_unref (view);
	return G_SOURCE_REMOVE;
}

static void
view_map (GtkWidget *view, gpointer data)
{
	if (g_object_get_data (G_OBJECT (view), "nx-mapped"))
		return;
	g_object_set_data (G_OBJECT (view), "nx-mapped", GINT_TO_POINTER (1));
	g_idle_add (view_scroll_first, g_object_ref (view));
}

static void
view_destroy (GtkWidget *view, gpointer data)
{
	view_priv *p = vp (view);
	GList *l;

	for (l = p->cols; l; l = l->next)
		g_strfreev (((nx_col *) l->data)->choices);
	g_list_free_full (p->cols, g_free);
	p->cols = NULL;
	g_clear_object (&p->store);
	if (p->typeahead)
		g_string_free (p->typeahead, TRUE);
	p->typeahead = NULL;
}

GtkWidget *
nx_view_new (nx_store *store)
{
	view_priv *p = g_new0 (view_priv, 1);
	GtkWidget *cv = gtk_column_view_new (NULL);
	GtkEventController *k;

	p->cv = cv;
	p->store = g_object_ref (store);
	p->mode = GTK_SELECTION_SINGLE;
	p->search_col = -1;
	p->typeahead = g_string_new (NULL);
	g_object_set_data_full (G_OBJECT (cv), "nx-view", p, g_free);
	gtk_widget_add_css_class (cv, "nx-list");
	gtk_column_view_set_reorderable (GTK_COLUMN_VIEW (cv), FALSE);
	gtk_column_view_set_tab_behavior (GTK_COLUMN_VIEW (cv), GTK_LIST_TAB_ITEM);
	build_models (cv);
	g_signal_connect (cv, "activate", G_CALLBACK (view_activate), cv);
	g_signal_connect (cv, "destroy", G_CALLBACK (view_destroy), NULL);
	g_signal_connect (cv, "map", G_CALLBACK (view_map), NULL);
	k = gtk_event_controller_key_new ();
	g_signal_connect (k, "key-pressed", G_CALLBACK (view_key), cv);
	gtk_widget_add_controller (cv, k);
	k = gtk_event_controller_focus_new ();
	g_signal_connect (k, "enter", G_CALLBACK (view_focus_in), cv);
	gtk_widget_add_controller (cv, k);
	return cv;
}

GtkWidget *
nx_view_new_scrolled (GtkWidget *box, nx_store *store)
{
	GtkWidget *win = nx_scrolled_window_new ();
	GtkWidget *view;

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (win),
										  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	add_to (box, win);
	view = nx_view_new (store);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (win), view);
	return view;
}

void
nx_view_set_store (GtkWidget *view, nx_store *store)
{
	view_priv *p = vp (view);

	g_object_ref (store);
	g_clear_object (&p->store);
	p->store = store;
	build_models (view);
}

nx_store *
nx_view_get_store (GtkWidget *view)
{
	return vp (view)->store;
}

void
nx_view_set_headers_visible (GtkWidget *view, gboolean visible)
{
	GtkWidget *c;

	/* GtkColumnView has no switch for it; its header is a child named "header" */
	for (c = gtk_widget_get_first_child (view); c; c = gtk_widget_get_next_sibling (c))
		if (!strcmp (gtk_widget_get_css_name (c), "header"))
			gtk_widget_set_visible (c, visible);
}

nx_col *
nx_view_add_column (GtkWidget *view, const char *title, int kind, int value_col)
{
	view_priv *p = vp (view);
	nx_col *col = g_new0 (nx_col, 1);
	GtkListItemFactory *f = gtk_signal_list_item_factory_new ();

	col->view = view;
	col->kind = kind;
	col->value_col = value_col;
	col->icon_col = col->fg_col = col->attrs_col = col->weight_col = -1;
	col->editable_col = col->family_col = col->sort_col = -1;
	p->cols = g_list_append (p->cols, col);
	g_signal_connect (f, "setup", G_CALLBACK (factory_setup), col);
	g_signal_connect (f, "bind", G_CALLBACK (factory_bind), col);
	g_signal_connect (f, "unbind", G_CALLBACK (factory_unbind), col);
	col->col = gtk_column_view_column_new (title, f);
	gtk_column_view_append_column (GTK_COLUMN_VIEW (p->cv), col->col);
	g_object_unref (col->col);
	last_fills (p);
	return col;
}

void
nx_col_add_icon (nx_col *col, int pixbuf_col)
{
	col->icon_col = pixbuf_col;
}

void
nx_col_bind (nx_col *col, int what, int value_col)
{
	switch (what)
	{
	case NX_BIND_FOREGROUND: col->fg_col = value_col; break;
	case NX_BIND_ATTRS: col->attrs_col = value_col; break;
	case NX_BIND_WEIGHT: col->weight_col = value_col; break;
	case NX_BIND_EDITABLE: col->editable_col = value_col; break;
	case NX_BIND_FAMILY: col->family_col = value_col; break;
	}
}

void
nx_col_set_editable (nx_col *col, gboolean editable)
{
	col->editable = editable;
}

void
nx_col_set_expand (nx_col *col, gboolean expand)
{
	col->expand_set = TRUE;
	gtk_column_view_column_set_expand (col->col, expand);
	last_fills (vp (col->view));
}

void
nx_col_set_resizable (nx_col *col, gboolean resizable)
{
	gtk_column_view_column_set_resizable (col->col, resizable);
}

void
nx_col_set_min_width (nx_col *col, int width)
{
	/* GtkColumnView has no minimum: the cells ask for it instead */
	col->min_width = width;
}

void
nx_col_set_fixed_width (nx_col *col, int width)
{
	gtk_column_view_column_set_fixed_width (col->col, width);
}

/* GtkColumnView has no title alignment: set it on the header buttons'
   labels once they exist (columns map to header buttons in order) */
static void
apply_title_align (GtkWidget *view)
{
	view_priv *p = vp (view);
	GtkWidget *h, *b;
	GList *l;

	for (h = gtk_widget_get_first_child (view); h; h = gtk_widget_get_next_sibling (h))
		if (!strcmp (gtk_widget_get_css_name (h), "header"))
			break;
	if (!h)
		return;
	for (b = gtk_widget_get_first_child (h), l = p->cols; b && l; b = gtk_widget_get_next_sibling (b), l = l->next)
	{
		nx_col *col = l->data;
		GtkWidget *w;
		/* button > box > label (+ sort arrow) */
		for (w = gtk_widget_get_first_child (b); w; w = gtk_widget_get_first_child (w))
		{
			gtk_widget_set_hexpand (w, TRUE);
			if (GTK_IS_LABEL (w))
			{
				gtk_label_set_xalign (GTK_LABEL (w), col->title_align);
				break;
			}
		}
	}
}

void
nx_col_set_xalign (nx_col *col, float xalign)
{
	col->xalign = xalign;
}

void
nx_col_set_title_align (nx_col *col, float xalign)
{
	col->title_align = xalign;
	if (!g_object_get_data (G_OBJECT (col->view), "nx-align-hooked"))
	{
		g_object_set_data (G_OBJECT (col->view), "nx-align-hooked", GINT_TO_POINTER (1));
		g_signal_connect (col->view, "map", G_CALLBACK (apply_title_align), NULL);
	}
	if (gtk_widget_get_mapped (col->view))
		apply_title_align (col->view);
}

static int
compare_values (nx_row *a, nx_row *b, int col)
{
	GType t = a->store->types[col];

	if (t == G_TYPE_STRING)
	{
		const char *sa = nx_row_peek_string (a, col), *sb = nx_row_peek_string (b, col);
		char *ka, *kb;
		int r;
		ka = g_utf8_collate_key (sa ? sa : "", -1);
		kb = g_utf8_collate_key (sb ? sb : "", -1);
		r = strcmp (ka, kb);
		g_free (ka);
		g_free (kb);
		return r;
	}
	if (t == G_TYPE_INT)
		return nx_row_get_int (a, col) - nx_row_get_int (b, col);
	if (t == G_TYPE_UINT)
	{
		guint ua = g_value_get_uint (&a->values[col]), ub = g_value_get_uint (&b->values[col]);
		return ua < ub ? -1 : ua > ub;
	}
	if (t == G_TYPE_BOOLEAN)
		return nx_row_get_bool (a, col) - nx_row_get_bool (b, col);
	return 0;
}

static int
col_compare (gconstpointer a, gconstpointer b, gpointer data)
{
	nx_col *col = data;
	nx_row *ra = item_row ((gpointer) a), *rb = item_row ((gpointer) b);
	int r;

	if (col->sort_fn)
		r = col->sort_fn (ra, rb, col->sort_ud);
	else
		r = compare_values (ra, rb, col->sort_col);
	return r < 0 ? GTK_ORDERING_SMALLER : r > 0 ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL;
}

static void
set_sorter (nx_col *col)
{
	GtkSorter *s = GTK_SORTER (gtk_custom_sorter_new (col_compare, col, NULL));

	gtk_column_view_column_set_sorter (col->col, s);
	g_object_unref (s);
}

void
nx_col_set_sort_column (nx_col *col, int value_col)
{
	col->sort_col = value_col;
	set_sorter (col);
}

void
nx_col_set_sort_func (nx_col *col, nx_row_compare_fn fn, gpointer userdata)
{
	col->sort_fn = fn;
	col->sort_ud = userdata;
	set_sorter (col);
}

void
nx_view_sort_by (GtkWidget *view, nx_col *col, GtkSortType order)
{
	gtk_column_view_sort_by_column (GTK_COLUMN_VIEW (vp (view)->cv), col ? col->col : NULL, order);
}

nx_col *
nx_view_get_column (GtkWidget *view, int n)
{
	return g_list_nth_data (vp (view)->cols, n);
}

void
nx_col_on_edited (nx_col *col, nx_edited_cb cb, gpointer userdata)
{
	col->edited = cb;
	col->edited_ud = userdata;
}

void
nx_col_on_toggled (nx_col *col, nx_toggled_cb cb, gpointer userdata)
{
	col->toggled = cb;
	col->toggled_ud = userdata;
}

void
nx_col_on_accel (nx_col *col, nx_accel_cb cb, gpointer userdata)
{
	col->accel = cb;
	col->accel_ud = userdata;
	col->editable = TRUE;
}

void
nx_col_set_choices (nx_col *col, const char * const *choices)
{
	g_strfreev (col->choices);
	col->choices = g_strdupv ((char **) choices);
	col->editable = TRUE;
}

void
nx_col_set_visible (nx_col *col, gboolean visible)
{
	gtk_column_view_column_set_visible (col->col, visible);
}

void
nx_view_set_select_mode (GtkWidget *view, GtkSelectionMode mode)
{
	view_priv *p = vp (view);

	if (p->mode == mode)
		return;
	p->mode = mode;
	build_models (view);
}

nx_row *
nx_view_get_selected (GtkWidget *view)
{
	view_priv *p = vp (view);
	GtkBitset *set = gtk_selection_model_get_selection (p->sel);
	nx_row *r = NULL;

	if (!gtk_bitset_is_empty (set))
		r = pos_row (p, gtk_bitset_get_minimum (set));
	gtk_bitset_unref (set);
	return r;
}

GList *
nx_view_get_selected_rows (GtkWidget *view)
{
	view_priv *p = vp (view);
	GtkBitset *set = gtk_selection_model_get_selection (p->sel);
	GtkBitsetIter it;
	guint pos;
	GList *l = NULL;

	if (gtk_bitset_iter_init_first (&it, set, &pos))
	{
		do
			l = g_list_prepend (l, pos_row (p, pos));
		while (gtk_bitset_iter_next (&it, &pos));
	}
	gtk_bitset_unref (set);
	return g_list_reverse (l);
}

int
nx_view_count_selected (GtkWidget *view)
{
	GtkBitset *set = gtk_selection_model_get_selection (vp (view)->sel);
	int n = gtk_bitset_get_size (set);

	gtk_bitset_unref (set);
	return n;
}

gboolean
nx_view_is_selected (GtkWidget *view, nx_row *row)
{
	view_priv *p = vp (view);
	guint pos = row_pos (p, row);

	return pos != G_MAXUINT && gtk_selection_model_is_selected (p->sel, pos);
}

void
nx_view_select (GtkWidget *view, nx_row *row)
{
	view_priv *p = vp (view);
	guint pos = row_pos (p, row);

	if (pos == G_MAXUINT)
		return;
	p->in_select = TRUE;
	gtk_selection_model_select_item (p->sel, pos, p->mode != GTK_SELECTION_MULTIPLE);
	p->in_select = FALSE;
	/* a list filled before it's shown starts at its top in GTK3; GtkListView
	   keeps wherever inserting left it: the map handler scrolls */
}

void
nx_view_unselect (GtkWidget *view, nx_row *row)
{
	view_priv *p = vp (view);
	guint pos = row_pos (p, row);

	if (pos != G_MAXUINT)
		gtk_selection_model_unselect_item (p->sel, pos);
}

void
nx_view_select_all (GtkWidget *view)
{
	gtk_selection_model_select_all (vp (view)->sel);
}

void
nx_view_unselect_all (GtkWidget *view)
{
	view_priv *p = vp (view);

	if (GTK_IS_SINGLE_SELECTION (p->sel))
		gtk_single_selection_set_selected (GTK_SINGLE_SELECTION (p->sel), GTK_INVALID_LIST_POSITION);
	else
		gtk_selection_model_unselect_all (p->sel);
}

void
nx_view_on_select (GtkWidget *view, nx_view_cb cb, gpointer userdata)
{
	view_priv *p = vp (view);

	p->select_cb = cb;
	p->select_ud = userdata;
}

void
nx_view_set_select_filter (GtkWidget *view, nx_row_filter_fn fn, gpointer userdata)
{
	view_priv *p = vp (view);

	p->filter = fn;
	p->filter_ud = userdata;
	/* refused rows can't be selected: they're made unselectable in CSS and
	   skipped when a click lands on them */
}

nx_row *
nx_view_row_at (GtkWidget *view, double x, double y)
{
	GtkWidget *w = gtk_widget_pick (view, x, y, GTK_PICK_DEFAULT);

	for (; w && w != view; w = gtk_widget_get_parent (w))
	{
		cell *c = g_object_get_data (G_OBJECT (w), "nx-cell");
		if (c)
			return c->row;
		/* the row widget around the cells: find a cell inside it */
		if (!strcmp (gtk_widget_get_css_name (w), "row"))
		{
			GtkWidget *k;
			for (k = gtk_widget_get_first_child (w); k; k = gtk_widget_get_next_sibling (k))
			{
				GtkWidget *ch = gtk_widget_get_first_child (k);
				if (ch && (c = g_object_get_data (G_OBJECT (ch), "nx-cell")))
					return c->row;
			}
		}
	}
	return NULL;
}

void
nx_view_on_activate (GtkWidget *view, nx_row_cb cb, gpointer userdata)
{
	view_priv *p = vp (view);

	p->activate_cb = cb;
	p->activate_ud = userdata;
}

/* the cell widget of col on the row now at pos, if it's realised */
static cell *
find_cell (GtkWidget *w, nx_col *col, nx_row *row)
{
	GtkWidget *c;
	cell *found;

	for (c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
	{
		cell *cl = g_object_get_data (G_OBJECT (c), "nx-cell");
		if (cl && cl->col == col && cl->row == row)
			return cl;
		if ((found = find_cell (c, col, row)))
			return found;
	}
	return NULL;
}

typedef struct
{
	GtkWidget *view;
	nx_row *row;
	nx_col *col;
} edit_later;

static gboolean
start_edit_idle (gpointer data)
{
	edit_later *e = data;
	cell *c = find_cell (e->view, e->col, e->row);

	if (c)
		cell_start_editing (c);
	g_object_unref (e->view);
	g_object_unref (e->row);
	g_free (e);
	return G_SOURCE_REMOVE;
}

void
nx_view_set_cursor (GtkWidget *view, nx_row *row, nx_col *edit_col)
{
	view_priv *p = vp (view);
	guint pos = row_pos (p, row);

	if (pos == G_MAXUINT)
		return;
	gtk_column_view_scroll_to (GTK_COLUMN_VIEW (p->cv), pos, NULL,
										GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT, NULL);
	if (edit_col)
	{
		/* the cell exists once the view has laid the row out */
		edit_later *e = g_new (edit_later, 1);
		e->view = g_object_ref (view);
		e->row = g_object_ref (row);
		e->col = edit_col;
		g_idle_add (start_edit_idle, e);
	}
}

void
nx_view_scroll_to (GtkWidget *view, nx_row *row, int how)
{
	view_priv *p = vp (view);
	guint pos = row_pos (p, row);
	GtkScrollInfo *si;

	if (pos == G_MAXUINT)
		return;
	switch (how)
	{
	case NX_SCROLL_CENTER:
	case NX_SCROLL_TOP:
	case NX_SCROLL_BOTTOM:
		/* GtkScrollInfo can't place a row; it scrolls just enough, which
		   is close for the lists nexchat has */
	default:
		si = NULL;
		break;
	}
	gtk_column_view_scroll_to (GTK_COLUMN_VIEW (p->cv), pos, NULL, GTK_LIST_SCROLL_NONE, si);
}

static GtkTreeListRow *
tree_row_for (view_priv *p, nx_row *row)
{
	guint i, n;

	if (!p->tree)
		return NULL;
	store_flush (p->store);
	n = g_list_model_get_n_items (G_LIST_MODEL (p->tree));
	for (i = 0; i < n; i++)
	{
		GtkTreeListRow *tr = gtk_tree_list_model_get_row (p->tree, i);
		if (tr && gtk_tree_list_row_get_item (tr) == (gpointer) row)
		{
			g_object_unref (row);	/* get_item reffed it */
			return tr;
		}
		if (tr)
		{
			GObject *it = gtk_tree_list_row_get_item (tr);
			g_object_unref (it);
			g_object_unref (tr);
		}
	}
	return NULL;
}

void
nx_view_expand_all (GtkWidget *view)
{
	view_priv *p = vp (view);
	guint i;

	if (!p->tree)
		return;
	/* rows appear as their parents open, so walk until the end */
	store_flush (p->store);
	for (i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (p->tree)); i++)
	{
		GtkTreeListRow *tr = gtk_tree_list_model_get_row (p->tree, i);
		if (tr)
		{
			gtk_tree_list_row_set_expanded (tr, TRUE);
			g_object_unref (tr);
		}
	}
}

void
nx_view_expand (GtkWidget *view, nx_row *row)
{
	GtkTreeListRow *tr = tree_row_for (vp (view), row);

	if (tr)
	{
		gtk_tree_list_row_set_expanded (tr, TRUE);
		g_object_unref (tr);
	}
}

void
nx_view_collapse (GtkWidget *view, nx_row *row)
{
	GtkTreeListRow *tr = tree_row_for (vp (view), row);

	if (tr)
	{
		gtk_tree_list_row_set_expanded (tr, FALSE);
		g_object_unref (tr);
	}
}

gboolean
nx_view_is_expanded (GtkWidget *view, nx_row *row)
{
	GtkTreeListRow *tr = tree_row_for (vp (view), row);
	gboolean ret = FALSE;

	if (tr)
	{
		ret = gtk_tree_list_row_get_expanded (tr);
		g_object_unref (tr);
	}
	return ret;
}

void
nx_view_set_search_column (GtkWidget *view, int col)
{
	vp (view)->search_col = col;
}

/* reordering: drag a row onto another to put it there (GTK3's
   gtk_tree_view_set_reorderable did the same for flat lists) */
static GdkContentProvider *
reorder_prepare (GtkDragSource *src, double x, double y, GtkWidget *view)
{
	nx_row *r = nx_view_row_at (view, x, y);

	if (!r)
		return NULL;
	g_object_set_data (G_OBJECT (view), "nx-drag-row", r);
	return gdk_content_provider_new_typed (G_TYPE_STRING, "nx-row");
}

static gboolean
reorder_drop (GtkDropTarget *t, const GValue *v, double x, double y, GtkWidget *view)
{
	nx_row *src = g_object_get_data (G_OBJECT (view), "nx-drag-row");
	nx_row *dst = nx_view_row_at (view, x, y);
	view_priv *p = vp (view);

	g_object_set_data (G_OBJECT (view), "nx-drag-row", NULL);
	if (!src || src->removed)
		return FALSE;
	if (!dst)
		nx_row_move (src, -1);
	else if (dst != src && dst->parent_row == src->parent_row)
		nx_row_move (src, nx_row_index (dst));
	nx_view_select (view, src);
	(void) p;
	return TRUE;
}

void
nx_view_set_reorderable (GtkWidget *view, gboolean reorderable)
{
	view_priv *p = vp (view);
	GtkDragSource *ds;
	GtkDropTarget *dt;

	if (!reorderable || p->reorderable)
		return;
	p->reorderable = TRUE;
	ds = gtk_drag_source_new ();
	gtk_drag_source_set_actions (ds, GDK_ACTION_MOVE);
	g_signal_connect (ds, "prepare", G_CALLBACK (reorder_prepare), view);
	gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (ds));
	dt = gtk_drop_target_new (G_TYPE_STRING, GDK_ACTION_MOVE);
	g_signal_connect (dt, "drop", G_CALLBACK (reorder_drop), view);
	gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (dt));
}

void
nx_view_set_compact (GtkWidget *view, gboolean compact)
{
	view_priv *p = vp (view);

	p->compact = compact;
	if (compact)
		gtk_widget_add_css_class (view, "nx-compact");
	else
		gtk_widget_remove_css_class (view, "nx-compact");
}

#else

/* ================= GTK3: GtkListStore/GtkTreeStore + GtkTreeView ======== */

/* A row is a GtkTreeRowReference in disguise: persistent, survives other
   rows' changes. The store keeps a map from row to reference so a row
   pointer handed out stays the same object. */

struct _NxStore
{
	GObject parent;
	GtkTreeModel *model;		/* GtkListStore or GtkTreeStore */
	gboolean tree;
	GHashTable *rows;			/* GtkTreeRowReference* -> nx_row* (owned) */
};

struct _nx_row
{
	nx_store *store;
	GtkTreeRowReference *ref;
};

#define NX_TYPE_STORE (nx_store_get_type ())
G_DECLARE_FINAL_TYPE (NxStore, nx_store, NX, STORE, GObject)
G_DEFINE_TYPE (NxStore, nx_store, G_TYPE_OBJECT)

/* every row's data pointer lives in a hidden last column */
#define ROW_COL(s) (gtk_tree_model_get_n_columns ((s)->model) - 1)

static void
nx_store_finalize (GObject *o)
{
	nx_store *s = (nx_store *) o;

	nx_store_clear (s);
	g_object_unref (s->model);
	G_OBJECT_CLASS (nx_store_parent_class)->finalize (o);
}

static void
nx_store_class_init (NxStoreClass *k)
{
	G_OBJECT_CLASS (k)->finalize = nx_store_finalize;
}

static void
nx_store_init (NxStore *s)
{
}

nx_store *
nx_store_new (gboolean tree, int n_values, ...)
{
	nx_store *s = g_object_new (NX_TYPE_STORE, NULL);
	GType *types = g_new (GType, n_values + 1);
	va_list ap;
	int i;

	va_start (ap, n_values);
	for (i = 0; i < n_values; i++)
		types[i] = va_arg (ap, GType);
	va_end (ap);
	types[n_values] = G_TYPE_POINTER;
	s->tree = tree;
	if (tree)
		s->model = GTK_TREE_MODEL (gtk_tree_store_newv (n_values + 1, types));
	else
		s->model = GTK_TREE_MODEL (gtk_list_store_newv (n_values + 1, types));
	g_free (types);
	return s;
}

GtkTreeModel *
nx_store_get_model (nx_store *s)
{
	return s->model;
}

static gboolean
row_iter (nx_row *r, GtkTreeIter *iter)
{
	GtkTreePath *path;
	gboolean ok;

	if (!r || !r->ref || !gtk_tree_row_reference_valid (r->ref))
		return FALSE;
	path = gtk_tree_row_reference_get_path (r->ref);
	ok = gtk_tree_model_get_iter (r->store->model, iter, path);
	gtk_tree_path_free (path);
	return ok;
}

nx_row *
nx_store_row_from_iter (nx_store *s, GtkTreeIter *iter)
{
	nx_row *r = NULL;

	gtk_tree_model_get (s->model, iter, ROW_COL (s), &r, -1);
	return r;
}

static nx_row *
row_attach (nx_store *s, GtkTreeIter *iter)
{
	nx_row *r = g_new0 (nx_row, 1);
	GtkTreePath *path = gtk_tree_model_get_path (s->model, iter);

	r->store = s;
	r->ref = gtk_tree_row_reference_new (s->model, path);
	gtk_tree_path_free (path);
	if (s->tree)
		gtk_tree_store_set (GTK_TREE_STORE (s->model), iter, ROW_COL (s), r, -1);
	else
		gtk_list_store_set (GTK_LIST_STORE (s->model), iter, ROW_COL (s), r, -1);
	return r;
}

nx_row *
nx_store_insert (nx_store *s, nx_row *parent, int pos)
{
	GtkTreeIter iter, piter;

	if (s->tree)
		gtk_tree_store_insert (GTK_TREE_STORE (s->model), &iter,
									  parent && row_iter (parent, &piter) ? &piter : NULL, pos);
	else
		gtk_list_store_insert (GTK_LIST_STORE (s->model), &iter, pos);
	return row_attach (s, &iter);
}

nx_row *
nx_store_append (nx_store *s, nx_row *parent)
{
	return nx_store_insert (s, parent, -1);
}

/* GtkListStore appends are cheap enough as they come */
void
nx_store_freeze (nx_store *s)
{
}

void
nx_store_thaw (nx_store *s)
{
}

nx_row *
nx_store_prepend (nx_store *s, nx_row *parent)
{
	return nx_store_insert (s, parent, 0);
}

nx_row *
nx_store_insert_before (nx_store *s, nx_row *parent, nx_row *sibling)
{
	return nx_store_insert (s, parent, sibling ? nx_row_index (sibling) : -1);
}

static void row_free_tree (nx_store *s, GtkTreeIter *iter);

static void
row_free (nx_store *s, GtkTreeIter *iter)
{
	nx_row *r = nx_store_row_from_iter (s, iter);

	if (r)
	{
		gtk_tree_row_reference_free (r->ref);
		g_free (r);
	}
}

static void
row_free_tree (nx_store *s, GtkTreeIter *iter)
{
	GtkTreeIter child;

	if (gtk_tree_model_iter_children (s->model, &child, iter))
	{
		do
			row_free_tree (s, &child);
		while (gtk_tree_model_iter_next (s->model, &child));
	}
	row_free (s, iter);
}

void
nx_store_clear (nx_store *s)
{
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_first (s->model, &iter))
	{
		do
			row_free_tree (s, &iter);
		while (gtk_tree_model_iter_next (s->model, &iter));
	}
	if (s->tree)
		gtk_tree_store_clear (GTK_TREE_STORE (s->model));
	else
		gtk_list_store_clear (GTK_LIST_STORE (s->model));
}

int
nx_store_n_rows (nx_store *s, nx_row *parent)
{
	GtkTreeIter piter;

	if (parent)
		return row_iter (parent, &piter) ? gtk_tree_model_iter_n_children (s->model, &piter) : 0;
	return gtk_tree_model_iter_n_children (s->model, NULL);
}

nx_row *
nx_store_nth (nx_store *s, nx_row *parent, int n)
{
	GtkTreeIter iter, piter;

	if (parent && !row_iter (parent, &piter))
		return NULL;
	if (!gtk_tree_model_iter_nth_child (s->model, &iter, parent ? &piter : NULL, n))
		return NULL;
	return nx_store_row_from_iter (s, &iter);
}

void
nx_row_set (nx_row *r, ...)
{
	GtkTreeIter iter;
	va_list ap;

	if (!row_iter (r, &iter))
		return;
	va_start (ap, r);
	if (r->store->tree)
		gtk_tree_store_set_valist (GTK_TREE_STORE (r->store->model), &iter, ap);
	else
		gtk_list_store_set_valist (GTK_LIST_STORE (r->store->model), &iter, ap);
	va_end (ap);
}

void
nx_row_get (nx_row *r, ...)
{
	GtkTreeIter iter;
	va_list ap;

	if (!row_iter (r, &iter))
		return;
	va_start (ap, r);
	gtk_tree_model_get_valist (r->store->model, &iter, ap);
	va_end (ap);
}

/* GtkTreeModel only hands out copies; keep the last one per row+column so
   peeking doesn't leak (the store's own string can't be reached) */
const char *
nx_row_peek_string (nx_row *r, int col)
{
	GtkTreeIter iter;
	char *s = NULL;
	char key[16];

	if (!row_iter (r, &iter))
		return NULL;
	gtk_tree_model_get (r->store->model, &iter, col, &s, -1);
	g_snprintf (key, sizeof key, "nx-peek-%d", col);
	g_object_set_data_full (G_OBJECT (r->store), key, s, g_free);
	return s;
}

gpointer
nx_row_get_pointer (nx_row *r, int col)
{
	GtkTreeIter iter;
	GValue v = G_VALUE_INIT;
	gpointer p = NULL;

	if (!row_iter (r, &iter))
		return NULL;
	gtk_tree_model_get_value (r->store->model, &iter, col, &v);
	if (G_VALUE_HOLDS_POINTER (&v))
		p = g_value_get_pointer (&v);
	else if (G_VALUE_HOLDS_OBJECT (&v))
		p = g_value_get_object (&v);
	else if (G_VALUE_HOLDS_BOXED (&v))
		p = g_value_get_boxed (&v);
	/* objects/boxed stay alive in the store */
	g_value_unset (&v);
	return p;
}

int
nx_row_get_int (nx_row *r, int col)
{
	int v = 0;
	nx_row_get (r, col, &v, -1);
	return v;
}

gboolean
nx_row_get_bool (nx_row *r, int col)
{
	gboolean v = FALSE;
	nx_row_get (r, col, &v, -1);
	return v;
}

nx_store *
nx_row_get_store (nx_row *r)
{
	return r->store;
}

nx_row *
nx_row_next (nx_row *r)
{
	GtkTreeIter iter;

	if (!row_iter (r, &iter) || !gtk_tree_model_iter_next (r->store->model, &iter))
		return NULL;
	return nx_store_row_from_iter (r->store, &iter);
}

nx_row *
nx_row_prev (nx_row *r)
{
	GtkTreeIter iter;

	if (!row_iter (r, &iter) || !gtk_tree_model_iter_previous (r->store->model, &iter))
		return NULL;
	return nx_store_row_from_iter (r->store, &iter);
}

nx_row *
nx_row_parent (nx_row *r)
{
	GtkTreeIter iter, parent;

	if (!row_iter (r, &iter) || !gtk_tree_model_iter_parent (r->store->model, &parent, &iter))
		return NULL;
	return nx_store_row_from_iter (r->store, &parent);
}

int
nx_row_index (nx_row *r)
{
	GtkTreePath *path;
	int i;

	if (!r->ref || !gtk_tree_row_reference_valid (r->ref))
		return -1;
	path = gtk_tree_row_reference_get_path (r->ref);
	i = gtk_tree_path_get_indices (path)[gtk_tree_path_get_depth (path) - 1];
	gtk_tree_path_free (path);
	return i;
}

void
nx_row_move (nx_row *r, int pos)
{
	GtkTreeIter iter, other;
	nx_row *parent = nx_row_parent (r);
	int n = nx_store_n_rows (r->store, parent);
	nx_row *o;

	if (pos < 0 || pos >= n)
		pos = n - 1;
	if (pos == nx_row_index (r) || !row_iter (r, &iter))
		return;
	o = nx_store_nth (r->store, parent, pos);
	if (!o || !row_iter (o, &other))
		return;
	if (r->store->tree)
	{
		if (pos > nx_row_index (r))
			gtk_tree_store_move_after (GTK_TREE_STORE (r->store->model), &iter, &other);
		else
			gtk_tree_store_move_before (GTK_TREE_STORE (r->store->model), &iter, &other);
	}
	else
	{
		if (pos > nx_row_index (r))
			gtk_list_store_move_after (GTK_LIST_STORE (r->store->model), &iter, &other);
		else
			gtk_list_store_move_before (GTK_LIST_STORE (r->store->model), &iter, &other);
	}
}

void
nx_row_swap (nx_row *a, nx_row *b)
{
	GtkTreeIter ia, ib;

	if (!row_iter (a, &ia) || !row_iter (b, &ib))
		return;
	if (a->store->tree)
		gtk_tree_store_swap (GTK_TREE_STORE (a->store->model), &ia, &ib);
	else
		gtk_list_store_swap (GTK_LIST_STORE (a->store->model), &ia, &ib);
}

void
nx_row_remove (nx_row *r)
{
	GtkTreeIter iter;
	nx_store *s = r->store;

	if (!row_iter (r, &iter))
		return;
	row_free_tree (s, &iter);
	if (s->tree)
		gtk_tree_store_remove (GTK_TREE_STORE (s->model), &iter);
	else
		gtk_list_store_remove (GTK_LIST_STORE (s->model), &iter);
}

static nx_row *
find_pointer3 (nx_store *s, GtkTreeIter *parent, int col, gconstpointer ptr)
{
	GtkTreeIter iter;
	nx_row *found;

	if (!gtk_tree_model_iter_children (s->model, &iter, parent))
		return NULL;
	do
	{
		gpointer p = NULL;
		GValue v = G_VALUE_INIT;

		gtk_tree_model_get_value (s->model, &iter, col, &v);
		if (G_VALUE_HOLDS_POINTER (&v))
			p = g_value_get_pointer (&v);
		else if (G_VALUE_HOLDS_OBJECT (&v))
			p = g_value_get_object (&v);
		g_value_unset (&v);
		if (p == ptr)
			return nx_store_row_from_iter (s, &iter);
		if ((found = find_pointer3 (s, &iter, col, ptr)))
			return found;
	}
	while (gtk_tree_model_iter_next (s->model, &iter));
	return NULL;
}

nx_row *
nx_store_find_pointer (nx_store *s, int col, gconstpointer ptr)
{
	return find_pointer3 (s, NULL, col, ptr);
}

/* ---- views ---- */

struct _nx_col
{
	GtkWidget *view;
	GtkTreeViewColumn *col;
	GtkCellRenderer *render;
	GtkCellRenderer *icon;
	int kind;
	int value_col;
	nx_edited_cb edited;
	gpointer edited_ud;
	nx_toggled_cb toggled;
	gpointer toggled_ud;
	nx_row_compare_fn sort_fn;
	gpointer sort_ud;
	int sort_id;
	nx_accel_cb accel;
	gpointer accel_ud;
};

typedef struct
{
	nx_store *store;
	GList *cols;
	nx_view_cb select_cb;
	gpointer select_ud;
	nx_row_filter_fn filter;
	gpointer filter_ud;
	nx_row_cb activate_cb;
	gpointer activate_ud;
	gboolean compact;
} view_priv;

static view_priv *
vp (GtkWidget *view)
{
	return g_object_get_data (G_OBJECT (view), "nx-view");
}

static void
view_priv_free (gpointer data)
{
	view_priv *p = data;

	g_list_free_full (p->cols, g_free);
	g_clear_object (&p->store);
	g_free (p);
}

static void
sel_changed (GtkTreeSelection *sel, GtkWidget *view)
{
	view_priv *p = vp (view);

	if (p->select_cb)
		p->select_cb (view, p->select_ud);
}

static void
row_activated (GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *c, GtkWidget *view)
{
	view_priv *p = vp (view);
	GtkTreeIter iter;

	if (p->activate_cb && gtk_tree_model_get_iter (p->store->model, &iter, path))
		p->activate_cb (view, nx_store_row_from_iter (p->store, &iter), p->activate_ud);
}

GtkWidget *
nx_view_new (nx_store *store)
{
	view_priv *p = g_new0 (view_priv, 1);
	GtkWidget *view = gtk_tree_view_new_with_model (store->model);

	p->store = g_object_ref (store);
	g_object_set_data_full (G_OBJECT (view), "nx-view", p, view_priv_free);
	/* GTK3 searched the first string column by default; nexchat's lists
	   are told what to search */
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (view), FALSE);
	g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (view)), "changed",
							G_CALLBACK (sel_changed), view);
	g_signal_connect (view, "row-activated", G_CALLBACK (row_activated), view);
	return view;
}

GtkWidget *
nx_view_new_scrolled (GtkWidget *box, nx_store *store)
{
	GtkWidget *win = nx_scrolled_window_new ();
	GtkWidget *view;

	add_to (box, win);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (win),
										  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	nx_widget_show (win);
	view = nx_view_new (store);
	gtk_container_add (GTK_CONTAINER (win), view);
	nx_widget_show (view);
	return view;
}

void
nx_view_set_store (GtkWidget *view, nx_store *store)
{
	view_priv *p = vp (view);

	g_object_ref (store);
	g_clear_object (&p->store);
	p->store = store;
	gtk_tree_view_set_model (GTK_TREE_VIEW (view), store->model);
}

nx_store *
nx_view_get_store (GtkWidget *view)
{
	return vp (view)->store;
}

void
nx_view_set_headers_visible (GtkWidget *view, gboolean visible)
{
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), visible);
}

static void
text_edited (GtkCellRendererText *cr, char *pathstr, char *text, nx_col *col)
{
	view_priv *p = vp (col->view);
	GtkTreeIter iter;

	if (col->edited && gtk_tree_model_get_iter_from_string (p->store->model, &iter, pathstr))
		col->edited (nx_store_row_from_iter (p->store, &iter), text, col->edited_ud);
}

static void
accel_edited3 (GtkCellRendererAccel *cr, char *pathstr, guint key, GdkModifierType mods,
					guint code, nx_col *col)
{
	view_priv *p = vp (col->view);
	GtkTreeIter iter;

	if (col->accel && gtk_tree_model_get_iter_from_string (p->store->model, &iter, pathstr))
		col->accel (nx_store_row_from_iter (p->store, &iter), key, mods, col->accel_ud);
}

static void
accel_cleared3 (GtkCellRendererAccel *cr, char *pathstr, nx_col *col)
{
	accel_edited3 (cr, pathstr, 0, 0, 0, col);
}

static void
toggle_toggled (GtkCellRendererToggle *cr, char *pathstr, nx_col *col)
{
	view_priv *p = vp (col->view);
	GtkTreeIter iter;

	if (col->toggled && gtk_tree_model_get_iter_from_string (p->store->model, &iter, pathstr))
		col->toggled (nx_store_row_from_iter (p->store, &iter), col->toggled_ud);
}

nx_col *
nx_view_add_column (GtkWidget *view, const char *title, int kind, int value_col)
{
	view_priv *p = vp (view);
	nx_col *col = g_new0 (nx_col, 1);
	const char *attr;

	col->view = view;
	col->kind = kind;
	col->value_col = value_col;
	switch (kind)
	{
	case NX_COL_TOGGLE:
		col->render = gtk_cell_renderer_toggle_new ();
		attr = "active";
		g_signal_connect (col->render, "toggled", G_CALLBACK (toggle_toggled), col);
		break;
	case NX_COL_ICON:
		col->render = gtk_cell_renderer_pixbuf_new ();
		attr = "pixbuf";
		break;
	case NX_COL_MARKUP:
		col->render = gtk_cell_renderer_text_new ();
		attr = "markup";
		break;
	case NX_COL_ACCEL:
		col->render = gtk_cell_renderer_accel_new ();
		g_object_set (col->render, "editable", TRUE,
#ifndef WIN32
						  "accel-mode", GTK_CELL_RENDERER_ACCEL_MODE_OTHER,
#endif
						  NULL);
		g_signal_connect (col->render, "accel-edited", G_CALLBACK (accel_edited3), col);
		g_signal_connect (col->render, "accel-cleared", G_CALLBACK (accel_cleared3), col);
		attr = "text";
		break;
	case NX_COL_COMBO:
		col->render = gtk_cell_renderer_combo_new ();
		g_object_set (col->render, "has-entry", FALSE, "editable", TRUE, "text-column", 0, NULL);
		attr = "text";
		break;
	default:
		col->render = gtk_cell_renderer_text_new ();
		attr = "text";
		break;
	}
	if (p->compact)
		g_object_set (col->render, "ypad", 0, NULL);
	if (GTK_IS_CELL_RENDERER_TEXT (col->render))
		g_signal_connect (col->render, "edited", G_CALLBACK (text_edited), col);
	col->col = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (col->col, title ? title : "");
	gtk_tree_view_column_pack_start (col->col, col->render, TRUE);
	gtk_tree_view_column_add_attribute (col->col, col->render, attr, value_col);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view), col->col);
	p->cols = g_list_append (p->cols, col);
	return col;
}

GtkTreeViewColumn *
nx_col_get_column (nx_col *col)
{
	return col->col;
}

GtkCellRenderer *
nx_col_get_renderer (nx_col *col)
{
	return col->render;
}

void
nx_col_add_icon (nx_col *col, int pixbuf_col)
{
	col->icon = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (col->col, col->icon, FALSE);
	gtk_cell_layout_reorder (GTK_CELL_LAYOUT (col->col), col->icon, 0);
	gtk_tree_view_column_add_attribute (col->col, col->icon, "pixbuf", pixbuf_col);
}

void
nx_col_bind (nx_col *col, int what, int value_col)
{
	static const char *const names[] = { "foreground-rgba", "attributes", "weight", "editable", "family" };

	gtk_tree_view_column_add_attribute (col->col, col->render, names[what], value_col);
}

void
nx_col_set_editable (nx_col *col, gboolean editable)
{
	g_object_set (col->render, "editable", editable, NULL);
}

void
nx_col_set_expand (nx_col *col, gboolean expand)
{
	gtk_tree_view_column_set_expand (col->col, expand);
}

void
nx_col_set_resizable (nx_col *col, gboolean resizable)
{
	gtk_tree_view_column_set_resizable (col->col, resizable);
}

void
nx_col_set_min_width (nx_col *col, int width)
{
	gtk_tree_view_column_set_min_width (col->col, width);
}

void
nx_col_set_fixed_width (nx_col *col, int width)
{
	gtk_tree_view_column_set_sizing (col->col, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (col->col, width);
}

void
nx_col_set_title_align (nx_col *col, float xalign)
{
	gtk_tree_view_column_set_alignment (col->col, xalign);
}

void
nx_col_set_xalign (nx_col *col, float xalign)
{
	g_object_set (col->render, "xalign", xalign, NULL);
}

void
nx_col_set_sort_column (nx_col *col, int value_col)
{
	gtk_tree_view_column_set_sort_column_id (col->col, value_col);
	col->sort_id = value_col;
}

static int
sort_func3 (GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	nx_col *col = data;
	view_priv *p = vp (col->view);

	return col->sort_fn (nx_store_row_from_iter (p->store, a),
								nx_store_row_from_iter (p->store, b), col->sort_ud);
}

void
nx_col_set_sort_func (nx_col *col, nx_row_compare_fn fn, gpointer userdata)
{
	view_priv *p = vp (col->view);

	col->sort_fn = fn;
	col->sort_ud = userdata;
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (p->store->model), col->value_col,
												sort_func3, col, NULL);
	nx_col_set_sort_column (col, col->value_col);
}

void
nx_view_sort_by (GtkWidget *view, nx_col *col, GtkSortType order)
{
	view_priv *p = vp (view);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (p->store->model),
		col ? col->sort_id : GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order);
}

nx_col *
nx_view_get_column (GtkWidget *view, int n)
{
	return g_list_nth_data (vp (view)->cols, n);
}

void
nx_col_on_edited (nx_col *col, nx_edited_cb cb, gpointer userdata)
{
	col->edited = cb;
	col->edited_ud = userdata;
}

void
nx_col_on_toggled (nx_col *col, nx_toggled_cb cb, gpointer userdata)
{
	col->toggled = cb;
	col->toggled_ud = userdata;
	g_object_set (col->render, "activatable", TRUE, NULL);
}

void
nx_col_on_accel (nx_col *col, nx_accel_cb cb, gpointer userdata)
{
	col->accel = cb;
	col->accel_ud = userdata;
}

void
nx_col_set_choices (nx_col *col, const char * const *choices)
{
	GtkListStore *ls = gtk_list_store_new (1, G_TYPE_STRING);
	GtkTreeIter iter;

	for (; choices && *choices; choices++)
		gtk_list_store_insert_with_values (ls, &iter, -1, 0, *choices, -1);
	g_object_set (col->render, "model", ls, NULL);
	g_object_unref (ls);
}

void
nx_col_set_visible (nx_col *col, gboolean visible)
{
	gtk_tree_view_column_set_visible (col->col, visible);
}

static GtkTreeSelection *
sel_of (GtkWidget *view)
{
	return gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
}

void
nx_view_set_select_mode (GtkWidget *view, GtkSelectionMode mode)
{
	gtk_tree_selection_set_mode (sel_of (view), mode);
}

nx_row *
nx_view_get_selected (GtkWidget *view)
{
	GList *l = nx_view_get_selected_rows (view);
	nx_row *r = l ? l->data : NULL;

	g_list_free (l);
	return r;
}

GList *
nx_view_get_selected_rows (GtkWidget *view)
{
	view_priv *p = vp (view);
	GList *paths = gtk_tree_selection_get_selected_rows (sel_of (view), NULL), *l, *out = NULL;
	GtkTreeIter iter;

	for (l = paths; l; l = l->next)
		if (gtk_tree_model_get_iter (p->store->model, &iter, l->data))
			out = g_list_prepend (out, nx_store_row_from_iter (p->store, &iter));
	g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
	return g_list_reverse (out);
}

int
nx_view_count_selected (GtkWidget *view)
{
	return gtk_tree_selection_count_selected_rows (sel_of (view));
}

gboolean
nx_view_is_selected (GtkWidget *view, nx_row *row)
{
	GtkTreeIter iter;

	return row_iter (row, &iter) && gtk_tree_selection_iter_is_selected (sel_of (view), &iter);
}

void
nx_view_select (GtkWidget *view, nx_row *row)
{
	GtkTreeIter iter;

	if (row_iter (row, &iter))
		gtk_tree_selection_select_iter (sel_of (view), &iter);
}

void
nx_view_unselect (GtkWidget *view, nx_row *row)
{
	GtkTreeIter iter;

	if (row_iter (row, &iter))
		gtk_tree_selection_unselect_iter (sel_of (view), &iter);
}

void
nx_view_select_all (GtkWidget *view)
{
	gtk_tree_selection_select_all (sel_of (view));
}

void
nx_view_unselect_all (GtkWidget *view)
{
	gtk_tree_selection_unselect_all (sel_of (view));
}

void
nx_view_on_select (GtkWidget *view, nx_view_cb cb, gpointer userdata)
{
	view_priv *p = vp (view);

	p->select_cb = cb;
	p->select_ud = userdata;
}

static gboolean
select_filter3 (GtkTreeSelection *sel, GtkTreeModel *m, GtkTreePath *path,
					 gboolean currently, gpointer data)
{
	GtkWidget *view = data;
	view_priv *p = vp (view);
	GtkTreeIter iter;

	if (!gtk_tree_model_get_iter (m, &iter, path))
		return FALSE;
	return p->filter (nx_store_row_from_iter (p->store, &iter), p->filter_ud);
}

void
nx_view_set_select_filter (GtkWidget *view, nx_row_filter_fn fn, gpointer userdata)
{
	view_priv *p = vp (view);

	p->filter = fn;
	p->filter_ud = userdata;
	gtk_tree_selection_set_select_function (sel_of (view), select_filter3, view, NULL);
}

nx_row *
nx_view_row_at (GtkWidget *view, double x, double y)
{
	view_priv *p = vp (view);
	GtkTreePath *path;
	GtkTreeIter iter;
	nx_row *r = NULL;

	if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (view), x, y, &path, NULL, NULL, NULL))
		return NULL;
	if (gtk_tree_model_get_iter (p->store->model, &iter, path))
		r = nx_store_row_from_iter (p->store, &iter);
	gtk_tree_path_free (path);
	return r;
}

void
nx_view_on_activate (GtkWidget *view, nx_row_cb cb, gpointer userdata)
{
	view_priv *p = vp (view);

	p->activate_cb = cb;
	p->activate_ud = userdata;
}

void
nx_view_set_cursor (GtkWidget *view, nx_row *row, nx_col *edit_col)
{
	GtkTreePath *path;

	if (!row->ref || !gtk_tree_row_reference_valid (row->ref))
		return;
	path = gtk_tree_row_reference_get_path (row->ref);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (view), path, edit_col ? edit_col->col : NULL,
									  edit_col != NULL);
	gtk_tree_path_free (path);
}

void
nx_view_scroll_to (GtkWidget *view, nx_row *row, int how)
{
	GtkTreePath *path;

	if (!row->ref || !gtk_tree_row_reference_valid (row->ref))
		return;
	path = gtk_tree_row_reference_get_path (row->ref);
	switch (how)
	{
	case NX_SCROLL_CENTER:
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view), path, NULL, TRUE, 0.5, 0.5);
		break;
	case NX_SCROLL_TOP:
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view), path, NULL, TRUE, 0.0, 0.0);
		break;
	case NX_SCROLL_BOTTOM:
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view), path, NULL, TRUE, 1.0, 0.0);
		break;
	default:
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view), path, NULL, FALSE, 0, 0);
		break;
	}
	gtk_tree_path_free (path);
}

void
nx_view_expand_all (GtkWidget *view)
{
	gtk_tree_view_expand_all (GTK_TREE_VIEW (view));
}

void
nx_view_expand (GtkWidget *view, nx_row *row)
{
	GtkTreePath *path;

	if (!row->ref || !gtk_tree_row_reference_valid (row->ref))
		return;
	path = gtk_tree_row_reference_get_path (row->ref);
	gtk_tree_view_expand_row (GTK_TREE_VIEW (view), path, FALSE);
	gtk_tree_path_free (path);
}

void
nx_view_collapse (GtkWidget *view, nx_row *row)
{
	GtkTreePath *path;

	if (!row->ref || !gtk_tree_row_reference_valid (row->ref))
		return;
	path = gtk_tree_row_reference_get_path (row->ref);
	gtk_tree_view_collapse_row (GTK_TREE_VIEW (view), path);
	gtk_tree_path_free (path);
}

gboolean
nx_view_is_expanded (GtkWidget *view, nx_row *row)
{
	GtkTreePath *path;
	gboolean ret;

	if (!row->ref || !gtk_tree_row_reference_valid (row->ref))
		return FALSE;
	path = gtk_tree_row_reference_get_path (row->ref);
	ret = gtk_tree_view_row_expanded (GTK_TREE_VIEW (view), path);
	gtk_tree_path_free (path);
	return ret;
}

void
nx_view_set_search_column (GtkWidget *view, int col)
{
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (view), col >= 0);
	if (col >= 0)
		gtk_tree_view_set_search_column (GTK_TREE_VIEW (view), col);
}

void
nx_view_set_reorderable (GtkWidget *view, gboolean reorderable)
{
	gtk_tree_view_set_reorderable (GTK_TREE_VIEW (view), reorderable);
}

void
nx_view_set_compact (GtkWidget *view, gboolean compact)
{
	view_priv *p = vp (view);
	GList *l;

	p->compact = compact;
	for (l = p->cols; l; l = l->next)
		g_object_set (((nx_col *) l->data)->render, "ypad", compact ? 0 : 2, NULL);
}

#endif
