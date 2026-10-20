/* nexchat
 * Copyright (C) 2026 nexchat contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Rest the pointer on a link to an image and the image pops up next to it
   (gui_url_preview). Nothing is fetched until you hover, and only after a
   short pause, so passing over links costs nothing; one fetch at a time,
   images up to 8 MB, results remembered for the session. Off while away
   and in channels where it's turned off... the fetch tells the image's
   site that someone looked, that's what a hover means here. */

#include <string.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/httpget.h"
#include "gtkutil.h"
#include "xtext.h"
#include "urlpreview.h"

#define HOVER_MS 450
#define MAX_IMAGE (8 * 1024 * 1024)
#define MAX_SIDE 360			/* the popup's largest side, in points */
#define CACHE_MAX 64

typedef struct
{
	GtkWidget *xtext;
	GtkWidget *pop;
	GtkWidget *pic;
	char *url;					/* hovered, waiting or shown */
	GdkRectangle at;
	guint timer;
	GCancellable *cancel;
} preview;

/* url -> GdkTexture, or a "not an image" marker (the cache value is the
   preview class's "nothing" sentinel) */
static GHashTable *cache;
static GQueue cache_order = G_QUEUE_INIT;
#define NOT_IMAGE ((gpointer) 1)

static void
cache_put (const char *url, gpointer v)
{
	if (!cache)
		cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	if (g_hash_table_contains (cache, url))
		return;
	g_hash_table_insert (cache, g_strdup (url), v);
	g_queue_push_tail (&cache_order, g_strdup (url));
	while (g_queue_get_length (&cache_order) > CACHE_MAX)
	{
		char *old = g_queue_pop_head (&cache_order);
		gpointer ov = g_hash_table_lookup (cache, old);
		if (ov && ov != NOT_IMAGE)
			g_object_unref (ov);
		g_hash_table_remove (cache, old);
		g_free (old);
	}
}

/* links that may well be images: by the name's ending (the server still
   has to say image/*). Pages aren't fetched at all. */
static gboolean
looks_like_image (const char *url)
{
	static const char *ext[] = { ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".svg", NULL };
	char *lower, *q;
	int i;
	gboolean yes = FALSE;

	if (g_ascii_strncasecmp (url, "http://", 7) && g_ascii_strncasecmp (url, "https://", 8))
		return FALSE;
	lower = g_ascii_strdown (url, -1);
	if ((q = strpbrk (lower, "?#")))
		*q = 0;
	for (i = 0; ext[i] && !yes; i++)
		yes = g_str_has_suffix (lower, ext[i]);
	g_free (lower);
	return yes;
}

static void
preview_hide (preview *p)
{
	if (p->timer)
	{
		g_source_remove (p->timer);
		p->timer = 0;
	}
	if (p->cancel)
	{
		g_cancellable_cancel (p->cancel);
		g_clear_object (&p->cancel);
	}
	if (p->pop && gtk_widget_get_visible (p->pop))
		gtk_popover_popdown (GTK_POPOVER (p->pop));
	g_clear_pointer (&p->url, g_free);
}

static void
preview_show (preview *p, GdkTexture *t)
{
	int w = gdk_texture_get_width (t), h = gdk_texture_get_height (t);
	double s = MIN (1.0, (double) MAX_SIDE / MAX (w, h));

	if (!p->pop)
	{
		p->pop = gtk_popover_new ();
		gtk_popover_set_has_arrow (GTK_POPOVER (p->pop), FALSE);
		gtk_popover_set_autohide (GTK_POPOVER (p->pop), FALSE);
		gtk_popover_set_position (GTK_POPOVER (p->pop), GTK_POS_TOP);
		gtk_widget_set_can_target (p->pop, FALSE);	/* the pointer stays the chat's */
		gtk_widget_set_parent (p->pop, p->xtext);
		p->pic = gtk_picture_new ();
		gtk_picture_set_content_fit (GTK_PICTURE (p->pic), GTK_CONTENT_FIT_CONTAIN);
		gtk_popover_set_child (GTK_POPOVER (p->pop), p->pic);
	}
	gtk_picture_set_paintable (GTK_PICTURE (p->pic), GDK_PAINTABLE (t));
	gtk_widget_set_size_request (p->pic, MAX (1, (int) (w * s)), MAX (1, (int) (h * s)));
	gtk_popover_set_pointing_to (GTK_POPOVER (p->pop), &p->at);
	gtk_popover_popup (GTK_POPOVER (p->pop));
	if (g_getenv ("NEXCHAT_PREVIEW_DEBUG"))
		g_printerr ("preview: shown %dx%d\n", w, h);
}

