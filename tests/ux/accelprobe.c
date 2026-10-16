/* Does an accelerator for <Meta>q fire on a plain q on this display?
   cc $(pkg-config --cflags --libs gtk+-3.0) -o /tmp/accelprobe tests/ux/accelprobe.c */
#include <gtk/gtk.h>

static void
fired (GtkWidget *w, gpointer d)
{
	printf ("accelerator %s fired\n", (char *) d);
	fflush (stdout);
}

int
main (int argc, char **argv)
{
	GtkWidget *win, *bar, *menu, *top, *item, *box, *entry;
	GtkAccelGroup *ag;

	gtk_init (&argc, &argv);
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_move (GTK_WINDOW (win), 0, 0);
	gtk_window_set_default_size (GTK_WINDOW (win), 600, 600);
	ag = gtk_accel_group_new ();
	gtk_window_add_accel_group (GTK_WINDOW (win), ag);

	bar = gtk_menu_bar_new ();
	menu = gtk_menu_new ();
	top = gtk_menu_item_new_with_label ("File");
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (top), menu);
	gtk_menu_shell_append (GTK_MENU_SHELL (bar), top);

	item = gtk_menu_item_new_with_label ("Meta+Q");
	gtk_widget_add_accelerator (item, "activate", ag, GDK_KEY_q, GDK_META_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect (item, "activate", G_CALLBACK (fired), "<Meta>q");
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label ("Ctrl+W");
	gtk_widget_add_accelerator (item, "activate", ag, GDK_KEY_w, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect (item, "activate", G_CALLBACK (fired), "<Control>w");
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (box), bar, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (win), box);
	gtk_widget_show_all (win);
	gtk_widget_grab_focus (entry);
	gtk_main ();
	return 0;
}
