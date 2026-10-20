/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 * =========================================================================
 *
 * xtext, the text widget used by X-Chat.
 * By Peter Zelezny <zed@xchat.org>.
 *
 */

#define GDK_MULTIHEAD_SAFE
#define MARGIN 2						/* dont touch. */
#define REFRESH_TIMEOUT 20
#define WORDWRAP_LIMIT 24

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/hexchatc.h"
#include "../common/url.h"

#ifdef WIN32
#include "marshal.h"
#else
#include "../common/marshal.h"
#endif

#include "fe-gtk.h"
#include "xtext.h"
#include "fkeys.h"
#include "gtkevents.h"

static gboolean gtk_xtext_button_press (GtkWidget *, guint, int, double, double,
													 GdkModifierType, gpointer);
static gboolean gtk_xtext_button_release (GtkWidget *, guint, int, double, double,
														GdkModifierType, gpointer);
static void gtk_xtext_motion_notify (GtkWidget *, double, double, GdkModifierType,
												 gpointer);
static gboolean gtk_xtext_smooth_scroll (GtkWidget *, double, double, GdkScrollUnit,
													GdkModifierType, gpointer);
static void gtk_xtext_decelerate (GtkWidget *, double, double, gpointer);
static void gtk_xtext_crossing (GtkWidget *, gboolean, gpointer);

#define charlen(str) g_utf8_skip[*(guchar *)(str)]

/* the length in bytes of the one character a reader sees at str: a whole
   grapheme cluster, not one code point. "👍🏽" (thumb + skin tone),
   "👨‍👩‍👧" (joined with ZWJ), "🇩🇪" (two regional indicators), "é" written
   e + combining acute, "1️⃣" all come out as one. Clicking, selecting,
   wrapping and measuring step by this, so they agree with how Pango
   draws the run. Stops at end, at a control byte (mIRC colours), and
   never cuts a broken UTF-8 sequence short of one byte. */
static int
xtext_cluster_len (const unsigned char *str, const unsigned char *end)
{
	const unsigned char *p = str;
	gunichar c, prev;
	int ri = 0;	/* regional indicators so far */

	if (p >= end)
		return 0;
	if (*p < 0x80)
	{
		/* ASCII: alone, unless a combining mark or variation selector follows */
		p++;
		if (p >= end || *p < 0x80)
			return 1;
		prev = str[0];
	}
	else
	{
		int n = charlen (p);
		if (n < 1 || p + n > end || g_utf8_get_char_validated ((const char *) p, end - p) >= (gunichar) -2)
			return 1;	/* invalid: one byte, so it can't swallow good text */
		prev = g_utf8_get_char ((const char *) p);
		if (prev >= 0x1F1E6 && prev <= 0x1F1FF)
			ri = 1;
		p += n;
	}

	while (p < end && *p >= 0x80)
	{
		int n = charlen (p);
		GUnicodeType t;

		if (p + n > end)
			break;
		c = g_utf8_get_char_validated ((const char *) p, end - p);
		if (c >= (gunichar) -2)
			break;
		t = g_unichar_type (c);
		if (t == G_UNICODE_NON_SPACING_MARK || t == G_UNICODE_SPACING_MARK ||
			 t == G_UNICODE_ENCLOSING_MARK ||
			 (c >= 0xFE00 && c <= 0xFE0F) ||		/* variation selectors */
			 (c >= 0xE0100 && c <= 0xE01EF) ||
			 (c >= 0x1F3FB && c <= 0x1F3FF) ||		/* skin tones */
			 (c >= 0xE0020 && c <= 0xE007F) ||		/* tag sequences (flags of regions) */
			 c == 0x200D)							/* ZWJ: glue what follows too */
		{
			p += n;
			prev = c;
			continue;
		}
		if (prev == 0x200D)	/* the one after a ZWJ */
		{
			p += n;
			prev = c;
			continue;
		}
		if (ri == 1 && c >= 0x1F1E6 && c <= 0x1F1FF)	/* second half of a flag */
		{
			p += n;
			ri = 2;
			prev = c;
			continue;
		}
		/* Hangul syllables built from jamo */
		if ((c >= 0x1160 && c <= 0x11FF) && ((prev >= 0x1100 && prev <= 0x11FF) || (prev >= 0xAC00 && prev <= 0xD7A3)))
		{
			p += n;
			prev = c;
			continue;
		}
		break;
	}
	return p - str;
}

#ifdef WIN32
#include <windows.h>
#include <io.h>
#include <gdk/gdk.h>
#else
#include <unistd.h>
#endif

/* is delimiter */
#define is_del(c) \
	(c == ' ' || c == '\n' || c == '>' || c == '<' || c == 0)

/* force scrolling off */
#define dontscroll(buf) (buf)->last_pixel_pos = 0x7fffffff

static GtkWidgetClass *parent_class = NULL;

struct textentry
{
	struct textentry *next;
	struct textentry *prev;
	unsigned char *str;
	time_t stamp;
	gint16 str_width;
	gint16 str_len;
	gint16 mark_start;
	gint16 mark_end;
	gint16 indent;
	gint16 left_len;
	GSList *slp;
	GSList *sublines;
	guchar tag;
	guchar no_stamp;	/* a day separator: no time in front */
	guchar pad2;	/* 32-bit align : 44 bytes total */
	GList *marks;	/* List of found strings */
};

enum
{
	WORD_CLICK,
	LAST_SIGNAL
};

/* values for selection info */
enum
{
	TARGET_UTF8_STRING,
	TARGET_STRING,
	TARGET_TEXT,
	TARGET_COMPOUND_TEXT
};

static guint xtext_signals[LAST_SIGNAL];

char *nocasestrstr (const char *text, const char *tofind);	/* util.c */
int xtext_get_stamp_str (time_t, char **);
static void gtk_xtext_render_page (GtkXText * xtext);
static void gtk_xtext_render_page_now (GtkXText * xtext);
static void gtk_xtext_calc_lines (xtext_buffer *buf, int);
static char *gtk_xtext_selection_get_text (GtkXText *xtext, int *len_ret);
static void xtext_set_cursor (GtkXText *xtext, const char *name);

/* the page looks different now: drawn on the next frame (GTK4: only
   ::snapshot draws) */
static void
xtext_queue_draw (GtkXText *xtext)
{
	xtext->redraw_pending = TRUE;
	gtk_widget_queue_draw (GTK_WIDGET (xtext));
}
static textentry *gtk_xtext_nth (GtkXText *xtext, int line, int *subline);
static void gtk_xtext_adjustment_changed (GtkAdjustment * adj,
														GtkXText * xtext);
static void gtk_xtext_recalc_widths (xtext_buffer *buf, int);
static void gtk_xtext_fix_indent (xtext_buffer *buf);
static int gtk_xtext_find_subline (GtkXText *xtext, textentry *ent, int line);
/* static char *gtk_xtext_conv_color (unsigned char *text, int len, int *newlen); */
/* For use by gtk_xtext_strip_color() and its callers -- */
struct offlen_s {
	guint16 off;
	guint16 len;
	guint16 emph;
	guint16 width;
};
typedef struct offlen_s offlen_t;
static unsigned char *
gtk_xtext_strip_color (unsigned char *text, int len, unsigned char *outbuf,
							  int *newlen, GSList **slp, int strip_hidden);
static gboolean gtk_xtext_check_ent_visibility (GtkXText * xtext, textentry *find_ent, int add);
static int gtk_xtext_render_page_timeout (GtkXText * xtext);
static int gtk_xtext_search_offset (xtext_buffer *buf, textentry *ent, unsigned int off);
static GList * gtk_xtext_search_textentry (xtext_buffer *, textentry *);
static void gtk_xtext_search_textentry_add (xtext_buffer *, textentry *, GList *, gboolean);
static void gtk_xtext_search_textentry_del (xtext_buffer *, textentry *);
static void gtk_xtext_search_textentry_fini (gpointer, gpointer);
static void gtk_xtext_search_fini (xtext_buffer *);
static gboolean gtk_xtext_search_init (xtext_buffer *buf, const gchar *text, gtk_xtext_search_flags flags, GError **perr);
static char * gtk_xtext_get_word (GtkXText * xtext, int x, int y, textentry ** ret_ent, int *ret_off, int *ret_len, GSList **slp);

/* ======================================= */
/* ========== CAIRO DRAWING ============== */
/* ======================================= */

/* GTK2 xtext drew straight onto its GdkWindow with GCs, outside of any
   expose. GTK3 only lets you draw inside ::draw, so every one of those
   draws now lands on ->backing (an image surface the size of the widget)
   and ::draw just copies it out. The render code above this layer is
   unchanged. */

static void
xtext_damage (GtkXText *xtext)
{
	if (!xtext->in_draw)
		xtext_queue_draw (xtext);
}

static void
xtext_source_color (cairo_t *cr, const GdkRGBA *col)
{
	cairo_set_source_rgb (cr, col->red, col->green, col->blue);
}

static void
xtext_fill_rect (GtkXText *xtext, const GdkRGBA *col, int x, int y, int w, int h)
{
	if (!xtext->cr || w <= 0 || h <= 0)
		return;
	xtext_source_color (xtext->cr, col);
	cairo_rectangle (xtext->cr, x, y, w, h);
	cairo_fill (xtext->cr);
	xtext_damage (xtext);
}

/* gdk_draw_line() with a 1px GC, for the horizontal/vertical lines we use */
static void
xtext_draw_line (GtkXText *xtext, const GdkRGBA *col, int x1, int y1, int x2, int y2)
{
	if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
	if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
	xtext_fill_rect (xtext, col, x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}

/* fill with the background colour, or the tiled background image */
static void
xtext_draw_bg (GtkXText *xtext, int x, int y, int w, int h)
{
	if (!xtext->cr || w <= 0 || h <= 0)
		return;
	if (xtext->pixmap)
	{
		cairo_set_source (xtext->cr, xtext->pixmap);
		cairo_rectangle (xtext->cr, x, y, w, h);
		cairo_fill (xtext->cr);
		xtext_damage (xtext);
	} else
		xtext_fill_rect (xtext, &xtext->bgc.fg, x, y, w, h);
}

/* (re)create the backing surface when the widget's size changes */
static void
xtext_backing_update (GtkXText *xtext);

static void
xtext_scale_changed (GObject *o, GParamSpec *ps, gpointer d)
{
	GtkXText *xtext = GTK_XTEXT (o);

	/* the separator, marker and text widths are in points and stay; only
	   the pixels behind them change */
	gtk_widget_queue_draw (GTK_WIDGET (xtext));
	xtext->redraw_pending = TRUE;
}

/* (re)create the backing surface when the widget's size changes */
static void
xtext_backing_update (GtkXText *xtext)
{
	GtkWidget *widget = GTK_WIDGET (xtext);
	int w = MAX (1, nx_widget_get_allocated_width (widget));
	int h = MAX (1, nx_widget_get_allocated_height (widget));
	int scale;

	if (!gtk_widget_get_realized (widget))
		return;
	scale = MAX (1, gtk_widget_get_scale_factor (widget));
	if (xtext->backing && xtext->backing_scale == scale &&
		 cairo_image_surface_get_width (xtext->backing) == w * scale &&
		 cairo_image_surface_get_height (xtext->backing) == h * scale)
		return;

	if (xtext->cr)
		cairo_destroy (xtext->cr);
	if (xtext->backing)
		cairo_surface_destroy (xtext->backing);

	/* an image surface at the widget's scale, made by hand: GTK3's
	   gdk_window_create_similar_image_surface() sizes it in points on
	   Quartz, so Retina only filled the top-left quarter of the view */
	xtext->backing_scale = scale;
	xtext->backing = cairo_image_surface_create (CAIRO_FORMAT_RGB24, w * scale, h * scale);
	cairo_surface_set_device_scale (xtext->backing, scale, scale);
	xtext->cr = cairo_create (xtext->backing);
	xtext->backing_stale = TRUE;
	xtext_draw_bg (xtext, 0, 0, w, h);
}

static void
xtext_backing_free (GtkXText *xtext)
{
	if (xtext->cr)
	{
		cairo_destroy (xtext->cr);
		xtext->cr = NULL;
	}
	if (xtext->backing)
	{
		cairo_surface_destroy (xtext->backing);
		xtext->backing = NULL;
	}
}

/* ======================================= */
/* ============ PANGO BACKEND ============ */
/* ======================================= */

#define EMPH_ITAL 1
#define EMPH_BOLD 2
#define EMPH_HIDDEN 4
static PangoAttrList *attr_lists[4];
static int fontwidths[4][128];
/* widths of non-ASCII clusters, per emphasis: "bytes" -> width; measuring
   one through Pango each time a line is wrapped or clicked is slow */
static GHashTable *cluster_widths[4];

static int
xtext_cluster_width (GtkXText *xtext, const unsigned char *str, int len, int emphasis)
{
	char *key;
	gpointer v;
	int w;

	if (!cluster_widths[emphasis])
		cluster_widths[emphasis] = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	key = g_strndup ((const char *) str, len);
	if (g_hash_table_lookup_extended (cluster_widths[emphasis], key, NULL, &v))
	{
		g_free (key);
		return GPOINTER_TO_INT (v);
	}
	pango_layout_set_attributes (xtext->layout, attr_lists[emphasis]);
	pango_layout_set_text (xtext->layout, (const char *) str, len);
	pango_layout_get_pixel_size (xtext->layout, &w, NULL);
	g_hash_table_insert (cluster_widths[emphasis], key, GINT_TO_POINTER (w));
	return w;
}

static PangoAttribute *
xtext_pango_attr (PangoAttribute *attr)
{
	attr->start_index = PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
	attr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	return attr;
}

static void
xtext_pango_init (GtkXText *xtext)
{
	size_t i;
	int j;
	char buf[2] = "\000";

	if (attr_lists[0])
	{
		for (i = 0; i < (EMPH_ITAL | EMPH_BOLD); i++)
			pango_attr_list_unref (attr_lists[i]);
	}

	for (i = 0; i < sizeof attr_lists / sizeof attr_lists[0]; i++)
	{
		attr_lists[i] = pango_attr_list_new ();
		switch (i)
		{
		case 0:		/* Roman */
			break;
		case EMPH_ITAL:		/* Italic */
			pango_attr_list_insert (attr_lists[i],
				xtext_pango_attr (pango_attr_style_new (PANGO_STYLE_ITALIC)));
			break;
		case EMPH_BOLD:		/* Bold */
			pango_attr_list_insert (attr_lists[i],
				xtext_pango_attr (pango_attr_weight_new (PANGO_WEIGHT_BOLD)));
			break;
		case EMPH_ITAL | EMPH_BOLD:		/* Italic Bold */
			pango_attr_list_insert (attr_lists[i],
				xtext_pango_attr (pango_attr_style_new (PANGO_STYLE_ITALIC)));
			pango_attr_list_insert (attr_lists[i],
				xtext_pango_attr (pango_attr_weight_new (PANGO_WEIGHT_BOLD)));
			break;
		}

		/* Now initialize fontwidths[i] */
		if (cluster_widths[i])
			g_hash_table_remove_all (cluster_widths[i]);	/* a new font */
		pango_layout_set_attributes (xtext->layout, attr_lists[i]);
		for (j = 0; j < 128; j++)
		{
			buf[0] = j;
			pango_layout_set_text (xtext->layout, buf, 1);
			pango_layout_get_pixel_size (xtext->layout, &fontwidths[i][j], NULL);
		}
	}
	xtext->space_width = fontwidths[0][' '];
}

static void
backend_font_close (GtkXText *xtext)
{
	pango_font_description_free (xtext->font->font);
}

static void
backend_init (GtkXText *xtext)
{
	if (xtext->layout == NULL)
	{
		/* GTK4's default context measures unhinted: wider cells and gaps
		   between runs. Use GTK3's hinted metrics. */
		PangoContext *wctx = gtk_widget_get_pango_context (GTK_WIDGET (xtext));
		PangoContext *ctx = pango_font_map_create_context (pango_context_get_font_map (wctx));
		cairo_font_options_t *fo = cairo_font_options_create ();

		pango_context_set_language (ctx, pango_context_get_language (wctx));
		pango_context_set_base_dir (ctx, pango_context_get_base_dir (wctx));
		pango_cairo_context_set_resolution (ctx, pango_cairo_context_get_resolution (wctx));
		cairo_font_options_set_hint_metrics (fo, CAIRO_HINT_METRICS_ON);
		cairo_font_options_set_hint_style (fo, CAIRO_HINT_STYLE_SLIGHT);
		cairo_font_options_set_antialias (fo, CAIRO_ANTIALIAS_GRAY);
		pango_cairo_context_set_font_options (ctx, fo);
		pango_context_set_round_glyph_positions (ctx, TRUE);
		cairo_font_options_destroy (fo);
		xtext->layout = pango_layout_new (ctx);
		g_object_unref (ctx);
		if (xtext->font)
			pango_layout_set_font_description (xtext->layout, xtext->font->font);
	}
}

static void
backend_deinit (GtkXText *xtext)
{
	if (xtext->layout)
	{
		g_object_unref (xtext->layout);
		xtext->layout = NULL;
	}
}

static PangoFontDescription *
backend_font_open_real (char *name, int zoom)
{
	PangoFontDescription *font;

	font = pango_font_description_from_string (name);
	if (font && pango_font_description_get_size (font) == 0)
	{
		pango_font_description_free (font);
		font = pango_font_description_from_string ("sans 11");
	}
	if (!font)
		font = pango_font_description_from_string ("sans 11");

	/* text zoom, in percent */
	if (zoom > 0 && zoom != 100)
	{
		int size = pango_font_description_get_size (font) * zoom / 100;
		if (pango_font_description_get_size_is_absolute (font))
			pango_font_description_set_absolute_size (font, MAX (size, 4 * PANGO_SCALE));
		else
			pango_font_description_set_size (font, MAX (size, 4 * PANGO_SCALE));
	}

	return font;
}

static void
backend_font_open (GtkXText *xtext, char *name)
{
	PangoLanguage *lang;
	PangoContext *context;
	PangoFontMetrics *metrics;

	xtext->font = &xtext->pango_font;
	xtext->font->font = backend_font_open_real (name, xtext->zoom);
	if (!xtext->font->font)
	{
		xtext->font = NULL;
		return;
	}

	backend_init (xtext);
	pango_layout_set_font_description (xtext->layout, xtext->font->font);
	xtext_pango_init (xtext);

	/* vte does it this way */
	context = pango_layout_get_context (xtext->layout);
	lang = pango_context_get_language (context);
	metrics = pango_context_get_metrics (context, xtext->font->font, lang);
	xtext->font->ascent = pango_font_metrics_get_ascent (metrics) / PANGO_SCALE;
	xtext->font->descent = pango_font_metrics_get_descent (metrics) / PANGO_SCALE;

	/*
	 * In later versions of pango, a font's height should be calculated like
	 * this to account for line gap; a typical symptom of not doing so is
	 * cutting off the underscore on some fonts.
	 */
#if PANGO_VERSION_CHECK(1, 44, 0)
	xtext->fontsize = pango_font_metrics_get_height (metrics) / PANGO_SCALE + 1;

	if (xtext->fontsize == 0)
		xtext->fontsize = xtext->font->ascent + xtext->font->descent;
#else
	xtext->fontsize = xtext->font->ascent + xtext->font->descent;
#endif

	pango_font_metrics_unref (metrics);
}

static int
backend_get_text_width_emph (GtkXText *xtext, guchar *str, int len, int emphasis)
{
	int width;
	int deltaw;
	int mbl;

	if (*str == 0)
		return 0;

	if ((emphasis & EMPH_HIDDEN))
		return 0;
	emphasis &= (EMPH_ITAL | EMPH_BOLD);

	width = 0;
	pango_layout_set_attributes (xtext->layout, attr_lists[emphasis]);
	while (len > 0)
	{
		if (*str < 128 && (len == 1 || str[1] < 128))
		{
			deltaw = fontwidths[emphasis][*str];
			mbl = 1;
		}
		else
		{
			/* a whole cluster at once (emoji with skin tone, flags, accents
			   as combining marks): its parts measured alone come out far
			   wider than the one glyph Pango draws */
			mbl = xtext_cluster_len (str, str + len);
			if (mbl < 1)
				mbl = 1;
			deltaw = xtext_cluster_width (xtext, str, mbl, emphasis);
		}
		width += deltaw;
		str += mbl;
		len -= mbl;
	}

	return width;
}

static int
backend_get_text_width_slp (GtkXText *xtext, guchar *str, GSList *slp)
{
	int width = 0;

	while (slp)
	{
		offlen_t *meta;

		meta = slp->data;
		width += backend_get_text_width_emph (xtext, str, meta->len, meta->emph);
		str += meta->len;
		slp = g_slist_next (slp);
	}

	return width;
}

static void
backend_draw_text_emph (GtkXText *xtext, int dofill, xtext_gc *gc, int x, int y,
						 char *str, int len, int str_width, int emphasis)
{
	PangoLayoutLine *line;

	pango_layout_set_attributes (xtext->layout, attr_lists[emphasis]);
	pango_layout_set_text (xtext->layout, str, len);

	if (dofill)
		xtext_fill_rect (xtext, &gc->bg, x, y - xtext->font->ascent,
							  str_width, xtext->fontsize);

	if (!xtext->cr)
		return;

	line = pango_layout_get_lines (xtext->layout)->data;

	/* inside this row only: a glyph taller than the row (emoji, accents,
	   a fallback font) would otherwise paint into the rows above and below,
	   and when one row is redrawn alone (selecting, a link under the
	   mouse) the neighbours' leftovers stay on it */
	cairo_save (xtext->cr);
	cairo_rectangle (xtext->cr, 0, y - xtext->font->ascent,
	                 nx_widget_get_width (GTK_WIDGET (xtext)), xtext->fontsize);
	cairo_clip (xtext->cr);
	/* y is the baseline, same as gdk_draw_glyphs() took */
	xtext_source_color (xtext->cr, &gc->fg);
	cairo_move_to (xtext->cr, x, y);
	pango_cairo_show_layout_line (xtext->cr, line);
	cairo_restore (xtext->cr);
	xtext_damage (xtext);
}

static void
xtext_set_fg (GtkXText *xtext, xtext_gc *gc, int index)
{
	gc->fg = xtext->palette[index];
}

static void
xtext_set_bg (GtkXText *xtext, xtext_gc *gc, int index)
{
	gc->bg = xtext->palette[index];
}

static void
gtk_xtext_init (GtkXText * xtext)
{
	xtext->pixmap = NULL;
	xtext->io_tag = 0;
	xtext->add_io_tag = 0;
	xtext->scroll_tag = 0;
	xtext->max_lines = 0;
	xtext->col_back = XTEXT_BG;
	xtext->col_fore = XTEXT_FG;
	xtext->nc = 0;
	xtext->pixel_offset = 0;
	xtext->underline = FALSE;
	xtext->strikethrough = FALSE;
	xtext->hidden = FALSE;
	xtext->font = NULL;
	xtext->layout = NULL;
	xtext->urlcheck_function = NULL;
	xtext->color_paste = FALSE;
	xtext->dont_render = FALSE;
	xtext->backing = NULL;
	xtext->cr = NULL;
	xtext->backing_scale = 1;

	/* GTK2 handed us an adjustment through set_scroll_adjustments; GTK3 dropped
	   that, and nothing ever passed one in anyway. Own it from the start. */
	xtext->adj = gtk_adjustment_new (0, 0, 1, 1, 1, 1);
	g_object_ref_sink (xtext->adj);
	xtext->vc_signal_tag = g_signal_connect (xtext->adj, "value-changed",
							G_CALLBACK (gtk_xtext_adjustment_changed), xtext);

	gtk_widget_set_overflow (GTK_WIDGET (xtext), GTK_OVERFLOW_HIDDEN);

	nx_on_click (GTK_WIDGET (xtext), gtk_xtext_button_press, NULL);
	nx_on_release (GTK_WIDGET (xtext), gtk_xtext_button_release, NULL);
	nx_on_motion (GTK_WIDGET (xtext), gtk_xtext_motion_notify, NULL);
	nx_on_smooth_scroll (GTK_WIDGET (xtext), gtk_xtext_smooth_scroll, gtk_xtext_decelerate, NULL);
	nx_on_crossing (GTK_WIDGET (xtext), gtk_xtext_crossing, NULL);
	/* moved to a display with another scale (Retina <-> not): the backing is
	   remade at the new scale on the next frame, so ask for one */
	g_signal_connect (xtext, "notify::scale-factor", G_CALLBACK (xtext_scale_changed), NULL);
	/* the I-beam is the widget's own cursor from the start: GTK shows it as
	   soon as the pointer is over the text, before any motion handler runs */
	xtext_set_cursor (xtext, "text");

	/* separator bar colours, these never change (16-bit values from GTK2) */
	{
		static const GdkRGBA light = { 0xffff / 65535.0, 0xffff / 65535.0, 0xffff / 65535.0, 1 };
		static const GdkRGBA dark  = { 0x1111 / 65535.0, 0x1111 / 65535.0, 0x1111 / 65535.0, 1 };
		static const GdkRGBA thin  = { 0x8e38 / 65535.0, 0x8e38 / 65535.0, 0x9f38 / 65535.0, 1 };
		xtext->light_gc.fg = light;
		xtext->dark_gc.fg = dark;
		xtext->thin_gc.fg = thin;
	}
}


/* GTK2 code poked adj->value / adj->upper directly, which never emitted
   value-changed. GTK3 hides the fields; do the same thing through the API
   with our own handler blocked. GTK3 clamps value to [0, upper-page]. */
#define NX_ADJ_UPPER(upper, page) MAX ((upper), (page))

static void
xtext_adj_set_silent (GtkXText *xtext, gdouble value, gdouble upper)
{
	GtkAdjustment *adj = xtext->adj;
	gdouble page = gtk_adjustment_get_page_size (adj);

	if (xtext->vc_signal_tag)
		g_signal_handler_block (adj, xtext->vc_signal_tag);
	gtk_adjustment_configure (adj, value, 0, NX_ADJ_UPPER (upper, page), 1,
									  gtk_adjustment_get_page_increment (adj), page);
	if (xtext->vc_signal_tag)
		g_signal_handler_unblock (adj, xtext->vc_signal_tag);
}

static void
gtk_xtext_adjustment_set (xtext_buffer *buf, int fire_signal)
{
	GtkAdjustment *adj = buf->xtext->adj;

	if (buf->xtext->buffer == buf)
	{
		gdouble upper, page_size, value;

		upper = buf->num_lines;
		if (upper == 0)
			upper = 1;

		page_size = nx_widget_get_allocated_height (GTK_WIDGET (buf->xtext)) /
						 buf->xtext->fontsize;

		value = gtk_adjustment_get_value (adj);
		if (value > upper - page_size)
		{
			buf->scrollbar_down = TRUE;
			value = upper - page_size;
		}

		if (value < 0)
			value = 0;

		/* GTK2 wrote the fields directly and only announced it when asked to;
		   configure() always notifies, so hold back value-changed unless
		   fire_signal says the old code would have told anyone. */
		if (!fire_signal)
			g_signal_handler_block (adj, buf->xtext->vc_signal_tag);
		/* fewer lines than fit: GTK3 takes upper < page_size, GTK4 asserts
		   (and draws a full thumb either way) */
		gtk_adjustment_configure (adj, value, 0, NX_ADJ_UPPER (upper, page_size), 1, page_size, page_size);
		if (!fire_signal)
			g_signal_handler_unblock (adj, buf->xtext->vc_signal_tag);
	}
}

static gint
gtk_xtext_adjustment_timeout (GtkXText * xtext)
{
	gtk_xtext_render_page (xtext);
	xtext->io_tag = 0;
	return 0;
}

static void
gtk_xtext_adjustment_changed (GtkAdjustment * adj, GtkXText * xtext)
{
	if (!gtk_widget_get_realized (GTK_WIDGET (xtext)))
		return;

	if (xtext->buffer->old_value != gtk_adjustment_get_value (xtext->adj))
	{
		if (gtk_adjustment_get_value (xtext->adj) >= gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj))
		{
			if (!xtext->buffer->scrollbar_down || xtext->jump_unread)
				xtext_queue_draw (xtext);
			xtext->buffer->scrollbar_down = TRUE;
			xtext->jump_unread = 0;
		}
		else
		{
			if (xtext->buffer->scrollbar_down)
				xtext_queue_draw (xtext);
			xtext->buffer->scrollbar_down = FALSE;
		}

		/* drawn on the next frame, whatever moved it (a drag of the bar, a
		   trackpad, a wheel): GTK4 coalesces them to one per frame anyway.
		   (GTK2 delayed all but single-line arrow steps behind a 30 ms timer
		   to save expensive redraws; that made trackpad scrolling lag) */
		if (xtext->io_tag)
		{
			g_source_remove (xtext->io_tag);
			xtext->io_tag = 0;
		}
		gtk_xtext_render_page (xtext);
	}
	xtext->buffer->old_value = gtk_adjustment_get_value (adj);
}

