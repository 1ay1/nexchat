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
 */
#define _FILE_OFFSET_BITS 64 /* allow selection of large files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fe-gtk.h"

#if defined (WIN32) || defined (__APPLE__)
#include <pango/pangocairo.h>
#endif

#if   defined(GDK_WINDOWING_X11)
#include <gdk/x11/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif
#ifdef HAVE_WLACTIVATE
#include "wlactivate.h"
#endif
#if defined(GDK_WINDOWING_WIN32)
#include <gdk/win32/gdkwin32.h>
#include <windows.h>
#endif
#if defined(__APPLE__)
#include "platform-macos.h"
#include <gdk/macos/gdkmacos.h>

static void *
macos_nswindow (GtkWindow *win)
{
	GdkSurface *s;

	if (!win || !GDK_IS_MACOS_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
		return NULL;
	s = gtk_native_get_surface (GTK_NATIVE (win));
	return s ? gdk_macos_surface_get_native_window (GDK_MACOS_SURFACE (s)) : NULL;
}

static void
macos_move_on_map (GtkWidget *w, gpointer unused)
{
	g_signal_handlers_disconnect_by_func (w, macos_move_on_map, unused);
	macos_window_move (macos_nswindow (GTK_WINDOW (w)),
							 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "nx-move-x")),
							 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "nx-move-y")));
}

static void
macos_place_on_map (GtkWidget *w, gpointer how)
{
	g_signal_handlers_disconnect_by_func (w, macos_place_on_map, how);
	macos_window_place (macos_nswindow (GTK_WINDOW (w)),
							  macos_nswindow (gtk_window_get_transient_for (GTK_WINDOW (w))),
							  GPOINTER_TO_INT (how));
}
#endif

#include "../common/hexchat.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/cfgfiles.h"
#include "../common/hexchatc.h"
#include "../common/typedef.h"
#include "gtkutil.h"
#include "pixmaps.h"

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/* gtkutil.c, just some gtk wrappers */

extern void path_part (char *file, char *path, int pathlen);

GtkWidget *
gtkutil_box_new (GtkOrientation orientation, gboolean homogeneous, int spacing)
{
	GtkWidget *box = gtk_box_new (orientation, spacing);
	gtk_box_set_homogeneous (GTK_BOX (box), homogeneous);
	return box;
}

/* gtk_container_add() into a GTK2 GtkHBox/GtkVBox packed the child with
   expand=TRUE, fill=TRUE. A GTK3 gtk_box_new() box packs it with
   expand=FALSE, so whole panes collapse. Keep the GTK2 behaviour for every
   box, and plain gtk_container_add for everything else.
   GTK4 has no GtkContainer: each single-child parent has its own setter. */
void
gtkutil_container_add (GtkWidget *container, GtkWidget *child)
{
	if (GTK_IS_BOX (container))
		nx_box_pack_start (container, child, TRUE, TRUE, 0);
	else if (GTK_IS_WINDOW (container))
	{
		guint b = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (container), "nx-border"));
		gtk_window_set_child (GTK_WINDOW (container), child);
		if (b)	/* nx_set_border_width on the window before it had a child */
			nx_set_border_width (child, b - 1);
	}
	else if (GTK_IS_FRAME (container))
		gtk_frame_set_child (GTK_FRAME (container), child);
	else if (GTK_IS_SCROLLED_WINDOW (container))
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (container), child);
	else if (GTK_IS_VIEWPORT (container))
		gtk_viewport_set_child (GTK_VIEWPORT (container), child);
	else if (GTK_IS_BUTTON (container))
		gtk_button_set_child (GTK_BUTTON (container), child);
	else if (GTK_IS_EXPANDER (container))
		gtk_expander_set_child (GTK_EXPANDER (container), child);
	else if (GTK_IS_PANED (container))
	{
		/* gtk_container_add on a paned fills child1, then child2 */
		if (!gtk_paned_get_start_child (GTK_PANED (container)))
			gtk_paned_set_start_child (GTK_PANED (container), child);
		else
			gtk_paned_set_end_child (GTK_PANED (container), child);
	}
	else
		g_warning ("gtkutil_container_add: no GTK4 equivalent for %s",
					  G_OBJECT_TYPE_NAME (container));
}

typedef struct
{
	GMainLoop *loop;
	int response;
	gboolean destroyed;
} run_info;

static void
run_response_cb (GtkWidget *dialog, int response, run_info *ri)
{
	ri->response = response;
	if (g_main_loop_is_running (ri->loop))
		g_main_loop_quit (ri->loop);
}

static void
run_destroy_cb (GtkWidget *dialog, run_info *ri)
{
	ri->destroyed = TRUE;
	if (g_main_loop_is_running (ri->loop))
		g_main_loop_quit (ri->loop);
}

static gboolean
run_delete_cb (GtkWidget *dialog, run_info *ri)
{
	/* like gtk_dialog_run: closing the window answers DELETE_EVENT and the
	   dialog stays alive for the caller to destroy */
	ri->response = GTK_RESPONSE_DELETE_EVENT;
	if (g_main_loop_is_running (ri->loop))
		g_main_loop_quit (ri->loop);
	return TRUE;
}

