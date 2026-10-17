/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* GTK3 <-> GTK4 compatibility for the calls that differ only in spelling.
 *
 * Phase 4, step 1 (ROADMAP): call sites use these now, while the frontend
 * still builds against GTK3, so every change is tested on the GTK3 build
 * and CI. The GTK3 branch is exactly what the code did before. The GTK4
 * branch is compile-checked against real GTK4 headers by
 * tests/gtkcompat-gtk4.c. When the switch to GTK4 is done the GTK3 branch
 * and most of this file go away.
 *
 * Rule: no behaviour lives here, only spelling. Anything that needs logic
 * (drawing, events, menus) gets its own file. */

#ifndef NEXCHAT_GTKCOMPAT_H
#define NEXCHAT_GTKCOMPAT_H

#include <string.h>
#include <gtk/gtk.h>


/* ---- GTK4 ---- */

static inline void nx_button_box_child (GtkWidget *box, GtkWidget *child);

/* GtkBox: pack_start(child, expand, fill, padding). GTK4 has no per-child
   expand/fill/padding: expand -> hexpand/vexpand along the box, fill ->
   align FILL else CENTER across... GTK2/3 'fill' was along the box axis,
   so with expand && !fill the child was centred in its extra space.
   padding -> margins on both sides along the box axis. */
static inline void
nx_box_pack (GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill,
				 guint padding, gboolean at_end)
{
	gboolean horiz = gtk_orientable_get_orientation (GTK_ORIENTABLE (box)) == GTK_ORIENTATION_HORIZONTAL;

	/* GTK3's padding came on top of the child's own border/margins */
	if (horiz)
	{
		gtk_widget_set_hexpand (child, expand);
		if (expand && !fill)
			gtk_widget_set_halign (child, GTK_ALIGN_CENTER);
		gtk_widget_set_margin_start (child, gtk_widget_get_margin_start (child) + padding);
		gtk_widget_set_margin_end (child, gtk_widget_get_margin_end (child) + padding);
	}
	else
	{
		gtk_widget_set_vexpand (child, expand);
		if (expand && !fill)
			gtk_widget_set_valign (child, GTK_ALIGN_CENTER);
		gtk_widget_set_margin_top (child, gtk_widget_get_margin_top (child) + padding);
		gtk_widget_set_margin_bottom (child, gtk_widget_get_margin_bottom (child) + padding);
	}
	if (at_end)
	{
		/* GTK3 kept pack_end children in a section after all pack_start
		   children, the first one packed being last. Reproduce that: the
		   new end child goes right after the last start child. Start
		   children are marked so we can find the boundary. */
		GtkWidget *c, *last_start = NULL;
		for (c = gtk_widget_get_first_child (GTK_WIDGET (box)); c; c = gtk_widget_get_next_sibling (c))
			if (!g_object_get_data (G_OBJECT (c), "nx-pack-end"))
				last_start = c;
		g_object_set_data (G_OBJECT (child), "nx-pack-end", GINT_TO_POINTER (1));
		gtk_box_insert_child_after (box, child, last_start);	/* NULL = first */
	}
	else
	{
		/* append to the start section: before the first end child */
		GtkWidget *c, *last_start = NULL;
		for (c = gtk_widget_get_first_child (GTK_WIDGET (box)); c; c = gtk_widget_get_next_sibling (c))
		{
			if (g_object_get_data (G_OBJECT (c), "nx-pack-end"))
				break;
			last_start = c;
		}
		gtk_box_insert_child_after (box, child, last_start);
	}
	if (g_object_get_data (G_OBJECT (box), "nx-bbox"))
		nx_button_box_child (GTK_WIDGET (box), child);
}
#define nx_box_pack_start(box, child, expand, fill, pad) \
	nx_box_pack (GTK_BOX (box), (child), (expand), (fill), (pad), FALSE)
