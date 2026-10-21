/* nexchat TUI — link previews: fetch an image link and decode it to
   pixels the UI draws as half-block cells (fe-gtk/urlpreview.c's job for
   the terminal). gdk-pixbuf decodes when it's there; without it the
   preview just says it can't. */

#include "config.h"
#include <string.h>
#include <glib.h>

#include "../common/hexchat.h"
#include "../common/httpget.h"
#include "bridge.h"

#ifdef HAVE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#define MAX_IMAGE (8 * 1024 * 1024)

typedef struct
{
	int cols, rows;
	nx_image_done done;
	void *ud;
} fetch;

int
nx_core_url_is_image (const char *url)
{
	static const char *ext[] = { ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", NULL };
	char *lower, *q;
	int i, yes = 0;

	if (!url || (g_ascii_strncasecmp (url, "http://", 7) && g_ascii_strncasecmp (url, "https://", 8)))
		return 0;
	lower = g_ascii_strdown (url, -1);
	if ((q = strpbrk (lower, "?#")))
		*q = 0;
	for (i = 0; ext[i] && !yes; i++)
		yes = g_str_has_suffix (lower, ext[i]);
	g_free (lower);
	return yes;
}

static void
fetched (httpget_result *res, gpointer data)
{
	fetch *f = data;

	if (!res->body || res->truncated || res->status != 200 || !res->content_type ||
		 !g_str_has_prefix (res->content_type, "image/"))
	{
		f->done (f->ud, 0, 0, NULL, res->error ? res->error
					: res->truncated ? "too big" : res->status != 200 ? "the site said no" : "not an image");
		g_free (f);
		return;
	}
#ifdef HAVE_GDK_PIXBUF
	{
		GInputStream *in = g_memory_input_stream_new_from_bytes (res->body);
		GError *err = NULL;
		/* each cell is one pixel wide and two tall (a half block) */
		GdkPixbuf *pb = gdk_pixbuf_new_from_stream_at_scale (in, f->cols, f->rows * 2, TRUE, NULL, &err);
		g_object_unref (in);
		if (!pb)
		{
			f->done (f->ud, 0, 0, NULL, err ? err->message : "can't read the image");
			g_clear_error (&err);
		}
		else
		{
			const int w = gdk_pixbuf_get_width (pb), h = gdk_pixbuf_get_height (pb);
			const int n = gdk_pixbuf_get_n_channels (pb), stride = gdk_pixbuf_get_rowstride (pb);
			const gboolean alpha = gdk_pixbuf_get_has_alpha (pb);
			const guchar *px = gdk_pixbuf_read_pixels (pb);
			guchar *rgb = g_malloc ((gsize) w * h * 3);
			int x, y;

			for (y = 0; y < h; y++)
				for (x = 0; x < w; x++)
				{
					const guchar *p = px + y * stride + x * n;
					guchar *o = rgb + (y * w + x) * 3;
					/* transparent over a mid grey, so it reads on light and dark */
					const int a = alpha ? p[3] : 255;
					o[0] = (guchar) ((p[0] * a + 128 * (255 - a)) / 255);
					o[1] = (guchar) ((p[1] * a + 128 * (255 - a)) / 255);
					o[2] = (guchar) ((p[2] * a + 128 * (255 - a)) / 255);
				}
			f->done (f->ud, w, h, rgb, NULL);
			g_free (rgb);
			g_object_unref (pb);
		}
	}
#else
	f->done (f->ud, 0, 0, NULL, "built without gdk-pixbuf");
#endif
	g_free (f);
}

void
nx_core_image_fetch (const char *url, int cols, int rows, nx_image_done done, void *ud)
{
	fetch *f = g_new0 (fetch, 1);

	f->cols = MAX (1, cols);
	f->rows = MAX (1, rows);
	f->done = done;
	f->ud = ud;
	httpget (url, MAX_IMAGE, NULL, fetched, f);
}