int
gtkutil_dialog_run_blocking (NxDialog *dialog)
{
	run_info ri = { NULL, GTK_RESPONSE_NONE, FALSE };
	gulong h1, h2, h3;

	g_object_ref (dialog);
	h1 = g_signal_connect (dialog, "response", G_CALLBACK (run_response_cb), &ri);
	h2 = g_signal_connect (dialog, "destroy", G_CALLBACK (run_destroy_cb), &ri);
	h3 = g_signal_connect (dialog, "close-request", G_CALLBACK (run_delete_cb), &ri);

	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	nx_widget_show (GTK_WIDGET (dialog));

	ri.loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (ri.loop);
	g_main_loop_unref (ri.loop);

	if (!ri.destroyed)
	{
		g_signal_handler_disconnect (dialog, h1);
		g_signal_handler_disconnect (dialog, h2);
		g_signal_handler_disconnect (dialog, h3);
	}
	g_object_unref (dialog);
	return ri.response;
}

gboolean
gtkutil_window_can_position (GtkWindow *win)
{
#ifdef GDK_WINDOWING_WAYLAND
	if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
		return FALSE;
#endif
	return TRUE;
}

#if defined(GDK_WINDOWING_X11)
/* GTK4 has no window position or urgency API; on X11 do what GTK3 did
   with Xlib on the toplevel's own X window. GTK 4.18 deprecated the X11
   backend's getters with no replacement. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static Window
x11_xid (GtkWindow *win)
{
	GdkSurface *s;

	if (!GDK_IS_X11_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
		return None;
	s = gtk_native_get_surface (GTK_NATIVE (win));
	return s ? gdk_x11_surface_get_xid (s) : None;
}

static Display *
x11_dpy (GtkWindow *win)
{
	return gdk_x11_display_get_xdisplay (gtk_widget_get_display (GTK_WIDGET (win)));
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void
x11_move_now (GtkWindow *win, int x, int y)
{
	Window xid = x11_xid (win);
	XSizeHints *h;
	long supplied;

	if (!xid)
		return;
	/* USPosition: the user asked for this place, WMs honour it */
	h = XAllocSizeHints ();
	XGetWMNormalHints (x11_dpy (win), xid, h, &supplied);
	h->flags |= USPosition | PPosition;
	h->x = x;
	h->y = y;
	XSetWMNormalHints (x11_dpy (win), xid, h);
	XFree (h);
	XMoveWindow (x11_dpy (win), xid, x, y);
}

/* a move asked for before the window has an X window: done at realize */
static void
x11_move_on_realize (GtkWidget *w, gpointer unused)
{
	int x = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "nx-move-x"));
	int y = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "nx-move-y"));

	x11_move_now (GTK_WINDOW (w), x, y);
}

/* GTK_WIN_POS_MOUSE / _CENTER: once the window is mapped (its size is
   known), centre it on the pointer or the screen, kept on screen */
static void
x11_place_on_map (GtkWidget *w, gpointer data)
{
	gboolean at_mouse = GPOINTER_TO_INT (data);
	Window root, child, xid = x11_xid (GTK_WINDOW (w));
	int rx, ry, wx, wy, x, y, ww, wh;
	unsigned int mask;
	Screen *scr;

	g_signal_handlers_disconnect_matched (w, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, x11_place_on_map, NULL);
	if (!xid)
		return;
	scr = DefaultScreenOfDisplay (x11_dpy (GTK_WINDOW (w)));
	ww = gtk_widget_get_width (w);
	wh = gtk_widget_get_height (w);
	if (ww <= 1 || wh <= 1)
	{
		/* not allocated yet: what it'll ask for */
		gtk_widget_measure (w, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &ww, NULL, NULL);
		gtk_widget_measure (w, GTK_ORIENTATION_VERTICAL, ww, NULL, &wh, NULL, NULL);
	}
	if (at_mouse == 1 && XQueryPointer (x11_dpy (GTK_WINDOW (w)), RootWindowOfScreen (scr), &root, &child,
											&rx, &ry, &wx, &wy, &mask))
	{
		x = rx - ww / 2;
		y = ry - wh / 2;
	}
	else if (at_mouse == 2 && gtk_window_get_transient_for (GTK_WINDOW (w)) &&
				gtkutil_window_get_position (gtk_window_get_transient_for (GTK_WINDOW (w)), &rx, &ry))
	{
		/* over the middle of the window it belongs to */
		GtkWidget *pw = GTK_WIDGET (gtk_window_get_transient_for (GTK_WINDOW (w)));
		x = rx + (gtk_widget_get_width (pw) - ww) / 2;
		y = ry + (gtk_widget_get_height (pw) - wh) / 2;
	}
	else
	{
		x = (WidthOfScreen (scr) - ww) / 2;
		y = (HeightOfScreen (scr) - wh) / 2;
	}
	x = CLAMP (x, 0, MAX (0, WidthOfScreen (scr) - ww));
	y = CLAMP (y, 0, MAX (0, HeightOfScreen (scr) - wh));
	x11_move_now (GTK_WINDOW (w), x, y);
}
#endif

#if defined(GDK_WINDOWING_WIN32)
/* the window's HWND, NULL before it's realized */
#undef gtkutil_window_hwnd
void *
gtkutil_window_hwnd_ (GtkWindow *win)
{
	GdkSurface *s;

	if (!GDK_IS_WIN32_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
		return NULL;
	s = gtk_native_get_surface (GTK_NATIVE (win));
	return s ? (void *) gdk_win32_surface_get_handle (s) : NULL;
}
#define gtkutil_window_hwnd(w) ((HWND) gtkutil_window_hwnd_ (w))

/* the work area (screen minus taskbar) of the monitor the window, or
   else the pointer, is on */
static void
win32_work_area (HWND hwnd, RECT *r)
{
	MONITORINFO mi = { sizeof mi };
	HMONITOR m;
	POINT pt;

	if (hwnd)
		m = MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST);
	else
	{
		GetCursorPos (&pt);
		m = MonitorFromPoint (pt, MONITOR_DEFAULTTONEAREST);
	}
	GetMonitorInfoW (m, &mi);
	*r = mi.rcWork;
}

