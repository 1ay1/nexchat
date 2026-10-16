/* What does a plain GtkMenuItem's accel label report, and when does
   sensitivity show up? Mirrors menu.c: gtk_menu_item_new_with_mnemonic +
   gtk_widget_add_accelerator.
   cc $(pkg-config --cflags --libs gtk+-3.0) -o /tmp/menuprobe tests/ux/menuprobe.c */
#include <gtk/gtk.h>

int
main (int argc, char **argv)
{
	GtkWidget *win, *menu, *item, *child;
	GtkAccelGroup *ag;
	guint key = 0;
	GdkModifierType mods = 0;

	gtk_init (&argc, &argv);
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	ag = gtk_accel_group_new ();
	gtk_window_add_accel_group (GTK_WINDOW (win), ag);
	menu = gtk_menu_new ();
	gtk_menu_set_accel_group (GTK_MENU (menu), ag);
	item = gtk_menu_item_new_with_mnemonic ("_Quit");
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_add_accelerator (item, "activate", ag, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	child = gtk_bin_get_child (GTK_BIN (item));
	printf ("child type: %s\n", G_OBJECT_TYPE_NAME (child));
	gtk_accel_label_get_accel (GTK_ACCEL_LABEL (child), &key, &mods);
	printf ("before refetch: key=%u mods=0x%x\n", key, mods);
	gtk_accel_label_refetch (GTK_ACCEL_LABEL (child));
	gtk_accel_label_get_accel (GTK_ACCEL_LABEL (child), &key, &mods);
	printf ("after refetch:  key=%u mods=0x%x\n", key, mods);
	{
		GList *closures = gtk_widget_list_accel_closures (item);
		printf ("accel closures on item: %u\n", g_list_length (closures));
		g_list_free (closures);
	}
	return 0;
}
