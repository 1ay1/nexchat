/* Builds against GTK3 or GTK4 (whichever pkg-config gives) and checks that
   gtkcompat.h produces the same child order and expand/fill/padding as
   GTK3's gtk_box_pack_start/pack_end did. Also compile-checks every other
   macro in gtkcompat.h on that GTK.
   GTK3: cc $(pkg-config --cflags --libs gtk+-3.0) -I src/fe-gtk tests/gtkcompat-test.c
   GTK4: cc $(pkg-config --cflags --libs gtk4)     -I src/fe-gtk tests/gtkcompat-test.c */
#include "gtkcompat.h"
#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond, ...) do { if (!(cond)) { printf ("FAIL: " __VA_ARGS__); printf ("\n"); fails++; } } while (0)

static GtkWidget *
lbl (const char *name)
{
	GtkWidget *w = gtk_label_new (name);
	gtk_widget_set_name (w, name);
	return w;
}

/* child order as names, "a,b,c" */
static void
order (GtkWidget *box, char *out, size_t n)
{
	out[0] = 0;
#if GTK_CHECK_VERSION(4, 0, 0)
	GtkWidget *c;
	for (c = gtk_widget_get_first_child (box); c; c = gtk_widget_get_next_sibling (c))
#else
	GList *l, *kids = gtk_container_get_children (GTK_CONTAINER (box));
	/* GTK3's child list isn't visual order; measure where each child really
	   ends up after allocation and sort by x */
	GtkWidget *win = gtk_offscreen_window_new ();
	GtkAllocation a;
	GList *sorted = NULL;
	gtk_container_add (GTK_CONTAINER (win), box);
	gtk_widget_show_all (win);
	while (gtk_events_pending ())
		gtk_main_iteration ();
	for (l = kids; l; l = l->next)
	{
		GList *s;
		int pos = 0;
		gtk_widget_get_allocation (l->data, &a);
		for (s = sorted; s; s = s->next, pos++)
		{
			GtkAllocation b;
			gtk_widget_get_allocation (s->data, &b);
			if (a.x < b.x)
				break;
		}
		sorted = g_list_insert (sorted, l->data, pos);
	}
	g_list_free (kids);
	kids = sorted;
	for (l = kids; l; l = l->next)
#endif
	{
#if !GTK_CHECK_VERSION(4, 0, 0)
		GtkWidget *c = l->data;
#endif
		if (out[0])
			g_strlcat (out, ",", n);
		g_strlcat (out, gtk_widget_get_name (c), n);
	}
#if !GTK_CHECK_VERSION(4, 0, 0)
	g_list_free (kids);
#endif
}

int
main (int argc, char **argv)
{
	GtkWidget *box, *e, *w;
	char got[128];

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_init ();
	printf ("GTK %d.%d\n", GTK_MAJOR_VERSION, GTK_MINOR_VERSION);
#else
	gtk_init (&argc, &argv);
	printf ("GTK %d.%d\n", GTK_MAJOR_VERSION, GTK_MINOR_VERSION);
#endif

	/* the order GTK3 gives for: start a, end x, start b, end y, start c
	   is a,b,c,y,x */
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (box, lbl ("a"), FALSE, FALSE, 0);
	nx_box_pack_end (box, lbl ("x"), FALSE, FALSE, 0);
	nx_box_pack_start (box, lbl ("b"), FALSE, FALSE, 0);
	nx_box_pack_end (box, lbl ("y"), FALSE, FALSE, 0);
	nx_box_pack_start (box, lbl ("c"), FALSE, FALSE, 0);
	order (box, got, sizeof got);
	CHECK (strcmp (got, "a,b,c,y,x") == 0, "pack order: got %s, want a,b,c,y,x", got);

#if GTK_CHECK_VERSION(4, 0, 0)
	/* expand/fill/padding map to widget properties */
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	w = lbl ("ex");
	nx_box_pack_start (box, w, TRUE, FALSE, 4);
	CHECK (gtk_widget_get_hexpand (w), "expand -> hexpand");
	CHECK (gtk_widget_get_halign (w) == GTK_ALIGN_CENTER, "expand && !fill -> centred");
	CHECK (gtk_widget_get_margin_start (w) == 4 && gtk_widget_get_margin_end (w) == 4, "padding -> margins");
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	w = lbl ("v");
	nx_box_pack_start (box, w, TRUE, TRUE, 2);
	CHECK (gtk_widget_get_vexpand (w) && gtk_widget_get_valign (w) == GTK_ALIGN_FILL, "vertical expand+fill");
	CHECK (gtk_widget_get_margin_top (w) == 2, "vertical padding");
#endif

	/* the rest compiles and round-trips */
	e = gtk_entry_new ();
	nx_entry_set_text (e, "hello");
	CHECK (strcmp (nx_entry_get_text (e), "hello") == 0, "entry text round-trip");
	w = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	nx_set_border_width (w, 6);
	nx_widget_show_all (w);
	{
		/* a child destroyed through the compat call leaves its parent */
		GtkWidget *parent = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *kid = lbl ("kid");
		g_object_ref_sink (parent);
		nx_box_pack_start (parent, kid, FALSE, FALSE, 0);
		nx_widget_destroy (kid);
#if GTK_CHECK_VERSION(4, 0, 0)
		CHECK (gtk_widget_get_first_child (parent) == NULL, "destroy removes child");
#else
		{
			GList *k = gtk_container_get_children (GTK_CONTAINER (parent));
			CHECK (k == NULL, "destroy removes child");
			g_list_free (k);
		}
#endif
		g_object_unref (parent);
	}
	{
		GtkWidget *b = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		GtkWidget *btn = gtk_button_new_with_label ("btn");
		GList *kids;
		g_object_ref_sink (b);
		nx_box_pack_start (b, lbl ("k1"), FALSE, FALSE, 0);
		nx_box_pack_start (b, lbl ("k2"), FALSE, FALSE, 0);
		kids = nx_widget_get_children (b);
		CHECK (g_list_length (kids) == 2 && strcmp (gtk_widget_get_name (kids->data), "k1") == 0,
				 "get_children: two, in order");
		g_list_free (kids);
		CHECK (GTK_IS_LABEL (nx_bin_get_child (btn)), "bin child of a label button is the label");
		g_object_unref (b);
	}
	{
		GtkWidget *bb = nx_button_box_new (GTK_ORIENTATION_HORIZONTAL, NX_BUTTONBOX_SPREAD);
		GtkWidget *sw = nx_scrolled_window_new ();
		g_object_ref_sink (bb);
		g_object_ref_sink (sw);
		nx_scrolled_window_set_shadow_in (sw, TRUE);
#if GTK_CHECK_VERSION(4, 0, 0)
		CHECK (gtk_box_get_homogeneous (GTK_BOX (bb)), "SPREAD button box is homogeneous");
		CHECK (gtk_scrolled_window_get_has_frame (GTK_SCROLLED_WINDOW (sw)), "shadow in -> has_frame");
		CHECK (nx_widget_get_toplevel (bb) == NULL, "unparented widget has no toplevel");
#else
		CHECK (gtk_button_box_get_layout (GTK_BUTTON_BOX (bb)) == GTK_BUTTONBOX_SPREAD, "button box layout");
		CHECK (gtk_scrolled_window_get_shadow_type (GTK_SCROLLED_WINDOW (sw)) == GTK_SHADOW_IN, "shadow in");
#endif
		g_object_unref (bb);
		g_object_unref (sw);
	}

	if (fails)
	{
		printf ("gtkcompat: %d failure(s)\n", fails);
		return 1;
	}
	printf ("gtkcompat: ok\n");
	return 0;
}
