/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Windows tray icon for GTK4: Shell_NotifyIcon on a hidden message
   window. Left click runs activate, right click asks for the menu, which
   comes up as a native Win32 popup menu built from the nx_menu (so it
   looks and behaves like every other tray menu on Windows). */

#include <windows.h>
#include <shellapi.h>
#include <string.h>

#include "tray-backend.h"

#define WM_NX_TRAY (WM_APP + 1)
#define NX_TRAY_ID 1
/* menu item ids start here; index into the flattened item list */
#define NX_CMD_BASE 1000

struct _tray_backend
{
	HWND hwnd;
	NOTIFYICONDATAW nid;
	HICON hicon;
	GdkPixbuf *icon;
	tray_backend_callbacks cb;
	gpointer userdata;
	gboolean added;
	UINT taskbar_created;	/* explorer restarted: add the icon again */
	GPtrArray *cmds;			/* nx_item * by command id - NX_CMD_BASE */
};

/* a GdkPixbuf as an HICON (32-bit with alpha) */
static HICON
pixbuf_to_hicon (GdkPixbuf *pb)
{
	int w, h, x, y, rs, nch;
	const guchar *src;
	BITMAPV5HEADER bi;
	HBITMAP color, mask;
	ICONINFO ii;
	guchar *bits;
	HDC dc;
	HICON icon;

	if (!pb)
		return NULL;
	pb = gdk_pixbuf_scale_simple (pb, GetSystemMetrics (SM_CXSMICON),
											GetSystemMetrics (SM_CYSMICON), GDK_INTERP_BILINEAR);
	w = gdk_pixbuf_get_width (pb);
	h = gdk_pixbuf_get_height (pb);
	rs = gdk_pixbuf_get_rowstride (pb);
	nch = gdk_pixbuf_get_n_channels (pb);
	src = gdk_pixbuf_read_pixels (pb);

	memset (&bi, 0, sizeof bi);
	bi.bV5Size = sizeof bi;
	bi.bV5Width = w;
	bi.bV5Height = -h;	/* top-down */
	bi.bV5Planes = 1;
	bi.bV5BitCount = 32;
	bi.bV5Compression = BI_BITFIELDS;
	bi.bV5RedMask = 0x00ff0000;
	bi.bV5GreenMask = 0x0000ff00;
	bi.bV5BlueMask = 0x000000ff;
	bi.bV5AlphaMask = 0xff000000;
	dc = GetDC (NULL);
	color = CreateDIBSection (dc, (BITMAPINFO *) &bi, DIB_RGB_COLORS, (void **) &bits, NULL, 0);
	ReleaseDC (NULL, dc);
	if (!color)
	{
		g_object_unref (pb);
		return NULL;
	}
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			const guchar *p = src + y * rs + x * nch;
			guchar *d = bits + (y * w + x) * 4;
			guchar a = nch == 4 ? p[3] : 255;
			/* premultiplied BGRA */
			d[0] = p[2] * a / 255;
			d[1] = p[1] * a / 255;
			d[2] = p[0] * a / 255;
			d[3] = a;
		}
	mask = CreateBitmap (w, h, 1, 1, NULL);
	ii.fIcon = TRUE;
	ii.xHotspot = ii.yHotspot = 0;
	ii.hbmMask = mask;
	ii.hbmColor = color;
	icon = CreateIconIndirect (&ii);
	DeleteObject (color);
	DeleteObject (mask);
	g_object_unref (pb);
	return icon;
}

static void
tray_add (tray_backend *tb)
{
	tb->nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
	tb->added = Shell_NotifyIconW (NIM_ADD, &tb->nid);
	if (tb->added)
	{
		tb->nid.uVersion = NOTIFYICON_VERSION_4;
		Shell_NotifyIconW (NIM_SETVERSION, &tb->nid);
	}
	if (tb->cb.embedded_changed)
		tb->cb.embedded_changed (tb, tb->added, tb->userdata);
}

/* GTK's "_File" mnemonic is Win32's "&File"; a literal & doubles */
static char *
win32_label (nx_item *it)
{
	const char *l = nx_item_get_label (it);
	const char *t = nx_item_get_text (it);
	GString *s;

	/* markup labels: use the plain text */
	if (!l || strchr (l, '<'))
		l = t ? t : "";
	s = g_string_new (NULL);
	for (; *l; l++)
	{
		if (*l == '&')
			g_string_append (s, "&&");
		else if (*l == '_' && l[1] == '_')
		{
			g_string_append_c (s, '_');
			l++;
		}
		else if (*l == '_')
			g_string_append_c (s, '&');
		else
			g_string_append_c (s, *l);
	}
	return g_string_free (s, FALSE);
}

/* nx_menu -> HMENU, items numbered for WM_COMMAND */
static HMENU
build_menu (tray_backend *tb, nx_menu *m)
{
	HMENU h = CreatePopupMenu ();
	int i, n = nx_menu_get_n_items (m);

	for (i = 0; i < n; i++)
	{
		nx_item *it = nx_menu_get_item (m, i);
		nx_item_kind kind = nx_item_get_kind (it);
		UINT flags = MF_STRING;
		char *label;
		wchar_t *wl;
		nx_menu *sub;

		if (kind == NX_KIND_SEPARATOR)
		{
			AppendMenuW (h, MF_SEPARATOR, 0, NULL);
			continue;
		}
		label = win32_label (it);
		wl = g_utf8_to_utf16 (label, -1, NULL, NULL, NULL);
		g_free (label);
		if (!nx_item_get_sensitive (it))
			flags |= MF_GRAYED;
		if ((kind == NX_KIND_CHECK || kind == NX_KIND_RADIO) && nx_item_get_active (it))
			flags |= MF_CHECKED;
		sub = nx_item_get_submenu (it);
		if (sub)
			AppendMenuW (h, flags | MF_POPUP, (UINT_PTR) build_menu (tb, sub), wl);
		else
		{
			g_ptr_array_add (tb->cmds, it);
			AppendMenuW (h, flags, NX_CMD_BASE + tb->cmds->len - 1, wl);
		}
		g_free (wl);
	}
	return h;
}