GtkWidget *
gtk_xtext_new (GdkRGBA palette[], int separator)
{
	GtkXText *xtext;

	xtext = g_object_new (gtk_xtext_get_type (), NULL);
	xtext->separator = separator;
	xtext->wordwrap = TRUE;
	xtext->buffer = gtk_xtext_buffer_new (xtext);
	xtext->orig_buffer = xtext->buffer;

	gtk_xtext_set_palette (xtext, palette);

	return GTK_WIDGET (xtext);
}

/* GTK4 has no ::destroy vfunc; dispose runs at the same point (and may
   run twice, everything below is NULLed after freeing) */
static void
gtk_xtext_destroy (GObject * object)
{
	GtkXText *xtext = GTK_XTEXT (object);

	if (xtext->add_io_tag)
	{
		g_source_remove (xtext->add_io_tag);
		xtext->add_io_tag = 0;
	}

	if (xtext->scroll_tag)
	{
		g_source_remove (xtext->scroll_tag);
		xtext->scroll_tag = 0;
	}

	if (xtext->io_tag)
	{
		g_source_remove (xtext->io_tag);
		xtext->io_tag = 0;
	}

	if (xtext->toast_tag)
	{
		g_source_remove (xtext->toast_tag);
		xtext->toast_tag = 0;
	}
	g_clear_pointer (&xtext->toast, g_free);

	if (xtext->primary_tag)
	{
		g_signal_handler_disconnect (gtk_widget_get_primary_clipboard (GTK_WIDGET (xtext)),
											  xtext->primary_tag);
		xtext->primary_tag = 0;
	}
	g_clear_object (&xtext->primary_content);

	if (xtext->pixmap)
	{
		cairo_pattern_destroy (xtext->pixmap);
		xtext->pixmap = NULL;
	}

	if (xtext->font)
	{
		backend_font_close (xtext);
		xtext->font = NULL;
	}

	if (xtext->adj)
	{
		g_signal_handlers_disconnect_matched (G_OBJECT (xtext->adj),
					G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, xtext);
		g_object_unref (G_OBJECT (xtext->adj));
		xtext->adj = NULL;
		xtext->vc_signal_tag = 0;
	}

	xtext_backing_free (xtext);

	if (xtext->orig_buffer)
	{
		gtk_xtext_buffer_free (xtext->orig_buffer);
		xtext->orig_buffer = NULL;
	}

	/* a right-click menu still open (or not freed yet) hangs off us */
	{
		GtkWidget *c;
		while ((c = gtk_widget_get_first_child (GTK_WIDGET (xtext))))
			gtk_widget_unparent (c);
	}
	if (G_OBJECT_CLASS (parent_class)->dispose)
		(*G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
gtk_xtext_unrealize (GtkWidget * widget)
{
	backend_deinit (GTK_XTEXT (widget));
	xtext_backing_free (GTK_XTEXT (widget));
	/* the cursor is the widget's (GTK4), it stays across unrealize */

	/* GTK3: GtkWidget's unrealize unregisters and destroys our GdkWindow */
	if (GTK_WIDGET_CLASS (parent_class)->unrealize)
		(* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

static void
gtk_xtext_realize (GtkWidget * widget)
{
	GtkXText *xtext;

	xtext = GTK_XTEXT (widget);

	/* no window of our own: drawing goes through snapshot */
	GTK_WIDGET_CLASS (parent_class)->realize (widget);

	/* for the marker bar (marker) */
	xtext->marker_gc.fg = xtext->palette[XTEXT_MARKER];

	xtext_set_fg (xtext, &xtext->fgc, XTEXT_FG);
	xtext_set_bg (xtext, &xtext->fgc, XTEXT_BG);
	xtext_set_fg (xtext, &xtext->bgc, XTEXT_BG);

	xtext_backing_update (xtext);

	backend_init (xtext);
}

static void
gtk_xtext_measure (GtkWidget *widget, GtkOrientation orientation, int for_size,
						 int *minimum, int *natural, int *min_baseline, int *nat_baseline)
{
	/* the old preferred sizes */
	*minimum = *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? 200 : 90;
}

static void
gtk_xtext_resized (GtkXText *xtext, int width, int height);

static void
gtk_xtext_size_allocate (GtkWidget * widget, int width, int height, int baseline)
{
	gtk_xtext_resized (GTK_XTEXT (widget), width, height);
}

/* the widget got a new size: relayout the text for it */
static void
gtk_xtext_resized (GtkXText *xtext, int width, int height)
{
	int height_only = FALSE;

	if (width == xtext->buffer->window_width)
		height_only = TRUE;

	if (gtk_widget_get_realized (GTK_WIDGET (xtext)))
	{
		xtext->buffer->window_width = width;
		xtext->buffer->window_height = height;

		xtext_backing_update (xtext);
		dontscroll (xtext->buffer);	/* force scrolling off */
		if (!height_only)
			gtk_xtext_calc_lines (xtext->buffer, FALSE);
		else
		{
			xtext->buffer->pagetop_ent = NULL;
			gtk_xtext_adjustment_set (xtext->buffer, FALSE);
		}
		if (xtext->buffer->scrollbar_down)
			gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj) -
											  gtk_adjustment_get_page_size (xtext->adj));
	}
}

static int
gtk_xtext_selection_clear (xtext_buffer *buf)
{
	textentry *ent;
	int ret = 0;

	/* every entry, not just last_ent_start..last_ent_end: if that range
	   was ever out of date (a selection grown while the page scrolled, the
	   ends recorded the other way round) the marks outside it stayed lit
	   for good. One int test per line is nothing next to a redraw. */
	for (ent = buf->text_first; ent; ent = ent->next)
	{
		if (ent->mark_start != -1)
		{
			ret = 1;
			ent->mark_start = -1;
			ent->mark_end = -1;
		}
	}

	return ret;
}

static int
find_x (GtkXText *xtext, textentry *ent, int x, int subline, int indent)
{
	int xx = indent;
	int suboff;
	GSList *list;
	GSList *hid = NULL;
	offlen_t *meta;
	int off, len, wid, mbl, mbw;

	/* Skip to the first chunk of stuff for the subline */
	if (subline > 0)
	{
		suboff = GPOINTER_TO_INT (g_slist_nth_data (ent->sublines, subline - 1));
		for (list = ent->slp; list; list = g_slist_next (list))
		{
			meta = list->data;
			if (meta->off + meta->len > suboff)
				break;
		}
	}
	else
	{
		suboff = 0;
		list = ent->slp;
	} 
	/* Step to the first character of the subline */
	if (list == NULL)
		return 0;
	meta = list->data;
	off = meta->off;
	len = meta->len;
	if (meta->emph & EMPH_HIDDEN)
		hid = list;
	while (len > 0)
	{
		if (off >= suboff)
			break;
		mbl = xtext_cluster_len (ent->str + off, ent->str + meta->off + meta->len);
		if (mbl < 1) mbl = 1;
		len -= mbl;
		off += mbl;
	}
	if (len < 0)
		return ent->str_len;		/* Bad char -- return max offset. */

	/* Step through characters to find the one at the x position */
	wid = x - indent;
	len = meta->len - (off - meta->off);
	while (wid > 0)
	{
		/* a whole cluster: a click inside an emoji with a skin tone lands
		   before or after it, never between its parts */
		mbl = xtext_cluster_len (ent->str + off, ent->str + off + MAX (len, 1));
		if (mbl < 1) mbl = 1;
		mbw = backend_get_text_width_emph (xtext, ent->str + off, mbl, meta->emph);
		wid -= mbw;
		xx += mbw;
		if (xx >= x)
			return off;
		len -= mbl;
		off += mbl;
		if (len <= 0)
		{
			if (meta->emph & EMPH_HIDDEN)
				hid = list;
			list = g_slist_next (list);
			if (list == NULL)
				return ent->str_len;
			meta = list->data;
			off = meta->off;
			len = meta->len;
		}
	}

	/* If previous chunk exists and is marked hidden, regard it as unhidden */
	if (hid && list && hid->next == list)
	{
		meta = hid->data;
		off = meta->off;
	}

	/* Return offset of character at x within subline */
	return off;
}

static int
gtk_xtext_find_x (GtkXText * xtext, int x, textentry * ent, int subline,
						int line, int *out_of_bounds)
{
	int indent;
	unsigned char *str;

	if (subline < 1)
		indent = ent->indent;
	else
		indent = xtext->buffer->indent;

	if (line > gtk_adjustment_get_page_size (xtext->adj) || line < 0)
	{
		*out_of_bounds = TRUE;
		return 0;
	}

	str = ent->str + gtk_xtext_find_subline (xtext, ent, subline);
	if (str >= ent->str + ent->str_len)
		return 0;

	/* Let user select left a few pixels to grab hidden text e.g. '<' */
	if (x < indent - xtext->space_width)
	{
		*out_of_bounds = 1;
		return (str - ent->str);
	}

	*out_of_bounds = 0;

	return find_x (xtext, ent, x, subline, indent);
}

static textentry *
gtk_xtext_find_char (GtkXText * xtext, int x, int y, int *off, int *out_of_bounds)
{
	textentry *ent;
	int line;
	int subline;
	int outofbounds = FALSE;

	/* Adjust y value for negative rounding, double to int */
	if (y < 0)
		y -= xtext->fontsize;

	line = (y + xtext->pixel_offset) / xtext->fontsize;
	ent = gtk_xtext_nth (xtext, line + (int)gtk_adjustment_get_value (xtext->adj), &subline);
	if (!ent)
		return NULL;

	if (off)
		*off = gtk_xtext_find_x (xtext, x, ent, subline, line, &outofbounds);
	if (out_of_bounds)
		*out_of_bounds = outofbounds;

	return ent;
}

static void
gtk_xtext_draw_sep (GtkXText * xtext, int y)
{
	int x, height;
	const GdkRGBA *light, *dark;

	if (y == -1)
	{
		y = 0;
		height = nx_widget_get_allocated_height (GTK_WIDGET (xtext));
	} else
	{
		height = xtext->fontsize;
	}

	/* draw the separator line */
	if (xtext->separator && xtext->buffer->indent)
	{
		light = &xtext->light_gc.fg;
		dark = &xtext->dark_gc.fg;

		x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (x < 1)
			return;

		if (xtext->thinline)
		{
			if (xtext->moving_separator)
				xtext_draw_line (xtext, light, x, y, x, y + height);
			else
				xtext_draw_line (xtext, &xtext->thin_gc.fg, x, y, x, y + height);
		} else
		{
			if (xtext->moving_separator)
			{
				xtext_draw_line (xtext, light, x - 1, y, x - 1, y + height);
				xtext_draw_line (xtext, dark, x, y, x, y + height);
			} else
			{
				xtext_draw_line (xtext, dark, x - 1, y, x - 1, y + height);
				xtext_draw_line (xtext, light, x, y, x, y + height);
			}
		}
	}
}

static void
gtk_xtext_draw_marker (GtkXText * xtext, textentry * ent, int y)
{
	int x, width, render_y;

	if (!xtext->marker) return;

	if (xtext->buffer->marker_pos == ent)
	{
		render_y = y + xtext->font->descent;
	}
	else if (xtext->buffer->marker_pos == ent->next && ent->next != NULL)
	{
		render_y = y + xtext->font->descent + xtext->fontsize * g_slist_length (ent->sublines);
	}
	else return;

	x = 0;
	width = nx_widget_get_allocated_width (GTK_WIDGET (xtext));

	xtext_draw_line (xtext, &xtext->marker_gc.fg, x, render_y, x + width, render_y);

	if (nx_widget_window_is_active (GTK_WIDGET (xtext)))
	{
		xtext->buffer->marker_seen = TRUE;
	}
}

/* the "jump to latest" pill: shown while scrolled up, bottom right, with
   the number of lines that came in since; click it to go to the bottom */
static gboolean
xtext_jump_visible (GtkXText *xtext)
{
	return xtext->jump_button && xtext->buffer && !xtext->buffer->scrollbar_down &&
			 gtk_adjustment_get_upper (xtext->adj) > gtk_adjustment_get_page_size (xtext->adj);
}

/* in the chat's own font, like everything else drawn here */
static PangoLayout *
xtext_overlay_layout (GtkXText *xtext, const char *text)
{
	PangoLayout *layout = gtk_widget_create_pango_layout (GTK_WIDGET (xtext), text);

	if (xtext->font)
		pango_layout_set_font_description (layout, xtext->font->font);
	return layout;
}

/* a plain box like the rest of the classic UI: chat colours, square
   corners, a 1px border, no shadow */
static void
xtext_draw_box (GtkXText *xtext, cairo_t *cr, GdkRectangle *r, gboolean hot)
{
	GdkRGBA bg = xtext->palette[hot ? XTEXT_MARK_BG : XTEXT_BG];
	GdkRGBA border = xtext->palette[XTEXT_FG];

	border.alpha = 0.5;
	cairo_save (cr);
	gdk_cairo_set_source_rgba (cr, &bg);
	cairo_rectangle (cr, r->x, r->y, r->width, r->height);
	cairo_fill (cr);
	gdk_cairo_set_source_rgba (cr, &border);
	cairo_set_line_width (cr, 1);
	cairo_rectangle (cr, r->x + 0.5, r->y + 0.5, r->width - 1, r->height - 1);
	cairo_stroke (cr);
	cairo_restore (cr);
}

static PangoLayout *
xtext_jump_layout (GtkXText *xtext)
{
	PangoLayout *layout;
	char *s;

	/* U+2193 down arrow */
	if (xtext->jump_unread > 0)
		s = g_strdup_printf ("\342\206\223 %d %s", xtext->jump_unread,
									xtext->jump_unread == 1 ? _("new line") : _("new lines"));
	else
		s = g_strdup_printf ("\342\206\223 %s", _("Jump to latest"));
	layout = xtext_overlay_layout (xtext, s);
	g_free (s);
	return layout;
}

static void
xtext_jump_rect (GtkXText *xtext, PangoLayout *layout, GdkRectangle *r)
{
	int tw, th;

	pango_layout_get_pixel_size (layout, &tw, &th);
	r->width = tw + 2 * 6;
	r->height = th + 2 * 2;
	r->x = nx_widget_get_allocated_width (GTK_WIDGET (xtext)) - r->width - 4;
	r->y = nx_widget_get_allocated_height (GTK_WIDGET (xtext)) - r->height - 4;
}

static void
xtext_draw_jump (GtkXText *xtext, cairo_t *cr)
{
	PangoLayout *layout;
	GdkRectangle r;

	if (!xtext_jump_visible (xtext))
		return;
	layout = xtext_jump_layout (xtext);
	xtext_jump_rect (xtext, layout, &r);
	xtext_draw_box (xtext, cr, &r, xtext->jump_hover);
	cairo_save (cr);
	gdk_cairo_set_source_rgba (cr, &xtext->palette[xtext->jump_hover ? XTEXT_MARK_FG : XTEXT_FG]);
	cairo_move_to (cr, r.x + 6, r.y + 2);
	pango_cairo_show_layout (cr, layout);
	cairo_restore (cr);
	g_object_unref (layout);
}

static gboolean
xtext_jump_hit (GtkXText *xtext, int x, int y)
{
	PangoLayout *layout;
	GdkRectangle r;

	if (!xtext_jump_visible (xtext))
		return FALSE;
	layout = xtext_jump_layout (xtext);
	xtext_jump_rect (xtext, layout, &r);
	g_object_unref (layout);
	return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

/* the line just appended shows no time in front (day separators) */
void
gtk_xtext_last_no_stamp (xtext_buffer *buf)
{
	if (buf->text_last)
		buf->text_last->no_stamp = TRUE;
}

/* scroll to the newest line */
void
gtk_xtext_scroll_to_end (GtkXText *xtext)
{
	GtkAdjustment *adj = xtext->adj;

	gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj));
	xtext->jump_unread = 0;
	xtext_queue_draw (xtext);
}

void
gtk_xtext_set_jump_button (GtkXText *xtext, gboolean show)
{
	xtext->jump_button = show;
	xtext_queue_draw (xtext);
}

/* a short message ("Zoom 125%") centred near the top, then gone */
static gboolean
xtext_toast_timeout (gpointer data)
{
	GtkXText *xtext = data;

	xtext->toast_tag = 0;
	g_clear_pointer (&xtext->toast, g_free);
	xtext_queue_draw (xtext);
	return G_SOURCE_REMOVE;
}

void
gtk_xtext_show_toast (GtkXText *xtext, const char *text, int ms)
{
	g_free (xtext->toast);
	xtext->toast = g_strdup (text);
	if (xtext->toast_tag)
		g_source_remove (xtext->toast_tag);
	xtext->toast_tag = g_timeout_add (ms, xtext_toast_timeout, xtext);
	xtext_queue_draw (xtext);
}

static void
xtext_draw_toast (GtkXText *xtext, cairo_t *cr)
{
	PangoLayout *layout;
	GdkRectangle r;
	int tw, th;

	if (!xtext->toast)
		return;
	layout = xtext_overlay_layout (xtext, xtext->toast);
	pango_layout_get_pixel_size (layout, &tw, &th);
	/* top right, same box as the jump button */
	r.width = tw + 2 * 6;
	r.height = th + 2 * 2;
	r.x = nx_widget_get_allocated_width (GTK_WIDGET (xtext)) - r.width - 4;
	r.y = 4;
	xtext_draw_box (xtext, cr, &r, FALSE);
	cairo_save (cr);
	gdk_cairo_set_source_rgba (cr, &xtext->palette[XTEXT_FG]);
	cairo_move_to (cr, r.x + 6, r.y + 2);
	pango_cairo_show_layout (cr, layout);
	cairo_restore (cr);
	g_object_unref (layout);
}

/* tests: what's in the backing now (drawn bit by bit as things changed)
   against the same page rendered from scratch; the number of pixels that
   differ. Anything but 0 is a row left half-drawn. */
int
gtk_xtext_backing_diff (GtkXText *xtext, const char *png_now, const char *png_full)
{
	cairo_surface_t *now;
	unsigned char *a, *b;
	int w, h, stride, x, y, diff = 0;
	GdkRectangle all;

	if (!xtext->backing)
		return -1;
	cairo_surface_flush (xtext->backing);
	w = cairo_image_surface_get_width (xtext->backing);
	h = cairo_image_surface_get_height (xtext->backing);
	now = cairo_image_surface_create (CAIRO_FORMAT_RGB24, w, h);
	{
		cairo_t *c = cairo_create (now);
		cairo_surface_set_device_scale (xtext->backing, 1, 1);
		cairo_set_source_surface (c, xtext->backing, 0, 0);
		cairo_paint (c);
		cairo_destroy (c);
		cairo_surface_set_device_scale (xtext->backing, xtext->backing_scale, xtext->backing_scale);
	}
	if (png_now)
		cairo_surface_write_to_png (now, png_now);

	all.x = all.y = 0;
	all.width = nx_widget_get_allocated_width (GTK_WIDGET (xtext));
	all.height = nx_widget_get_allocated_height (GTK_WIDGET (xtext));
	/* "now" is what the last frame drew; the reference is the same state
	   drawn again onto a cleared backing. Equal unless drawing depends on
	   anything but the current state. */
	{
		cairo_t *c = cairo_create (xtext->backing);
		cairo_set_source_rgb (c, 1, 0, 1);
		cairo_paint (c);
		cairo_destroy (c);
	}
	gtk_xtext_render_page_now (xtext);
	(void) all;
	cairo_surface_flush (xtext->backing);
	if (png_full)
		cairo_surface_write_to_png (xtext->backing, png_full);

	a = cairo_image_surface_get_data (now);
	b = cairo_image_surface_get_data (xtext->backing);
	stride = cairo_image_surface_get_stride (now);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			if ((((guint32 *) (a + y * stride))[x] & 0xffffff) != (((guint32 *) (b + y * stride))[x] & 0xffffff))
				diff++;
	cairo_surface_destroy (now);
	return diff;
}

/* tests: "press X Y", "move X Y", "release X Y", "dpress X Y" (double),
   "hover X Y" in order, one per call, as GTK delivers them */
void
gtk_xtext_test_event (GtkXText *xtext, const char *what, double x, double y)
{
	GtkWidget *w = GTK_WIDGET (xtext);

	if (!strcmp (what, "press"))
		gtk_xtext_button_press (w, 1, 1, x, y, 0, NULL);
	else if (!strcmp (what, "dpress"))
		gtk_xtext_button_press (w, 1, 2, x, y, 0, NULL);
	else if (!strcmp (what, "move"))
		gtk_xtext_motion_notify (w, x, y, GDK_BUTTON1_MASK, NULL);
	else if (!strcmp (what, "hover"))
		gtk_xtext_motion_notify (w, x, y, 0, NULL);
	else if (!strcmp (what, "release"))
		gtk_xtext_button_release (w, 1, 1, x, y, 0, NULL);
	else if (!strcmp (what, "leave"))
		gtk_xtext_crossing (w, FALSE, NULL);
	else if (!strcmp (what, "enter"))
		gtk_xtext_crossing (w, TRUE, NULL);
	/* "pixscroll 0 DY": a trackpad's pixels; "wheel 0 STEPS"; "flick 0 VY" */
	else if (!strcmp (what, "pixscroll"))
		gtk_xtext_smooth_scroll (w, x, y, GDK_SCROLL_UNIT_SURFACE, 0, NULL);
	else if (!strcmp (what, "wheel"))
		gtk_xtext_smooth_scroll (w, x, y, GDK_SCROLL_UNIT_WHEEL, 0, NULL);
	else if (!strcmp (what, "flick"))
		gtk_xtext_decelerate (w, x, y, NULL);
}

double
gtk_xtext_scroll_value (GtkXText *xtext)
{
	return gtk_adjustment_get_value (xtext->adj);
}

char *
gtk_xtext_selection_text (GtkXText *xtext, int *len)
{
	return gtk_xtext_selection_get_text (xtext, len);
}

gboolean
gtk_xtext_redraw_pending (GtkXText *xtext)
{
	return xtext->redraw_pending && gtk_widget_get_mapped (GTK_WIDGET (xtext));
}

/* tests: entries that carry a mark but lie outside last_ent_start..
   last_ent_end (shown highlighted, never copied: a stale selection) */
int
gtk_xtext_stray_marks (GtkXText *xtext)
{
	xtext_buffer *buf = xtext->buffer;
	textentry *e;
	gboolean inside = FALSE;
	int stray = 0;

	for (e = buf->text_first; e; e = e->next)
	{
		if (e == buf->last_ent_start)
			inside = TRUE;
		if (!inside && e->mark_start != -1)
			stray++;
		if (e == buf->last_ent_end)
			inside = FALSE;
	}
	return stray;
}

/* GTK4: draw from the current state, every frame, in ::snapshot. State
   changes (a line appended, a scroll, a selection, a link under the
   pointer, a resize) only call gtk_widget_queue_draw(); the whole page is
   rendered here into the backing image and put on screen. GTK2's way, kept
   until now, was to paint the change straight away into a persistent
   image and patch it up piece by piece (shifted for scrolls, a few rows
   or characters redrawn), which is where half-drawn rows came from when
   any one of those patches was off. A page is a few dozen rows: Pango
   draws that well within a frame. */
static gboolean
gtk_xtext_draw (GtkWidget * widget, cairo_t * cr)
{
	GtkXText *xtext = GTK_XTEXT (widget);

	xtext_backing_update (xtext);
	if (!xtext->backing)
		return FALSE;

	xtext->in_draw = TRUE;
	xtext->redraw_pending = FALSE;
	gtk_xtext_render_page_now (xtext);
	xtext->in_draw = FALSE;

	cairo_surface_flush (xtext->backing);
	cairo_save (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface (cr, xtext->backing, 0, 0);
	cairo_paint (cr);
	cairo_restore (cr);
	xtext_draw_jump (xtext, cr);
	xtext_draw_toast (xtext, cr);
	return FALSE;
}

static void
gtk_xtext_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
	graphene_rect_t r;
	cairo_t *cr;

	graphene_rect_init (&r, 0, 0, gtk_widget_get_width (widget), gtk_widget_get_height (widget));
	cr = gtk_snapshot_append_cairo (snapshot, &r);
	gtk_xtext_draw (widget, cr);
	cairo_destroy (cr);
}

/* how rows look changed without anything moving (the selection, a link
   under the pointer, the separator): draw again on the next frame */
static void
xtext_render_visible (GtkXText *xtext)
{
	xtext_queue_draw (xtext);
}

/* the selection changed from last_ent_start..last_ent_end to start..end:
   repaint what's on screen, every row whole. GTK2 redrew only the
   characters that changed (jump_in/jump_out offsets, no background fills)
   straight onto the window; with proportional fonts, colour runs and
   HiDPI that left slivers of old glyphs. Redrawing a span of entries
   instead missed the part of a selection scrolled above the page (the
   range walk starts at the page top), leaving rows highlighted that
   weren't selected any more. The visible page is a few dozen rows into
   our own backing: cheap, and always right. */
static void
gtk_xtext_selection_render (GtkXText *xtext, textentry *start_ent, textentry *end_ent)
{
	xtext_buffer *buf = xtext->buffer;

	buf->last_ent_start = start_ent;
	buf->last_ent_end = end_ent;
	buf->last_offset_start = start_ent->mark_start;
	buf->last_offset_end = end_ent->mark_end;
	xtext_render_visible (xtext);
}

static void
gtk_xtext_selection_draw (GtkXText * xtext, gboolean render)
{
	textentry *ent;
	textentry *ent_end;
	textentry *ent_start;
	int offset_start = 0;
	int offset_end = 0;
	textentry *low_ent, *high_ent;
	int low_x, low_y, low_offs;
	int high_x, high_y, high_offs, high_len;

	if (xtext->buffer->text_first == NULL)
		return;

	ent_start = gtk_xtext_find_char (xtext, xtext->select_start_x, xtext->select_start_y, &offset_start, NULL);
	ent_end = gtk_xtext_find_char (xtext, xtext->select_end_x, xtext->select_end_y, &offset_end, NULL);
	if (ent_start == NULL && ent_end == NULL)
		return;

	if	((ent_start != ent_end && xtext->select_start_y > xtext->select_end_y) || /* different entries */
		(ent_start == ent_end && offset_start > offset_end))	/* same entry, different character offsets */
	{
		/* marking up */
		low_ent = ent_end;
		low_x = xtext->select_end_x;
		low_y = xtext->select_end_y;
		low_offs = offset_end;
		high_ent = ent_start;
		high_x = xtext->select_start_x;
		high_y = xtext->select_start_y;
		high_offs = offset_start;
	}
	else
	{
		/* marking down */
		low_ent = ent_start;
		low_x = xtext->select_start_x;
		low_y = xtext->select_start_y;
		low_offs = offset_start;
		high_ent = ent_end;
		high_x = xtext->select_end_x;
		high_y = xtext->select_end_y;
		high_offs = offset_end;
	}
	if (low_ent == NULL)
	{
		low_ent = xtext->buffer->text_first;
		low_offs = 0;
	}
	if (high_ent == NULL)
	{
		high_ent = xtext->buffer->text_last;
		high_offs = high_ent->str_len;
	}

	/* word selection */
	if (xtext->word_select)
	{
		/* a word selection cannot be started if the cursor is out of bounds in gtk_xtext_button_press */
		gtk_xtext_get_word (xtext, low_x, low_y, NULL, &low_offs, NULL, NULL);

		/* in case the cursor is out of bounds we keep offset_end from gtk_xtext_find_char and fix the length */
		if (gtk_xtext_get_word (xtext, high_x, high_y, NULL, &high_offs, &high_len, NULL) == NULL)
			high_len = high_offs == high_ent->str_len? 0: -1; /* -1 for the space, 0 if at the end */
		high_offs += high_len;
		if (low_y < 0)
			low_offs = xtext->buffer->last_offset_start;
		if (high_y > xtext->buffer->window_height)
			high_offs = xtext->buffer->last_offset_end;
	}
	/* line/ent selection */
	else if (xtext->line_select)
	{
		low_offs = 0;
		high_offs = high_ent->str_len;
	}
	/* character selection */
	else
	{
		if (low_y < 0)
			low_offs = xtext->buffer->last_offset_start;
		if (high_y > xtext->buffer->window_height)
			high_offs = xtext->buffer->last_offset_end;
	}

	/* set all the old mark_ fields to -1 */
	gtk_xtext_selection_clear (xtext->buffer);

	low_ent->mark_start = low_offs;
	low_ent->mark_end = high_offs;

	if (low_ent != high_ent)
	{
		low_ent->mark_end = low_ent->str_len;
		if (high_offs != 0)
		{
			high_ent->mark_start = 0;
			high_ent->mark_end = high_offs;
		}

		/* set all the mark_ fields of the ents within the selection */
		ent = low_ent->next;
		while (ent && ent != high_ent)
		{
			ent->mark_start = 0;
			ent->mark_end = ent->str_len;
			ent = ent->next;
		}
	}

	if (render)
		gtk_xtext_selection_render (xtext, low_ent, high_ent);
	else
	{
		/* drawn by the caller (a full redraw follows), but the range must
		   be known now: the next clear only unmarks inside it, and marks
		   left outside it stay highlighted and never go away */
		xtext->buffer->last_ent_start = low_ent;
		xtext->buffer->last_ent_end = high_ent;
		xtext->buffer->last_offset_start = low_ent->mark_start;
		xtext->buffer->last_offset_end = high_ent->mark_end;
	}
}

static int
gtk_xtext_timeout_ms (GtkXText *xtext, int pixes)
{
	int apixes = abs(pixes);

	if (apixes < 6) return 100;
	if (apixes < 12) return 50;
	if (apixes < 20) return 20;
	return 10;
}
static gint
gtk_xtext_scrolldown_timeout (GtkXText * xtext)
{
	int p_y, win_height;
	xtext_buffer *buf = xtext->buffer;
	GtkAdjustment *adj = xtext->adj;

	p_y = xtext->pointer_y;	/* motion keeps it current */
	win_height = nx_widget_get_height (GTK_WIDGET (xtext));

	if (buf->last_ent_end == NULL ||	/* If context has changed OR */
		 buf->pagetop_ent == NULL ||	/* pagetop_ent is reset OR */
		 p_y <= win_height ||			/* pointer not below bottom margin OR */
		 gtk_adjustment_get_value (adj) >= gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj)) 	/* we're scrolled to bottom */
	{
		xtext->scroll_tag = 0;
		return 0;
	}

	xtext->select_start_y -= xtext->fontsize;
	xtext->select_start_adj++;
	gtk_adjustment_set_value (adj, gtk_adjustment_get_value (adj) + 1);
	/* draws the scrolled page with the new selection */
	gtk_xtext_selection_draw (xtext, TRUE);
	xtext->scroll_tag = g_timeout_add (gtk_xtext_timeout_ms (xtext, p_y - win_height),
													(GSourceFunc)
													gtk_xtext_scrolldown_timeout,
													xtext);

	return 0;
}

