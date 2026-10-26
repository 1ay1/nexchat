/* nexchat
 * Copyright (C) 2026 nexchat contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Sharing a file: drop it on the chat (or paste an image) and it goes to
   the file host the server itself offers (soju.im/FILEHOST: soju and
   other bouncers have one), never anywhere else. The link it gets back
   is put in the input box, to send or not. Logged in the way the server
   knows us: the SASL PLAIN user and password, over https only. */

#include <string.h>

#include "fe-gtk.h"

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/servlist.h"
#include "../common/httpget.h"
#include "../common/text.h"
#include "gtkutil.h"
#include "maingui.h"
#include "upload.h"

#define MAX_UPLOAD (64 * 1024 * 1024)

typedef struct
{
	session *sess;
	char *name;
} upload;

gboolean
upload_available (session *sess)
{
	return sess && sess->server && sess->server->filehost;
}

/* tests: NEXCHAT_UPLOAD=path uploads it in the front tab once the server
   has said where (as a drop would) */
static gboolean
test_upload_cb (gpointer d)
{
	GFile *f;

	if (!upload_available (current_sess))
		return G_SOURCE_CONTINUE;
	f = g_file_new_for_path (g_getenv ("NEXCHAT_UPLOAD"));
	upload_file (current_sess, f);
	g_object_unref (f);
	return G_SOURCE_REMOVE;
}

void
upload_test_init (void)
{
	if (g_getenv ("NEXCHAT_UPLOAD"))
		g_timeout_add (500, test_upload_cb, NULL);
}

static void
uploaded (httpget_result *res, gpointer data)
{
	upload *u = data;

	if (is_session (u->sess))
	{
		if (!res->error && res->status == 201 && res->final_url)
		{
			GtkWidget *entry = u->sess->gui->input_box;
			const char *had = SPELL_ENTRY_GET_TEXT (entry);
			char *text;

			/* after what's typed already, with a space between */
			text = g_strdup_printf ("%s%s%s", had, *had && !g_str_has_suffix (had, " ") ? " " : "",
											res->final_url);
			if (u->sess == current_sess)
			{
				SPELL_ENTRY_SET_TEXT (entry, text);
				SPELL_ENTRY_SET_POS (entry, -1);
				gtk_widget_grab_focus (entry);
			}
			else
			{
				g_free (u->sess->res->input_text);
				u->sess->res->input_text = g_strdup (text);
			}
			PrintTextf (u->sess, _("*\t%s uploaded: %s\n"), u->name, res->final_url);
			g_free (text);
		}
		else
			PrintTextf (u->sess, _("*\tCouldn't upload %s: %s\n"), u->name,
							res->error ? res->error : _("no link came back"));
	}
	g_free (u->name);
	g_free (u);
}

/* HTTP Basic with what we log in to the server with (SASL PLAIN), if we
   do; a bouncer knows us by that */
static char *
upload_auth (server *serv)
{
	ircnet *net = serv->network;
	const char *user;
	char *pair, *b64, *auth;

	if (!serv->password[0] || !g_str_has_prefix (serv->filehost, "https://"))
		return NULL;
	if (net && net->user && !(net->flags & FLAG_USE_GLOBAL))
		user = net->user;
	else
		user = prefs.hex_irc_user_name;
	pair = g_strdup_printf ("%s:%s", user, serv->password);
	b64 = g_base64_encode ((guchar *) pair, strlen (pair));
	auth = g_strdup_printf ("Basic %s", b64);
	g_free (pair);
	g_free (b64);
	return auth;
}

void
upload_bytes (session *sess, GBytes *data, const char *name, const char *type)
{
	upload *u;
	char *auth;

	if (!upload_available (sess))
		return;
	if (g_bytes_get_size (data) > MAX_UPLOAD)
	{
		PrintTextf (sess, _("*\t%s is too big to upload (64 MB at most)\n"), name);
		return;
	}
	u = g_new0 (upload, 1);
	u->sess = sess;
	u->name = g_strdup (name);
	PrintTextf (sess, _("*\tUploading %s...\n"), name);
	auth = upload_auth (sess->server);
	httppost (sess->server->filehost, data, type, name, auth, NULL, uploaded, u);
	g_free (auth);
}

static void
pasted_texture (GObject *src, GAsyncResult *ar, gpointer data)
{
	session *sess = data;
	GdkTexture *t = gdk_clipboard_read_texture_finish (GDK_CLIPBOARD (src), ar, NULL);
	GBytes *png;
	char *name;
	GDateTime *now;

	if (!t)
		return;
	if (is_session (sess))
	{
		png = gdk_texture_save_to_png_bytes (t);
		now = g_date_time_new_now_local ();
		name = g_date_time_format (now, "pasted-%Y%m%d-%H%M%S.png");
		upload_bytes (sess, png, name, "image/png");
		g_free (name);
		g_date_time_unref (now);
		g_bytes_unref (png);
	}
	g_object_unref (t);
}

/* Cmd/Ctrl+V in the input box with an image on the clipboard (a
   screenshot, a picture copied from a browser) and no text: upload it.
   Anything else pastes as always. TRUE: taken care of. */
gboolean
upload_paste (session *sess, GtkWidget *entry)
{
	GdkClipboard *cb = gtk_widget_get_clipboard (entry);
	GdkContentFormats *f = gdk_clipboard_get_formats (cb);

	if (!upload_available (sess) || !f)
		return FALSE;
	if (gdk_content_formats_contain_gtype (f, G_TYPE_STRING) ||
		 gdk_content_formats_contain_mime_type (f, "text/plain") ||
		 gdk_content_formats_contain_mime_type (f, "text/plain;charset=utf-8"))
		return FALSE;
	if (!gdk_content_formats_contain_gtype (f, GDK_TYPE_TEXTURE))
		return FALSE;
	gdk_clipboard_read_texture_async (cb, NULL, pasted_texture, sess);
	return TRUE;
}

void
upload_file (session *sess, GFile *file)
{
	char *path = g_file_get_path (file), *base, *type = NULL, *contents;
	gsize len;
	GBytes *b;

	if (!path)
		return;
	base = g_path_get_basename (path);
	if (!g_file_get_contents (path, &contents, &len, NULL))
	{
		PrintTextf (sess, _("*\tCouldn't read %s\n"), base);
		g_free (base);
		g_free (path);
		return;
	}
	{
		gboolean uncertain;
		char *ct = g_content_type_guess (base, (guchar *) contents, MIN (len, 4096), &uncertain);
		type = g_content_type_get_mime_type (ct);
		g_free (ct);
		/* a bare family ("text/") isn't a type: say what it most likely is */
		if (!type || strchr (type, '*'))
		{
			gboolean text = (type && g_str_has_prefix (type, "text/")) ||
								 g_utf8_validate (contents, MIN (len, 4096), NULL);
			g_free (type);
			type = g_strdup (text ? "text/plain; charset=utf-8" : "application/octet-stream");
		}
	}
	b = g_bytes_new_take (contents, len);
	upload_bytes (sess, b, base, type ? type : "application/octet-stream");
	g_bytes_unref (b);
	g_free (type);
	g_free (base);
	g_free (path);
}
