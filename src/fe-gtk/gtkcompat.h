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

#if GTK_CHECK_VERSION(4, 0, 0)

/* ---- GTK4 ---- */

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

	if (horiz)
	{
		gtk_widget_set_hexpand (child, expand);
		if (expand && !fill)
			gtk_widget_set_halign (child, GTK_ALIGN_CENTER);
		gtk_widget_set_margin_start (child, padding);
		gtk_widget_set_margin_end (child, padding);
	}
	else
	{
		gtk_widget_set_vexpand (child, expand);
		if (expand && !fill)
			gtk_widget_set_valign (child, GTK_ALIGN_CENTER);
		gtk_widget_set_margin_top (child, padding);
		gtk_widget_set_margin_bottom (child, padding);
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
}
#define nx_box_pack_start(box, child, expand, fill, pad) \
	nx_box_pack (GTK_BOX (box), (child), (expand), (fill), (pad), FALSE)
#define nx_box_pack_end(box, child, expand, fill, pad) \
	nx_box_pack (GTK_BOX (box), (child), (expand), (fill), (pad), TRUE)

/* gtk_container_set_border_width: margins on all four sides */
static inline void
nx_set_border_width (GtkWidget *w, guint px)
{
	gtk_widget_set_margin_start (w, px);
	gtk_widget_set_margin_end (w, px);
	gtk_widget_set_margin_top (w, px);
	gtk_widget_set_margin_bottom (w, px);
}

/* widgets are visible by default in GTK4; show_all is gone */
#define nx_widget_show_all(w) gtk_widget_set_visible (GTK_WIDGET (w), TRUE)

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

/* GtkButtonBox is gone. Our boxes are all horizontal rows of buttons with
   a layout: SPREAD (equal gaps, homogeneous) or END (packed right).
   A homogeneous GtkBox with the right halign gives the same placement. */
typedef enum { NX_BUTTONBOX_SPREAD, NX_BUTTONBOX_END, NX_BUTTONBOX_START, NX_BUTTONBOX_EDGE } nx_buttonbox_layout;
static inline GtkWidget *
nx_button_box_new (GtkOrientation o, nx_buttonbox_layout layout)
{
	GtkWidget *box = gtk_box_new (o, 6);
	gtk_box_set_homogeneous (GTK_BOX (box), layout == NX_BUTTONBOX_SPREAD || layout == NX_BUTTONBOX_EDGE);
	if (layout == NX_BUTTONBOX_END)
		gtk_widget_set_halign (box, GTK_ALIGN_END);
	else if (layout == NX_BUTTONBOX_START)
		gtk_widget_set_halign (box, GTK_ALIGN_START);
	return box;
}

/* window placement hints: GTK4 leaves placement to the window manager */
#define nx_window_set_position_mouse(win) ((void) (win))
#define nx_window_set_position_center(win) ((void) (win))

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

/* the window holding w is the active one (has the keyboard) */
#define nx_widget_window_is_active(w) gtk_window_is_active (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (w))))

/* pointer shape by CSS cursor name ("pointer", "w-resize"); NULL: default */
#define nx_widget_set_cursor_name(w, name) \
	gtk_widget_set_cursor_from_name (GTK_WIDGET (w), (name))

#else

/* ---- GTK3: exactly the old calls ---- */

#define nx_box_pack_start(box, child, expand, fill, pad) \
	gtk_box_pack_start (GTK_BOX (box), (child), (expand), (fill), (pad))
#define nx_box_pack_end(box, child, expand, fill, pad) \
	gtk_box_pack_end (GTK_BOX (box), (child), (expand), (fill), (pad))
#define nx_set_border_width(w, px) \
	gtk_container_set_border_width (GTK_CONTAINER (w), (px))
#define nx_widget_show_all(w) gtk_widget_show_all (GTK_WIDGET (w))
#define nx_entry_get_text(e) gtk_entry_get_text (GTK_ENTRY (e))
#define nx_entry_set_text(e, t) gtk_entry_set_text (GTK_ENTRY (e), (t))
#define nx_widget_destroy(w) gtk_widget_destroy (GTK_WIDGET (w))
#define nx_widget_get_children(w) gtk_container_get_children (GTK_CONTAINER (w))
#define nx_bin_get_child(w) gtk_bin_get_child (GTK_BIN (w))

#define nx_scrolled_window_new() gtk_scrolled_window_new (NULL, NULL)
#define nx_scrolled_window_set_shadow_in(sw, on) \
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), (on) ? GTK_SHADOW_IN : GTK_SHADOW_NONE)

typedef enum { NX_BUTTONBOX_SPREAD, NX_BUTTONBOX_END, NX_BUTTONBOX_START, NX_BUTTONBOX_EDGE } nx_buttonbox_layout;
static inline GtkWidget *
nx_button_box_new (GtkOrientation o, nx_buttonbox_layout layout)
{
	GtkWidget *box = gtk_button_box_new (o);
	static const GtkButtonBoxStyle map[] = {
		GTK_BUTTONBOX_SPREAD, GTK_BUTTONBOX_END, GTK_BUTTONBOX_START, GTK_BUTTONBOX_EDGE };
	gtk_button_box_set_layout (GTK_BUTTON_BOX (box), map[layout]);
	return box;
}

#define nx_window_set_position_mouse(win) \
	gtk_window_set_position (GTK_WINDOW (win), GTK_WIN_POS_MOUSE)
#define nx_window_set_position_center(win) \
	gtk_window_set_position (GTK_WINDOW (win), GTK_WIN_POS_CENTER)

#define nx_widget_get_toplevel(w) gtk_widget_get_toplevel (GTK_WIDGET (w))

static inline void
nx_widget_make_default (GtkWidget *button)
{
	gtk_widget_set_can_default (button, TRUE);
	gtk_widget_grab_default (button);
}

#define nx_widget_get_width(w) gdk_window_get_width (gtk_widget_get_window (GTK_WIDGET (w)))
#define nx_widget_get_height(w) gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET (w)))
#define nx_widget_get_allocated_width(w) gtk_widget_get_allocated_width (GTK_WIDGET (w))
#define nx_widget_get_allocated_height(w) gtk_widget_get_allocated_height (GTK_WIDGET (w))

#define nx_widget_window_is_active(w) gtk_window_has_toplevel_focus (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (w))))

/* GTK3: the old GdkCursorType for the two names we use, so the shapes
   don't change; anything else goes by name */
static inline void
nx_widget_set_cursor_name (GtkWidget *w, const char *name)
{
	GdkWindow *win = gtk_widget_get_window (w);
	GdkCursor *c = NULL;

	if (!win)
		return;
	if (name && !strcmp (name, "pointer"))
		c = gdk_cursor_new_for_display (gdk_window_get_display (win), GDK_HAND1);
	else if (name && !strcmp (name, "w-resize"))
		c = gdk_cursor_new_for_display (gdk_window_get_display (win), GDK_LEFT_SIDE);
	else if (name)
		c = gdk_cursor_new_from_name (gdk_window_get_display (win), name);
	gdk_window_set_cursor (win, c);
	if (c)
		g_object_unref (c);
}

#endif

#endif
