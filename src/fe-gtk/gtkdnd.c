/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "gtkdnd.h"

typedef struct
{
	gpointer motion, leave, drop;
	gpointer ud;
	guint kinds;
} dnd_hook;

static dnd_hook *
dnd_hook_new (gpointer motion, gpointer leave, gpointer drop, gpointer ud, guint kinds)
{
	dnd_hook *h = g_new (dnd_hook, 1);
	h->motion = motion;
	h->leave = leave;
	h->drop = drop;
	h->ud = ud;
	h->kinds = kinds;
	return h;
}

static void
dnd_hook_free (gpointer h, GClosure *c)
{
	g_free (h);
}

#define ICON_KEY "nx-dnd-icon"


/* ================= GTK4: GtkDragSource / GtkDropTarget ================= */

#define MIME_CHANVIEW "application/x-nexchat-chanview"
#define MIME_USERLIST "application/x-nexchat-userlist"

static const char *
kind_mime (nx_dnd_kind kind)
{
	return kind == NX_DND_USERLIST ? MIME_USERLIST : MIME_CHANVIEW;
}

static GdkContentProvider *
source_prepare (GtkDragSource *src, double x, double y, gpointer kind)
{
	GBytes *b = g_bytes_new_static ("", 0);
	GdkContentProvider *p = gdk_content_provider_new_for_bytes (kind_mime (GPOINTER_TO_INT (kind)), b);
	g_bytes_unref (b);
	return p;
}

/* the icon: the widget shrunk to 4/5 width, 1/2 height, as GTK3 did */
static void
source_begin (GtkDragSource *src, GdkDrag *drag, gpointer kind)
{
	GtkWidget *w = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (src));
	int width = gtk_widget_get_width (w), height = gtk_widget_get_height (w);
	GdkPaintable *pt;
	GtkSnapshot *s;
	GskRenderNode *node;
	GskRenderer *r;
	GdkTexture *tex;
	graphene_rect_t rect;

	if (width < 2 || height < 2)
		return;
	pt = gtk_widget_paintable_new (w);
	s = gtk_snapshot_new ();
	gtk_snapshot_scale (s, 4.0 / 5.0, 0.5);
	gdk_paintable_snapshot (pt, s, width, height);
	node = gtk_snapshot_free_to_node (s);
	g_object_unref (pt);
	if (!node)
		return;
	r = gtk_native_get_renderer (gtk_widget_get_native (w));
	graphene_rect_init (&rect, 0, 0, width * 4 / 5, height / 2);
	tex = gsk_renderer_render_texture (r, node, &rect);
	gsk_render_node_unref (node);
	gtk_drag_source_set_icon (src, GDK_PAINTABLE (tex), 0, 0);
	g_object_unref (tex);
}

void
nx_dnd_layout_source (GtkWidget *w, nx_dnd_kind kind)
{
	GtkDragSource *src = gtk_drag_source_new ();

	/* the actions GTK3 used; drops tell the kinds apart by mime type */
	gtk_drag_source_set_actions (src, kind == NX_DND_USERLIST ? GDK_ACTION_MOVE : GDK_ACTION_COPY);
	g_signal_connect (src, "prepare", G_CALLBACK (source_prepare), GINT_TO_POINTER (kind));
	g_signal_connect (src, "drag-begin", G_CALLBACK (source_begin), GINT_TO_POINTER (kind));
	gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (src));
}

/* which of our layout kinds this drop is, 0 if none we take (or it comes
   from another program: layout drags are same-app, like GTK_TARGET_SAME_APP) */
static nx_dnd_kind
drop_kind (GdkDrop *drop, dnd_hook *h)
{
	GdkContentFormats *f = gdk_drop_get_formats (drop);

	if (!gdk_drop_get_drag (drop))
		return 0;
	if ((h->kinds & NX_DND_CHANVIEW) && gdk_content_formats_contain_mime_type (f, MIME_CHANVIEW))
		return NX_DND_CHANVIEW;
	if ((h->kinds & NX_DND_USERLIST) && gdk_content_formats_contain_mime_type (f, MIME_USERLIST))
		return NX_DND_USERLIST;
	return 0;
}

static gboolean
layout_accept (GtkDropTargetAsync *t, GdkDrop *drop, dnd_hook *h)
{
	return drop_kind (drop, h) != 0;
}