#define nx_box_pack_end(box, child, expand, fill, pad) \
	nx_box_pack (GTK_BOX (box), (child), (expand), (fill), (pad), TRUE)

/* gtk_container_set_border_width: margins on all four sides. A window
   ignores its own margins, so there it's the child's (now or once set). */
static inline void
nx_set_border_width (GtkWidget *w, guint px)
{
	if (GTK_IS_WINDOW (w))
	{
		GtkWidget *c = gtk_window_get_child (GTK_WINDOW (w));
		g_object_set_data (G_OBJECT (w), "nx-border", GUINT_TO_POINTER (px + 1));
		if (!c)
			return;
		w = c;
	}
	gtk_widget_set_margin_start (w, px);
	gtk_widget_set_margin_end (w, px);
	gtk_widget_set_margin_top (w, px);
	gtk_widget_set_margin_bottom (w, px);
}

/* widgets are visible by default in GTK4; show_all is gone */
#define nx_widget_show_all(w) gtk_widget_set_visible (GTK_WIDGET (w), TRUE)
/* gtk_widget_show/hide are deprecated. Not gtk_window_present for windows:
   that also activates them, which gtk_widget_show never did */
#define nx_widget_show(w) gtk_widget_set_visible (GTK_WIDGET (w), TRUE)
#define nx_widget_hide(w) gtk_widget_set_visible (GTK_WIDGET (w), FALSE)

/* entry text: GtkEditable in GTK4 */
#define nx_entry_get_text(e) gtk_editable_get_text (GTK_EDITABLE (e))
#define nx_entry_set_text(e, t) gtk_editable_set_text (GTK_EDITABLE (e), (t))

/* gtk_widget_destroy: toplevels are destroyed, children unparented
   (which drops the parent's reference, so they go away the same way) */
static inline void
nx_widget_destroy (GtkWidget *w)
{
	if (GTK_IS_WINDOW (w))
		gtk_window_destroy (GTK_WINDOW (w));
	else if (gtk_widget_get_parent (w))
		gtk_widget_unparent (w);
}

/* gtk_container_get_children: a new GList the caller frees */
static inline GList *
nx_widget_get_children (GtkWidget *w)
{
	GList *list = NULL;
	GtkWidget *c;
	for (c = gtk_widget_get_last_child (w); c; c = gtk_widget_get_prev_sibling (c))
		list = g_list_prepend (list, c);
	return list;
}

/* gtk_bin_get_child: the single child (buttons, frames, windows, ...) */
#define nx_bin_get_child(w) gtk_widget_get_first_child (GTK_WIDGET (w))

/* GtkScrolledWindow: new(NULL,NULL) -> new(); the inset frame
   (GTK_SHADOW_IN) is the "frame" style property in GTK4 */
#define nx_scrolled_window_new() gtk_scrolled_window_new ()
#define nx_scrolled_window_set_shadow_in(sw, on) \
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (sw), (on))
/* GTK_SHADOW_ETCHED_IN: GTK4 has one frame style */
#define nx_scrolled_window_set_shadow_etched(sw) \
	gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (sw), TRUE)

/* GtkButtonBox is gone. Its layouts give every button the width of the
   widest one (at least 85px, GTK3's default) and place them: SPREAD (equal
   gaps around), EDGE (first and last at the edges), START, END. A box
   gives each button an equal slot; nx_button_box_add sizes and places it. */
