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

#endif

#endif