static GdkDragAction
layout_motion (GtkDropTargetAsync *t, GdkDrop *drop, double x, double y, dnd_hook *h)
{
	nx_dnd_kind k = drop_kind (drop, h);

	if (!k)
		return 0;
	if (h->motion)
		((nx_dnd_layout_cb) h->motion) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (t)),
												  y, k, h->ud);
	return k == NX_DND_USERLIST ? GDK_ACTION_MOVE : GDK_ACTION_COPY;
}

static void
layout_leave (GtkDropTargetAsync *t, GdkDrop *drop, dnd_hook *h)
{
	if (h->leave)
		((nx_dnd_leave_cb) h->leave) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (t)), h->ud);
}

static gboolean
layout_drop (GtkDropTargetAsync *t, GdkDrop *drop, double x, double y, dnd_hook *h)
{
	nx_dnd_kind k = drop_kind (drop, h);
	GdkDragAction a = k == NX_DND_USERLIST ? GDK_ACTION_MOVE : GDK_ACTION_COPY;

	if (!k)
		return FALSE;
	/* nothing to transfer: the drop itself is the message */
	gdk_drop_finish (drop, a);
	if (h->drop)
		((nx_dnd_layout_cb) h->drop) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (t)),
												y, k, h->ud);
	return TRUE;
}

void
nx_dnd_layout_dest (GtkWidget *w, guint kinds, nx_dnd_layout_cb motion,
						  nx_dnd_leave_cb leave, nx_dnd_layout_cb drop, gpointer ud)
{
	GdkContentFormatsBuilder *b = gdk_content_formats_builder_new ();
	GtkDropTargetAsync *t;
	dnd_hook *h = dnd_hook_new (motion, leave, drop, ud, kinds);

	if (kinds & NX_DND_CHANVIEW)
		gdk_content_formats_builder_add_mime_type (b, MIME_CHANVIEW);
	if (kinds & NX_DND_USERLIST)
		gdk_content_formats_builder_add_mime_type (b, MIME_USERLIST);
	t = gtk_drop_target_async_new (gdk_content_formats_builder_free_to_formats (b),
											 GDK_ACTION_MOVE | GDK_ACTION_COPY);
	g_signal_connect (t, "accept", G_CALLBACK (layout_accept), h);
	g_signal_connect (t, "drag-enter", G_CALLBACK (layout_motion), h);
	g_signal_connect (t, "drag-motion", G_CALLBACK (layout_motion), h);
	g_signal_connect (t, "drag-leave", G_CALLBACK (layout_leave), h);
	g_signal_connect_data (t, "drop", G_CALLBACK (layout_drop), h, dnd_hook_free, 0);
	gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (t));
}

static GdkDragAction
file_motion (GtkDropTarget *t, double x, double y, dnd_hook *h)
{
	if (h->motion)
		((nx_dnd_point_cb) h->motion) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (t)),
												 x, y, h->ud);
	return GDK_ACTION_COPY;
}

static void
file_leave (GtkDropTarget *t, dnd_hook *h)
{
	if (h->leave)
		((nx_dnd_leave_cb) h->leave) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (t)), h->ud);
}

static gboolean
file_drop (GtkDropTarget *t, const GValue *v, double x, double y, dnd_hook *h)
{
	GSList *files = g_value_get_boxed (v), *l;
	GString *uris = g_string_new (NULL);

	/* back to the text/uri-list GTK3 handed over */
	for (l = files; l; l = l->next)
	{
		char *u = g_file_get_uri (l->data);
		g_string_append (uris, u);
		g_string_append (uris, "\r\n");
		g_free (u);
	}
	if (uris->len)
		((nx_dnd_files_cb) h->drop) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (t)),
											  x, y, uris->str, h->ud);
	g_string_free (uris, TRUE);
	return TRUE;
}

void
nx_dnd_file_dest (GtkWidget *w, nx_dnd_files_cb drop, nx_dnd_point_cb motion,
						nx_dnd_leave_cb leave, gpointer ud)
{
	GtkDropTarget *t = gtk_drop_target_new (GDK_TYPE_FILE_LIST,
														 GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
	dnd_hook *h = dnd_hook_new (motion, leave, drop, ud, 0);

	g_signal_connect (t, "enter", G_CALLBACK (file_motion), h);
	g_signal_connect (t, "motion", G_CALLBACK (file_motion), h);
	g_signal_connect (t, "leave", G_CALLBACK (file_leave), h);
	g_signal_connect_data (t, "drop", G_CALLBACK (file_drop), h, dnd_hook_free, 0);
	gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (t));
}