typedef enum { NX_BUTTONBOX_SPREAD, NX_BUTTONBOX_END, NX_BUTTONBOX_START, NX_BUTTONBOX_EDGE } nx_buttonbox_layout;
static inline GtkWidget *
nx_button_box_new (GtkOrientation o, nx_buttonbox_layout layout)
{
	/* GtkButtonBox put no spacing between buttons when the row was tight;
	   SPREAD/EDGE get their gaps from the equal slots instead */
	GtkWidget *box = gtk_box_new (o, layout == NX_BUTTONBOX_SPREAD || layout == NX_BUTTONBOX_EDGE ? 0 : 6);
	GtkSizeGroup *sg = gtk_size_group_new (o == GTK_ORIENTATION_HORIZONTAL ?
														GTK_SIZE_GROUP_HORIZONTAL : GTK_SIZE_GROUP_VERTICAL);

	gtk_box_set_homogeneous (GTK_BOX (box), layout == NX_BUTTONBOX_SPREAD || layout == NX_BUTTONBOX_EDGE);
	if (layout == NX_BUTTONBOX_END)
		gtk_widget_set_halign (box, GTK_ALIGN_END);
	else if (layout == NX_BUTTONBOX_START)
		gtk_widget_set_halign (box, GTK_ALIGN_START);
	g_object_set_data (G_OBJECT (box), "nx-bbox", GINT_TO_POINTER (layout + 1));
	g_object_set_data_full (G_OBJECT (box), "nx-bbox-sg", sg, g_object_unref);
	return box;
}

/* a child of a button box: same width as its siblings, GTK3's place */
static inline void
nx_button_box_child (GtkWidget *box, GtkWidget *child)
{
	int layout = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), "nx-bbox")) - 1;
	GtkSizeGroup *sg = g_object_get_data (G_OBJECT (box), "nx-bbox-sg");
	GtkWidget *c;

	if (layout < 0 || !GTK_IS_BUTTON (child))
		return;
	gtk_size_group_add_widget (sg, child);
	if (gtk_orientable_get_orientation (GTK_ORIENTABLE (box)) == GTK_ORIENTATION_HORIZONTAL)
		gtk_widget_set_size_request (child, 85, -1);
	gtk_widget_set_hexpand (child, FALSE);
	if (layout != NX_BUTTONBOX_SPREAD && layout != NX_BUTTONBOX_EDGE)
		return;
	/* in equal slots: SPREAD centres each; EDGE puts the ends at the edges */
	for (c = gtk_widget_get_first_child (box); c; c = gtk_widget_get_next_sibling (c))
	{
		GtkAlign a = GTK_ALIGN_CENTER;
		if (layout == NX_BUTTONBOX_EDGE && !gtk_widget_get_prev_sibling (c))
			a = GTK_ALIGN_START;
		else if (layout == NX_BUTTONBOX_EDGE && !gtk_widget_get_next_sibling (c))
			a = GTK_ALIGN_END;
		gtk_widget_set_halign (c, a);
	}
}

/* window placement hints: GTK4 has none; on X11 gtkutil places the window
   at the pointer / screen centre itself (Wayland: the compositor decides) */
void gtkutil_window_place (GtkWidget *win, int how);
#define nx_window_set_position_mouse(win) gtkutil_window_place (GTK_WIDGET (win), TRUE)
#define nx_window_set_position_center(win) gtkutil_window_place (GTK_WIDGET (win), FALSE)

/* toplevel of a widget, NULL if it isn't in a window yet */
static inline GtkWidget *
nx_widget_get_toplevel (GtkWidget *w)
{
	GtkRoot *r = gtk_widget_get_root (w);
	return r ? GTK_WIDGET (r) : NULL;
}

/* default button: GTK4 sets it on the window */
static inline void
nx_widget_make_default (GtkWidget *button)
{
	GtkRoot *r = gtk_widget_get_root (button);
	if (r && GTK_IS_WINDOW (r))
		gtk_window_set_default_widget (GTK_WINDOW (r), button);
}

/* size of a realized widget (GTK3 read it off the GdkWindow) */
#define nx_widget_get_width(w) gtk_widget_get_width (GTK_WIDGET (w))
#define nx_widget_get_height(w) gtk_widget_get_height (GTK_WIDGET (w))
#define nx_widget_get_allocated_width(w) gtk_widget_get_width (GTK_WIDGET (w))
#define nx_widget_get_allocated_height(w) gtk_widget_get_height (GTK_WIDGET (w))
/* where w is inside ancestor (GTK3: its allocation, which was relative to
   the nearest GdkWindow; for the tab strip that's the strip itself) */