static void
win32_move_now (GtkWindow *win, int x, int y)
{
	HWND h = gtkutil_window_hwnd (win);

	if (h)
		SetWindowPos (h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void
win32_move_on_map (GtkWidget *w, gpointer unused)
{
	g_signal_handlers_disconnect_by_func (w, win32_move_on_map, unused);
	win32_move_now (GTK_WINDOW (w),
						 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "nx-move-x")),
						 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "nx-move-y")));
}

/* placed like GTK3's GTK_WIN_POS_*: at the pointer, centred on the parent
   or on the screen, kept inside the work area */
static void
win32_place_on_map (GtkWidget *w, gpointer how)
{
	HWND h = gtkutil_window_hwnd (GTK_WINDOW (w)), ph;
	GtkWindow *parent = gtk_window_get_transient_for (GTK_WINDOW (w));
	RECT wr, pr, area;
	POINT pt;
	int ww, wh, x, y;

	g_signal_handlers_disconnect_by_func (w, win32_place_on_map, how);
	if (!h || !GetWindowRect (h, &wr))
		return;
	ww = wr.right - wr.left;
	wh = wr.bottom - wr.top;
	ph = parent ? gtkutil_window_hwnd (parent) : NULL;
	if (GPOINTER_TO_INT (how) == 1)
	{
		GetCursorPos (&pt);
		x = pt.x - ww / 2;
		y = pt.y - wh / 2;
		win32_work_area (NULL, &area);
	}
	else if (GPOINTER_TO_INT (how) == 2 && ph && GetWindowRect (ph, &pr))
	{
		x = pr.left + (pr.right - pr.left - ww) / 2;
		y = pr.top + (pr.bottom - pr.top - wh) / 2;
		win32_work_area (ph, &area);
	}
	else
	{
		win32_work_area (ph, &area);
		x = area.left + (area.right - area.left - ww) / 2;
		y = area.top + (area.bottom - area.top - wh) / 2;
	}
	x = CLAMP (x, area.left, MAX (area.left, area.right - ww));
	y = CLAMP (y, area.top, MAX (area.top, area.bottom - wh));
	SetWindowPos (h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
#endif

/* how: 1 at the pointer, 0 screen centre, 2 centred on its parent */
void
gtkutil_window_place (GtkWidget *win, int at_mouse)
{
#ifdef GDK_WINDOWING_WIN32
	if (GTK_IS_WINDOW (win) && GDK_IS_WIN32_DISPLAY (gtk_widget_get_display (win)))
	{
		g_signal_handlers_disconnect_matched (win, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, win32_place_on_map, NULL);
		g_signal_connect (win, "map", G_CALLBACK (win32_place_on_map), GINT_TO_POINTER (at_mouse));
		return;
	}
#endif
#ifdef GDK_WINDOWING_MACOS
	if (GTK_IS_WINDOW (win) && GDK_IS_MACOS_DISPLAY (gtk_widget_get_display (win)))
	{
		g_signal_handlers_disconnect_matched (win, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, macos_place_on_map, NULL);
		g_signal_connect (win, "map", G_CALLBACK (macos_place_on_map), GINT_TO_POINTER (at_mouse));
		return;
	}
#endif
#ifdef GDK_WINDOWING_X11
	if (!GTK_IS_WINDOW (win) || !GDK_IS_X11_DISPLAY (gtk_widget_get_display (win)))
		return;
	/* the last placement asked for wins */
	g_signal_handlers_disconnect_matched (win, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, x11_place_on_map, NULL);
	g_signal_connect (win, "map", G_CALLBACK (x11_place_on_map), GINT_TO_POINTER (at_mouse));
#endif
}

void
gtkutil_window_move (GtkWindow *win, int x, int y)
{
#ifdef GDK_WINDOWING_WIN32
	if (GDK_IS_WIN32_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
	{
		if (gtk_widget_get_mapped (GTK_WIDGET (win)))
			win32_move_now (win, x, y);
		else
		{
			g_object_set_data (G_OBJECT (win), "nx-move-x", GINT_TO_POINTER (x));
			g_object_set_data (G_OBJECT (win), "nx-move-y", GINT_TO_POINTER (y));
			g_signal_handlers_disconnect_by_func (win, win32_move_on_map, NULL);
			g_signal_connect (win, "map", G_CALLBACK (win32_move_on_map), NULL);
		}
		return;
	}
#endif
#ifdef GDK_WINDOWING_MACOS
	if (GDK_IS_MACOS_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
	{
		if (gtk_widget_get_mapped (GTK_WIDGET (win)))
			macos_window_move (macos_nswindow (win), x, y);
		else
		{
			g_object_set_data (G_OBJECT (win), "nx-move-x", GINT_TO_POINTER (x));
			g_object_set_data (G_OBJECT (win), "nx-move-y", GINT_TO_POINTER (y));
			g_signal_handlers_disconnect_by_func (win, macos_move_on_map, NULL);
			g_signal_connect (win, "map", G_CALLBACK (macos_move_on_map), NULL);
		}
		return;
	}
#endif
#ifdef GDK_WINDOWING_X11
	if (!GDK_IS_X11_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
		return;		/* Wayland: the compositor places windows */
	if (gtk_widget_get_realized (GTK_WIDGET (win)))
		x11_move_now (win, x, y);
	else
	{
		g_object_set_data (G_OBJECT (win), "nx-move-x", GINT_TO_POINTER (x));
		g_object_set_data (G_OBJECT (win), "nx-move-y", GINT_TO_POINTER (y));
		if (!g_object_get_data (G_OBJECT (win), "nx-move-tag"))
			g_object_set_data (G_OBJECT (win), "nx-move-tag", GUINT_TO_POINTER (
				g_signal_connect_after (win, "realize", G_CALLBACK (x11_move_on_realize), NULL)));
	}
#else
	(void) win; (void) x; (void) y;
#endif
}

gboolean
gtkutil_window_get_position (GtkWindow *win, int *x, int *y)
{
#ifdef GDK_WINDOWING_WIN32
	if (GDK_IS_WIN32_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
	{
		HWND h = gtkutil_window_hwnd (win);
		RECT r;

		if (!h || !GetWindowRect (h, &r))
			return FALSE;
		*x = r.left;
		*y = r.top;
		return TRUE;
	}
#endif
#ifdef GDK_WINDOWING_MACOS
	if (GDK_IS_MACOS_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
		return macos_window_get_position (macos_nswindow (win), x, y);
#endif
#ifdef GDK_WINDOWING_X11
	Window xid = x11_xid (win), child;
	XWindowAttributes a;

	/* where the window is on screen, as gtk_window_get_position gave it */
	if (!xid || !XGetWindowAttributes (x11_dpy (win), xid, &a))
		return FALSE;
	XTranslateCoordinates (x11_dpy (win), xid, a.root, 0, 0, x, y, &child);
	return TRUE;
#else
	return FALSE;
#endif
}

void
gtkutil_window_set_urgent (GtkWindow *win, gboolean urgent)
{
#ifdef HAVE_WLACTIVATE
	GdkDisplay *wdpy = gtk_widget_get_display (GTK_WIDGET (win));

	/* no hint on Wayland; the compositor clears it once focused */
	if (GDK_IS_WAYLAND_DISPLAY (wdpy))
	{
		GdkSurface *s = gtk_native_get_surface (GTK_NATIVE (win));
		if (urgent && s)
			wl_request_attention (gdk_wayland_display_get_wl_display (wdpy),
										 gdk_wayland_surface_get_wl_surface (s));
		return;
	}
#endif
#if defined(__APPLE__)
	/* the Dock icon bounces until nexchat is brought to the front */
	macos_request_attention (urgent);
	return;
#endif
#ifdef GDK_WINDOWING_WIN32
	/* the taskbar button flashes until the window is activated */
	if (GDK_IS_WIN32_DISPLAY (gtk_widget_get_display (GTK_WIDGET (win))))
	{
		HWND h = gtkutil_window_hwnd (win);
		FLASHWINFO fi = { sizeof fi };

		if (!h)
			return;
		fi.hwnd = h;
		fi.dwFlags = urgent ? (FLASHW_TRAY | FLASHW_TIMERNOFG) : FLASHW_STOP;
		FlashWindowEx (&fi);
		return;
	}
#endif
#ifdef GDK_WINDOWING_X11
	Window xid = x11_xid (win);
	XWMHints *h;

	if (xid)
	{
		h = XGetWMHints (x11_dpy (win), xid);
		if (!h)
			h = XAllocWMHints ();
		if (urgent)
			h->flags |= XUrgencyHint;
		else
			h->flags &= ~XUrgencyHint;
		XSetWMHints (x11_dpy (win), xid, h);
		XFree (h);
		return;
	}
#endif
	(void) win; (void) urgent;
}


void
gtkutil_force_images (void)
{
	static gboolean done;
	guint sig;

	if (done)
		return;
	done = TRUE;
	/* GTK4 buttons/menus always show their images */
	(void) sig;
	return;
}

/* GTK 3.24's own stock table: stock id -> icon name (gtkiconfactory.c) and
   label (gtkstock.c). Ids whose image only exists under the stock name
   (gtk-ok, gtk-cancel, gtk-connect, ...) map to themselves: GTK3 ships
   those PNGs as named icons. */
static const struct
{
	const char *stock;
	const char *icon;
	const char *context;	/* gettext context in GTK's gtk30 domain */
	const char *label;
} stock_table[] = {
	{ "gtk-about", "help-about", "Stock label", "_About" },
	{ "gtk-add", "list-add", "Stock label", "_Add" },
	{ "gtk-apply", "gtk-apply", "Stock label", "_Apply" },
	{ "gtk-cancel", "gtk-cancel", "Stock label", "_Cancel" },
	{ "gtk-clear", "edit-clear", "Stock label", "_Clear" },
	{ "gtk-close", "window-close", "Stock label", "_Close" },
	{ "gtk-connect", "gtk-connect", "Stock label", "C_onnect" },
	{ "gtk-copy", "edit-copy", "Stock label", "_Copy" },
	{ "gtk-delete", "edit-delete", "Stock label", "_Delete" },
	{ "gtk-dialog-error", "dialog-error", "Stock label", "Error" },
	{ "gtk-dialog-warning", "dialog-warning", "Stock label", "Warning" },
	{ "gtk-disconnect", "gtk-disconnect", "Stock label", "_Disconnect" },
	{ "gtk-find", "edit-find", "Stock label", "_Find" },
	{ "gtk-go-back", "go-previous", "Stock label, navigation", "_Back" },
	{ "gtk-go-down", "go-down", "Stock label, navigation", "_Down" },
	{ "gtk-go-forward", "go-next", "Stock label, navigation", "_Forward" },
	{ "gtk-go-up", "go-up", "Stock label, navigation", "_Up" },
	{ "gtk-help", "help-contents", "Stock label", "_Help" },
	{ "gtk-index", "gtk-index", "Stock label", "_Index" },
	{ "gtk-jump-to", "go-jump", "Stock label", "_Jump to" },
	{ "gtk-justify-left", "format-justify-left", "Stock label", "_Left" },
	{ "gtk-media-play", "media-playback-start", "Stock label, media", "_Play" },
	{ "gtk-network", "network-idle", "Stock label", "_Network" },
	{ "gtk-new", "document-new", "Stock label", "_New" },
	{ "gtk-no", "gtk-no", "Stock label", "_No" },
	{ "gtk-ok", "gtk-ok", "Stock label", "_OK" },
	{ "gtk-open", "document-open", "Stock label", "_Open" },
	{ "gtk-preferences", "gtk-preferences", "Stock label", "_Preferences" },
	{ "gtk-quit", "application-exit", "Stock label", "_Quit" },
	{ "gtk-redo", "edit-redo", "Stock label", "_Redo" },
	{ "gtk-refresh", "view-refresh", "Stock label", "_Refresh" },
	{ "gtk-remove", "list-remove", "Stock label", "_Remove" },
	{ "gtk-revert-to-saved", "document-revert", "Stock label", "_Revert" },
	{ "gtk-save", "document-save", "Stock label", "_Save" },
	{ "gtk-save-as", "document-save-as", "Stock label", "Save _As" },
	{ "gtk-spell-check", "tools-check-spelling", "Stock label", "_Spell Check" },
	{ "gtk-yes", "gtk-yes", "Stock label", "_Yes" },
};

static int
stock_find (const char *stock_id)
{
	guint i;

	if (!stock_id)
		return -1;
	for (i = 0; i < G_N_ELEMENTS (stock_table); i++)
		if (strcmp (stock_table[i].stock, stock_id) == 0)
			return i;
	return -1;
}

const char *
gtkutil_stock_icon_name (const char *stock_id)
{
	int i = stock_find (stock_id);
	return i >= 0 ? stock_table[i].icon : stock_id;
}

GtkWidget *
gtkutil_image_new_from_stock (const char *stock_id, nx_icon_size size)
{
	return nx_image_new_from_icon_name (gtkutil_stock_icon_name (stock_id), size);
}

const char *
gtkutil_stock_label (const char *stock_id)
{
	int i = stock_find (stock_id);

	if (i < 0)
		return stock_id;
	/* GTK translated these in its own catalogue; use the same strings so
	   every language keeps its existing button text */
	return g_dpgettext2 ("gtk30", stock_table[i].context, stock_table[i].label);
}

GtkWidget *
gtkutil_button_new_from_stock (const char *stock_id)
{
	GtkWidget *button = gtk_button_new_with_mnemonic (gtkutil_stock_label (stock_id));
	nx_button_set_image (button,
								 gtkutil_image_new_from_stock (stock_id, NX_ICON_BUTTON));
	return button;
}

GtkWidget *
gtkutil_dialog_add_stock_button (NxDialog *dialog, const char *stock_id, int response)
{
	GtkWidget *button = gtkutil_button_new_from_stock (stock_id);

	nx_widget_show (button);
	nx_dialog_add_action_widget (dialog, button, response);
	return button;
}

GdkPixbuf *
gtkutil_render_icon (GtkWidget *widget, const char *stock_id, nx_icon_size size, const char *unused)
{
	/* a themed icon at the old pixel size, as a pixbuf (tree view cells) */
	GtkIconTheme *theme = gtk_icon_theme_get_for_display (widget ? gtk_widget_get_display (widget)
																			: gdk_display_get_default ());
	GtkIconPaintable *ip = gtk_icon_theme_lookup_icon (theme, gtkutil_stock_icon_name (stock_id),
																		NULL, size, 1, GTK_TEXT_DIR_NONE, 0);
	GFile *f = ip ? gtk_icon_paintable_get_file (ip) : NULL;
	char *path = f ? g_file_get_path (f) : NULL;
	GdkPixbuf *pix = NULL;

	if (path)
		pix = gdk_pixbuf_new_from_file_at_size (path, size, size, NULL);
	g_free (path);
	if (f)
		g_object_unref (f);
	if (ip)
		g_object_unref (ip);
	return pix;
}

void
gtkutil_grid_attach (GtkWidget *grid, GtkWidget *child,
							int left, int right, int top, int bottom,
							int xoptions, int yoptions, int xpadding, int ypadding)
{
	/* explicit FALSE too: GtkTable never let a child's own wish to expand
	   leak into a column that wasn't attached with EXPAND */
	gtk_widget_set_hexpand (child, (xoptions & GU_EXPAND) != 0);
	gtk_widget_set_vexpand (child, (yoptions & GU_EXPAND) != 0);
	gtk_widget_set_halign (child, (xoptions & GU_FILL) ? GTK_ALIGN_FILL : GTK_ALIGN_CENTER);
	gtk_widget_set_valign (child, (yoptions & GU_FILL) ? GTK_ALIGN_FILL : GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start (child, xpadding);
	gtk_widget_set_margin_end (child, xpadding);
	gtk_widget_set_margin_top (child, ypadding);
	gtk_widget_set_margin_bottom (child, ypadding);
	gtk_grid_attach (GTK_GRID (grid), child, left, top, right - left, bottom - top);
}

struct file_req
{
	void *userdata;
	filereqcallback callback;
	int flags;		/* FRF_* flags */
};


static void
gtkutil_check_file (char *filename, struct file_req *freq)
{
	int axs = FALSE;

	GFile *file = g_file_new_for_path (filename);

	if (freq->flags & FRF_WRITE)
	{
		GFile *parent = g_file_get_parent (file);

		GFileInfo *fi = g_file_query_info (parent, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
		if (fi != NULL)
		{
			if (g_file_info_get_attribute_boolean (fi, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE))
			{
				axs = TRUE;
			}

			g_object_unref (fi);
		}

		g_object_unref (parent);
	}
	else
	{
		GFileInfo *fi = g_file_query_info (file, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, G_FILE_QUERY_INFO_NONE, NULL, NULL);

		if (fi != NULL)
		{
			if (g_file_info_get_file_type (fi) != G_FILE_TYPE_DIRECTORY || (freq->flags & FRF_CHOOSEFOLDER))
			{
				axs = TRUE;
			}

			g_object_unref (fi);
		}
	}

	g_object_unref (file);

	if (axs)
	{
		char *filename_utf8 = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
		if (filename_utf8 != NULL)
		{
			freq->callback (freq->userdata, filename_utf8);
			g_free (filename_utf8);
		}
		else
		{
			fe_message ("Filename encoding is corrupt.", FE_MSG_ERROR);
		}
	}
	else
	{
		if (freq->flags & FRF_WRITE)
		{
			fe_message (_("Cannot write to that file."), FE_MSG_ERROR);
		}
		else
		{
			fe_message (_("Cannot read that file."), FE_MSG_ERROR);
		}
	}
}


/* GtkFileDialog: the platform's dialog (portal on Linux, NSOpenPanel,
   IFileDialog). Same contract as before: callback (userdata, file) for each
   accepted file, then callback (userdata, NULL) once the dialog is gone. */

static void
gtkutil_file_req_finish (struct file_req *freq)
{
	freq->callback (freq->userdata, NULL);
	g_free (freq);
}

static void
gtkutil_file_req_check_gfile (GFile *f, struct file_req *freq)
{
	char *path = f ? g_file_get_path (f) : NULL;

	if (path)
		gtkutil_check_file (path, freq);
	g_free (path);
}

static void
gtkutil_file_req_ready (GObject *src, GAsyncResult *res, gpointer data)
{
	struct file_req *freq = data;
	GtkFileDialog *fd = GTK_FILE_DIALOG (src);
	GError *err = NULL;

	if (freq->flags & FRF_MULTIPLE)
	{
		GListModel *files = gtk_file_dialog_open_multiple_finish (fd, res, &err);
		if (files)
		{
			guint i, n = g_list_model_get_n_items (files);
			for (i = 0; i < n; i++)
			{
				GFile *f = g_list_model_get_item (files, i);
				gtkutil_file_req_check_gfile (f, freq);
				g_object_unref (f);
			}
			g_object_unref (files);
		}
	}
	else
	{
		GFile *f;

		if (freq->flags & FRF_CHOOSEFOLDER)
			f = gtk_file_dialog_select_folder_finish (fd, res, &err);
		else if (freq->flags & FRF_WRITE)
			f = gtk_file_dialog_save_finish (fd, res, &err);
		else
			f = gtk_file_dialog_open_finish (fd, res, &err);
		if (f)
		{
			gtkutil_file_req_check_gfile (f, freq);
			g_object_unref (f);
		}
	}
	/* cancelling is an error here; it was silent before too */
	g_clear_error (&err);
	gtkutil_file_req_finish (freq);
}

void
gtkutil_file_req (GtkWindow *parent, const char *title, void *callback, void *userdata, char *filter, char *extensions,
						int flags)
{
	struct file_req *freq = g_new (struct file_req, 1);
	GtkFileDialog *fd = gtk_file_dialog_new ();
	GFile *folder = NULL;

	freq->flags = flags;
	freq->callback = callback;
	freq->userdata = userdata;

	gtk_file_dialog_set_title (fd, title);
	gtk_file_dialog_set_modal (fd, (flags & FRF_MODAL) != 0);
	/* FRF_NOASKOVERWRITE: GTK4's dialogs always confirm overwriting */

	if (filter && filter[0] && (flags & FRF_FILTERISINITIAL))
	{
		if (flags & FRF_WRITE)
		{
			char temp[1024];
			path_part (filter, temp, sizeof (temp));
			folder = g_file_new_for_path (temp);
			gtk_file_dialog_set_initial_name (fd, file_part (filter));
		}
		else
			folder = g_file_new_for_path (filter);
	}
	else if (!(flags & FRF_RECENTLYUSED))
		folder = g_file_new_for_path (get_xdir ());
	if (folder)
	{
		gtk_file_dialog_set_initial_folder (fd, folder);
		g_object_unref (folder);
	}

	if ((flags & FRF_EXTENSIONS || flags & FRF_MIMETYPES) && extensions != NULL)
	{
		GtkFileFilter *filefilter = gtk_file_filter_new ();
		char *tokenbuffer = g_strdup (extensions);
		char *token = strtok (tokenbuffer, ";");

		while (token != NULL)
		{
			if (flags & FRF_EXTENSIONS)
				gtk_file_filter_add_pattern (filefilter, token);
			else
				gtk_file_filter_add_mime_type (filefilter, token);
			token = strtok (NULL, ";");
		}
		g_free (tokenbuffer);
		gtk_file_dialog_set_default_filter (fd, filefilter);
		g_object_unref (filefilter);
	}
	/* (the "<xdir>" shortcut folder: GtkFileDialog has no shortcuts list) */

	if (flags & FRF_CHOOSEFOLDER)
		gtk_file_dialog_select_folder (fd, parent, NULL, gtkutil_file_req_ready, freq);
	else if (flags & FRF_MULTIPLE)
		gtk_file_dialog_open_multiple (fd, parent, NULL, gtkutil_file_req_ready, freq);
	else if (flags & FRF_WRITE)
		gtk_file_dialog_save (fd, parent, NULL, gtkutil_file_req_ready, freq);
	else
		gtk_file_dialog_open (fd, parent, NULL, gtkutil_file_req_ready, freq);
	g_object_unref (fd);
}


static gboolean
gtkutil_esc_destroy (GtkWidget * win, guint keyval, GdkModifierType state, gpointer userdata)
{
	GtkWidget *wid;

	/* Destroy the window of detached utils */
	/* every util is its own GtkWindow on GTK4 (no type hints to tell a
	   detached one apart) */
	if (!GTK_IS_WINDOW (win))
		return FALSE;
	wid = win;

	if (keyval == GDK_KEY_Escape)
		nx_widget_destroy (wid);
			
	return FALSE;
}

void
gtkutil_destroy_on_esc (GtkWidget *win)
{
	nx_on_key (win, gtkutil_esc_destroy, win);
}

void
gtkutil_destroy (GtkWidget * igad, GtkWidget * dgad)
{
	nx_widget_destroy (dgad);
}

static void
gtkutil_get_str_response (GtkWidget *dialog, gint arg1, gpointer entry)
{
	void (*callback) (int cancel, char *text, void *user_data);
	char *text;
	void *user_data;

	text = (char *) nx_entry_get_text (entry);
	callback = g_object_get_data (G_OBJECT (dialog), "cb");
	user_data = g_object_get_data (G_OBJECT (dialog), "ud");

	switch (arg1)
	{
	case GTK_RESPONSE_REJECT:
		callback (TRUE, text, user_data);
		nx_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_ACCEPT:
		callback (FALSE, text, user_data);
		nx_widget_destroy (GTK_WIDGET (dialog));
		break;
	}
}

static void
gtkutil_str_enter (GtkWidget *entry, GtkWidget *dialog)
{
	nx_dialog_response (NX_DIALOG_CAST (dialog), GTK_RESPONSE_ACCEPT);
}

void
fe_get_str (char *msg, char *def, void *callback, void *userdata)
{
	GtkWidget *dialog;
	GtkWidget *entry;
	GtkWidget *hbox;
	GtkWidget *label;
	extern GtkWidget *parent_window;

	dialog = nx_dialog_new (msg, NULL, FALSE);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_OK, GTK_RESPONSE_ACCEPT);

	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));
	gtk_box_set_homogeneous (GTK_BOX (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog))), TRUE);

	if (userdata == (void *)1)	/* nick box is usually on the very bottom, make it centered */
	{
		nx_window_set_position_center (dialog);
	}
	else
	{
		nx_window_set_position_mouse (dialog);
	}

	hbox = gtkutil_box_new (GTK_ORIENTATION_HORIZONTAL, TRUE, 0);

	g_object_set_data (G_OBJECT (dialog), "cb", callback);
	g_object_set_data (G_OBJECT (dialog), "ud", userdata);

	entry = gtk_entry_new ();
	g_signal_connect (G_OBJECT (entry), "activate",
						 	G_CALLBACK (gtkutil_str_enter), dialog);
	nx_entry_set_text (entry, def);
	nx_box_pack_end (hbox, entry, 0, 0, 0);

	label = gtk_label_new (msg);
	nx_box_pack_end (hbox, label, 0, 0, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (gtkutil_get_str_response), entry);

	gtkutil_container_add (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog)), hbox);

	nx_widget_show_all (dialog);
}

static void
gtkutil_get_number_response (GtkWidget *dialog, gint arg1, gpointer spin)
{
	void (*callback) (int cancel, int value, void *user_data);
	int num;
	void *user_data;

	num = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin));
	callback = g_object_get_data (G_OBJECT (dialog), "cb");
	user_data = g_object_get_data (G_OBJECT (dialog), "ud");

	switch (arg1)
	{
	case GTK_RESPONSE_REJECT:
		callback (TRUE, num, user_data);
		nx_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_ACCEPT:
		callback (FALSE, num, user_data);
		nx_widget_destroy (GTK_WIDGET (dialog));
		break;
	}
}