static gint
gtk_xtext_scrollup_timeout (GtkXText * xtext)
{
	int p_y;
	xtext_buffer *buf = xtext->buffer;
	GtkAdjustment *adj = xtext->adj;
	int delta_y;

	p_y = xtext->pointer_y;	/* motion keeps it current */

	if (buf->last_ent_start == NULL ||	/* If context has changed OR */
		 buf->pagetop_ent == NULL ||		/* pagetop_ent is reset OR */
		 p_y >= 0 ||							/* not above top margin OR */
		 gtk_adjustment_get_value (adj) == 0)						/* we're scrolled to the top */
	{
		xtext->scroll_tag = 0;
		return 0;
	}

	{
		gdouble value = gtk_adjustment_get_value (adj);
		if (value < 0)
		{
			delta_y = value * xtext->fontsize;
			value = 0;
		} else {
			delta_y = xtext->fontsize;
			value--;
		}
		xtext->select_start_y += delta_y;
		xtext->select_start_adj = value;
		gtk_adjustment_set_value (adj, value);
	}
	gtk_xtext_selection_draw (xtext, TRUE);
	xtext->scroll_tag = g_timeout_add (gtk_xtext_timeout_ms (xtext, p_y),
													(GSourceFunc)
													gtk_xtext_scrollup_timeout,
													xtext);

	return 0;
}

