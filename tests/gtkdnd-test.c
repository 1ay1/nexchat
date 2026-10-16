/* gtkdnd.h must set up drag and drop exactly as the code it replaced:
 * same targets (names, flags, info) in the same order on one target list,
 * the same source targets, nothing extra. GTK3 only (GTK4's controllers
 * are compile-checked; the drops themselves are in tests/ux/mouse.sh).
 *
 * The expectations are the literal GtkTargetEntry tables and
 * gtk_drag_dest_set calls from before gtkdnd.c (nexchat 99daa706).
 */
#include <string.h>
#include <gtk/gtk.h>
#include "gtkdnd.h"

static int fails;

#define CHECK(c, what) do { if (!(c)) { printf ("FAIL: %s\n", what); fails++; } } while (0)

typedef struct { const char *name; guint flags, info; } want;

static void
check_targets (GtkTargetList *list, const want *w, int n, const char *what)
{
	GtkTargetEntry *t;
	int got, i;
	char buf[200];

	t = gtk_target_table_new_from_list (list, &got);
	snprintf (buf, sizeof buf, "%s: %d targets (want %d)", what, got, n);
	CHECK (got == n, buf);
	for (i = 0; i < got && i < n; i++)
	{
		snprintf (buf, sizeof buf, "%s: target %d is %s/%u/%u (want %s/%u/%u)", what, i,
					 t[i].target, t[i].flags, t[i].info, w[i].name, w[i].flags, w[i].info);
		CHECK (!strcmp (t[i].target, w[i].name) && t[i].flags == w[i].flags && t[i].info == w[i].info, buf);
	}
	gtk_target_table_free (t, got);
}

static void
files_cb (GtkWidget *w, double x, double y, const char *uris, gpointer ud)
{
}

static void
layout_cb (GtkWidget *w, double y, nx_dnd_kind kind, gpointer ud)
{
}

int
main (int argc, char **argv)
{
	GtkWidget *ulist, *tree, *scbar, *xtext;
	static const want ulist_dest[] = {
		{ "text/uri-list", 0, 1 }, { "HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 } };
	static const want ulist_src[] = { { "HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 } };
	static const want tree_dest[] = { { "HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 } };
	static const want tree_src[] = { { "HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 } };
	static const want scbar_dest[] = {
		{ "HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 }, { "HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 } };
	static const want xtext_dest[] = { { "text/uri-list", 0, 1 } };

	gtk_init (&argc, &argv);

	/* userlistgui.c */
	ulist = gtk_tree_view_new ();
	g_object_ref_sink (ulist);
	nx_dnd_file_dest (ulist, files_cb, NULL, NULL, NULL);
	nx_dnd_layout_dest (ulist, NX_DND_CHANVIEW, layout_cb, NULL, layout_cb, NULL);
	nx_dnd_layout_source (ulist, NX_DND_USERLIST);
	check_targets (gtk_drag_dest_get_target_list (ulist), ulist_dest, 2, "user list dest");
	check_targets (gtk_drag_source_get_target_list (ulist), ulist_src, 1, "user list source");

	/* chanview-tree.c */
	tree = gtk_tree_view_new ();
	g_object_ref_sink (tree);
	nx_dnd_layout_dest (tree, NX_DND_USERLIST, layout_cb, NULL, layout_cb, NULL);
	nx_dnd_layout_source (tree, NX_DND_CHANVIEW);
	check_targets (gtk_drag_dest_get_target_list (tree), tree_dest, 1, "channel tree dest");
	check_targets (gtk_drag_source_get_target_list (tree), tree_src, 1, "channel tree source");

	/* maingui.c: the chat scrollbar (dest flags 5) and the chat view */
	scbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, NULL);
	g_object_ref_sink (scbar);
	nx_dnd_layout_dest (scbar, NX_DND_CHANVIEW | NX_DND_USERLIST | NX_DND_NO_THEME_HIGHLIGHT,
							  layout_cb, NULL, layout_cb, NULL);
	check_targets (gtk_drag_dest_get_target_list (scbar), scbar_dest, 2, "scrollbar dest");
	CHECK (gtk_drag_source_get_target_list (scbar) == NULL, "scrollbar is not a drag source");

	xtext = gtk_drawing_area_new ();
	g_object_ref_sink (xtext);
	nx_dnd_file_dest (xtext, files_cb, NULL, NULL, NULL);
	check_targets (gtk_drag_dest_get_target_list (xtext), xtext_dest, 1, "chat view dest");

	if (fails)
		return 1;
	printf ("gtkdnd: ok\n");
	return 0;
}