static void
gtkutil_get_bool_response (GtkWidget *dialog, gint arg1, gpointer spin)
{
	void (*callback) (int value, void *user_data);
	void *user_data;

	callback = g_object_get_data (G_OBJECT (dialog), "cb");
	user_data = g_object_get_data (G_OBJECT (dialog), "ud");

	switch (arg1)
	{
	case GTK_RESPONSE_REJECT:
		callback (0, user_data);
		nx_widget_destroy (GTK_WIDGET (dialog));
		break;
	case GTK_RESPONSE_ACCEPT:
		callback (1, user_data);
		nx_widget_destroy (GTK_WIDGET (dialog));
		break;
	}
}

void
fe_get_int (char *msg, int def, void *callback, void *userdata)
{
	GtkWidget *dialog;
	GtkWidget *spin;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkAdjustment *adj;
	extern GtkWidget *parent_window;

	dialog = nx_dialog_new (msg, NULL, FALSE);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_CANCEL, GTK_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_OK, GTK_RESPONSE_ACCEPT);
	gtk_box_set_homogeneous (GTK_BOX (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog))), TRUE);
	nx_window_set_position_mouse (dialog);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));

	hbox = gtkutil_box_new (GTK_ORIENTATION_HORIZONTAL, TRUE, 0);

	g_object_set_data (G_OBJECT (dialog), "cb", callback);
	g_object_set_data (G_OBJECT (dialog), "ud", userdata);

	spin = gtk_spin_button_new (NULL, 1, 0);
	adj = gtk_spin_button_get_adjustment ((GtkSpinButton*)spin);
	gtk_adjustment_set_lower (adj, 0);
	gtk_adjustment_set_upper (adj, 1024);
	gtk_adjustment_set_step_increment (adj, 1);
	gtk_spin_button_set_value ((GtkSpinButton*)spin, def);
	nx_box_pack_end (hbox, spin, 0, 0, 0);

	label = gtk_label_new (msg);
	nx_box_pack_end (hbox, label, 0, 0, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (gtkutil_get_number_response), spin);

	gtkutil_container_add (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog)), hbox);

	nx_widget_show_all (dialog);
}