static void
gtk_xtext_selection_update (GtkXText * xtext, int p_y, gboolean render)
{
	int win_height;
	int moved;

	if (xtext->scroll_tag)
	{
		return;
	}

	win_height = nx_widget_get_height (GTK_WIDGET (xtext));

	/* selecting past top of window, scroll up! */
	if (p_y < 0 && gtk_adjustment_get_value (xtext->adj) >= 0)
	{
		gtk_xtext_scrollup_timeout (xtext);
	}

	/* selecting past bottom of window, scroll down! */
	else if (p_y > win_height &&
		 gtk_adjustment_get_value (xtext->adj) < (gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj)))
	{
		gtk_xtext_scrolldown_timeout (xtext);
	}
	else
	{
		moved = (int)gtk_adjustment_get_value (xtext->adj) - xtext->select_start_adj;
		xtext->select_start_y -= (moved * xtext->fontsize);
		xtext->select_start_adj = gtk_adjustment_get_value (xtext->adj);
		gtk_xtext_selection_draw (xtext, render);
	}
}

static char *
gtk_xtext_get_word (GtkXText * xtext, int x, int y, textentry ** ret_ent,
						  int *ret_off, int *ret_len, GSList **slp)
{
	textentry *ent;
	int offset;
	unsigned char *word;
	unsigned char *last, *end;
	int len;
	int out_of_bounds = 0;
	int len_to_offset = 0;

	ent = gtk_xtext_find_char (xtext, x, y, &offset, &out_of_bounds);
	if (ent == NULL || out_of_bounds || offset < 0 || offset >= ent->str_len)
		return NULL;

	word = ent->str + offset;
	while ((word = g_utf8_find_prev_char (ent->str, word)))
	{
		if (is_del (*word))
		{
			word++;
			len_to_offset--;
			break;
		}
		len_to_offset += charlen (word);
	}
	if (!word)
		word = ent->str;

	/* remove color characters from the length */
	gtk_xtext_strip_color (word, len_to_offset, xtext->scratch_buffer, &len_to_offset, NULL, FALSE);

	last = word;
	end = ent->str + ent->str_len;
	len = 0;
	do
	{
		if (is_del (*last))
			break;
		len += charlen (last);
		last = g_utf8_find_next_char (last, end);
	}
	while (last);

	if (len > 0 && word[len-1]=='.')
		len--;

	if (ret_ent)
		*ret_ent = ent;
	if (ret_off)
		*ret_off = word - ent->str;
	if (ret_len)
		*ret_len = len;		/* Length before stripping */

	word = gtk_xtext_strip_color (word, len, xtext->scratch_buffer, NULL, slp, FALSE);

	/* avoid turning the cursor into a hand for non-url part of the word */
	if (xtext->urlcheck_function && xtext->urlcheck_function (GTK_WIDGET (xtext), word))
	{
		int start, end;
		url_last (&start, &end);

		/* make sure we're not before the start of the match */
		if (len_to_offset < start)
			return NULL;

		/* and not after it */
		if (len_to_offset - start >= end - start)
			return NULL;
	}

	return word;
}

/* pointer shape, set only when it changes */
static void
xtext_set_cursor (GtkXText *xtext, const char *name)
{
	if (xtext->cursor_name == name)
		return;
	xtext->cursor_name = name;
	nx_widget_set_cursor_name (GTK_WIDGET (xtext), name);
}

/* I-beam over text, arrow over empty space */
static void
xtext_set_base_cursor (GtkXText *xtext, int x, int y)
{
	/* the whole text area takes text (you can start a selection anywhere
	   in it, past the end of a line or below the last one), so the I-beam
	   everywhere; GTK2 showed the arrow off the characters, which made the
	   I-beam come and go as the pointer crossed the end of each line */
	(void) x;
	(void) y;
	xtext_set_cursor (xtext, "text");
}

/* within a few px of the separator line (a 1px target is too fiddly) */
static gboolean
xtext_on_separator (GtkXText *xtext, int x)
{
	int line_x;

	if (!xtext->separator || !xtext->buffer->indent)
		return FALSE;
	line_x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
	return abs (x - line_x) <= 3;
}

/* hovering a URL: its full text as the tooltip, so a link that reads like
   one site but goes to another (or is cut off by the window) shows where it
   really goes. Not for nicks and channels. */
static void
gtk_xtext_set_url_tooltip (GtkXText *xtext, int word_type, int x, int y)
{
	char *word, *url = NULL;
	int start, end;

	if (word_type == WORD_URL || word_type == WORD_HOST || word_type == WORD_HOST6)
	{
		word = gtk_xtext_get_word (xtext, x, y, NULL, NULL, NULL, NULL);
		if (word && xtext->urlcheck_function (GTK_WIDGET (xtext), word) > 0 &&
			 url_last (&start, &end) && end > start)
			url = g_strndup (word + start, end - start);
	}
	gtk_widget_set_tooltip_text (GTK_WIDGET (xtext), url);
	g_free (url);
}

/* pointer is off any link / the separator: drop hilight and cursor */
static void
gtk_xtext_unhover (GtkXText *xtext)
{
	/* only when there is one: clearing it on every motion makes GTK tear
	   down and re-query the tooltip popup each time (on macOS a hide/show of
	   its window, flickering the pointer and tripping GTK's surface freeze
	   bug) */
	if (gtk_widget_get_has_tooltip (GTK_WIDGET (xtext)))
		gtk_widget_set_tooltip_text (GTK_WIDGET (xtext), NULL);
	if (xtext->cursor_hand || xtext->cursor_resize)
	{
		gboolean had_link = xtext->hilight_ent != NULL;

		xtext->hilight_start = -1;
		xtext->hilight_end = -1;
		xtext->cursor_hand = FALSE;
		xtext->cursor_resize = FALSE;
		xtext->hilight_ent = NULL;
		/* the link's underline off: one redraw */
		if (had_link)
			xtext_render_visible (xtext);
	}
}

static void
gtk_xtext_crossing (GtkWidget *widget, gboolean entered, gpointer unused)
{
	GtkXText *xtext = GTK_XTEXT (widget);

	/* GTK shows a widget's cursor itself whenever the pointer is over it;
	   leaving just drops the link/separator hover. The cursor is the
	   I-beam again for the next entry (it used to be cleared here and only
	   set back on the next motion, so coming in without moving left the
	   arrow, or the last link's hand) */
	if (!entered && !xtext->moving_separator && !xtext->button_down)
	{
		gtk_xtext_unhover (xtext);
		xtext_set_cursor (xtext, "text");
	}
}

/* check if we should mark time stamps, and if a redraw is needed */

static gboolean
gtk_xtext_check_mark_stamp (GtkXText *xtext, GdkModifierType mask)
{
	gboolean redraw = FALSE;

	if ((mask & STATE_SHIFT || prefs.hex_text_autocopy_stamp)
	    && (!prefs.hex_stamp_text || prefs.hex_text_indent))
	{
		if (!xtext->mark_stamp)
		{
			redraw = TRUE;	/* must redraw all */
			xtext->mark_stamp = TRUE;
		}
	} else
	{
		if (xtext->mark_stamp)
		{
			redraw = TRUE;	/* must redraw all */
			xtext->mark_stamp = FALSE;
		}
	}
	return redraw;
}

static int
gtk_xtext_get_word_adjust (GtkXText *xtext, int x, int y, textentry **word_ent, int *offset, int *len)
{
	GSList *slp = NULL;
	unsigned char *word;
	int word_type = 0;

	word = gtk_xtext_get_word (xtext, x, y, word_ent, offset, len, &slp);
	if (word)
	{
		int laststart, lastend;

		word_type = xtext->urlcheck_function (GTK_WIDGET (xtext), word);
		if (word_type > 0)
		{
			if (url_last (&laststart, &lastend))
			{
				int cumlen, startadj = 0, endadj = 0;
				offlen_t *meta;
				GSList *sl;

				for (sl = slp, cumlen = 0; sl; sl = g_slist_next (sl))
				{
					meta = sl->data;
					startadj = meta->off - cumlen;
					cumlen += meta->len;
					if (laststart < cumlen)
						break;
				}
				for (sl = slp, cumlen = 0; sl; sl = g_slist_next (sl))
				{
					meta = sl->data;
					endadj = meta->off - cumlen;
					cumlen += meta->len;
					if (lastend < cumlen)
						break;
				}
				laststart += startadj;
				*offset += laststart;
				*len = lastend + endadj - laststart;
			}
		}
	}
	g_slist_free_full (slp, g_free);

	return word_type;
}

static void
gtk_xtext_motion_notify (GtkWidget * widget, double ex, double ey,
								 GdkModifierType mask, gpointer unused)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	int redraw, tmp, x, y, offset, len;
	textentry *word_ent;
	int word_type;

	x = (int) ex;
	y = (int) ey;
	xtext->pointer_y = y;

	if (xtext->moving_separator)
	{
		xtext_set_cursor (xtext, "col-resize");
		if (x < (3 * nx_widget_get_allocated_width (widget)) / 5 && x > 15)
		{
			tmp = xtext->buffer->indent;
			xtext->buffer->indent = x;
			gtk_xtext_fix_indent (xtext->buffer);
			if (tmp != xtext->buffer->indent)
			{
				gtk_xtext_recalc_widths (xtext->buffer, FALSE);
				if (xtext->buffer->scrollbar_down)
					gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj) -
													  gtk_adjustment_get_page_size (xtext->adj));
				if (!xtext->io_tag)
					xtext->io_tag = g_timeout_add (REFRESH_TIMEOUT,
																(GSourceFunc)
																gtk_xtext_adjustment_timeout,
																xtext);
			}
		}
		return;
	}

	if (xtext->button_down)
	{
		/* a click, not a drag, until the pointer has moved past the drag
		   threshold: a hand (a trackpad more so) moves a pixel or two between
		   press and release, and that must not select a character or two
		   instead of clearing the selection */
		if (!xtext->drag_started)
		{
			int t = 8;
			g_object_get (gtk_widget_get_settings (widget), "gtk-dnd-drag-threshold", &t, NULL);
			if (abs (x - xtext->select_start_x) <= t && abs (y - xtext->select_start_y) <= t)
				return;
			xtext->drag_started = TRUE;
		}
		xtext_set_cursor (xtext, "text");
		redraw = gtk_xtext_check_mark_stamp (xtext, mask);
		xtext->select_end_x = x;
		xtext->select_end_y = y;
		gtk_xtext_selection_update (xtext, y, !redraw);

		/* user has pressed or released SHIFT, must redraw entire selection */
		if (redraw)
		{
			xtext->force_stamp = TRUE;
			xtext_render_visible (xtext);
			xtext->force_stamp = FALSE;
		}
		return;
	}

	{
		gboolean hover = xtext_jump_hit (xtext, x, y);
		if (hover != xtext->jump_hover)
		{
			xtext->jump_hover = hover;
			xtext_queue_draw (GTK_XTEXT (widget));
		}
		if (hover)
		{
			gtk_xtext_unhover (xtext);
			xtext_set_cursor (xtext, "pointer");
			return;
		}
	}

	if (xtext_on_separator (xtext, x))
	{
		if (!xtext->cursor_resize)
		{
			gtk_xtext_unhover (xtext);
			xtext->cursor_resize = TRUE;
		}
		xtext_set_cursor (xtext, "col-resize");
		return;
	}

	if (xtext->urlcheck_function == NULL)
	{
		gtk_xtext_unhover (xtext);
		xtext_set_base_cursor (xtext, x, y);
		return;
	}

	word_type = gtk_xtext_get_word_adjust (xtext, x, y, &word_ent, &offset, &len);
	if (word_type > 0)
	{
		if (!xtext->cursor_hand ||
			 xtext->hilight_ent != word_ent ||
			 xtext->hilight_start != offset ||
			 xtext->hilight_end != offset + len)
		{
			/* a link shows where it goes before you click it */
			gtk_xtext_set_url_tooltip (xtext, word_type, x, y);
			if (!xtext->cursor_hand)
			{
				xtext->cursor_hand = TRUE;
				xtext->cursor_resize = FALSE;
			}
			xtext_set_cursor (xtext, "pointer");

			/* the underline moves to this link: one redraw does both */
			xtext->hilight_ent = word_ent;
			xtext->hilight_start = offset;
			xtext->hilight_end = offset + len;

			xtext_render_visible (xtext);
		}
		return;
	}

	gtk_xtext_unhover (xtext);
	xtext_set_base_cursor (xtext, x, y);
}

static gboolean gtk_xtext_selection_kill (GtkXText *xtext, gpointer unused);

/* PRIMARY changed hands: if it isn't ours any more, another program (or
   widget) has claimed the selection, as GTK3's selection-clear-event said */
static void
gtk_xtext_primary_changed (GdkClipboard *cb, GtkXText *xtext)
{
	if (xtext->primary_content &&
		 gdk_clipboard_get_content (cb) != (GdkContentProvider *) xtext->primary_content)
	{
		g_clear_object (&xtext->primary_content);
		gtk_xtext_selection_kill (xtext, NULL);
	}
}

static void
gtk_xtext_set_clip_owner (GtkWidget * xtext, guint32 time)
{
	char *str;
	int len;

	if (GTK_XTEXT (xtext)->selection_buffer &&
		GTK_XTEXT (xtext)->selection_buffer != GTK_XTEXT (xtext)->buffer)
		gtk_xtext_selection_clear (GTK_XTEXT (xtext)->selection_buffer);

	GTK_XTEXT (xtext)->selection_buffer = GTK_XTEXT (xtext)->buffer;

	str = gtk_xtext_selection_get_text (GTK_XTEXT (xtext), &len);
	if (str)
	{
		if (str[0])
		{
			GtkXText *xt = GTK_XTEXT (xtext);
			GdkClipboard *primary = gtk_widget_get_primary_clipboard (xtext);
			GdkContentProvider *p;

			gdk_clipboard_set_text (gtk_widget_get_clipboard (xtext), str);
			/* the text as it is now; GTK3 served it on request, but we
			   re-own PRIMARY every time the selection is finished anyway */
			p = gdk_content_provider_new_typed (G_TYPE_STRING, str);
			g_clear_object (&xt->primary_content);
			xt->primary_content = g_object_ref (p);
			gdk_clipboard_set_content (primary, p);
			g_object_unref (p);
			if (!xt->primary_tag)
				xt->primary_tag = g_signal_connect (primary, "changed",
											G_CALLBACK (gtk_xtext_primary_changed), xt);
		}

		g_free (str);
	}
}

void
gtk_xtext_copy_selection (GtkXText *xtext)
{
	gtk_xtext_set_clip_owner (GTK_WIDGET (xtext), GDK_CURRENT_TIME);
}

/* something is selected in the buffer on show */
gboolean
gtk_xtext_has_selection (GtkXText *xtext)
{
	return xtext->buffer->last_ent_start != NULL;
}

/* select every line of the buffer on show */
void
gtk_xtext_select_all (GtkXText *xtext)
{
	xtext_buffer *buf = xtext->buffer;
	textentry *ent;

	if (!buf->text_first)
		return;
	if (xtext->selection_buffer && xtext->selection_buffer != buf)
		gtk_xtext_selection_clear (xtext->selection_buffer);
	for (ent = buf->text_first; ent; ent = ent->next)
	{
		ent->mark_start = 0;
		ent->mark_end = ent->str_len;
	}
	buf->last_ent_start = buf->text_first;
	buf->last_ent_end = buf->text_last;
	buf->last_offset_start = 0;
	buf->last_offset_end = buf->text_last->str_len;
	xtext->selection_buffer = buf;
	dontscroll (buf);	/* nothing moved: a full render, not a scroll copy */
	gtk_xtext_refresh (xtext);
	if (prefs.hex_text_autocopy_text)
		gtk_xtext_set_clip_owner (GTK_WIDGET (xtext), GDK_CURRENT_TIME);
}

/* the line at x,y without colour codes (g_free it), or NULL */
char *
gtk_xtext_line_text_at (GtkXText *xtext, int x, int y)
{
	textentry *ent = gtk_xtext_find_char (xtext, x, y, NULL, NULL);

	if (!ent || ent->str_len < 1)
		return NULL;
	return (char *) gtk_xtext_strip_color (ent->str, ent->str_len, NULL, NULL, NULL, FALSE);
}

static void
gtk_xtext_unselect (GtkXText *xtext)
{
	gtk_xtext_selection_clear (xtext->buffer);
	xtext->buffer->last_ent_start = NULL;
	xtext->buffer->last_ent_end = NULL;
	/* whole page, see gtk_xtext_selection_render */
	xtext_render_visible (xtext);
}

static void
gtk_xtext_emit_click (GtkXText *xtext, char *word, guint button, int n_press,
							 GdkModifierType state)
{
	GtkXTextClick click;

	click.button = button;
	click.n_press = n_press;
	click.state = state;
	click.x = xtext->click_x;
	click.y = xtext->click_y;
	g_signal_emit (G_OBJECT (xtext), xtext_signals[WORD_CLICK], 0, word, &click);
}

static gboolean
gtk_xtext_button_release (GtkWidget * widget, guint button, int n_press,
								  double ex, double ey, GdkModifierType state, gpointer unused)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	unsigned char *word;
	int old, x = (int) ex, y = (int) ey;

	if (xtext->moving_separator)
	{
		xtext->moving_separator = FALSE;
		old = xtext->buffer->indent;
		if (x < (4 * nx_widget_get_allocated_width (widget)) / 5 && x > 15)
			xtext->buffer->indent = x;
		gtk_xtext_fix_indent (xtext->buffer);
		if (xtext->buffer->indent != old)
		{
			gtk_xtext_recalc_widths (xtext->buffer, FALSE);
			gtk_xtext_adjustment_set (xtext->buffer, TRUE);
			gtk_xtext_render_page (xtext);
		} else
			xtext_render_visible (xtext);
		if (!xtext_on_separator (xtext, x))
		{
			xtext->cursor_resize = FALSE;
			xtext_set_base_cursor (xtext, x, y);
		}
		return FALSE;
	}

	if (button == 1)
	{
		xtext->button_down = FALSE;
		if (xtext->scroll_tag)
		{
			g_source_remove (xtext->scroll_tag);
			xtext->scroll_tag = 0;
		}


		/* got a new selection? (a drag, or a double/triple click) */
		if (xtext->buffer->last_ent_start &&
			 (xtext->drag_started || xtext->word_select || xtext->line_select))
		{
			xtext->color_paste = FALSE;
			if (state & STATE_CTRL || prefs.hex_text_autocopy_color)
				xtext->color_paste = TRUE;
			if (prefs.hex_text_autocopy_text)
			{
				GdkEvent *ev = nx_current_event ();
				gtk_xtext_set_clip_owner (GTK_WIDGET (xtext),
												  ev ? gdk_event_get_time (ev) : GDK_CURRENT_TIME);
			}
		}

		if (xtext->word_select || xtext->line_select)
		{
			xtext->word_select = FALSE;
			xtext->line_select = FALSE;
			xtext->drag_started = FALSE;
			return FALSE;
		}

		/* a click (no drag past the threshold): clears any selection, and is
		   a click on whatever's there (a link, a nick) */
		if (!xtext->drag_started)
		{
			if (xtext->buffer->last_ent_start)
			{
				gtk_xtext_unselect (xtext);
				xtext->mark_stamp = FALSE;
				return FALSE;
			}
			word = gtk_xtext_get_word (xtext, x, y, 0, 0, 0, 0);
			gtk_xtext_emit_click (xtext, (char *) word, button, 1, state);
			return FALSE;
		}
		xtext->drag_started = FALSE;
	}

	return FALSE;
}