static inline void
nx_widget_get_position_in (GtkWidget *w, GtkWidget *ancestor, int *x, int *y)
{
	graphene_point_t in = GRAPHENE_POINT_INIT (0, 0), out;

	if (gtk_widget_compute_point (w, ancestor, &in, &out))
	{
		*x = (int) out.x;
		*y = (int) out.y;
	}
	else
		*x = *y = 0;
}

/* the window holding w is the active one (has the keyboard) */
#define nx_widget_window_is_active(w) gtk_window_is_active (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (w))))

/* pointer shape by CSS cursor name ("pointer", "w-resize"); NULL: default */
#define nx_widget_set_cursor_name(w, name) \
	gtk_widget_set_cursor_from_name (GTK_WIDGET (w), (name))

/* ---- step 10 ---- */

/* GTK_RELIEF_NONE: a flat button */
#define nx_button_set_flat(b) gtk_button_set_has_frame (GTK_BUTTON (b), FALSE)
/* frame shadows are gone (a frame always draws its border) */
#define nx_frame_set_shadow(f, t) ((void) (f))
/* viewports never draw a frame in GTK4 */
#define nx_viewport_set_no_shadow(v) ((void) (v))

/* GtkPaned pack1/pack2 (child, resize, shrink) */
static inline void
nx_paned_pack (GtkWidget *paned, GtkWidget *child, gboolean second,
					gboolean resize, gboolean shrink)
{
	if (second)
	{
		gtk_paned_set_end_child (GTK_PANED (paned), child);
		gtk_paned_set_resize_end_child (GTK_PANED (paned), resize);
		gtk_paned_set_shrink_end_child (GTK_PANED (paned), shrink);
	}
	else
	{
		gtk_paned_set_start_child (GTK_PANED (paned), child);
		gtk_paned_set_resize_start_child (GTK_PANED (paned), resize);
		gtk_paned_set_shrink_start_child (GTK_PANED (paned), shrink);
	}
}
#define nx_paned_get_child1(p) gtk_paned_get_start_child (GTK_PANED (p))
#define nx_paned_get_child2(p) gtk_paned_get_end_child (GTK_PANED (p))

/* gtk_box_reorder_child (box, child, pos): pos -1 is the end */
static inline void
nx_box_reorder_child (GtkWidget *box, GtkWidget *child, int pos)
{
	GtkWidget *sib = NULL, *c;
	int i = 0;

	if (pos != 0)
		for (c = gtk_widget_get_first_child (box); c; c = gtk_widget_get_next_sibling (c))
		{
			if (c == child)
				continue;
			sib = c;
			if (++i == pos)
				break;
		}
	gtk_box_reorder_child_after (GTK_BOX (box), child, sib);
}

/* radio buttons: GTK4 groups check buttons. group is any earlier member
   (NULL: start one); nx_radio_button_new returns the new button. */
static inline GtkWidget *
nx_radio_button_new (GtkWidget *group, const char *mnemonic)
{
	GtkWidget *b = gtk_check_button_new_with_mnemonic (mnemonic);
	if (group)
		gtk_check_button_set_group (GTK_CHECK_BUTTON (b), GTK_CHECK_BUTTON (group));
	else
		gtk_check_button_set_active (GTK_CHECK_BUTTON (b), TRUE);	/* GTK3 radios did */
	return b;
}

/* window hints GTK4 dropped: roles and type hints are gone (the WM decides
   from transient-for / modality) */
#define nx_window_set_role(w, role) ((void) (w))
#define nx_window_set_dialog_hint(w) ((void) (w))

#define nx_label_set_wrap(l, on) gtk_label_set_wrap (GTK_LABEL (l), (on))

