/* nexchat
 * Copyright (C) 2026 nexchat contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Images from bytes, for link previews: GTK itself reads PNG, JPEG and
   TIFF; everything else (GIF, WebP, BMP, ICO, SVG...) goes through
   gdk-pixbuf's loaders, the ones the app bundle ships. An animated GIF
   becomes a paintable that plays its frames (each with its own delay),
   so a GtkPicture shows it moving. */

#include <string.h>
#include <gtk/gtk.h>
/* gdk-pixbuf's animation API (and the GTimeVal its iterator takes) is
   marked deprecated with nothing to replace it for decoding a GIF's
   frames; this file is the one place that uses it */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "nximage.h"

#define MAX_FRAMES 300			/* a longer animation stops at this many */
#define MAX_PIXELS (4096 * 4096)	/* bigger than this: not decoded at all */

/* ---- an animation as a paintable ---- */

#define NX_TYPE_ANIM (nx_anim_get_type ())
G_DECLARE_FINAL_TYPE (NxAnim, nx_anim, NX, ANIM, GObject)

struct _NxAnim
{
	GObject parent;
	GPtrArray *frames;	/* GdkTexture */
	GArray *delays;		/* int ms, per frame */
	guint current;
	guint timer;
	int w, h;
};

static void nx_anim_paintable_init (GdkPaintableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (NxAnim, nx_anim, G_TYPE_OBJECT,
								 G_IMPLEMENT_INTERFACE (GDK_TYPE_PAINTABLE, nx_anim_paintable_init))

static gboolean
nx_anim_tick (gpointer data)
{
	NxAnim *a = data;
	int delay;

	a->current = (a->current + 1) % a->frames->len;
	gdk_paintable_invalidate_contents (GDK_PAINTABLE (a));
	delay = g_array_index (a->delays, int, a->current);
	a->timer = delay > 0 ? g_timeout_add (delay, nx_anim_tick, a) : 0;
	return G_SOURCE_REMOVE;
}

static void
nx_anim_snapshot (GdkPaintable *p, GdkSnapshot *s, double w, double h)
{
	NxAnim *a = NX_ANIM (p);

	gdk_paintable_snapshot (GDK_PAINTABLE (g_ptr_array_index (a->frames, a->current)), s, w, h);
}

static int
nx_anim_width (GdkPaintable *p)
{
	return NX_ANIM (p)->w;
}

static int
nx_anim_height (GdkPaintable *p)
{
	return NX_ANIM (p)->h;
}

static GdkPaintable *
nx_anim_current_image (GdkPaintable *p)
{
	NxAnim *a = NX_ANIM (p);
	return g_object_ref (GDK_PAINTABLE (g_ptr_array_index (a->frames, a->current)));
}

static void
nx_anim_paintable_init (GdkPaintableInterface *iface)
{
	iface->snapshot = nx_anim_snapshot;
	iface->get_intrinsic_width = nx_anim_width;
	iface->get_intrinsic_height = nx_anim_height;
	iface->get_current_image = nx_anim_current_image;
}

static void
nx_anim_finalize (GObject *o)
{
	NxAnim *a = NX_ANIM (o);

	if (a->timer)
		g_source_remove (a->timer);
	g_ptr_array_unref (a->frames);
	g_array_unref (a->delays);
	G_OBJECT_CLASS (nx_anim_parent_class)->finalize (o);
}

static void
nx_anim_class_init (NxAnimClass *c)
{
	G_OBJECT_CLASS (c)->finalize = nx_anim_finalize;
}

static void
nx_anim_init (NxAnim *a)
{
	a->frames = g_ptr_array_new_with_free_func (g_object_unref);
	a->delays = g_array_new (FALSE, FALSE, sizeof (int));
}

/* ---- decoding ---- */

static GdkTexture *
texture_from_pixbuf (GdkPixbuf *pb)
{
	return gdk_texture_new_for_pixbuf (pb);
}

/* same pixels: the loop has come round to the first frame again */
static gboolean
same_pixels (GdkPixbuf *a, GdkPixbuf *b)
{
	gsize la, lb;
	const guint8 *pa, *pb;

	if (gdk_pixbuf_get_width (a) != gdk_pixbuf_get_width (b) ||
		 gdk_pixbuf_get_height (a) != gdk_pixbuf_get_height (b) ||
		 gdk_pixbuf_get_rowstride (a) != gdk_pixbuf_get_rowstride (b))
		return FALSE;
	pa = gdk_pixbuf_read_pixels (a);
	pb = gdk_pixbuf_read_pixels (b);
	la = gdk_pixbuf_get_byte_length (a);
	lb = gdk_pixbuf_get_byte_length (b);
	return la == lb && !memcmp (pa, pb, la);
}

/* all the frames of an animation, each composited the way it shows
   (gdk-pixbuf's iterator does the disposal and blending), one loop of
   it: the iterator loops forever, so stop when it's back at the start */
static GdkPaintable *
anim_from (GdkPixbufAnimation *pa)
{
	NxAnim *a;
	GdkPixbufAnimationIter *it;
	GdkPixbuf *first = NULL;
	GTimeVal t = { 0, 0 };	/* gdk-pixbuf's iterator API still takes it */
	guint n;

	it = gdk_pixbuf_animation_get_iter (pa, &t);
	a = g_object_new (NX_TYPE_ANIM, NULL);
	a->w = gdk_pixbuf_animation_get_width (pa);
	a->h = gdk_pixbuf_animation_get_height (pa);
	for (n = 0; n < MAX_FRAMES; n++)
	{
		int delay = gdk_pixbuf_animation_iter_get_delay_time (it);
		/* a copy: the iterator reuses its buffer for the next frame */
		GdkPixbuf *frame = gdk_pixbuf_copy (gdk_pixbuf_animation_iter_get_pixbuf (it));

		if (n > 0 && same_pixels (frame, first))
		{
			g_object_unref (frame);
			break;
		}
		if (!first)
			first = g_object_ref (frame);
		g_ptr_array_add (a->frames, texture_from_pixbuf (frame));
		g_object_unref (frame);
		/* -1: this frame stays forever (a still image, or the end) */
		if (delay < 0)
		{
			int forever = -1;
			g_array_append_val (a->delays, forever);
			break;
		}
		/* browsers play 0-10 ms frames at 100 ms */
		if (delay <= 10)
			delay = 100;
		g_array_append_val (a->delays, delay);
		t.tv_usec += (glong) delay * 1000;
		t.tv_sec += t.tv_usec / G_USEC_PER_SEC;
		t.tv_usec %= G_USEC_PER_SEC;
		gdk_pixbuf_animation_iter_advance (it, &t);
	}
	g_object_unref (it);
	if (first)
		g_object_unref (first);
	return GDK_PAINTABLE (a);
}

/* start playing (the first frame shows until then) */
static void
anim_start (NxAnim *a)
{
	int d;

	if (a->frames->len < 2 || a->timer)
		return;
	d = g_array_index (a->delays, int, 0);
	if (d > 0)
		a->timer = g_timeout_add (d, nx_anim_tick, a);
}

GdkPaintable *
nx_image_from_bytes (GBytes *bytes, int *width, int *height, gboolean *animated)
{
	GdkTexture *t;
	GInputStream *in;
	GdkPixbufAnimation *pa;
	GdkPaintable *p = NULL;

	*animated = FALSE;
	/* GTK's own loaders first: PNG, JPEG, TIFF, quick and exact. Not for
	   a GIF: GTK (4.25+) would give only its first frame */
	{
		gsize n;
		const guint8 *d = g_bytes_get_data (bytes, &n);
		gboolean gif = n >= 6 && (!memcmp (d, "GIF87a", 6) || !memcmp (d, "GIF89a", 6));

		if (!gif && (t = gdk_texture_new_from_bytes (bytes, NULL)))
		{
			*width = gdk_texture_get_width (t);
			*height = gdk_texture_get_height (t);
			return GDK_PAINTABLE (t);
		}
	}
	/* the rest through gdk-pixbuf (GIF, WebP, BMP, ICO, SVG...) */
	in = g_memory_input_stream_new_from_bytes (bytes);
	pa = gdk_pixbuf_animation_new_from_stream (in, NULL, NULL);
	g_object_unref (in);
	if (!pa)
		return NULL;
	*width = gdk_pixbuf_animation_get_width (pa);
	*height = gdk_pixbuf_animation_get_height (pa);
	if (*width <= 0 || *height <= 0 || (gint64) *width * *height > MAX_PIXELS)
	{
		g_object_unref (pa);
		return NULL;
	}
	if (gdk_pixbuf_animation_is_static_image (pa))
		p = GDK_PAINTABLE (texture_from_pixbuf (gdk_pixbuf_animation_get_static_image (pa)));
	else
	{
		p = anim_from (pa);
		*animated = NX_ANIM (p)->frames->len > 1;
		anim_start (NX_ANIM (p));
	}
	g_object_unref (pa);
	return p;
}

G_GNUC_END_IGNORE_DEPRECATIONS
