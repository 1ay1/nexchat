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

#if GTK_CHECK_VERSION(4, 0, 0)

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

#else

/* ================= GTK3: drag signals, same setup as before ================= */

#define TARGET_CHANVIEW "HEXCHAT_CHANVIEW"
#define TARGET_USERLIST "HEXCHAT_USERLIST"
#define TARGET_URIS "text/uri-list"

/* every dest call adds its targets to one list on the widget, in call order,
   so a widget that takes files and layout drags gets the one
   gtk_drag_dest_set it always had */
static void
dest_add (GtkWidget *w, const char *target, guint flags, guint info, GtkDestDefaults defaults)
{
	GtkTargetList *list = gtk_drag_dest_get_target_list (w);

	if (!list)
	{
		gtk_drag_dest_set (w, defaults, NULL, 0,
								 GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK);
		list = gtk_target_list_new (NULL, 0);
		gtk_drag_dest_set_target_list (w, list);
		gtk_target_list_unref (list);
	}
	gtk_target_list_add (list, gdk_atom_intern_static_string (target), flags, info);
}

/* kind of a layout drag (first target HEXCHAT_*), 0 for anything else */
static nx_dnd_kind
context_kind (GdkDragContext *context)
{
	GList *t = context ? gdk_drag_context_list_targets (context) : NULL;
	nx_dnd_kind k = 0;
	char *name;

	if (!t || !t->data)
		return 0;
	name = gdk_atom_name (t->data);
	if (name && !strcmp (name, TARGET_CHANVIEW))
		k = NX_DND_CHANVIEW;
	else if (name && !strcmp (name, TARGET_USERLIST))
		k = NX_DND_USERLIST;
	g_free (name);
	return k;
}

static void
source_begin (GtkWidget *widget, GdkDragContext *context, gpointer unused)
{
	int width, height;
	GdkPixbuf *pix, *pix2;

	if (!context_kind (context))
		return;

	width = gdk_window_get_width (gtk_widget_get_window (widget));
	height = gdk_window_get_height (gtk_widget_get_window (widget));

	pix = gdk_pixbuf_get_from_window (gtk_widget_get_window (widget), 0, 0, width, height);
	pix2 = gdk_pixbuf_scale_simple (pix, width * 4 / 5, height / 2, GDK_INTERP_HYPER);
	g_object_unref (pix);

	gtk_drag_set_icon_pixbuf (context, pix2, 0, 0);
	g_object_set_data_full (G_OBJECT (widget), ICON_KEY, pix2, g_object_unref);
}

static void
source_end (GtkWidget *widget, GdkDragContext *context, gpointer unused)
{
	if (context_kind (context))
		g_object_set_data (G_OBJECT (widget), ICON_KEY, NULL);
}

void
nx_dnd_layout_source (GtkWidget *w, nx_dnd_kind kind)
{
	static const GtkTargetEntry chanview[] = { { TARGET_CHANVIEW, GTK_TARGET_SAME_APP, 75 } };
	static const GtkTargetEntry userlist[] = { { TARGET_USERLIST, GTK_TARGET_SAME_APP, 75 } };

	if (kind == NX_DND_USERLIST)
		gtk_drag_source_set (w, GDK_BUTTON1_MASK, userlist, 1, GDK_ACTION_MOVE);
	else
		gtk_drag_source_set (w, GDK_BUTTON1_MASK, chanview, 1, GDK_ACTION_COPY);
	g_signal_connect (w, "drag-begin", G_CALLBACK (source_begin), NULL);
	g_signal_connect (w, "drag-end", G_CALLBACK (source_end), NULL);
}

static gboolean
layout_motion (GtkWidget *w, GdkDragContext *context, int x, int y, guint time, dnd_hook *h)
{
	nx_dnd_kind k = context_kind (context);

	if (!k)
		return FALSE;
	if (h->motion)
		((nx_dnd_layout_cb) h->motion) (w, y, k, h->ud);
	return TRUE;
}

static void
layout_leave (GtkWidget *w, GdkDragContext *context, guint time, dnd_hook *h)
{
	if (h->leave)
		((nx_dnd_leave_cb) h->leave) (w, h->ud);
}

static gboolean
layout_drop (GtkWidget *w, GdkDragContext *context, int x, int y, guint time, dnd_hook *h)
{
	nx_dnd_kind k = context_kind (context);

	if (!k)
		return FALSE;
	if (h->drop)
		((nx_dnd_layout_cb) h->drop) (w, y, k, h->ud);
	return TRUE;
}

void
nx_dnd_layout_dest (GtkWidget *w, guint kinds, nx_dnd_layout_cb motion,
						  nx_dnd_leave_cb leave, nx_dnd_layout_cb drop, gpointer ud)
{
	GtkDestDefaults d = (kinds & NX_DND_NO_THEME_HIGHLIGHT) ?
		GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP : GTK_DEST_DEFAULT_ALL;
	dnd_hook *h = dnd_hook_new (motion, leave, drop, ud, kinds);

	if (kinds & NX_DND_CHANVIEW)
		dest_add (w, TARGET_CHANVIEW, GTK_TARGET_SAME_APP, 75, d);
	if (kinds & NX_DND_USERLIST)
		dest_add (w, TARGET_USERLIST, GTK_TARGET_SAME_APP, 75, d);
	g_signal_connect (w, "drag-drop", G_CALLBACK (layout_drop), h);
	g_signal_connect (w, "drag-motion", G_CALLBACK (layout_motion), h);
	g_signal_connect_data (w, "drag-leave", G_CALLBACK (layout_leave), h, dnd_hook_free, 0);
}

static gboolean
file_motion (GtkWidget *w, GdkDragContext *context, int x, int y, guint time, dnd_hook *h)
{
	if (h->motion)
		((nx_dnd_point_cb) h->motion) (w, x, y, h->ud);
	return FALSE;
}

static void
file_leave (GtkWidget *w, GdkDragContext *context, guint time, dnd_hook *h)
{
	if (h->leave)
		((nx_dnd_leave_cb) h->leave) (w, h->ud);
}

static void
file_received (GtkWidget *w, GdkDragContext *context, int x, int y,
					GtkSelectionData *sel, guint info, guint time, dnd_hook *h)
{
	const guchar *data = gtk_selection_data_get_data (sel);

	/* layout drags arrive here too, with no data */
	if (info == 1 && data && gtk_selection_data_get_length (sel) > 0)
		((nx_dnd_files_cb) h->drop) (w, x, y, (const char *) data, h->ud);
}

void
nx_dnd_file_dest (GtkWidget *w, nx_dnd_files_cb drop, nx_dnd_point_cb motion,
						nx_dnd_leave_cb leave, gpointer ud)
{
	dnd_hook *h = dnd_hook_new (motion, leave, drop, ud, 0);

	dest_add (w, TARGET_URIS, 0, 1, GTK_DEST_DEFAULT_ALL);
	g_signal_connect (w, "drag-motion", G_CALLBACK (file_motion), h);
	g_signal_connect (w, "drag-leave", G_CALLBACK (file_leave), h);
	g_signal_connect_data (w, "drag-data-received", G_CALLBACK (file_received), h,
								  dnd_hook_free, 0);
}

#endif