/* GtkButton's image: an icon-only button takes it as its child; with a
   label (set before, mnemonic or not) it's [image label] in a box, as
   GTK3's always-show-image laid it out */
static inline void
nx_button_set_image (GtkWidget *button, GtkWidget *image)
{
	const char *text = gtk_button_get_label (GTK_BUTTON (button));

	if (text && *text)
	{
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
		GtkWidget *lbl = gtk_label_new (NULL);

		if (gtk_button_get_use_underline (GTK_BUTTON (button)))
			gtk_label_set_text_with_mnemonic (GTK_LABEL (lbl), text);
		else
			gtk_label_set_text (GTK_LABEL (lbl), text);
		gtk_label_set_mnemonic_widget (GTK_LABEL (lbl), button);
		gtk_box_append (GTK_BOX (box), image);
		gtk_box_append (GTK_BOX (box), lbl);
		gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
		gtk_button_set_child (GTK_BUTTON (button), box);
		gtk_widget_add_css_class (button, "nx-image-text");
	}
	else
		gtk_button_set_child (GTK_BUTTON (button), image);
}

/* the size the window would open at again: its default size tracks the
   user's resizes */
#define nx_window_get_size(win, w, h) \
	gtk_window_get_default_size (GTK_WINDOW (win), (w), (h))

/* a grid child's cell */
#define nx_grid_child_get(grid, child, l, t, w, h) \
	gtk_grid_query_child (GTK_GRID (grid), (child), (l), (t), (w), (h))

/* GtkPaned's handle: GTK4 has no style property. GTK3 Adwaita's
   handle-size was 5, and the user list width is kept from the right edge
   with it, so use the same number for the same layout */
#define nx_paned_handle_size(p) 5

/* take a child out of whatever container it's in (box, paned, ...) */
static inline void
nx_widget_unparent_child (GtkWidget *child)
{
	GtkWidget *parent = gtk_widget_get_parent (child);

	if (!parent)
		return;
	if (GTK_IS_BOX (parent))
		gtk_box_remove (GTK_BOX (parent), child);
	else if (GTK_IS_PANED (parent))
	{
		if (gtk_paned_get_start_child (GTK_PANED (parent)) == child)
			gtk_paned_set_start_child (GTK_PANED (parent), NULL);
		else
			gtk_paned_set_end_child (GTK_PANED (parent), NULL);
	}
	else if (GTK_IS_GRID (parent))
		gtk_grid_remove (GTK_GRID (parent), child);
	else
		gtk_widget_unparent (child);
}

/* GtkEventBox (its own input window) is gone: every widget takes input.
   A plain box stands in where code packed a child into one. */
#define nx_event_box_new() gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0)

/* GdkPixbuf -> GdkTexture (gdk_texture_new_for_pixbuf is deprecated) */
static inline GdkTexture *
nx_texture_new_for_pixbuf (GdkPixbuf *pix)
{
	GBytes *b = gdk_pixbuf_read_pixel_bytes (pix);
	GdkTexture *t = gdk_memory_texture_new (gdk_pixbuf_get_width (pix), gdk_pixbuf_get_height (pix),
														 gdk_pixbuf_get_has_alpha (pix) ? GDK_MEMORY_R8G8B8A8
																							: GDK_MEMORY_R8G8B8,
														 b, gdk_pixbuf_get_rowstride (pix));
	g_bytes_unref (b);
	return t;
}

