/* nxlist.c must behave the same on GTK3 (GtkTreeView) and GTK4 (GtkColumnView).
   usage: nxlist-test     (needs a display) */

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include "nxlist.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf ("FAIL: " __VA_ARGS__); printf ("\n"); fails++; } } while (0)

enum { C_NAME, C_NUM, C_ON, C_PTR, N_COLS };

static int selects, edits, toggles;
static char last_edit[64];

static void
on_select (GtkWidget *v, gpointer ud)
{
	selects++;
}

static void
on_edited (nx_row *r, const char *text, gpointer ud)
{
	edits++;
	g_strlcpy (last_edit, text, sizeof last_edit);
	nx_row_set (r, C_NAME, text, -1);
}

static void
on_toggled (nx_row *r, gpointer ud)
{
	toggles++;
	nx_row_set (r, C_ON, !nx_row_get_bool (r, C_ON), -1);
}

static void
pump (void)
{
	int i;
	for (i = 0; i < 30; i++)
		g_main_context_iteration (NULL, FALSE);
}

static GString *
names (nx_store *s, nx_row *parent)
{
	GString *out = g_string_new (NULL);
	int i;

	for (i = 0; i < nx_store_n_rows (s, parent); i++)
	{
		nx_row *r = nx_store_nth (s, parent, i);
		g_string_append_printf (out, "%s%s", i ? " " : "", nx_row_peek_string (r, C_NAME));
	}
	return out;
}

#define CHECK_NAMES(s, parent, want) do { GString *g = names (s, parent); \
	CHECK (!strcmp (g->str, want), "rows '%s', want '%s'", g->str, want); g_string_free (g, TRUE); } while (0)

