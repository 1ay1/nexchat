/* Print the GdkEventKey state for each key press, to see which modifier
   bits GDK reports on this display.
   cc $(pkg-config --cflags --libs gtk+-3.0) -o /tmp/keyprobe tests/ux/keyprobe.c */
#include <gtk/gtk.h>

static gboolean
on_key (GtkWidget *w, GdkEventKey *e, gpointer d)
{
	GdkModifierType mapped = e->state;
	gdk_keymap_add_virtual_modifiers (gdk_keymap_get_for_display (gtk_widget_get_display (w)), &mapped);
	printf ("key %-10s state=0x%04x mapped=0x%08x shift=%d ctrl=%d mod1=%d meta=%d super=%d\n",
			  gdk_keyval_name (e->keyval), e->state, mapped,
			  !!(e->state & GDK_SHIFT_MASK), !!(e->state & GDK_CONTROL_MASK),
			  !!(e->state & GDK_MOD1_MASK), !!(e->state & GDK_META_MASK),
			  !!(e->state & GDK_SUPER_MASK));
	fflush (stdout);
	return FALSE;
}

int
main (int argc, char **argv)
{
	GtkWidget *win, *entry;
	gtk_init (&argc, &argv);
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_move (GTK_WINDOW (win), 0, 0);
	gtk_window_set_default_size (GTK_WINDOW (win), 600, 600);
	entry = gtk_entry_new ();
	gtk_container_add (GTK_CONTAINER (win), entry);
	g_signal_connect (entry, "key-press-event", G_CALLBACK (on_key), NULL);
	gtk_widget_show_all (win);
	gtk_main ();
	return 0;
}