/* the pixbuf an image shows: we keep it on the image ourselves */
static inline void
nx_image_set_from_pixbuf (GtkWidget *img, GdkPixbuf *pix)
{
	g_object_set_data_full (G_OBJECT (img), "nx-pixbuf", pix ? g_object_ref (pix) : NULL,
									pix ? g_object_unref : NULL);
	if (pix)
	{
		GdkTexture *t = nx_texture_new_for_pixbuf (pix);
		gtk_image_set_from_paintable (GTK_IMAGE (img), GDK_PAINTABLE (t));
		g_object_unref (t);
	}
	else
		gtk_image_clear (GTK_IMAGE (img));
}
#define nx_image_get_pixbuf(img) ((GdkPixbuf *) g_object_get_data (G_OBJECT (img), "nx-pixbuf"))
static inline GtkWidget *
nx_image_new_from_pixbuf (GdkPixbuf *pix)
{
	GtkWidget *img = gtk_image_new ();
	nx_image_set_from_pixbuf (img, pix);
	return img;
}

#define nx_window_iconify(w) gtk_window_minimize (GTK_WINDOW (w))
#define nx_window_deiconify(w) gtk_window_unminimize (GTK_WINDOW (w))

/* GTK4: a GtkApplication with the desktop file's id, so Wayland docks
   match windows to io.github.nexchat. Non-unique: a second nexchat starts
   as before (-e/--existing is the D-Bus remote, not GApplication). */
#define nx_gtk_init(argc, argv) nx_app_init ()
void nx_app_init (void);
GtkApplication *nx_app (void);
GMainLoop *nx_main_loop (void);
#define nx_main() g_main_loop_run (nx_main_loop ())
#define nx_main_quit() g_main_loop_quit (nx_main_loop ())

/* window state, as flags */
typedef enum { NX_WIN_ICONIFIED = 1, NX_WIN_MAXIMIZED = 2, NX_WIN_FULLSCREEN = 4 } nx_window_state;
typedef void (*nx_window_state_cb) (GtkWindow *win, nx_window_state changed,
												nx_window_state now, gpointer ud);
/* the window was moved or resized */
typedef void (*nx_window_configure_cb) (GtkWindow *win, gpointer ud);
void nx_on_window_state (GtkWidget *win, nx_window_state_cb cb, gpointer ud);
void nx_on_window_configure (GtkWidget *win, nx_window_configure_cb cb, gpointer ud);
/* the user asked to close the window: return TRUE to keep it open
   (GTK3 delete-event / GTK4 close-request) */
typedef gboolean (*nx_close_request_cb) (GtkWidget *win, gpointer ud);
#define nx_on_close_request(win, cb, ud) \
	g_signal_connect (G_OBJECT (win), "close-request", G_CALLBACK (cb), (ud))
/* GTK4: gtk_window_new () takes no type */
#define nx_window_new() gtk_window_new ()

/* active state of a toggle or check/radio button (GTK4: GtkCheckButton is
   no longer a GtkToggleButton) */
static inline gboolean
nx_toggle_get_active (GtkWidget *w)
{
	if (GTK_IS_CHECK_BUTTON (w))
		return gtk_check_button_get_active (GTK_CHECK_BUTTON (w));
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}
static inline void
nx_toggle_set_active (GtkWidget *w, gboolean on)
{
	if (GTK_IS_CHECK_BUTTON (w))
		gtk_check_button_set_active (GTK_CHECK_BUTTON (w), on);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), on);
}

/* GTK3's labelled frame: the line runs through the label's middle.
   GTK4 draws the border round the whole widget, label included, so make
   it from an unlabelled frame pulled up under a label in the window
   colour. Returns the frame (add the child to it, with a top margin for
   the label's lower half); pack gtk_widget_get_parent (frame). */
static inline GtkWidget *
nx_labelled_frame_new (const char *text)
{
	GtkWidget *ov = gtk_overlay_new ();
	GtkWidget *frame = gtk_frame_new (NULL);
	GtkWidget *label = gtk_label_new (text);
	int h;

	gtk_widget_measure (label, GTK_ORIENTATION_VERTICAL, -1, &h, NULL, NULL, NULL);
	/* the box line goes through the label's middle */
	gtk_widget_set_margin_top (frame, h / 2);
	gtk_overlay_set_child (GTK_OVERLAY (ov), frame);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_START);
	gtk_widget_add_css_class (label, "nx-frame-label");
	gtk_overlay_add_overlay (GTK_OVERLAY (ov), label);
	g_object_set_data (G_OBJECT (frame), "nx-label-h", GINT_TO_POINTER (h - h / 2));
	return frame;
}