static gboolean
gtk_xtext_button_press (GtkWidget * widget, guint button, int n_press,
								double ex, double ey, GdkModifierType mask, gpointer unused)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	textentry *ent;
	unsigned char *word;
	int x = (int) ex, y = (int) ey, offset, len;

	xtext->pointer_y = y;
	xtext->click_x = x;
	xtext->click_y = y;

	if (button == 1 && xtext_jump_hit (xtext, x, y))
	{
		gtk_xtext_scroll_to_end (xtext);
		return TRUE;
	}

	if (button == 3 || button == 2) /* right/middle click */
	{
		word = gtk_xtext_get_word (xtext, x, y, 0, 0, 0, 0);
		gtk_xtext_emit_click (xtext, word ? (char *) word : "", button, n_press, mask);
		return FALSE;
	}

	if (button != 1)		  /* we only want left button */
		return FALSE;

	if (n_press == 2)	/* WORD select */
	{
		gtk_xtext_check_mark_stamp (xtext, mask);
		if (gtk_xtext_get_word (xtext, x, y, &ent, &offset, &len, 0))
		{
			if (len == 0)
				return FALSE;
			gtk_xtext_selection_clear (xtext->buffer);
			ent->mark_start = offset;
			ent->mark_end = offset + len;
			gtk_xtext_selection_render (xtext, ent, ent);
			xtext->word_select = TRUE;
		}

		return FALSE;
	}

	if (n_press == 3)	/* LINE select */
	{
		gtk_xtext_check_mark_stamp (xtext, mask);
		if (gtk_xtext_get_word (xtext, x, y, &ent, 0, 0, 0))
		{
			gtk_xtext_selection_clear (xtext->buffer);
			ent->mark_start = 0;
			ent->mark_end = ent->str_len;
			gtk_xtext_selection_render (xtext, ent, ent);
			xtext->line_select = TRUE;
		}

		return FALSE;
	}

	/* check if it was a separator-bar click */
	if (xtext_on_separator (xtext, x))
	{
		xtext->moving_separator = TRUE;
		/* the separator drawn in its "moving" look */
		xtext_render_visible (xtext);
		return FALSE;
	}

	xtext->button_down = TRUE;
	xtext->drag_started = FALSE;
	xtext->select_start_x = x;
	xtext->select_start_y = y;
	xtext->select_start_adj = gtk_adjustment_get_value (xtext->adj);

	return FALSE;
}

/* another program has claimed the selection */

static gboolean
gtk_xtext_selection_kill (GtkXText *xtext, gpointer unused)
{
#ifndef WIN32
	if (xtext->buffer->last_ent_start)
		gtk_xtext_unselect (xtext);
#endif
	return TRUE;
}

static char *
gtk_xtext_selection_get_text (GtkXText *xtext, int *len_ret)
{
	textentry *ent;
	char *txt;
	char *pos;
	char *stripped;
	int len;
	int first = TRUE;
	xtext_buffer *buf;

	buf = xtext->selection_buffer;
	if (!buf)
		return NULL;

	/* first find out how much we need to malloc ... */
	len = 0;
	ent = buf->last_ent_start;
	while (ent)
	{
		if (ent->mark_start != -1)
		{
			/* include timestamp? */
			if (ent->mark_start == 0 && xtext->mark_stamp)
			{
				char *time_str;
				int stamp_size = xtext_get_stamp_str (ent->stamp, &time_str);
				g_free (time_str);
				len += stamp_size;
			}

			if (ent->mark_end - ent->mark_start > 0)
				len += (ent->mark_end - ent->mark_start) + 1;
			else
				len++;
		}
		if (ent == buf->last_ent_end)
			break;
		ent = ent->next;
	}

	if (len < 1)
		return NULL;

	/* now allocate mem and copy buffer */
	pos = txt = g_malloc (len);
	ent = buf->last_ent_start;
	while (ent)
	{
		if (ent->mark_start != -1)
		{
			if (!first)
			{
				*pos = '\n';
				pos++;
			}
			first = FALSE;
			if (ent->mark_end - ent->mark_start > 0)
			{
				/* include timestamp? */
				if (ent->mark_start == 0 && xtext->mark_stamp)
				{
					char *time_str;
					int stamp_size = xtext_get_stamp_str (ent->stamp, &time_str);
					memcpy (pos, time_str, stamp_size);
					g_free (time_str);
					pos += stamp_size;
				}

				memcpy (pos, ent->str + ent->mark_start,
						  ent->mark_end - ent->mark_start);
				pos += ent->mark_end - ent->mark_start;
			}
		}
		if (ent == buf->last_ent_end)
			break;
		ent = ent->next;
	}
	*pos = 0;

	if (xtext->color_paste)
	{
		/*stripped = gtk_xtext_conv_color (txt, strlen (txt), &len);*/
		stripped = txt;
		len = strlen (txt);
	}
	else
	{
		stripped = gtk_xtext_strip_color (txt, strlen (txt), NULL, &len, NULL, FALSE);
		g_free (txt);
	}

	*len_ret = len;
	return stripped;
}


/* scroll by a number of lines (fractions too: the page is drawn with a
   pixel offset), kept inside the buffer */
static void
xtext_scroll_lines (GtkXText *xtext, double lines)
{
	GtkAdjustment *a = xtext->adj;
	double v = gtk_adjustment_get_value (a) + lines;
	double top = gtk_adjustment_get_upper (a) - gtk_adjustment_get_page_size (a);

	gtk_adjustment_set_value (a, CLAMP (v, gtk_adjustment_get_lower (a), MAX (top, 0)));
}