static void
fetched (httpget_result *res, gpointer data)
{
	preview *p = data;
	GdkTexture *t = NULL;
	const char *url = res->final_url;
	char *asked = g_object_get_data (G_OBJECT (p->xtext), "nx-preview-asked");

	if (res->body && !res->truncated && res->status == 200 && res->content_type &&
		 g_str_has_prefix (res->content_type, "image/"))
		t = gdk_texture_new_from_bytes (res->body, NULL);
	if (g_getenv ("NEXCHAT_PREVIEW_DEBUG"))
		g_printerr ("preview: %s -> %d %s %s\n", asked ? asked : url, res->status,
						res->content_type ? res->content_type : "-",
						t ? "image" : res->error ? res->error : "not an image");
	if (asked)
		cache_put (asked, t ? (gpointer) g_object_ref (t) : NOT_IMAGE);
	/* still on that link: show it */
	if (t && p->url && asked && !strcmp (p->url, asked))
		preview_show (p, t);
	if (t)
		g_object_unref (t);
	g_clear_object (&p->cancel);
}

static gboolean
hover_settled (gpointer data)
{
	preview *p = data;
	gpointer v;

	p->timer = 0;
	if (!p->url)
		return G_SOURCE_REMOVE;
	v = cache ? g_hash_table_lookup (cache, p->url) : NULL;
	if (v == NOT_IMAGE)
		return G_SOURCE_REMOVE;
	if (v)
	{
		preview_show (p, v);
		return G_SOURCE_REMOVE;
	}
	if (p->cancel)
		g_cancellable_cancel (p->cancel);
	g_clear_object (&p->cancel);
	p->cancel = g_cancellable_new ();
	g_object_set_data_full (G_OBJECT (p->xtext), "nx-preview-asked", g_strdup (p->url), g_free);
	httpget (p->url, MAX_IMAGE, p->cancel, fetched, p);
	return G_SOURCE_REMOVE;
}

static gboolean
allowed (void)
{
	if (!prefs.hex_gui_url_preview)
		return FALSE;
	/* away: you aren't looking, nothing to fetch for */
	if (current_sess && current_sess->server && current_sess->server->is_away)
		return FALSE;
	return TRUE;
}

static void
on_url_hover (GtkWidget *xtext, const char *url, GdkRectangle *at, preview *p)
{
	if (url && p->url && !strcmp (url, p->url))
	{
		if (at)
			p->at = *at;	/* same link, pointer moved along it */
		return;
	}
	preview_hide (p);
	if (!url || !allowed () || !looks_like_image (url))
		return;
	p->url = g_strdup (url);
	if (at)
		p->at = *at;
	p->timer = g_timeout_add (HOVER_MS, hover_settled, p);
}

static void
preview_free (preview *p)
{
	preview_hide (p);
	if (p->pop)
		gtk_widget_unparent (p->pop);
	g_free (p);
}

static gboolean
test_hover_cb (gpointer xtext)
{
	GdkRectangle at = { 120, 60, 1, 1 };
	g_signal_emit_by_name (xtext, "url-hover", g_getenv ("NEXCHAT_PREVIEW_URL"), &at);
	return G_SOURCE_REMOVE;
}

void
urlpreview_attach (GtkWidget *xtext)
{
	preview *p = g_new0 (preview, 1);

	p->xtext = xtext;
	g_signal_connect (xtext, "url-hover", G_CALLBACK (on_url_hover), p);
	g_object_set_data_full (G_OBJECT (xtext), "nx-urlpreview", p, (GDestroyNotify) preview_free);
	/* tests: NEXCHAT_PREVIEW_URL=url hovers it once the window is up */
	if (g_getenv ("NEXCHAT_PREVIEW_URL"))
		g_timeout_add (2500, test_hover_cb, xtext);
}

/* tests: hover this link now, as the pointer would */
void
urlpreview_test_hover (GtkWidget *xtext, const char *url)
{
	GdkRectangle at = { 40, 40, 1, 1 };
	g_signal_emit_by_name (xtext, "url-hover", url, &at);
}