/* a display that can draw translucent windows */
#define nx_widget_is_composited(w) gdk_display_is_composited (gtk_widget_get_display (GTK_WIDGET (w)))
#define nx_css_provider_load(prov, css) gtk_css_provider_load_from_string ((prov), (css))
/* GTK3's width-chars set the natural width too; GTK4 needs max-width-chars */
#define nx_entry_set_width_chars(e, n) \
	(gtk_editable_set_width_chars (GTK_EDITABLE (e), (n)), \
	 gtk_editable_set_max_width_chars (GTK_EDITABLE (e), (n)))

/* hand the key event being handled to an entry, as if typed there
   (GTK3: gtk_widget_event). GTK4 has no synthetic dispatch: the key's
   text is inserted at the cursor. */
static inline void
nx_widget_forward_key (GtkWidget *entry, GdkEvent *ev)
{
	guint kv;
	gunichar c;

	if (!ev || gdk_event_get_event_type (ev) != GDK_KEY_PRESS)
		return;
	kv = gdk_key_event_get_keyval (ev);
	c = gdk_keyval_to_unicode (kv);
	if (c)
	{
		char buf[8];
		int pos = gtk_editable_get_position (GTK_EDITABLE (entry));
		buf[g_unichar_to_utf8 (c, buf)] = 0;
		gtk_editable_insert_text (GTK_EDITABLE (entry), buf, -1, &pos);
		gtk_editable_set_position (GTK_EDITABLE (entry), pos);
	}
}

/* icon sizes: GTK4 has normal (16) / large (32); pixel sizes keep the old
   GTK3 ones exact */
typedef enum { NX_ICON_MENU = 16, NX_ICON_BUTTON = 16, NX_ICON_LARGE_TOOLBAR = 24,
					NX_ICON_DIALOG = 48 } nx_icon_size;
static inline GtkWidget *
nx_image_new_from_icon_name (const char *name, nx_icon_size size)
{
	GtkWidget *img = gtk_image_new_from_icon_name (name);
	gtk_image_set_pixel_size (GTK_IMAGE (img), size);
	return img;
}

/* a choice of strings: GtkComboBoxText is deprecated, GtkDropDown on a
   GtkStringList. "changed" is notify::selected; the callback gets the
   widget, as GtkComboBox's "changed" did */
static inline GtkWidget *
nx_combo_new (const char * const *items)
{
	return gtk_drop_down_new_from_strings (items);
}
#define nx_combo_get_active(c) ((int) gtk_drop_down_get_selected (GTK_DROP_DOWN (c)))
#define nx_combo_set_active(c, i) \
	gtk_drop_down_set_selected (GTK_DROP_DOWN (c), (i) < 0 ? GTK_INVALID_LIST_POSITION : (guint) (i))
typedef void (*nx_combo_cb) (GtkWidget *combo, gpointer userdata);
typedef struct { nx_combo_cb cb; gpointer ud; } nx_combo_hook;
static void
nx_combo_selected_ (GObject *o, GParamSpec *ps, nx_combo_hook *h)
{
	h->cb (GTK_WIDGET (o), h->ud);
}
static void
nx_combo_hook_free_ (gpointer h, GClosure *c)
{
	g_free (h);
}
static inline void
nx_combo_on_changed (GtkWidget *c, nx_combo_cb cb, gpointer ud)
{
	nx_combo_hook *h = g_new (nx_combo_hook, 1);
	h->cb = cb;
	h->ud = ud;
	g_signal_connect_data (c, "notify::selected", G_CALLBACK (nx_combo_selected_), h,
								  nx_combo_hook_free_, 0);
}


#endif