void
fe_get_bool (char *title, char *prompt, void *callback, void *userdata)
{
	GtkWidget *dialog;
	GtkWidget *prompt_label;
	extern GtkWidget *parent_window;

	dialog = nx_dialog_new (title, NULL, FALSE);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_NO, GTK_RESPONSE_REJECT);
	gtkutil_dialog_add_stock_button (NX_DIALOG_CAST (dialog), NX_STOCK_YES, GTK_RESPONSE_ACCEPT);
	gtk_box_set_homogeneous (GTK_BOX (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog))), TRUE);
	nx_window_set_position_mouse (dialog);
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent_window));


	g_object_set_data (G_OBJECT (dialog), "cb", callback);
	g_object_set_data (G_OBJECT (dialog), "ud", userdata);

	prompt_label = gtk_label_new (prompt);

	g_signal_connect (G_OBJECT (dialog), "response",
		G_CALLBACK (gtkutil_get_bool_response), NULL);

	gtkutil_container_add (nx_dialog_get_content_area (NX_DIALOG_CAST (dialog)), prompt_label);

	nx_widget_show_all (dialog);
}

GtkWidget *
gtkutil_button (GtkWidget *box, char *stock, char *tip, void *callback,
					 void *userdata, char *labeltext)
{
	GtkWidget *wid, *img, *bbox;

	wid = gtk_button_new ();

	if (labeltext)
	{
		gtk_button_set_label (GTK_BUTTON (wid), labeltext);
		gtk_button_set_use_underline (GTK_BUTTON (wid), TRUE);
		nx_button_set_image (wid, gtkutil_image_new_from_stock (stock, NX_ICON_MENU));
		if (box)
			gtkutil_container_add (GTK_WIDGET (box), wid);
	}
	else
	{
		bbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		gtkutil_container_add (GTK_WIDGET (wid), bbox);
		nx_widget_show (bbox);

		img = gtkutil_image_new_from_stock (stock, NX_ICON_MENU);
		gtkutil_container_add (GTK_WIDGET (bbox), img);
		nx_widget_show (img);
		nx_box_pack_start (box, wid, 0, 0, 0);
	}

	g_signal_connect (G_OBJECT (wid), "clicked",
							G_CALLBACK (callback), userdata);
	nx_widget_show (wid);
	if (tip)
		gtk_widget_set_tooltip_text (wid, tip);

	return wid;
}