/* a flick's momentum: glides on the frame clock, slowing like AppKit's */
static gboolean
xtext_glide_tick (GtkWidget *w, GdkFrameClock *clock, gpointer d)
{
	GtkXText *xtext = GTK_XTEXT (w);
	gint64 now = gdk_frame_clock_get_frame_time (clock);
	double dt = (now - xtext->glide_time) / 1e6;
	double before = gtk_adjustment_get_value (xtext->adj);

	xtext->glide_time = now;
	xtext->glide_v *= pow (0.05, dt);	/* to 5% per second */
	if (fabs (xtext->glide_v) < 20 || !xtext->fontsize)
	{
		xtext->glide_tag = 0;
		return G_SOURCE_REMOVE;
	}
	xtext_scroll_lines (xtext, xtext->glide_v * dt / xtext->fontsize);
	/* hit the top or the bottom: stop */
	if (gtk_adjustment_get_value (xtext->adj) == before)
	{
		xtext->glide_tag = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
xtext_glide_stop (GtkXText *xtext)
{
	if (xtext->glide_tag)
	{
		gtk_widget_remove_tick_callback (GTK_WIDGET (xtext), xtext->glide_tag);
		xtext->glide_tag = 0;
	}
}

static void
gtk_xtext_decelerate (GtkWidget *widget, double vx, double vy, gpointer unused)
{
	GtkXText *xtext = GTK_XTEXT (widget);

	xtext_glide_stop (xtext);
	xtext->glide_v = vy;
	xtext->glide_time = g_get_monotonic_time ();
	xtext->glide_tag = gtk_widget_add_tick_callback (widget, xtext_glide_tick, NULL, NULL);
}

/* a trackpad moves the text with the fingers, pixel for pixel; a wheel
   step moves a few lines (as before); Ctrl/Cmd+wheel zooms */
static gboolean
gtk_xtext_smooth_scroll (GtkWidget *widget, double dx, double dy, GdkScrollUnit unit,
								 GdkModifierType state, gpointer unused)
{
	GtkXText *xtext = GTK_XTEXT (widget);

	xtext_glide_stop (xtext);
	if ((state & (STATE_CTRL | GDK_META_MASK)) && xtext->zoom_cb && dy != 0)
	{
		/* pixel deltas: zoom once per bigger move, not per event */
		xtext->zoom_accum += unit == GDK_SCROLL_UNIT_WHEEL ? dy * 20 : dy;
		if (fabs (xtext->zoom_accum) >= 20)
		{
			xtext->zoom_cb (widget, xtext->zoom_accum < 0 ? 1 : -1);
			xtext->zoom_accum = 0;
		}
		return TRUE;
	}
	if (dy == 0 || !xtext->fontsize)
		return FALSE;
	if (unit == GDK_SCROLL_UNIT_WHEEL)
		xtext_scroll_lines (xtext, dy * gtk_adjustment_get_page_increment (xtext->adj) / 10);
	else
		xtext_scroll_lines (xtext, dy / xtext->fontsize);
	return TRUE;
}


static void
gtk_xtext_class_init (GtkXTextClass * class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkXTextClass *xtext_class;

	object_class = (GObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	xtext_class = (GtkXTextClass *) class;

	parent_class = g_type_class_peek (gtk_widget_get_type ());

	xtext_signals[WORD_CLICK] =
		g_signal_new ("word_click",
							G_TYPE_FROM_CLASS (object_class),
							G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
							G_STRUCT_OFFSET (GtkXTextClass, word_click),
							NULL, NULL,
							_hexchat_marshal_VOID__POINTER_POINTER,
							G_TYPE_NONE,
							2, G_TYPE_POINTER, G_TYPE_POINTER);

	object_class->dispose = gtk_xtext_destroy;
	widget_class->realize = gtk_xtext_realize;
	widget_class->unrealize = gtk_xtext_unrealize;
	widget_class->measure = gtk_xtext_measure;
	widget_class->size_allocate = gtk_xtext_size_allocate;
	widget_class->snapshot = gtk_xtext_snapshot;

	xtext_class->word_click = NULL;
}

GType
gtk_xtext_get_type (void)
{
	static GType xtext_type = 0;

	if (!xtext_type)
	{
		static const GTypeInfo xtext_info =
		{
			sizeof (GtkXTextClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gtk_xtext_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GtkXText),
			0,		/* n_preallocs */
			(GInstanceInitFunc) gtk_xtext_init,
		};

		xtext_type = g_type_register_static (GTK_TYPE_WIDGET, "GtkXText",
														 &xtext_info, 0);
	}

	return xtext_type;
}

/* strip MIRC colors and other attribs. */

/* CL: needs to strip hidden when called by gtk_xtext_text_width, but not when copying text */

typedef struct chunk_s {
	GSList *slp;
	int off1, len1, emph;
	offlen_t meta;
} chunk_t;

static void
xtext_do_chunk(chunk_t *c)
{
	offlen_t *meta;

	if (c->len1 == 0)
		return;

	meta = g_new (offlen_t, 1);
	meta->off = c->off1;
	meta->len = c->len1;
	meta->emph = c->emph;
	meta->width = 0;
	c->slp = g_slist_append (c->slp, meta);

	c->len1 = 0;
}

static unsigned char *
gtk_xtext_strip_color (unsigned char *text, int len, unsigned char *outbuf,
							  int *newlen, GSList **slpp, int strip_hidden)
{
	chunk_t c;
	int i = 0;
	int rcol = 0, bgcol = 0;
	int hidden = FALSE;
	unsigned char *new_str;
	unsigned char *text0 = text;
	int mbl;	/* multi-byte length */

	if (outbuf == NULL)
		new_str = g_malloc (len + 2);
	else
		new_str = outbuf;

	c.slp = NULL;
	c.off1 = 0;
	c.len1 = 0;
	c.emph = 0;
	while (len > 0)
	{
		mbl = charlen (text);
		if (mbl > len)
			goto bad_utf8;

		if (rcol > 0 && (isdigit (*text) || (*text == ',' && isdigit (text[1]) && !bgcol)))
		{
			if (text[1] != ',') rcol--;
			if (*text == ',')
			{
				rcol = 2;
				bgcol = 1;
			}
		} else
		{
			rcol = bgcol = 0;
			switch (*text)
			{
			case ATTR_COLOR:
				xtext_do_chunk (&c);
				rcol = 2;
				break;
			case ATTR_BEEP:
			case ATTR_RESET:
			case ATTR_REVERSE:
			case ATTR_BOLD:
			case ATTR_UNDERLINE:
			case ATTR_STRIKETHROUGH:
			case ATTR_ITALICS:
				xtext_do_chunk (&c);
				if (*text == ATTR_RESET)
					c.emph = 0;
				if (*text == ATTR_ITALICS)
					c.emph ^= EMPH_ITAL;
				if (*text == ATTR_BOLD)
					c.emph ^= EMPH_BOLD;
				break;
			case ATTR_HIDDEN:
				xtext_do_chunk (&c);
				c.emph ^= EMPH_HIDDEN;
				hidden = !hidden;
				break;
			default:
				if (strip_hidden == 2 || (!(hidden && strip_hidden)))
				{
					if (c.len1 == 0)
						c.off1 = text - text0;
					memcpy (new_str + i, text, mbl);
					i += mbl;
					c.len1 += mbl;
				}
			}
		}
		text += mbl;
		len -= mbl;
	}

bad_utf8:		/* Normal ending sequence, and give up if bad utf8 */
	xtext_do_chunk (&c);

	new_str[i] = 0;

	if (newlen != NULL)
		*newlen = i;

	if (slpp)
		*slpp = c.slp;
	else
		g_slist_free_full (c.slp, g_free);

	return new_str;
}

/* gives width of a string, excluding the mIRC codes */

static int
gtk_xtext_text_width_ent (GtkXText *xtext, textentry *ent)
{
	unsigned char *new_buf;
	GSList *slp0, *slp;
	int width;

	if (ent->slp)
	{
		g_slist_free_full (ent->slp, g_free);
		ent->slp = NULL;
	}

	new_buf = gtk_xtext_strip_color (ent->str, ent->str_len, xtext->scratch_buffer,
												NULL, &slp0, 2);

	width =  backend_get_text_width_slp (xtext, new_buf, slp0);
	ent->slp = slp0;

	for (slp = slp0; slp; slp = g_slist_next (slp))
	{
		offlen_t *meta;

		meta = slp->data;
		meta->width = backend_get_text_width_emph (xtext, ent->str + meta->off, meta->len, meta->emph);
	}
	return width;
}

static int
gtk_xtext_text_width (GtkXText *xtext, unsigned char *text, int len)
{
	unsigned char *new_buf;
	int new_len;
	GSList *slp;
	int width;

	new_buf = gtk_xtext_strip_color (text, len, xtext->scratch_buffer,
												&new_len, &slp, !xtext->ignore_hidden);

	width =  backend_get_text_width_slp (xtext, new_buf, slp);
	g_slist_free_full (slp, g_free);

	return width;
}

/* actually draw text to screen (one run with the same color/attribs) */

static int
gtk_xtext_render_flush (GtkXText * xtext, int x, int y, unsigned char *str,
								int len, xtext_gc *gc, int *emphasis)
{
	int str_width, dofill;

	if (xtext->dont_render || len < 1 || xtext->hidden)
		return 0;

	str_width = backend_get_text_width_emph (xtext, str, len, *emphasis);

	dofill = TRUE;

	/* a background image behind text that has no colour of its own */
	if (!xtext->backcolor && xtext->pixmap)
	{
		xtext_draw_bg (xtext, x, y - xtext->font->ascent, str_width,
							xtext->fontsize);
		dofill = FALSE;	/* already drawn the background */
	}

	backend_draw_text_emph (xtext, dofill, gc, x, y, str, len, str_width, *emphasis);

	if (xtext->strikethrough)
	{
		/* pango_attr_strikethrough_new does not render in the custom widget so we need to reinvent the wheel */
		int sy = y - xtext->font->ascent + (xtext->fontsize / 2);
		xtext_draw_line (xtext, &gc->fg, x, sy, x + str_width - 1, sy);
	}

	if (xtext->underline)
	{
		/* one pixel under the baseline */
		xtext_draw_line (xtext, &gc->fg, x, y + 1, x + str_width - 1, y + 1);
	}

	return str_width;
}

static void
gtk_xtext_reset (GtkXText * xtext, int mark, int attribs)
{
	if (attribs)
	{
		xtext->underline = FALSE;
		xtext->strikethrough = FALSE;
		xtext->hidden = FALSE;
	}
	if (!mark)
	{
		xtext->backcolor = FALSE;
		if (xtext->col_fore != XTEXT_FG)
			xtext_set_fg (xtext, &xtext->fgc, XTEXT_FG);
		if (xtext->col_back != XTEXT_BG)
			xtext_set_bg (xtext, &xtext->fgc, XTEXT_BG);
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
	xtext->parsing_color = FALSE;
	xtext->parsing_backcolor = FALSE;
	xtext->nc = 0;
}

/*
 * gtk_xtext_search_offset (buf, ent, off) --
 * Look for arg offset in arg textentry
 * Return one or more flags:
 * 	GTK_MATCH_MID if we are in a match
 * 	GTK_MATCH_START if we're at the first byte of it
 * 	GTK_MATCH_END if we're at the first byte past it
 * 	GTK_MATCH_CUR if it is the current match
 */
#define GTK_MATCH_START	1
#define GTK_MATCH_MID	2
#define GTK_MATCH_END	4
#define GTK_MATCH_CUR	8
static int
gtk_xtext_search_offset (xtext_buffer *buf, textentry *ent, unsigned int off)
{
	GList *gl;
	offsets_t o;
	int flags = 0;

	for (gl = g_list_first (ent->marks); gl; gl = g_list_next (gl))
	{
		o.u = GPOINTER_TO_UINT (gl->data);
		if (off < o.o.start || off > o.o.end)
			continue;
		flags = GTK_MATCH_MID;
		if (off == o.o.start)
			flags |= GTK_MATCH_START;
		if (off == o.o.end)
		{
			gl = g_list_next (gl);
			if (gl)
			{
				o.u = GPOINTER_TO_UINT (gl->data);
				if (off ==  o.o.start)	/* If subseq match is adjacent */
				{
					flags |= (gl == buf->curmark)? GTK_MATCH_CUR: 0;
				}
				else		/* If subseq match is not adjacent */
				{
					flags |= GTK_MATCH_END;
				}
			}
			else		/* If there is no subseq match */
			{
				flags |= GTK_MATCH_END;
			}
		}
		else if (gl == buf->curmark)	/* If not yet at the end of this match */
		{
			flags |= GTK_MATCH_CUR;
		}
		break;
	}
	return flags;
}

/* render a single line, which WONT wrap, and parse mIRC colors */

#define RENDER_FLUSH x += gtk_xtext_render_flush (xtext, x, y, pstr, j, gc, emphasis)

static int
gtk_xtext_render_str (GtkXText * xtext, int y, textentry * ent,
							 unsigned char *str, int len, int win_width, int indent,
							 int line, int left_only, int *x_size_ret, int *emphasis)
{
	xtext_gc *gc;
	int i = 0, x = indent, j = 0;
	unsigned char *pstr = str;
	int col_num, tmp;
	int offset;
	int mark = FALSE;
	int ret = 1;
	int k;
	int srch_underline = FALSE;
	int srch_mark = FALSE;

	xtext->in_hilight = FALSE;

	offset = str - ent->str;

	gc = &xtext->fgc;				  /* our foreground GC */

	if (ent->mark_start != -1 &&
		 ent->mark_start <= i + offset && ent->mark_end > i + offset)
	{
		xtext_set_bg (xtext, gc, XTEXT_MARK_BG);
		xtext_set_fg (xtext, gc, XTEXT_MARK_FG);
		xtext->backcolor = TRUE;
		mark = TRUE;
	}
	if (xtext->hilight_ent == ent &&
		 xtext->hilight_start <= i + offset && xtext->hilight_end > i + offset)
	{
		xtext->underline = TRUE;
		xtext->in_hilight = TRUE;
	}

	if (!xtext->dont_render)
	{
		/* draw background to the left of the text */
		if (str == ent->str && indent > MARGIN && xtext->buffer->time_stamp)
		{
			/* don't overwrite the timestamp */
			if (indent > xtext->stamp_width)
			{
				xtext_draw_bg (xtext, xtext->stamp_width, y - xtext->font->ascent,
									indent - xtext->stamp_width, xtext->fontsize);
			}
		} else
		{
			/* fill the indent area with background gc */
			xtext_draw_bg (xtext, 0, y - xtext->font->ascent, indent, xtext->fontsize);
		}
	}

	while (i < len)
	{

		if (xtext->hilight_ent == ent && xtext->hilight_start == (i + offset))
		{
			RENDER_FLUSH;
			pstr += j;
			j = 0;
			xtext->underline = TRUE;

			xtext->in_hilight = TRUE;
		}

		if ((xtext->parsing_color && isdigit (str[i]) && xtext->nc < 2) ||
			 (xtext->parsing_color && str[i] == ',' && isdigit (str[i+1]) && xtext->nc < 3 && !xtext->parsing_backcolor))
		{
			pstr++;
			if (str[i] == ',')
			{
				xtext->parsing_backcolor = TRUE;
				if (xtext->nc)
				{
					xtext->num[xtext->nc] = 0;
					xtext->nc = 0;
					col_num = atoi (xtext->num);
					if (col_num == 99)	/* mIRC lameness */
						col_num = XTEXT_FG;
					else
					if (col_num > XTEXT_MAX_COLOR)
						col_num = col_num % XTEXT_MIRC_COLS;
					xtext->col_fore = col_num;
					if (!mark)
						xtext_set_fg (xtext, gc, col_num);
				}
			} else
			{
				xtext->num[xtext->nc] = str[i];
				if (xtext->nc < 7)
					xtext->nc++;
			}
		} else
		{
			if (xtext->parsing_color)
			{
				xtext->parsing_color = FALSE;
				if (xtext->nc)
				{
					xtext->num[xtext->nc] = 0;
					xtext->nc = 0;
					col_num = atoi (xtext->num);
					if (xtext->parsing_backcolor)
					{
						if (col_num == 99)	/* mIRC lameness */
							col_num = XTEXT_BG;
						else
						if (col_num > XTEXT_MAX_COLOR)
							col_num = col_num % XTEXT_MIRC_COLS;
						if (col_num == XTEXT_BG)
							xtext->backcolor = FALSE;
						else
							xtext->backcolor = TRUE;
						if (!mark)
							xtext_set_bg (xtext, gc, col_num);
						xtext->col_back = col_num;
					} else
					{
						if (col_num == 99)	/* mIRC lameness */
							col_num = XTEXT_FG;
						else
						if (col_num > XTEXT_MAX_COLOR)
							col_num = col_num % XTEXT_MIRC_COLS;
						if (!mark)
							xtext_set_fg (xtext, gc, col_num);
						xtext->col_fore = col_num;
					}
					xtext->parsing_backcolor = FALSE;
				} else
				{
					/* got a \003<non-digit>... i.e. reset colors */
					RENDER_FLUSH;
					pstr += j;
					j = 0;
					gtk_xtext_reset (xtext, mark, FALSE);
				}
			}

			if (!left_only && !mark &&
				 (k = gtk_xtext_search_offset (xtext->buffer, ent, offset + i)))
			{
				RENDER_FLUSH;
				pstr += j;
				j = 0;
				if (!(xtext->buffer->search_flags & highlight))
				{
					if (k & GTK_MATCH_CUR)
					{
						xtext_set_bg (xtext, gc, XTEXT_MARK_BG);
						xtext_set_fg (xtext, gc, XTEXT_MARK_FG);
						xtext->backcolor = TRUE;
						srch_mark = TRUE;
					} else
					{
						xtext_set_bg (xtext, gc, xtext->col_back);
						xtext_set_fg (xtext, gc, xtext->col_fore);
						xtext->backcolor = (xtext->col_back != XTEXT_BG)? TRUE: FALSE;
						srch_mark = FALSE;
					}
				}
				else
				{
					xtext->underline = (k & GTK_MATCH_CUR)? TRUE: FALSE;
					if (k & (GTK_MATCH_START | GTK_MATCH_MID))
					{
						xtext_set_bg (xtext, gc, XTEXT_MARK_BG);
						xtext_set_fg (xtext, gc, XTEXT_MARK_FG);
						xtext->backcolor = TRUE;
						srch_mark = TRUE;
					}
					if (k & GTK_MATCH_END)
					{
						xtext_set_bg (xtext, gc, xtext->col_back);
						xtext_set_fg (xtext, gc, xtext->col_fore);
						xtext->backcolor = (xtext->col_back != XTEXT_BG)? TRUE: FALSE;
						srch_mark = FALSE;
						xtext->underline = FALSE;
					}
					srch_underline = xtext->underline;
				}
			}

			switch (str[i])
			{
			case '\n':
			/*case ATTR_BEEP:*/
				break;
			case ATTR_REVERSE:
				RENDER_FLUSH;
				pstr += j + 1;
				j = 0;
				tmp = xtext->col_fore;
				xtext->col_fore = xtext->col_back;
				xtext->col_back = tmp;
				if (!mark)
				{
					xtext_set_fg (xtext, gc, xtext->col_fore);
					xtext_set_bg (xtext, gc, xtext->col_back);
				}
				if (xtext->col_back != XTEXT_BG)
					xtext->backcolor = TRUE;
				else
					xtext->backcolor = FALSE;
				break;
			case ATTR_BOLD:
				RENDER_FLUSH;
				*emphasis ^= EMPH_BOLD;
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_UNDERLINE:
				RENDER_FLUSH;
				xtext->underline = !xtext->underline;
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_STRIKETHROUGH:
				RENDER_FLUSH;
				xtext->strikethrough = !xtext->strikethrough;
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_ITALICS:
				RENDER_FLUSH;
				*emphasis ^= EMPH_ITAL;
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_HIDDEN:
				RENDER_FLUSH;
				xtext->hidden = (!xtext->hidden) & (!xtext->ignore_hidden);
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_RESET:
				RENDER_FLUSH;
				*emphasis = 0;
				pstr += j + 1;
				j = 0;
				gtk_xtext_reset (xtext, mark, !xtext->in_hilight);
				break;
			case ATTR_COLOR:
				RENDER_FLUSH;
				xtext->parsing_color = TRUE;
				pstr += j + 1;
				j = 0;
				break;
			default:
				tmp = charlen (str + i);
				/* invalid utf8 safe guard */
				if (tmp + i > len)
					tmp = len - i;
				j += tmp;	/* move to the next utf8 char */
			}
		}
		i += charlen (str + i);	/* move to the next utf8 char */
		/* invalid utf8 safe guard */
		if (i > len)
			i = len;

		/* Separate the left part, the space and the right part
		   into separate runs, and reset bidi state inbetween.
		   Perform this only on the first line of the message.
                */
		if (offset == 0)
		{
			/* we've reached the end of the left part? */
			if ((pstr-str)+j == ent->left_len)
			{
				RENDER_FLUSH;
				pstr += j;
				j = 0;
			}
			else if ((pstr-str)+j == ent->left_len+1)
			{
				RENDER_FLUSH;
				pstr += j;
				j = 0;
			}
		}

		if (xtext->hilight_ent == ent && xtext->hilight_end == (i + offset))
		{
			RENDER_FLUSH;
			pstr += j;
			j = 0;
			xtext->underline = FALSE;
			xtext->in_hilight = FALSE;
		}

		if (!mark && ent->mark_start == (i + offset))
		{
			RENDER_FLUSH;
			pstr += j;
			j = 0;
			xtext_set_bg (xtext, gc, XTEXT_MARK_BG);
			xtext_set_fg (xtext, gc, XTEXT_MARK_FG);
			xtext->backcolor = TRUE;
			if (srch_underline)
			{
				xtext->underline = FALSE;
				srch_underline = FALSE;
			}
			mark = TRUE;
		}

		if (mark && ent->mark_end == (i + offset))
		{
			RENDER_FLUSH;
			pstr += j;
			j = 0;
			xtext_set_bg (xtext, gc, xtext->col_back);
			xtext_set_fg (xtext, gc, xtext->col_fore);
			if (xtext->col_back != XTEXT_BG)
				xtext->backcolor = TRUE;
			else
				xtext->backcolor = FALSE;
			mark = FALSE;
		}

	}

	if (j)
		RENDER_FLUSH;

	if (mark || srch_mark)
	{
		xtext_set_bg (xtext, gc, xtext->col_back);
		xtext_set_fg (xtext, gc, xtext->col_fore);
		if (xtext->col_back != XTEXT_BG)
			xtext->backcolor = TRUE;
		else
			xtext->backcolor = FALSE;
	}

	/* draw background to the right of the text */
	if (!left_only && !xtext->dont_render)
	{
		/* draw separator now so it doesn't appear to flicker */
		gtk_xtext_draw_sep (xtext, y - xtext->font->ascent);
		xtext_draw_bg (xtext,
							x,	/* x */
							y - xtext->font->ascent, /* y */
							(win_width + MARGIN) - x, /* width */
							xtext->fontsize);		/* height */
	}

	/* return how much we drew in the x direction */
	if (x_size_ret)
		*x_size_ret = x - indent;

	return ret;
}

/* walk through str until this line doesn't fit anymore */

static int
find_next_wrap (GtkXText * xtext, textentry * ent, unsigned char *str,
					 int win_width, int indent)
{
	unsigned char *last_space = str;
	unsigned char *orig_str = str;
	int str_width = indent;
	int rcol = 0, bgcol = 0;
	int hidden = FALSE;
	int mbl;
	int char_width;
	int ret;
	int limit_offset = 0;
	int emphasis = 0;
	GSList *lp;

	/* single liners */
	if (win_width >= ent->str_width + ent->indent)
		return ent->str_len;

	/* it does happen! */
	if (win_width < 1)
	{
		ret = ent->str_len - (str - ent->str);
		goto done;
	}

	/* Find emphasis value for the offset that is the first byte of our string */
	for (lp = ent->slp; lp; lp = g_slist_next (lp))
	{
		offlen_t *meta = lp->data;
		unsigned char *start, *end;

		start = ent->str + meta->off;
		end = start + meta->len;
		if (str >= start && str < end)
		{
			emphasis = meta->emph;
			break;
		}
	}

	while (1)
	{
		if (rcol > 0 && (isdigit (*str) || (*str == ',' && isdigit (str[1]) && !bgcol)))
		{
			if (str[1] != ',') rcol--;
			if (*str == ',')
			{
				rcol = 2;
				bgcol = 1;
			}
			limit_offset++;
			str++;
		} else
		{
			rcol = bgcol = 0;
			switch (*str)
			{
			case ATTR_COLOR:
				rcol = 2;
			case ATTR_BEEP:
			case ATTR_RESET:
			case ATTR_REVERSE:
			case ATTR_BOLD:
			case ATTR_UNDERLINE:
			case ATTR_STRIKETHROUGH:
			case ATTR_ITALICS:
				if (*str == ATTR_RESET)
					emphasis = 0;
				if (*str == ATTR_ITALICS)
					emphasis ^= EMPH_ITAL;
				if (*str == ATTR_BOLD)
					emphasis ^= EMPH_BOLD;
				limit_offset++;
				str++;
				break;
			case ATTR_HIDDEN:
				if (xtext->ignore_hidden)
					goto def;
				hidden = !hidden;
				limit_offset++;
				str++;
				break;
			default:
			def:
				/* wrap between clusters: never split an emoji sequence, a
				   flag or a letter and its accent across two rows */
				mbl = xtext_cluster_len (str, ent->str + ent->str_len);
				if (mbl < 1) mbl = 1;
				char_width = backend_get_text_width_emph (xtext, str, mbl, emphasis);
				if (!hidden) str_width += char_width;
				if (str_width > win_width)
				{
					if (xtext->wordwrap)
					{
						if (str - last_space > WORDWRAP_LIMIT + limit_offset)
							ret = str - orig_str; /* fall back to character wrap */
						else
						{
							if (*last_space == ' ')
								last_space++;
							ret = last_space - orig_str;
							if (ret == 0) /* fall back to character wrap */
								ret = str - orig_str;
						}
						goto done;
					}
					ret = str - orig_str;
					goto done;
				}

				/* keep a record of the last space, for wordwrapping */
				if (is_del (*str))
				{
					last_space = str;
					limit_offset = 0;
				}

				/* progress to the next char */
				str += mbl;

			}
		}

		if (str >= ent->str + ent->str_len)
		{
			ret = str - orig_str;
			goto done;
		}
	}

done:

	/* must make progress */
	if (ret < 1)
		ret = 1;

	return ret;
}

/* find the offset, in bytes, that wrap number 'line' starts at */

static int
gtk_xtext_find_subline (GtkXText *xtext, textentry *ent, int line)
{
	int rlen = 0;

	if (line > 0)
	{
		rlen = GPOINTER_TO_UINT (g_slist_nth_data (ent->sublines, line - 1));
		if (rlen == 0)
			rlen = ent->str_len;
	}
	return rlen;
}

/* horrible hack for drawing time stamps */

static void
gtk_xtext_render_stamp (GtkXText * xtext, textentry * ent,
								char *text, int len, int line, int win_width)
{
	textentry tmp_ent;
	int hs;
	int xsize, y, emphasis;

	/* trashing ent here, so make a backup first */
	memcpy (&tmp_ent, ent, sizeof (tmp_ent));
	hs = xtext->hilight_start;
	xtext->hilight_start = 0xffff;	/* temp disable */
	emphasis = 0;

	if (xtext->mark_stamp)
	{
		/* if this line is marked, mark this stamp too */
		if (ent->mark_start == 0)	
		{
			ent->mark_start = 0;
			ent->mark_end = len;
		}
		else
		{
			ent->mark_start = -1;
			ent->mark_end = -1;
		}
		ent->str = text;
	}

	y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
	gtk_xtext_render_str (xtext, y, ent, text, len,
								 win_width, 2, line, TRUE, &xsize, &emphasis);

	/* restore everything back to how it was */
	memcpy (ent, &tmp_ent, sizeof (tmp_ent));
	xtext->hilight_start = hs;

	/* with a non-fixed-width font, sometimes we don't draw enough
		background i.e. when this stamp is shorter than xtext->stamp_width */
	xsize += MARGIN;
	if (xsize < xtext->stamp_width)
	{
		y -= xtext->font->ascent;
		xtext_draw_bg (xtext,
							xsize,	/* x */
							y,			/* y */
							xtext->stamp_width - xsize,	/* width */
							xtext->fontsize					/* height */);
	}
}

/* render a single line, which may wrap to more lines */

static int
gtk_xtext_render_line (GtkXText * xtext, textentry * ent, int line,
							  int lines_max, int subline, int win_width)
{
	unsigned char *str;
	int indent, taken, entline, len, y, start_subline;
	int emphasis = 0;

	entline = taken = 0;
	str = ent->str;
	indent = ent->indent;
	start_subline = subline;

	/* draw the timestamp */
	if (xtext->auto_indent && xtext->buffer->time_stamp && !ent->no_stamp)
	{
		char *time_str;
		int len;

		len = xtext_get_stamp_str (ent->stamp, &time_str);
		gtk_xtext_render_stamp (xtext, ent, time_str, len, line, win_width);
		g_free (time_str);
	}

	/* draw each line one by one */
	do
	{
		if (entline > 0)
			len = GPOINTER_TO_INT (g_slist_nth_data (ent->sublines, entline)) - GPOINTER_TO_INT (g_slist_nth_data (ent->sublines, entline - 1));
		else
			len = GPOINTER_TO_INT (g_slist_nth_data (ent->sublines, entline));

		entline++;

		y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
		if (!subline)
		{
			if (!gtk_xtext_render_str (xtext, y, ent, str, len, win_width,
												indent, line, FALSE, NULL, &emphasis))
			{
				/* small optimization */
				gtk_xtext_draw_marker (xtext, ent, y - xtext->fontsize * (taken + start_subline + 1));
				return g_slist_length (ent->sublines) - subline;
			}
		} else
		{
			xtext->dont_render = TRUE;
			gtk_xtext_render_str (xtext, y, ent, str, len, win_width,
										 indent, line, FALSE, NULL, &emphasis);
			xtext->dont_render = FALSE;
			subline--;
			line--;
			taken--;
		}

		indent = xtext->buffer->indent;
		line++;
		taken++;
		str += len;

		if (line >= lines_max)
			break;

	}
	while (str < ent->str + ent->str_len);

	gtk_xtext_draw_marker (xtext, ent, y - xtext->fontsize * (taken + start_subline));

	return taken;
}

void
gtk_xtext_set_palette (GtkXText * xtext, GdkRGBA palette[])
{
	int i;

	for (i = (XTEXT_COLS-1); i >= 0; i--)
	{
		xtext->palette[i] = palette[i];
	}

	if (gtk_widget_get_realized (GTK_WIDGET(xtext)))
	{
		xtext_set_fg (xtext, &xtext->fgc, XTEXT_FG);
		xtext_set_bg (xtext, &xtext->fgc, XTEXT_BG);
		xtext_set_fg (xtext, &xtext->bgc, XTEXT_BG);

		xtext->marker_gc.fg = xtext->palette[XTEXT_MARKER];
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
}

static void
gtk_xtext_fix_indent (xtext_buffer *buf)
{
	int j;

	/* make indent a multiple of the space width */
	if (buf->indent && buf->xtext->space_width)
	{
		j = 0;
		while (j < buf->indent)
		{
			j += buf->xtext->space_width;
		}
		buf->indent = j;
	}

	dontscroll (buf);	/* force scrolling off */
}

/* The separator column (buf->indent) is in pixels and auto-indent only
   ever widens it, to fit the widest nick seen. With a new font (zoom, or a
   font change) those pixels mean something else: zooming in widened it in
   big-font pixels, and zooming back out left that width -- the nicks stood
   off from the separator, and every zoom step moved it further. Derive it
   again from what the buffer holds, at the new font's widths. */
static void
gtk_xtext_reset_indent (xtext_buffer *buf)
{
	GtkXText *xtext = buf->xtext;
	textentry *ent;
	int space, want;

	if (!xtext->auto_indent)
		return;

	space = buf->time_stamp ? xtext->stamp_width : 0;
	/* what gtk_xtext_buffer_new and the first line start from */
	buf->indent = xtext->space_width * 2;
	for (ent = buf->text_first; ent; ent = ent->next)
	{
		if (ent->left_len == -1)
			continue;
		/* the same rule as gtk_xtext_append_indent */
		want = MARGIN + space + xtext->space_width +
				 gtk_xtext_text_width (xtext, ent->str, ent->left_len);
		if (want > buf->indent)
			buf->indent = want;
	}
	if (buf->indent > xtext->max_auto_indent)
		buf->indent = xtext->max_auto_indent;
	gtk_xtext_fix_indent (buf);
}

static void
gtk_xtext_recalc_widths (xtext_buffer *buf, int do_str_width)
{
	textentry *ent;

	/* since we have a new font, we have to recalc the text widths */
	if (do_str_width)
		gtk_xtext_reset_indent (buf);
	ent = buf->text_first;
	while (ent)
	{
		if (do_str_width)
		{
			ent->str_width = gtk_xtext_text_width_ent (buf->xtext, ent);
		}
		if (ent->left_len != -1)
		{
			ent->indent =
				(buf->indent -
				 gtk_xtext_text_width (buf->xtext, ent->str,
										ent->left_len)) - buf->xtext->space_width;
			if (ent->indent < MARGIN)
				ent->indent = MARGIN;
		}
		ent = ent->next;
	}

	gtk_xtext_calc_lines (buf, FALSE);
}

int
gtk_xtext_set_font (GtkXText *xtext, char *name)
{

	if (xtext->font)
		backend_font_close (xtext);

	/* realize now, so that font_open has a XDisplay */
	gtk_widget_realize (GTK_WIDGET (xtext));

	backend_font_open (xtext, name);
	if (xtext->font == NULL)
		return FALSE;

	{
		char *time_str;
		int stamp_size = xtext_get_stamp_str (time(0), &time_str);
		xtext->stamp_width =
			gtk_xtext_text_width (xtext, time_str, stamp_size) + MARGIN;
		g_free (time_str);
	}

	gtk_xtext_fix_indent (xtext->buffer);

	if (gtk_widget_get_realized (GTK_WIDGET(xtext)))
		gtk_xtext_recalc_widths (xtext->buffer, TRUE);

	return TRUE;
}

/* c * a / 255, rounded */
static inline guint
premul (guint c, guint a)
{
	guint t = c * a + 0x80;
	return ((t >> 8) + t) >> 8;
}

/* GdkPixbuf -> cairo image surface, the same conversion (and rounding)
   as GDK's own, whose helpers are deprecated in GTK4 */
static cairo_surface_t *
xtext_surface_from_pixbuf (GdkPixbuf *pixbuf)
{
	int w = gdk_pixbuf_get_width (pixbuf);
	int h = gdk_pixbuf_get_height (pixbuf);
	int n = gdk_pixbuf_get_n_channels (pixbuf);
	int sstride = gdk_pixbuf_get_rowstride (pixbuf);
	const guchar *src = gdk_pixbuf_read_pixels (pixbuf);
	cairo_surface_t *surf;
	guchar *dst;
	int dstride, x, y;

	surf = cairo_image_surface_create (n == 4 ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, w, h);
	cairo_surface_flush (surf);
	dst = cairo_image_surface_get_data (surf);
	dstride = cairo_image_surface_get_stride (surf);

	for (y = 0; y < h; y++)
	{
		const guchar *s = src + y * sstride;
		guint32 *d = (guint32 *) (dst + y * dstride);

		for (x = 0; x < w; x++, s += n)
		{
			if (n == 4)
			{
				guint a = s[3];
				d[x] = (a << 24) | (premul (s[0], a) << 16) | (premul (s[1], a) << 8) | premul (s[2], a);
			}
			else
				d[x] = 0xff000000u | (s[0] << 16) | (s[1] << 8) | s[2];
		}
	}
	cairo_surface_mark_dirty (surf);
	return surf;
}

/* the pixbuf is tiled from the widget's top-left corner, like GDK_TILED
   with ts_origin 0,0 was */
void
gtk_xtext_set_background (GtkXText * xtext, GdkPixbuf * pixbuf)
{
	if (xtext->pixmap)
	{
		cairo_pattern_destroy (xtext->pixmap);
		xtext->pixmap = NULL;
	}

	dontscroll (xtext->buffer);

	if (pixbuf)
	{
		cairo_surface_t *surf = xtext_surface_from_pixbuf (pixbuf);
		xtext->pixmap = cairo_pattern_create_for_surface (surf);
		cairo_pattern_set_extend (xtext->pixmap, CAIRO_EXTEND_REPEAT);
		cairo_surface_destroy (surf);
	}

	xtext->backing_stale = TRUE;
	xtext_queue_draw (xtext);
}

void
gtk_xtext_save (GtkXText * xtext, int fh)
{
	textentry *ent;
	int newlen;
	char *buf;

	ent = xtext->buffer->text_first;
	while (ent)
	{
		buf = gtk_xtext_strip_color (ent->str, ent->str_len, NULL,
											  &newlen, NULL, FALSE);
		write (fh, buf, newlen);
		write (fh, "\n", 1);
		g_free (buf);
		ent = ent->next;
	}
}

/* count how many lines 'ent' will take (with wraps) */

static int
gtk_xtext_lines_taken (xtext_buffer *buf, textentry * ent)
{
	unsigned char *str;
	int indent, len;
	int win_width;

	g_slist_free (ent->sublines);
	ent->sublines = NULL;
	win_width = buf->window_width - MARGIN;

	if (win_width >= ent->indent + ent->str_width)
	{
		ent->sublines = g_slist_append (ent->sublines, GINT_TO_POINTER (ent->str_len));
		return 1;
	}

	indent = ent->indent;
	str = ent->str;

	do
	{
		len = find_next_wrap (buf->xtext, ent, str, win_width, indent);
		ent->sublines = g_slist_append (ent->sublines, GINT_TO_POINTER (str + len - ent->str));
		indent = buf->indent;
		str += len;
	}
	while (str < ent->str + ent->str_len);

	return g_slist_length (ent->sublines);
}

/* Calculate number of actual lines (with wraps), to set gtk_adjustment_get_lower (adj). *
 * This should only be called when the window resizes.               */

static void
gtk_xtext_calc_lines (xtext_buffer *buf, int fire_signal)
{
	textentry *ent;
	int width;
	int height;
	int lines;

	height = nx_widget_get_height (GTK_WIDGET (buf->xtext));
	width = nx_widget_get_width (GTK_WIDGET (buf->xtext));
	width -= MARGIN;

	if (width < 30 || height < buf->xtext->fontsize || width < buf->indent + 30)
		return;

	lines = 0;
	ent = buf->text_first;
	while (ent)
	{
		lines += gtk_xtext_lines_taken (buf, ent);
		ent = ent->next;
	}

	buf->pagetop_ent = NULL;
	buf->num_lines = lines;
	gtk_xtext_adjustment_set (buf, fire_signal);
}

/* find the n-th line in the linked list, this includes wrap calculations */

static textentry *
gtk_xtext_nth (GtkXText *xtext, int line, int *subline)
{
	int lines = 0;
	textentry *ent;

	ent = xtext->buffer->text_first;

	/* -- optimization -- try to make a short-cut using the pagetop ent */
	if (xtext->buffer->pagetop_ent)
	{
		if (line == xtext->buffer->pagetop_line)
		{
			*subline = xtext->buffer->pagetop_subline;
			return xtext->buffer->pagetop_ent;
		}
		if (line > xtext->buffer->pagetop_line)
		{
			/* lets start from the pagetop instead of the absolute beginning */
			ent = xtext->buffer->pagetop_ent;
			lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
		}
		else if (line > xtext->buffer->pagetop_line - line)
		{
			/* move backwards from pagetop */
			ent = xtext->buffer->pagetop_ent;
			lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
			while (1)
			{
				if (lines <= line)
				{
					*subline = line - lines;
					return ent;
				}
				ent = ent->prev;
				if (!ent)
					break;
				lines -= g_slist_length (ent->sublines);
			}
			return NULL;
		}
	}
	/* -- end of optimization -- */

	while (ent)
	{
		lines += g_slist_length (ent->sublines);
		if (lines > line)
		{
			*subline = g_slist_length (ent->sublines) - (lines - line);
			return ent;
		}
		ent = ent->next;
	}
	return NULL;
}

/* render enta (or an inclusive range enta->entb) */


/* render a whole page/window, starting from 'startline': only from
   ::snapshot (gtk_xtext_draw). Everything else asks for it with
   gtk_xtext_render_page(), which just queues a draw. */

static void gtk_xtext_render_page_now (GtkXText * xtext);

/* the page changed: draw it on the next frame. The page top is worked out
   now too, since clicks and selections look rows up from it before the
   frame comes. */
static void
gtk_xtext_render_page (GtkXText * xtext)
{
	int startline, subline = 0;
	textentry *ent;

	if (!gtk_widget_get_realized (GTK_WIDGET (xtext)))
		return;
	startline = gtk_adjustment_get_value (xtext->adj);
	xtext->pixel_offset = (gtk_adjustment_get_value (xtext->adj) - startline) * xtext->fontsize;
	ent = startline > 0 ? gtk_xtext_nth (xtext, startline, &subline) : xtext->buffer->text_first;
	xtext->buffer->pagetop_ent = ent;
	xtext->buffer->pagetop_subline = subline;
	xtext->buffer->pagetop_line = startline;
	xtext_queue_draw (xtext);
}

static void
gtk_xtext_render_page_now (GtkXText * xtext)
{
	textentry *ent;
	int line;
	int lines_max;
	int width;
	int height;
	int subline;
	int startline = gtk_adjustment_get_value (xtext->adj);
	int pos;

	if(!gtk_widget_get_realized(GTK_WIDGET(xtext)))
	  return;

	if (xtext->buffer->indent < MARGIN)
		xtext->buffer->indent = MARGIN;	  /* 2 pixels is our left margin */

	width = nx_widget_get_allocated_width (GTK_WIDGET (xtext));
	height = nx_widget_get_allocated_height (GTK_WIDGET (xtext));

	if (width < 34 || height < xtext->fontsize || width < xtext->buffer->indent + 32)
		return;

	xtext->pixel_offset = (gtk_adjustment_get_value (xtext->adj) - startline) * xtext->fontsize;

	subline = line = 0;
	ent = xtext->buffer->text_first;

	if (startline > 0)
		ent = gtk_xtext_nth (xtext, startline, &subline);

	xtext->buffer->pagetop_ent = ent;
	xtext->buffer->pagetop_subline = subline;
	xtext->buffer->pagetop_line = startline;

	if (xtext->buffer->num_lines <= gtk_adjustment_get_page_size (xtext->adj))
		dontscroll (xtext->buffer);

	pos = gtk_adjustment_get_value (xtext->adj) * xtext->fontsize;
	xtext->buffer->last_pixel_pos = pos;

	width -= MARGIN;
	lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;

	while (ent)
	{
		gtk_xtext_reset (xtext, FALSE, TRUE);
		line += gtk_xtext_render_line (xtext, ent, line, lines_max,
												 subline, width);
		subline = 0;

		if (line >= lines_max)
			break;

		ent = ent->next;
	}

	line = (xtext->fontsize * line) - xtext->pixel_offset;
	/* fill any space below the last line with our background GC */
	xtext_draw_bg (xtext, 0, line, width + MARGIN, height - line);

	/* draw the separator line */
	gtk_xtext_draw_sep (xtext, -1);
}

void
gtk_xtext_refresh (GtkXText * xtext)
{
	if (gtk_widget_get_realized (GTK_WIDGET (xtext)))
	{
		gtk_xtext_render_page (xtext);
	}
}

static int
gtk_xtext_kill_ent (xtext_buffer *buffer, textentry *ent)
{
	int visible;

	/* Set visible to TRUE if this is the current buffer */
	/* and this ent shows up on the screen now */
	visible = buffer->xtext->buffer == buffer &&
				 gtk_xtext_check_ent_visibility (buffer->xtext, ent, 0);

	if (ent == buffer->pagetop_ent)
		buffer->pagetop_ent = NULL;

	if (ent == buffer->last_ent_start)
	{
		buffer->last_ent_start = ent->next;
		buffer->last_offset_start = 0;
	}

	if (ent == buffer->last_ent_end)
	{
		buffer->last_ent_start = NULL;
		buffer->last_ent_end = NULL;
	}

	if (buffer->marker_pos == ent)
	{
		/* Allow for "Marker line reset because exceeded scrollback limit. to appear. */
		buffer->marker_pos = ent->next;
		buffer->marker_state = MARKER_RESET_BY_KILL;
	}

	if (ent->marks)
	{
		gtk_xtext_search_textentry_del (buffer, ent);
	}

	g_slist_free_full (ent->slp, g_free);
	g_slist_free (ent->sublines);

	g_free (ent);
	return visible;
}

/* remove the topline from the list */

static void
gtk_xtext_remove_top (xtext_buffer *buffer)
{
	textentry *ent;

	ent = buffer->text_first;
	if (!ent)
		return;
	buffer->num_lines -= g_slist_length (ent->sublines);
	buffer->pagetop_line -= g_slist_length (ent->sublines);
	buffer->last_pixel_pos -= (g_slist_length (ent->sublines) * buffer->xtext->fontsize);
	buffer->text_first = ent->next;
	if (buffer->text_first)
		buffer->text_first->prev = NULL;
	else
		buffer->text_last = NULL;

	buffer->old_value -= g_slist_length (ent->sublines);
	if (buffer->xtext->buffer == buffer)	/* is it the current buffer? */
	{
		GtkAdjustment *a = buffer->xtext->adj;
		xtext_adj_set_silent (buffer->xtext, gtk_adjustment_get_value (a) - g_slist_length (ent->sublines),
									 gtk_adjustment_get_upper (a));
		buffer->xtext->select_start_adj -= g_slist_length (ent->sublines);
	}

	if (gtk_xtext_kill_ent (buffer, ent))
	{
		if (!buffer->xtext->add_io_tag)
		{
			/* remove scrolling events */
			if (buffer->xtext->io_tag)
			{
				g_source_remove (buffer->xtext->io_tag);
				buffer->xtext->io_tag = 0;
			}
			buffer->xtext->force_render = TRUE;
			buffer->xtext->add_io_tag = g_timeout_add (REFRESH_TIMEOUT * 2,
														(GSourceFunc)
														gtk_xtext_render_page_timeout,
														buffer->xtext);
		}
	}
}

static void
gtk_xtext_remove_bottom (xtext_buffer *buffer)
{
	textentry *ent;

	ent = buffer->text_last;
	if (!ent)
		return;
	buffer->num_lines -= g_slist_length (ent->sublines);
	buffer->text_last = ent->prev;
	if (buffer->text_last)
		buffer->text_last->next = NULL;
	else
		buffer->text_first = NULL;

	if (gtk_xtext_kill_ent (buffer, ent))
	{
		if (!buffer->xtext->add_io_tag)
		{
			/* remove scrolling events */
			if (buffer->xtext->io_tag)
			{
				g_source_remove (buffer->xtext->io_tag);
				buffer->xtext->io_tag = 0;
			}
			buffer->xtext->force_render = TRUE;
			buffer->xtext->add_io_tag = g_timeout_add (REFRESH_TIMEOUT * 2,
														(GSourceFunc)
														gtk_xtext_render_page_timeout,
														buffer->xtext);
		}
	}
}

/* If lines=0 => clear all */

void
gtk_xtext_clear (xtext_buffer *buf, int lines)
{
	textentry *next;
	int marker_reset = FALSE;

	if (lines != 0)
	{
		if (lines < 0)
		{
			/* delete lines from bottom */
			lines *= -1;
			while (lines)
			{
				if (buf->text_last == buf->marker_pos)
					marker_reset = TRUE;
				gtk_xtext_remove_bottom (buf);
				lines--;
			}
		}
		else
		{
			/* delete lines from top */
			while (lines)
			{
				if (buf->text_first == buf->marker_pos)
					marker_reset = TRUE;
				gtk_xtext_remove_top (buf);
				lines--;
			}
		}
	}
	else
	{
		/* delete all */
		if (buf->search_found)
			gtk_xtext_search_fini (buf);
		if (buf->xtext->auto_indent)
			buf->indent = MARGIN;
		buf->scrollbar_down = TRUE;
		buf->last_ent_start = NULL;
		buf->last_ent_end = NULL;
		buf->marker_pos = NULL;
		if (buf->text_first)
			marker_reset = TRUE;
		dontscroll (buf);

		while (buf->text_first)
		{
			next = buf->text_first->next;
			g_free (buf->text_first);
			buf->text_first = next;
		}
		buf->text_last = NULL;
	}

	if (buf->xtext->buffer == buf)
	{
		gtk_xtext_calc_lines (buf, TRUE);
		gtk_xtext_refresh (buf->xtext);
	} else
	{
		gtk_xtext_calc_lines (buf, FALSE);
	}

	if (marker_reset)
		buf->marker_state = MARKER_RESET_BY_CLEAR;
}

static gboolean
gtk_xtext_check_ent_visibility (GtkXText * xtext, textentry *find_ent, int add)
{
	textentry *ent;
	int lines;
	xtext_buffer *buf = xtext->buffer;
	int height;

	if (find_ent == NULL)
	{
		return FALSE;
	}

	height = nx_widget_get_height (GTK_WIDGET (xtext));

	ent = buf->pagetop_ent;
	/* If top line not completely displayed return FALSE */
	if (ent == find_ent && buf->pagetop_subline > 0)
	{
		return FALSE;
	}
	/* Loop through line positions looking for find_ent */
	lines = ((height + xtext->pixel_offset) / xtext->fontsize) + buf->pagetop_subline + add;
	while (ent)	
	{
		lines -= g_slist_length (ent->sublines);
		if (lines <= 0)
		{
			return FALSE;
		}
		if (ent == find_ent)
		{
			return TRUE;
		}
		ent = ent->next;
	}

	return FALSE;
}

void
gtk_xtext_check_marker_visibility (GtkXText * xtext)
{
	if (gtk_xtext_check_ent_visibility (xtext, xtext->buffer->marker_pos, 1))
		xtext->buffer->marker_seen = TRUE;
}

static void
gtk_xtext_unstrip_color (gint start, gint end, GSList *slp, GList **gl, gint maxo)
{
	gint off1, off2;
	GSList *cursl;
	offsets_t marks;
	offlen_t *meta;

	off1 = 0;
	cursl = slp;
	while (cursl)
	{
		meta = cursl->data;
		if (start < meta->len)
		{
			off1 = meta->off + start;
			break;
		}
		start -= meta->len;
		end -= meta->len;
		cursl = g_slist_next (cursl);
	}

	off2 = off1;
	while (cursl)
	{
		meta = cursl->data;
		if (end < meta->len)
		{
			off2 = meta->off + end;
			break;
		}
		end -= meta->len;
		cursl = g_slist_next (cursl);
	}
	if (!cursl)
	{
		off2 = maxo;
	}

	marks.o.start = off1;
	marks.o.end = off2;
	*gl = g_list_append (*gl, GUINT_TO_POINTER (marks.u));
}

/* Search a single textentry for occurrence(s) of search arg string */
static GList *
gtk_xtext_search_textentry (xtext_buffer *buf, textentry *ent)
{
	gchar *str;								/* text string to be searched */
	GList *gl = NULL;
	GSList *slp;
	gint lstr;

	if (buf->search_text == NULL)
	{
		return gl;
	}

	str = gtk_xtext_strip_color (ent->str, ent->str_len, buf->xtext->scratch_buffer,
										  &lstr, &slp, !buf->xtext->ignore_hidden);

	/* Regular-expression matching --- */
	if (buf->search_flags & regexp)
	{
		GMatchInfo *gmi;
		gint start, end;

		if (buf->search_re == NULL)
		{
			return gl;
		}
		g_regex_match (buf->search_re, str, 0, &gmi);
		while (g_match_info_matches (gmi))
		{
			g_match_info_fetch_pos (gmi, 0,  &start, &end);
			gtk_xtext_unstrip_color (start, end, slp, &gl, ent->str_len);
			g_match_info_next (gmi, NULL);
		}
		g_match_info_free (gmi);

	/* Non-regular-expression matching --- */
	} else {
		gchar *hay, *pos;
		gint lhay, off, len;
		gint match = buf->search_flags & case_match;

		hay = match? g_strdup (str): g_utf8_casefold (str, lstr);
		lhay = strlen (hay);

		for (pos = hay, len = lhay; len;
			  off += buf->search_lnee, pos = hay + off, len = lhay - off)
		{
			str = g_strstr_len (pos, len, buf->search_nee);
			if (str == NULL)
			{
				break;
			}
			off = str - hay;
			gtk_xtext_unstrip_color (off, off + buf->search_lnee,
											 slp, &gl, ent->str_len);
		}

		g_free (hay);
	}

	/* Common processing --- */
	g_slist_free_full (slp, g_free);
	return gl;
}

/* Add a list of found search results to an entry, maybe NULL */
static void
gtk_xtext_search_textentry_add (xtext_buffer *buf, textentry *ent, GList *gl, gboolean pre)
{
	ent->marks = gl;
	if (gl)
	{
		buf->search_found = (pre? g_list_prepend: g_list_append) (buf->search_found, ent);
		if (pre == FALSE && buf->hintsearch == NULL)
		{
			buf->hintsearch = ent;
		}
	}
}

/* Free all search information for a textentry */
static void
gtk_xtext_search_textentry_del (xtext_buffer *buf, textentry *ent)
{
	g_list_free (ent->marks);
	ent->marks = NULL;
	if (buf->cursearch && buf->cursearch->data == ent)
	{
		buf->cursearch = NULL;
		buf->curmark = NULL;
		buf->curdata.u = 0;
	}
	if (buf->pagetop_ent == ent)
	{
		buf->pagetop_ent = NULL;
	}
	if (buf->hintsearch == ent)
	{
		buf->hintsearch = NULL;
	}
	buf->search_found = g_list_remove (buf->search_found, ent);
}

/* Used only by glist_foreach */
static void
gtk_xtext_search_textentry_fini (gpointer entp, gpointer dummy)
{
	textentry *ent = entp;

	g_list_free (ent->marks);
	ent->marks = NULL;
}

/* Free all search information for all textentrys and the xtext_buffer */
static void
gtk_xtext_search_fini (xtext_buffer *buf)
{
	g_list_foreach (buf->search_found, gtk_xtext_search_textentry_fini, 0);
	g_list_free (buf->search_found);
	buf->search_found = NULL;
	g_free (buf->search_text);
	buf->search_text = NULL;
	g_free (buf->search_nee);
	buf->search_nee = NULL;
	buf->search_flags = 0;
	buf->cursearch = NULL;
	buf->curmark = NULL;
	/* but leave buf->curdata.u alone! */
	if (buf->search_re)
	{
		g_regex_unref (buf->search_re);
		buf->search_re = NULL;
	}
}

/* Returns TRUE if the base search information exists and is still okay to use */
static gboolean
gtk_xtext_search_init (xtext_buffer *buf, const gchar *text, gtk_xtext_search_flags flags, GError **perr)
{
	/* Of the five flags, backward and highlight_all do not need a new search */
	if (buf->search_found &&
		 strcmp (buf->search_text, text) == 0 &&
		 (buf->search_flags & case_match) == (flags & case_match) &&
		 (buf->search_flags & follow) == (flags & follow) &&
		 (buf->search_flags & regexp) == (flags & regexp))
	{
		return TRUE;
	}
	buf->hintsearch = buf->cursearch? buf->cursearch->data: NULL;
	gtk_xtext_search_fini (buf);
	buf->search_text = g_strdup (text);
	if (flags & regexp)
	{
		buf->search_re = g_regex_new (text, (flags & case_match)? 0: G_REGEX_CASELESS, 0, perr);
		if (perr && *perr)
		{
			return FALSE;
		}
	}
	else
	{
		if (flags & case_match)
		{
			buf->search_nee = g_strdup (text);
		}
		else
		{
			buf->search_nee = g_utf8_casefold (text, strlen (text));
		}
		buf->search_lnee = strlen (buf->search_nee);
	}
	buf->search_flags = flags;
	buf->cursearch = NULL;
	buf->curmark = NULL;
	/* but leave buf->curdata.u alone! */
	return FALSE;
}

#define BACKWARD (flags & backward)
#define FIRSTLAST(lp)  (BACKWARD? g_list_last(lp): g_list_first(lp))
#define NEXTPREVIOUS(lp) (BACKWARD? g_list_previous(lp): g_list_next(lp))
textentry *
gtk_xtext_search (GtkXText * xtext, const gchar *text, gtk_xtext_search_flags flags, GError **perr)
{
	textentry *ent = NULL;
	xtext_buffer *buf = xtext->buffer;
	GList *gl;

	if (buf->text_first == NULL)
	{
		return NULL;
	}

	/* If the text arg is NULL, one of these has been toggled: highlight follow */
	if (text == NULL)		/* Here on highlight or follow toggle */
	{
		gint oldfollow = buf->search_flags & follow;
		gint newfollow = flags & follow;

		/* If "Follow" has just been checked, search possible new textentries --- */
		if (newfollow && (newfollow != oldfollow))
		{
			gl = g_list_last (buf->search_found);
			ent = gl? gl->data: buf->text_first;
			for (; ent; ent = ent->next)
			{
				GList *gl;

				gl = gtk_xtext_search_textentry (buf, ent);
				gtk_xtext_search_textentry_add (buf, ent, gl, FALSE);
			}
		}
		buf->search_flags = flags;
		ent = buf->pagetop_ent;
	}

	/* if the text arg is "", the reset button has been clicked or Control-Shift-F has been hit */
	else if (text[0] == 0)		/* Let a null string do a reset. */
	{
		gtk_xtext_search_fini (buf);
	}

	/* If the text arg is neither NULL nor "", it's the search string */
	else
	{
		if (gtk_xtext_search_init (buf, text, flags, perr) == FALSE)	/* If a new search: */
		{
			if (perr && *perr)
			{
				return NULL;
			}
			for (ent = buf->text_first; ent; ent = ent->next)
			{
				GList *gl;

				gl = gtk_xtext_search_textentry (buf, ent);
				gtk_xtext_search_textentry_add (buf, ent, gl, TRUE);
			}
			buf->search_found = g_list_reverse (buf->search_found);
		}

		/* Now base search results are in place. */

		if (buf->search_found)
		{
			/* If we're in the midst of moving among found items */
			if (buf->cursearch)
			{
				ent = buf->cursearch->data;
				buf->curmark = NEXTPREVIOUS (buf->curmark);
				if (buf->curmark == NULL)
				{
					/* We've returned all the matches for this textentry. */
					buf->cursearch = NEXTPREVIOUS (buf->cursearch);
					if (buf->cursearch)
					{
						ent = buf->cursearch->data;
						buf->curmark = FIRSTLAST (ent->marks);
					}
					else	/* We've returned all the matches for all textentries */
					{
						ent = NULL;
					}
				}
			}

			/* If user changed the search, let's look starting where he was */
			else if (buf->hintsearch)
			{
				GList *mark;
				offsets_t last, this;
				/*
				 * If we already have a 'current' item from the last search, and if
				 * the first character of an occurrence on this line for this new search
				 * is within that former item, use the occurrence as current.
				 */
				ent = buf->hintsearch;
				last.u = buf->curdata.u;
				for (mark = ent->marks; mark; mark = mark->next)
				{
					this.u = GPOINTER_TO_UINT (mark->data);
					if (this.o.start >= last.o.start && this.o.start < last.o.end)
					break;
				}
				if (mark == NULL)
				{
					for (ent = buf->hintsearch; ent; ent = BACKWARD? ent->prev: ent->next)
						if (ent->marks)
							break;
					mark = ent? FIRSTLAST (ent->marks): NULL;
				}
				buf->cursearch = g_list_find (buf->search_found, ent);
				buf->curmark = mark;
			}

			/* This is a fresh search */
			else
			{
				buf->cursearch = FIRSTLAST (buf->search_found);
				ent = buf->cursearch->data;
				buf->curmark = FIRSTLAST (ent->marks);
			}
			buf->curdata.u = (buf->curmark)? GPOINTER_TO_UINT (buf->curmark->data): 0;
		}
	}
	buf->hintsearch = ent;

	if (!gtk_xtext_check_ent_visibility (xtext, ent, 1))
	{
		GtkAdjustment *adj = xtext->adj;
		float value;

		buf->pagetop_ent = NULL;
		for (value = 0, ent = buf->text_first;
			  ent && ent != buf->hintsearch; ent = ent->next)
		{
			value += g_slist_length (ent->sublines);
		}
		if (value > gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj))
		{
			value = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj);
		}
		else if ((flags & backward)  && ent)
		{
			value -= gtk_adjustment_get_page_size (adj) - g_slist_length (ent->sublines);
			if (value < 0)
			{
				value = 0;
			}
		}
		gtk_adjustment_set_value (adj, value);
	}

	xtext_queue_draw (xtext);

	return buf->hintsearch;
}
#undef BACKWARD
#undef FIRSTLAST
#undef NEXTPREVIOUS

static int
gtk_xtext_render_page_timeout (GtkXText * xtext)
{
	GtkAdjustment *adj = xtext->adj;

	xtext->add_io_tag = 0;

	/* less than a complete page? */
	if (xtext->buffer->num_lines <= gtk_adjustment_get_page_size (adj))
	{
		xtext->buffer->old_value = 0;
		xtext_adj_set_silent (xtext, 0, gtk_adjustment_get_upper (adj));
		gtk_xtext_render_page (xtext);
	} else if (xtext->buffer->scrollbar_down)
	{
		g_signal_handler_block (xtext->adj, xtext->vc_signal_tag);
		gtk_xtext_adjustment_set (xtext->buffer, FALSE);
		gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj));
		g_signal_handler_unblock (xtext->adj, xtext->vc_signal_tag);
		xtext->buffer->old_value = gtk_adjustment_get_value (adj);
		gtk_xtext_render_page (xtext);
	} else
	{
		gtk_xtext_adjustment_set (xtext->buffer, TRUE);
		if (xtext->force_render)
		{
			xtext->force_render = FALSE;
			gtk_xtext_render_page (xtext);
		}
	}

	return 0;
}

/* append a textentry to our linked list */

static void
gtk_xtext_append_entry (xtext_buffer *buf, textentry * ent, time_t stamp)
{
	int i;

	/* we don't like tabs */
	i = 0;
	while (i < ent->str_len)
	{
		if (ent->str[i] == '\t')
			ent->str[i] = ' ';
		i++;
	}

	ent->stamp = stamp;
	if (stamp == 0)
		ent->stamp = time (0);
	ent->slp = NULL;
	ent->str_width = gtk_xtext_text_width_ent (buf->xtext, ent);
	ent->mark_start = -1;
	ent->mark_end = -1;
	ent->next = NULL;
	ent->marks = NULL;

	if (ent->indent < MARGIN)
		ent->indent = MARGIN;	  /* 2 pixels is the left margin */

	/* append to our linked list */
	if (buf->text_last)
		buf->text_last->next = ent;
	else
		buf->text_first = ent;
	ent->prev = buf->text_last;
	buf->text_last = ent;

	ent->sublines = NULL;
	buf->num_lines += gtk_xtext_lines_taken (buf, ent);

	if ((buf->marker_pos == NULL || buf->marker_seen) && (buf->xtext->buffer != buf || 
		!nx_widget_window_is_active (GTK_WIDGET (buf->xtext))))
	{
		buf->marker_pos = ent;
		buf->marker_state = MARKER_IS_SET;
		dontscroll (buf); /* force scrolling off */
		buf->marker_seen = FALSE;
	}

	if (buf->xtext->max_lines > 2 && buf->xtext->max_lines < buf->num_lines)
	{
		gtk_xtext_remove_top (buf);
	}

	if (buf->xtext->buffer == buf)
	{
		/* this could be improved */
		if ((buf->num_lines - 1) <= gtk_adjustment_get_page_size (buf->xtext->adj))
			dontscroll (buf);

		if (!buf->scrollbar_down)
		{
			buf->xtext->jump_unread++;
			if (buf->xtext->jump_button)
				xtext_queue_draw (buf->xtext);
		}

		if (!buf->xtext->add_io_tag)
		{
			/* remove scrolling events */
			if (buf->xtext->io_tag)
			{
				g_source_remove (buf->xtext->io_tag);
				buf->xtext->io_tag = 0;
			}
			buf->xtext->add_io_tag = g_timeout_add (REFRESH_TIMEOUT * 2,
															(GSourceFunc)
															gtk_xtext_render_page_timeout,
															buf->xtext);
		}
	}
	if (buf->scrollbar_down)
	{
		buf->old_value = buf->num_lines - gtk_adjustment_get_page_size (buf->xtext->adj);
		if (buf->old_value < 0)
			buf->old_value = 0;
	}
	if (buf->search_flags & follow)
	{
		GList *gl;

		gl = gtk_xtext_search_textentry (buf, ent);
		gtk_xtext_search_textentry_add (buf, ent, gl, FALSE);
	}
}

/* the main two public functions */

void
gtk_xtext_append_indent (xtext_buffer *buf,
								 unsigned char *left_text, int left_len,
								 unsigned char *right_text, int right_len,
								 time_t stamp)
{
	textentry *ent;
	unsigned char *str;
	int space;
	int tempindent;
	int left_width;

	if (left_len == -1)
		left_len = strlen (left_text);

	if (right_len == -1)
		right_len = strlen (right_text);

	if (left_len + right_len + 2 >= sizeof (buf->xtext->scratch_buffer))
		right_len = sizeof (buf->xtext->scratch_buffer) - left_len - 2;

	if (right_text[right_len-1] == '\n')
		right_len--;

	ent = g_malloc (left_len + right_len + 2 + sizeof (textentry));
	str = (unsigned char *) ent + sizeof (textentry);

	if (left_len)
		memcpy (str, left_text, left_len);
	str[left_len] = ' ';
	if (right_len)
		memcpy (str + left_len + 1, right_text, right_len);
	str[left_len + 1 + right_len] = 0;

	left_width = gtk_xtext_text_width (buf->xtext, left_text, left_len);

	ent->left_len = left_len;
	ent->str = str;
	ent->str_len = left_len + 1 + right_len;
	ent->indent = (buf->indent - left_width) - buf->xtext->space_width;

	/* This is copied into the scratch buffer later, double check math */
	g_assert (ent->str_len < sizeof (buf->xtext->scratch_buffer));

	if (buf->time_stamp)
		space = buf->xtext->stamp_width;
	else
		space = 0;

	/* do we need to auto adjust the separator position? */
	if (buf->xtext->auto_indent &&
		 buf->indent < buf->xtext->max_auto_indent &&
		 ent->indent < MARGIN + space)
	{
		tempindent = MARGIN + space + buf->xtext->space_width + left_width;

		if (tempindent > buf->indent)
			buf->indent = tempindent;

		if (buf->indent > buf->xtext->max_auto_indent)
			buf->indent = buf->xtext->max_auto_indent;

		gtk_xtext_fix_indent (buf);
		gtk_xtext_recalc_widths (buf, FALSE);

		ent->indent = (buf->indent - left_width) - buf->xtext->space_width;
		buf->xtext->force_render = TRUE;
	}

	gtk_xtext_append_entry (buf, ent, stamp);
}

void
gtk_xtext_append (xtext_buffer *buf, unsigned char *text, int len, time_t stamp)
{
	textentry *ent;
	gboolean truncate = FALSE;

	if (len == -1)
		len = strlen (text);

	if (text[len-1] == '\n')
		len--;

	if (len >= sizeof (buf->xtext->scratch_buffer))
	{
		len = sizeof (buf->xtext->scratch_buffer) - 1;
		truncate = TRUE;
	}

	ent = g_malloc (len + 1 + sizeof (textentry));
	ent->str = (unsigned char *) ent + sizeof (textentry);
	ent->str_len = len;
	if (len)
	{
		if (!truncate)
		{
			memcpy (ent->str, text, len);
			ent->str[len] = '\0';
		}
		else
		{
			safe_strcpy (ent->str, text, sizeof (buf->xtext->scratch_buffer));
			ent->str_len = strlen (ent->str);
		}
	}
	ent->indent = 0;
	ent->left_len = -1;

	gtk_xtext_append_entry (buf, ent, stamp);
}

gboolean
gtk_xtext_is_empty (xtext_buffer *buf)
{
	return buf->text_first == NULL;
}


/* how many entries of search_area match out's search, without copying */
int
gtk_xtext_lastlog_count (xtext_buffer *out, xtext_buffer *search_area)
{
	textentry *ent;
	int matches = 0;
	GList *gl;

	for (ent = search_area->text_first; ent; ent = ent->next)
	{
		gl = gtk_xtext_search_textentry (out, ent);
		if (gl)
		{
			matches++;
			g_list_free (gl);
		}
	}
	return matches;
}

int
gtk_xtext_lastlog (xtext_buffer *out, xtext_buffer *search_area)
{
	textentry *ent;
	int matches;
	GList *gl;

	ent = search_area->text_first;
	matches = 0;

	while (ent)
	{
		gl = gtk_xtext_search_textentry (out, ent);
		if (gl)
		{
			matches++;
			/* copy the text over */
			if (search_area->xtext->auto_indent)
			{
				gtk_xtext_append_indent (out, ent->str, ent->left_len,
												 ent->str + ent->left_len + 1,
												 ent->str_len - ent->left_len - 1, 0);
			}
			else
			{
				gtk_xtext_append (out, ent->str, ent->str_len, 0);
			}

			if (out->text_last)
			{
				out->text_last->stamp = ent->stamp;
				gtk_xtext_search_textentry_add (out, out->text_last, gl, TRUE);
			}
		}
		ent = ent->next;
	}
	out->search_found = g_list_reverse (out->search_found);

	return matches;
}

void
gtk_xtext_foreach (xtext_buffer *buf, GtkXTextForeach func, void *data)
{
	textentry *ent = buf->text_first;

	while (ent)
	{
		(*func) (buf->xtext, ent->str, data);
		ent = ent->next;
	}
}

void
gtk_xtext_set_indent (GtkXText *xtext, gboolean indent)
{
	xtext->auto_indent = indent;
}

void
gtk_xtext_set_max_indent (GtkXText *xtext, int max_auto_indent)
{
	xtext->max_auto_indent = max_auto_indent;
}

void
gtk_xtext_set_max_lines (GtkXText *xtext, int max_lines)
{
	xtext->max_lines = max_lines;
}

void
gtk_xtext_set_show_marker (GtkXText *xtext, gboolean show_marker)
{
	xtext->marker = show_marker;
}

void
gtk_xtext_set_show_separator (GtkXText *xtext, gboolean show_separator)
{
	xtext->separator = show_separator;
}

void
gtk_xtext_set_thin_separator (GtkXText *xtext, gboolean thin_separator)
{
	xtext->thinline = thin_separator;
}

void
gtk_xtext_set_time_stamp (xtext_buffer *buf, gboolean time_stamp)
{
	buf->time_stamp = time_stamp;
}

void
gtk_xtext_set_urlcheck_function (GtkXText *xtext, int (*urlcheck_function) (GtkWidget *, char *))
{
	xtext->urlcheck_function = urlcheck_function;
}

void
gtk_xtext_set_wordwrap (GtkXText *xtext, gboolean wordwrap)
{
	xtext->wordwrap = wordwrap;
}

void
gtk_xtext_set_marker_last (session *sess)
{
	xtext_buffer *buf = sess->res->buffer;

	buf->marker_pos = buf->text_last;
	buf->marker_state = MARKER_IS_SET;
}

void
gtk_xtext_reset_marker_pos (GtkXText *xtext)
{
	if (xtext->buffer->marker_pos)
	{
		xtext->buffer->marker_pos = NULL;
		dontscroll (xtext->buffer); /* force scrolling off */
		gtk_xtext_render_page (xtext);
		xtext->buffer->marker_state = MARKER_RESET_MANUALLY;
	}
}

int
gtk_xtext_moveto_marker_pos (GtkXText *xtext)
{
	gdouble value = 0;
	xtext_buffer *buf = xtext->buffer;
	textentry *ent = buf->text_first;
	GtkAdjustment *adj = xtext->adj;

	if (buf->marker_pos == NULL)
		return buf->marker_state;

	if (gtk_xtext_check_ent_visibility (xtext, buf->marker_pos, 1) == FALSE)
	{
		while (ent)
		{
			if (ent == buf->marker_pos)
				break;
			value += g_slist_length (ent->sublines);
			ent = ent->next;
		}
		if (value >= gtk_adjustment_get_value (adj) && value < gtk_adjustment_get_value (adj) + gtk_adjustment_get_page_size (adj))
			return MARKER_IS_SET;
		value -= gtk_adjustment_get_page_size (adj) / 2;
		if (value < 0)
			value = 0;
		if (value > gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj))
			value = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj);
		gtk_adjustment_set_value (adj, value);
		gtk_xtext_render_page (xtext);
	}

	/* If we previously lost marker position to scrollback limit -- */
	if (buf->marker_pos == buf->text_first &&
		 buf->marker_state == MARKER_RESET_BY_KILL)
		return MARKER_RESET_BY_KILL;
	else
		return MARKER_IS_SET;
}