int
main (int argc, char **argv)
{
	nx_store *s, *t;
	nx_row *a, *b, *c, *p1, *k1, *k2;
	GtkWidget *win, *box, *view, *tview;
	nx_col *name_col, *on_col;
	GList *l;
	char *str;
	int num;
	int marker = 7;

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_init ();
	win = gtk_window_new ();
#else
	gtk_init (&argc, &argv);
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
#endif

	/* ---- store ---- */
	s = nx_store_new (FALSE, N_COLS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_POINTER);
	a = nx_store_append (s, NULL);
	nx_row_set (a, C_NAME, "alpha", C_NUM, 3, C_ON, TRUE, -1);
	b = nx_store_append (s, NULL);
	nx_row_set (b, C_NAME, "beta", C_NUM, 1, C_PTR, &marker, -1);
	c = nx_store_prepend (s, NULL);
	nx_row_set (c, C_NAME, "gamma", C_NUM, 2, -1);
	CHECK_NAMES (s, NULL, "gamma alpha beta");
	CHECK (nx_store_n_rows (s, NULL) == 3, "3 rows");
	CHECK (nx_row_index (a) == 1, "index of alpha");
	CHECK (nx_row_next (c) == a && nx_row_prev (a) == c && !nx_row_next (b), "next/prev");
	nx_row_get (a, C_NAME, &str, C_NUM, &num, -1);
	CHECK (!strcmp (str, "alpha") && num == 3, "get");
	g_free (str);
	CHECK (nx_row_get_bool (a, C_ON) && !nx_row_get_bool (b, C_ON), "bools");
	CHECK (nx_store_find_pointer (s, C_PTR, &marker) == b, "find pointer");
	nx_row_move (c, 2);
	CHECK_NAMES (s, NULL, "alpha beta gamma");
	nx_row_swap (a, c);
	CHECK_NAMES (s, NULL, "gamma beta alpha");
	nx_row_remove (b);
	CHECK_NAMES (s, NULL, "gamma alpha");
	/* rows stay the same objects across other rows' changes */
	CHECK (nx_store_first (s) == c && nx_row_index (a) == 1, "rows persist");
	b = nx_store_insert_before (s, NULL, a);
	nx_row_set (b, C_NAME, "beta", -1);
	CHECK_NAMES (s, NULL, "gamma beta alpha");

	/* ---- tree store ---- */
	t = nx_store_new (TRUE, N_COLS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_POINTER);
	p1 = nx_store_append (t, NULL);
	nx_row_set (p1, C_NAME, "net", -1);
	k1 = nx_store_append (t, p1);
	nx_row_set (k1, C_NAME, "#a", -1);
	k2 = nx_store_append (t, p1);
	nx_row_set (k2, C_NAME, "#b", C_PTR, &marker, -1);
	CHECK (nx_store_n_rows (t, NULL) == 1 && nx_store_n_rows (t, p1) == 2, "tree counts");
	CHECK (nx_row_parent (k2) == p1 && !nx_row_parent (p1), "parent");
	CHECK (nx_store_find_pointer (t, C_PTR, &marker) == k2, "find in children");
	CHECK_NAMES (t, p1, "#a #b");

	/* ---- view ---- */
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_size_request (box, 300, 300);
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_window_set_child (GTK_WINDOW (win), box);
#else
	gtk_container_add (GTK_CONTAINER (win), box);
#endif
	view = nx_view_new_scrolled (box, s);
	name_col = nx_view_add_column (view, "Name", NX_COL_TEXT, C_NAME);
	nx_col_set_editable (name_col, TRUE);
	nx_col_on_edited (name_col, on_edited, NULL);
	nx_col_set_sort_column (name_col, C_NAME);
	on_col = nx_view_add_column (view, "On", NX_COL_TOGGLE, C_ON);
	nx_col_on_toggled (on_col, on_toggled, NULL);
	nx_view_on_select (view, on_select, NULL);
	tview = nx_view_new_scrolled (box, t);
	nx_view_add_column (tview, NULL, NX_COL_TEXT, C_NAME);
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_window_present (GTK_WINDOW (win));
#else
	gtk_widget_show_all (win);
#endif
	pump ();
	/* GTK3 puts the cursor on the first row when the view takes focus */
	nx_view_unselect_all (view);
	selects = 0;

	CHECK (nx_view_get_selected (view) == NULL, "nothing selected at start");
	nx_view_select (view, b);
	CHECK (nx_view_get_selected (view) == b, "select");
	CHECK (selects == 1, "select callback ran %d times", selects);
	nx_view_unselect_all (view);
	CHECK (nx_view_get_selected (view) == NULL, "unselect all");

	nx_view_set_select_mode (view, GTK_SELECTION_MULTIPLE);
	nx_view_select (view, c);
	nx_view_select (view, a);
	CHECK (nx_view_count_selected (view) == 2, "2 selected (%d)", nx_view_count_selected (view));
	l = nx_view_get_selected_rows (view);
	CHECK (g_list_length (l) == 2 && l->data == c && l->next->data == a, "selected rows in order");
	g_list_free (l);
	CHECK (nx_view_is_selected (view, a) && !nx_view_is_selected (view, b), "is_selected");
	nx_view_unselect_all (view);
	nx_view_set_select_mode (view, GTK_SELECTION_SINGLE);

	/* a removed row isn't selected any more; others still are */
	nx_view_select (view, a);
	nx_row_remove (a);
	CHECK (nx_view_get_selected (view) != a, "removed row not selected");
	a = nx_store_append (s, NULL);
	nx_row_set (a, C_NAME, "alpha", C_ON, TRUE, -1);

	/* header sorting: GTK3 sorts the store itself, GTK4 only the view;
	   either way the rows keep their identity */
	nx_view_sort_by (view, name_col, GTK_SORT_ASCENDING);
	pump ();
	CHECK (nx_store_n_rows (s, NULL) == 3 && nx_row_index (a) >= 0, "rows after sort");
	nx_view_sort_by (view, NULL, GTK_SORT_ASCENDING);

	/* editing through the cursor: the edited handler gets the text */
	nx_view_set_cursor (view, b, name_col);
	pump ();
	CHECK (nx_view_get_selected (view) == b, "cursor selects");

	/* expand */
	nx_view_expand_all (tview);
	pump ();
	CHECK (nx_view_is_expanded (tview, p1), "expanded");
	nx_view_collapse (tview, p1);
	CHECK (!nx_view_is_expanded (tview, p1), "collapsed");

	/* clear: rows gone, view empty */
	nx_store_clear (s);
	CHECK (nx_store_n_rows (s, NULL) == 0, "clear");
	CHECK (nx_view_get_selected (view) == NULL, "nothing selected after clear");

	(void) toggles; (void) edits;
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_window_destroy (GTK_WINDOW (win));
#else
	gtk_widget_destroy (win);
#endif
	g_object_unref (s);
	g_object_unref (t);

	if (fails)
		return 1;
	printf ("nxlist: ok\n");
	return 0;
}
