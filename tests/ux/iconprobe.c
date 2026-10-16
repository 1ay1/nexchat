/* Which icon does each stock id and its proposed named replacement actually
   resolve to? Prints the file GTK3 picks for both, so the stock -> name
   table in gtkutil.c is chosen from evidence, not memory.
   build: cc $(pkg-config --cflags --libs gtk+-3.0) -o /tmp/iconprobe tests/ux/iconprobe.c */
#define GDK_DISABLE_DEPRECATION_WARNINGS
#include <gtk/gtk.h>

static const char *pairs[][2] = {
	{"gtk-ok", "gtk-ok"}, {"gtk-cancel", "gtk-cancel"}, {"gtk-apply", "gtk-apply"},
	{"gtk-yes", "gtk-yes"}, {"gtk-no", "gtk-no"},
	{"gtk-preferences", "gtk-preferences"}, {"gtk-connect", "gtk-connect"},
	{"gtk-disconnect", "gtk-disconnect"}, {"gtk-index", "gtk-index"},
	{"gtk-network", "network-idle"},
	{"gtk-dialog-warning", "dialog-warning"}, {"gtk-dialog-error", "dialog-error"},
	{"gtk-close", "window-close"}, {"gtk-quit", "application-exit"},
	{"gtk-help", "help-contents"}, {"gtk-about", "help-about"},
	{"gtk-new", "document-new"}, {"gtk-open", "document-open"},
	{"gtk-save", "document-save"}, {"gtk-save-as", "document-save-as"},
	{"gtk-revert-to-saved", "document-revert"}, {"gtk-delete", "edit-delete"},
	{"gtk-clear", "edit-clear"}, {"gtk-copy", "edit-copy"}, {"gtk-find", "edit-find"},
	{"gtk-redo", "edit-redo"}, {"gtk-jump-to", "go-jump"}, {"gtk-refresh", "view-refresh"},
	{"gtk-add", "list-add"}, {"gtk-remove", "list-remove"}, {"gtk-go-up", "go-up"},
	{"gtk-go-down", "go-down"}, {"gtk-go-back", "go-previous"}, {"gtk-go-forward", "go-next"},
	{"gtk-justify-left", "format-justify-left"}, {"gtk-media-play", "media-playback-start"},
	{"gtk-spell-check", "tools-check-spelling"},
};

int
main (int argc, char **argv)
{
	GtkIconTheme *theme;
	guint i;

	gtk_init (&argc, &argv);
	theme = gtk_icon_theme_get_default ();
	for (i = 0; i < G_N_ELEMENTS (pairs); i++)
	{
		GtkIconInfo *a = gtk_icon_theme_lookup_icon (theme, pairs[i][0], 16, 0);
		GtkIconInfo *b = gtk_icon_theme_lookup_icon (theme, pairs[i][1], 16, 0);
		const char *fa = a ? gtk_icon_info_get_filename (a) : NULL;
		const char *fb = b ? gtk_icon_info_get_filename (b) : NULL;
		printf ("%-22s %-22s %s  %s\n", pairs[i][0], pairs[i][1],
				  fa ? fa : "(none)", fb ? (g_strcmp0 (fa, fb) == 0 ? "=same" : fb) : "(none)");
		if (a) g_object_unref (a);
		if (b) g_object_unref (b);
	}
	return 0;
}