void
gtk_xtext_buffer_show (GtkXText *xtext, xtext_buffer *buf, int render)
{
	int w, h;

	buf->xtext = xtext;

	if (xtext->buffer == buf)
		return;

	xtext->jump_unread = 0;	/* the count was for the old buffer */
	xtext->jump_hover = FALSE;

/*printf("text_buffer_show: xtext=%p buffer=%p\n", xtext, buf);*/

	if (xtext->add_io_tag)
	{
		g_source_remove (xtext->add_io_tag);
		xtext->add_io_tag = 0;
	}

	if (xtext->io_tag)
	{
		g_source_remove (xtext->io_tag);
		xtext->io_tag = 0;
	}

	if (!gtk_widget_get_realized (GTK_WIDGET (xtext)))
		gtk_widget_realize (GTK_WIDGET (xtext));

	h = nx_widget_get_height (GTK_WIDGET (xtext));
	w = nx_widget_get_width (GTK_WIDGET (xtext));

	/* after a font change */
	if (buf->needs_recalc)
	{
		buf->needs_recalc = FALSE;
		gtk_xtext_recalc_widths (buf, TRUE);
	}

	/* now change to the new buffer */
	xtext->buffer = buf;
	dontscroll (buf);	/* force scrolling off */
	{
		gdouble value = buf->old_value;
		gdouble upper = buf->num_lines;
		gdouble page = gtk_adjustment_get_page_size (xtext->adj);

		/* if the scrollbar was down, keep it down */
		if (xtext->buffer->scrollbar_down && value < upper - page)
			value = upper - page;

		if (upper == 0)
			upper = 1;
		/* sanity check */
		else if (value > upper - page)
		{
			value = upper - page;
			if (value < 0)
				value = 0;
		}
		xtext_adj_set_silent (xtext, value, upper);
	}

	if (render)
	{
		/* did the window change size since this buffer was last shown? */
		if (buf->window_width != w)
		{
			buf->window_width = w;
			buf->window_height = h;
			gtk_xtext_calc_lines (buf, FALSE);
			if (buf->scrollbar_down)
				gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj) -
												  gtk_adjustment_get_page_size (xtext->adj));
		} else if (buf->window_height != h)
		{
			buf->window_height = h;
			buf->pagetop_ent = NULL;
			if (buf->scrollbar_down)
				xtext_adj_set_silent (xtext, gtk_adjustment_get_upper (xtext->adj),
											 gtk_adjustment_get_upper (xtext->adj));
			gtk_xtext_adjustment_set (buf, FALSE);
		}

		gtk_xtext_render_page (xtext);
		g_signal_emit_by_name (xtext->adj, "changed");
	}
}

xtext_buffer *
gtk_xtext_buffer_new (GtkXText *xtext)
{
	xtext_buffer *buf;

	buf = g_new0 (xtext_buffer, 1);
	buf->old_value = -1;
	buf->xtext = xtext;
	buf->scrollbar_down = TRUE;
	buf->indent = xtext->space_width * 2;
	dontscroll (buf);

	return buf;
}

void
gtk_xtext_buffer_free (xtext_buffer *buf)
{
	textentry *ent, *next;

	if (buf->xtext->buffer == buf)
		buf->xtext->buffer = buf->xtext->orig_buffer;

	if (buf->xtext->selection_buffer == buf)
		buf->xtext->selection_buffer = NULL;

	if (buf->search_found)
	{
		gtk_xtext_search_fini (buf);
	}

	ent = buf->text_first;
	while (ent)
	{
		next = ent->next;
		g_free (ent);
		ent = next;
	}

	g_free (buf);
}