void
gtkutil_label_new (char *text, GtkWidget * box)
{
	GtkWidget *label = gtk_label_new (text);
	gtkutil_container_add (GTK_WIDGET (box), label);
	nx_widget_show (label);
}

GtkWidget *
gtkutil_entry_new (int max, GtkWidget * box, void *callback,
						 gpointer userdata)
{
	GtkWidget *entry = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY (entry), max);
	gtkutil_container_add (GTK_WIDGET (box), entry);
	if (callback)
		g_signal_connect (G_OBJECT (entry), "changed",
								G_CALLBACK (callback), userdata);
	nx_widget_show (entry);
	return entry;
}

void
show_and_unfocus (GtkWidget * wid)
{
	gtk_widget_set_can_focus (wid, FALSE);
	nx_widget_show (wid);
}

void
gtkutil_set_icon (GtkWidget *win)
{
	/* GTK4: windows take the icon theme's app icon (set once, globally) */
	gtk_window_set_icon_name (GTK_WINDOW (win), "io.github.nexchat");
}

extern GtkWidget *parent_window;	/* maingui.c */

GtkWidget *
gtkutil_window_new (char *title, char *role, int width, int height, int flags)
{
	GtkWidget *win;

	win = gtk_window_new ();
	gtkutil_set_icon (win);
	gtk_window_set_title (GTK_WINDOW (win), title);
	gtk_window_set_default_size (GTK_WINDOW (win), width, height);
	nx_window_set_role (win, role);
	if (flags & 1)
		nx_window_set_position_mouse (win);
	if ((flags & 2) && parent_window)
	{
		nx_window_set_dialog_hint (win);
		gtk_window_set_transient_for (GTK_WINDOW (win), GTK_WINDOW (parent_window));
		gtk_window_set_destroy_with_parent (GTK_WINDOW (win), TRUE);
	}

	return win;
}

/* copy to both the clipboard and the primary (X11) selection */
void
gtkutil_copy_to_clipboard (GtkWidget *widget, const gchar *str)
{
	GtkWidget *win;

	win = nx_widget_get_toplevel (GTK_WIDGET (widget));
	if (win)
	{
		gdk_clipboard_set_text (gtk_widget_get_primary_clipboard (win), str);
		gdk_clipboard_set_text (gtk_widget_get_clipboard (win), str);
	}
}


gboolean
gtkutil_tray_icon_supported (GtkWindow *window)
{
	(void) window;
	return TRUE;
}


