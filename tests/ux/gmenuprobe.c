/* Feasibility probe for building nexchat's menus from GMenuModel + GAction
   on GTK3 (gtk_menu_bar_new_from_model). Builds a small menu with the
   features menu.c needs and dumps the resulting widget tree the same way
   menu_debug_dump does, so the output can be compared with
   tests/ux/menus.expected line by line.
   cc $(pkg-config --cflags --libs gtk+-3.0) -o /tmp/gmenuprobe tests/ux/gmenuprobe.c */
#include <gtk/gtk.h>
#include <string.h>

static gboolean
find_cb (GtkAccelKey *key, GClosure *closure, gpointer data)
{
	return closure == data;
}

static void
dump (GtkWidget *shell, int depth)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (shell)), *l;
	for (l = children; l; l = l->next)
	{
		GtkWidget *item = l->data, *child, *sub;
		const char *kind = "item", *text = "";
		char accel[64] = "";
		GList *cl, *c;

		if (!gtk_widget_get_visible (item))
			continue;
		if (GTK_IS_SEPARATOR_MENU_ITEM (item))
		{
			printf ("%*s---\n", depth * 2, "");
			continue;
		}
		if (GTK_IS_CHECK_MENU_ITEM (item))
			kind = gtk_check_menu_item_get_draw_as_radio (GTK_CHECK_MENU_ITEM (item)) ? "radio" : "check";
		child = gtk_bin_get_child (GTK_BIN (item));
		/* model menus put a box with [icon][accel label] inside */
		if (child && GTK_IS_CONTAINER (child) && !GTK_IS_LABEL (child))
		{
			GList *bc = gtk_container_get_children (GTK_CONTAINER (child)), *b;
			for (b = bc; b; b = b->next)
				if (GTK_IS_LABEL (b->data)) { child = b->data; break; }
			g_list_free (bc);
		}
		if (child && GTK_IS_LABEL (child))
			text = gtk_label_get_label (GTK_LABEL (child));
		if (child && GTK_IS_ACCEL_LABEL (child))
		{
			guint key; GdkModifierType mods;
			gtk_accel_label_get_accel (GTK_ACCEL_LABEL (child), &key, &mods);
			if (key) { char *n = gtk_accelerator_name (key, mods); g_strlcpy (accel, n, sizeof accel); g_free (n); }
		}
		cl = gtk_widget_list_accel_closures (item);
		for (c = cl; c && !accel[0]; c = c->next)
		{
			GtkAccelGroup *ag = gtk_accel_group_from_accel_closure (c->data);
			GtkAccelKey *k = ag ? gtk_accel_group_find (ag, find_cb, c->data) : NULL;
			if (k && k->accel_key) { char *n = gtk_accelerator_name (k->accel_key, k->accel_mods); g_strlcpy (accel, n, sizeof accel); g_free (n); }
		}
		g_list_free (cl);
		printf ("%*s%s \"%s\"%s%s%s%s\n", depth * 2, "", kind, text,
				  accel[0] ? " [" : "", accel, accel[0] ? "]" : "",
				  !gtk_widget_get_sensitive (item) ? " insensitive" : "");
		if (GTK_IS_CHECK_MENU_ITEM (item))
			printf ("%*s  = %s\n", depth * 2, "", gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)) ? "on" : "off");
		sub = gtk_menu_item_get_submenu (GTK_MENU_ITEM (item));
		if (sub)
			dump (sub, depth + 1);
	}
	g_list_free (children);
}

static void noop (GSimpleAction *a, GVariant *p, gpointer d) { printf ("activated %s\n", g_action_get_name (G_ACTION (a))); }

int
main (int argc, char **argv)
{
	GtkApplication *app;
	GtkWidget *win, *bar, *box;
	GMenu *top, *m, *sec1, *sec2, *sub;
	GSimpleActionGroup *ag;
	GSimpleAction *a;
	GtkAccelGroup *accel;

	gtk_init (&argc, &argv);
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	ag = g_simple_action_group_new ();
	gtk_widget_insert_action_group (win, "win", G_ACTION_GROUP (ag));
	accel = gtk_accel_group_new ();
	gtk_window_add_accel_group (GTK_WINDOW (win), accel);

	a = g_simple_action_new ("netlist", NULL); g_signal_connect (a, "activate", G_CALLBACK (noop), 0);
	g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (a));
	a = g_simple_action_new ("quit", NULL); g_signal_connect (a, "activate", G_CALLBACK (noop), 0);
	g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (a));
	a = g_simple_action_new ("disconnect", NULL); g_simple_action_set_enabled (a, FALSE);
	g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (a));
	a = g_simple_action_new_stateful ("menubar", NULL, g_variant_new_boolean (TRUE));
	g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (a));
	a = g_simple_action_new_stateful ("switcher", G_VARIANT_TYPE_STRING, g_variant_new_string ("tree"));
	g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (a));

	top = g_menu_new ();
	m = g_menu_new ();
	sec1 = g_menu_new ();
	g_menu_append (sec1, "Network Li_st", "win.netlist");
	g_menu_append_section (m, NULL, G_MENU_MODEL (sec1));
	sec2 = g_menu_new ();
	g_menu_append (sec2, "_Quit", "win.quit");
	g_menu_append_section (m, NULL, G_MENU_MODEL (sec2));
	g_menu_append_submenu (top, "ne_xchat", G_MENU_MODEL (m));

	m = g_menu_new ();
	g_menu_append (m, "_Menu Bar", "win.menubar");
	sub = g_menu_new ();
	g_menu_append (sub, "_Tabs", "win.switcher::tabs");
	g_menu_append (sub, "T_ree", "win.switcher::tree");
	g_menu_append_submenu (m, "_Channel Switcher", G_MENU_MODEL (sub));
	g_menu_append_submenu (top, "_View", G_MENU_MODEL (m));

	m = g_menu_new ();
	g_menu_append (m, "_Disconnect", "win.disconnect");
	g_menu_append_submenu (top, "_Server", G_MENU_MODEL (m));

	bar = gtk_menu_bar_new_from_model (G_MENU_MODEL (top));
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (box), bar, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (win), box);
	gtk_widget_show_all (win);

	/* accelerators for model items in plain GTK3 (no GtkApplication):
	   attribute "accel" is only honoured by GtkApplication; check it */
	{
		GMenuItem *it = g_menu_item_new ("_Quit2", "win.quit");
		g_menu_item_set_attribute (it, "accel", "s", "<Primary>q");
		g_menu_append_item (sec2, it);
		g_object_unref (it);
	}
	/* runtime add (plugin /MENU) and remove */
	g_menu_append (sec1, "Added at runtime", "win.netlist");
	g_menu_remove (sec1, 0);

	while (gtk_events_pending ()) gtk_main_iteration ();
	dump (bar, 0);
	return 0;
}