static LRESULT CALLBACK
tray_wndproc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	tray_backend *tb = (tray_backend *) GetWindowLongPtrW (hwnd, GWLP_USERDATA);

	if (tb && msg == WM_NX_TRAY)
	{
		switch (LOWORD (lp))
		{
		case WM_LBUTTONUP:
		case NIN_SELECT:
		case NIN_KEYSELECT:
			if (tb->cb.activate)
				tb->cb.activate (tb, tb->userdata);
			return 0;
		case WM_CONTEXTMENU:
		case WM_RBUTTONUP:
			if (tb->cb.menu)
				tb->cb.menu (tb, 3, GetMessageTime (), tb->userdata);
			return 0;
		}
		return 0;
	}
	if (tb && msg == tb->taskbar_created)
	{
		tray_add (tb);
		return 0;
	}
	return DefWindowProcW (hwnd, msg, wp, lp);
}

tray_backend *
tray_backend_new (GdkPixbuf *icon, const tray_backend_callbacks *cb, gpointer userdata)
{
	static ATOM cls;
	tray_backend *tb;

	if (!cls)
	{
		WNDCLASSW wc = { 0 };
		wc.lpfnWndProc = tray_wndproc;
		wc.hInstance = GetModuleHandleW (NULL);
		wc.lpszClassName = L"nexchat-tray";
		cls = RegisterClassW (&wc);
	}
	tb = g_new0 (tray_backend, 1);
	tb->cb = *cb;
	tb->userdata = userdata;
	tb->cmds = g_ptr_array_new ();
	tb->hwnd = CreateWindowW (L"nexchat-tray", L"nexchat", 0, 0, 0, 0, 0, HWND_MESSAGE,
									  NULL, GetModuleHandleW (NULL), NULL);
	if (!tb->hwnd)
	{
		g_ptr_array_free (tb->cmds, TRUE);
		g_free (tb);
		return NULL;
	}
	SetWindowLongPtrW (tb->hwnd, GWLP_USERDATA, (LONG_PTR) tb);
	tb->taskbar_created = RegisterWindowMessageW (L"TaskbarCreated");

	tb->nid.cbSize = sizeof tb->nid;
	tb->nid.hWnd = tb->hwnd;
	tb->nid.uID = NX_TRAY_ID;
	tb->nid.uCallbackMessage = WM_NX_TRAY;
	wcscpy (tb->nid.szTip, L"nexchat");
	tray_backend_set_icon (tb, icon);
	tray_add (tb);
	return tb;
}

void
tray_backend_free (tray_backend *tb)
{
	if (!tb)
		return;
	if (tb->added)
		Shell_NotifyIconW (NIM_DELETE, &tb->nid);
	if (tb->hicon)
		DestroyIcon (tb->hicon);
	if (tb->icon)
		g_object_unref (tb->icon);
	DestroyWindow (tb->hwnd);
	g_ptr_array_free (tb->cmds, TRUE);
	g_free (tb);
}

void
tray_backend_set_icon (tray_backend *tb, GdkPixbuf *icon)
{
	HICON old = tb->hicon;

	if (icon)
		g_object_ref (icon);
	if (tb->icon)
		g_object_unref (tb->icon);
	tb->icon = icon;
	tb->hicon = pixbuf_to_hicon (icon);
	tb->nid.hIcon = tb->hicon;
	if (tb->added)
	{
		tb->nid.uFlags = NIF_ICON;
		Shell_NotifyIconW (NIM_MODIFY, &tb->nid);
	}
	if (old)
		DestroyIcon (old);
}

GdkPixbuf *
tray_backend_get_icon (tray_backend *tb)
{
	return tb->icon;
}

void
tray_backend_set_tooltip (tray_backend *tb, const char *text)
{
	wchar_t *w = g_utf8_to_utf16 (text ? text : "", -1, NULL, NULL, NULL);

	wcsncpy (tb->nid.szTip, w ? w : L"", G_N_ELEMENTS (tb->nid.szTip) - 1);
	tb->nid.szTip[G_N_ELEMENTS (tb->nid.szTip) - 1] = 0;
	g_free (w);
	if (tb->added)
	{
		tb->nid.uFlags = NIF_TIP | NIF_SHOWTIP;
		Shell_NotifyIconW (NIM_MODIFY, &tb->nid);
	}
}

gboolean
tray_backend_is_embedded (tray_backend *tb)
{
	return tb->added;
}

void
tray_backend_show_menu (tray_backend *tb, nx_menu *menu, guint button, guint32 time)
{
	HMENU h;
	POINT pt;
	UINT cmd;

	g_ptr_array_set_size (tb->cmds, 0);
	h = build_menu (tb, menu);
	GetCursorPos (&pt);
	/* the menu closes when focus goes elsewhere only if our window is in
	   front (documented TrackPopupMenu quirk) */
	SetForegroundWindow (tb->hwnd);
	cmd = TrackPopupMenuEx (h, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
									pt.x, pt.y, tb->hwnd, NULL);
	PostMessageW (tb->hwnd, WM_NULL, 0, 0);
	DestroyMenu (h);
	if (cmd >= NX_CMD_BASE && cmd - NX_CMD_BASE < tb->cmds->len)
		nx_item_activate (g_ptr_array_index (tb->cmds, cmd - NX_CMD_BASE));
	g_ptr_array_set_size (tb->cmds, 0);
	nx_menu_free (menu);
}
