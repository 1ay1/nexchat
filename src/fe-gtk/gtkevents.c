/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "gtkevents.h"
#include "gtkcompat.h"
#if defined(GDK_WINDOWING_X11)
#include <gdk/x11/gdkx.h>
#endif

typedef struct
{
	gpointer cb;
	gpointer userdata;
	guint button;		/* GTK4 release: button seen at drag-begin */
	gpointer userdata2;
} hook;

static hook *
hook_new (gpointer cb, gpointer userdata)
{
	hook *h = g_new (hook, 1);
	h->cb = cb;
	h->userdata = userdata;
	h->button = 0;
	h->userdata2 = NULL;
	return h;
}

static void
hook_free (gpointer h, GClosure *c)
{
	g_free (h);
}


/* ================= GTK4: event controllers ================= */

static GdkEvent *cur_event;
static GtkWidget *cur_widget;

GdkEvent *
nx_current_event (void)
{
	return cur_event;
}

GtkWidget *
nx_current_event_widget (void)
{
	return cur_widget;
}

static void
click_pressed (GtkGestureClick *g, int n_press, double x, double y, hook *h)
{
	GtkEventController *ec = GTK_EVENT_CONTROLLER (g);
	GtkWidget *w = gtk_event_controller_get_widget (ec);
	guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (g));
	GdkModifierType state = gtk_event_controller_get_current_event_state (ec);
	gboolean stop;

	cur_event = gtk_event_controller_get_current_event (ec);
	cur_widget = w;
	stop = ((nx_click_cb) h->cb) (w, button, n_press, x, y, state, h->userdata);
	cur_event = NULL;
	cur_widget = NULL;
	if (stop)
		gtk_gesture_set_state (GTK_GESTURE (g), GTK_EVENT_SEQUENCE_CLAIMED);
}

void
nx_on_click (GtkWidget *w, nx_click_cb cb, gpointer userdata)
{
	GtkGesture *g = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (g), 0);	/* any button */
	/* capture phase: like GTK3, our handler runs before the widget's own */
	gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (g), GTK_PHASE_CAPTURE);
	g_signal_connect_data (g, "pressed", G_CALLBACK (click_pressed),
								  hook_new (cb, userdata), hook_free, 0);
	gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (g));
}

static gboolean
key_pressed (GtkEventControllerKey *k, guint keyval, guint keycode,
				 GdkModifierType state, hook *h)
{
	GtkWidget *w = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (k));
	gboolean stop;

	cur_event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (k));
	cur_widget = w;
	stop = ((nx_key_cb) h->cb) (w, keyval, state, h->userdata);
	cur_event = NULL;
	cur_widget = NULL;
	return stop;
}

void
nx_on_key (GtkWidget *w, nx_key_cb cb, gpointer userdata)
{
	GtkEventController *k = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (k, GTK_PHASE_CAPTURE);
	g_signal_connect_data (k, "key-pressed", G_CALLBACK (key_pressed),
								  hook_new (cb, userdata), hook_free, 0);
	gtk_widget_add_controller (w, k);
}

static gboolean
scroll_cb (GtkEventControllerScroll *s, double dx, double dy, hook *h)
{
	GtkEventController *ec = GTK_EVENT_CONTROLLER (s);
	GtkWidget *w = gtk_event_controller_get_widget (ec);
	GdkModifierType state = gtk_event_controller_get_current_event_state (ec);
	GdkScrollDirection dir;

	/* DISCRETE: wheels give whole steps; one call per step like GTK3 */
	if (dy < 0)
		dir = GDK_SCROLL_UP;
	else if (dy > 0)
		dir = GDK_SCROLL_DOWN;
	else if (dx < 0)
		dir = GDK_SCROLL_LEFT;
	else if (dx > 0)
		dir = GDK_SCROLL_RIGHT;
	else
		return FALSE;
	return ((nx_scroll_cb) h->cb) (w, dir, state, h->userdata);
}

void
nx_on_scroll (GtkWidget *w, nx_scroll_cb cb, gpointer userdata)
{
	GtkEventController *s = gtk_event_controller_scroll_new (
		GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
	g_signal_connect_data (s, "scroll", G_CALLBACK (scroll_cb),
								  hook_new (cb, userdata), hook_free, 0);
	gtk_widget_add_controller (w, s);
}

/* a scroll as the device gave it: a trackpad's pixel deltas (and the
   momentum after the fingers lift, as "decelerate"), or a wheel's steps.
   unit tells which: GDK_SCROLL_UNIT_SURFACE (pixels) or _WHEEL (steps). */
static gboolean
smooth_scroll_cb (GtkEventControllerScroll *s, double dx, double dy, hook *h)
{
	GtkEventController *ec = GTK_EVENT_CONTROLLER (s);

	return ((nx_smooth_scroll_cb) h->cb) (gtk_event_controller_get_widget (ec), dx, dy,
													 gtk_event_controller_scroll_get_unit (s),
													 gtk_event_controller_get_current_event_state (ec),
													 h->userdata);
}

static void
smooth_decelerate_cb (GtkEventControllerScroll *s, double vx, double vy, hook *h)
{
	/* the momentum: hand it over as a velocity (pixels/s), the widget
	   glides with it */
	if (h->userdata2)
		((nx_decelerate_cb) h->userdata2) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (s)),
													  vx, vy, h->userdata);
}

void
nx_on_smooth_scroll (GtkWidget *w, nx_smooth_scroll_cb cb, nx_decelerate_cb decel, gpointer userdata)
{
	GtkEventController *s = gtk_event_controller_scroll_new (
		GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_KINETIC);
	hook *h = hook_new (cb, userdata);

	h->userdata2 = decel;
	g_signal_connect_data (s, "scroll", G_CALLBACK (smooth_scroll_cb), h, hook_free, 0);
	g_signal_connect (s, "decelerate", G_CALLBACK (smooth_decelerate_cb), h);
	gtk_widget_add_controller (w, s);
}

static void
motion_enter (GtkEventControllerMotion *m, double x, double y, hook *h)
{
	((nx_crossing_cb) h->cb) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (m)), TRUE, h->userdata);
}

static void
motion_leave (GtkEventControllerMotion *m, hook *h)
{
	((nx_crossing_cb) h->cb) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (m)), FALSE, h->userdata);
}

void
nx_on_crossing (GtkWidget *w, nx_crossing_cb cb, gpointer userdata)
{
	GtkEventController *m = gtk_event_controller_motion_new ();
	hook *h = hook_new (cb, userdata);
	g_signal_connect (m, "enter", G_CALLBACK (motion_enter), h);
	g_signal_connect_data (m, "leave", G_CALLBACK (motion_leave), h, hook_free, 0);
	gtk_widget_add_controller (w, m);
}

static void
focus_enter (GtkEventControllerFocus *f, hook *h)
{
	((nx_focus_cb) h->cb) (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (f)), h->userdata);
}

/* a window gets focus when it becomes active: a focus controller only
   sees focus move between its children, not the window coming back */
static void
window_active (GtkWindow *w, GParamSpec *ps, hook *h)
{
	if (gtk_window_is_active (w))
		((nx_focus_cb) h->cb) (GTK_WIDGET (w), h->userdata);
}

void
nx_on_focus_in (GtkWidget *w, nx_focus_cb cb, gpointer userdata)
{
	GtkEventController *f;

	if (GTK_IS_WINDOW (w))
	{
		/* GTK3's focus-in-event on a window: the window becoming active (keyboard
		   focus stays inside a GTK4 window across activations, so a focus
		   controller only sees the first one) */
		g_signal_connect_data (w, "notify::is-active", G_CALLBACK (window_active),
									  hook_new (cb, userdata), hook_free, 0);
		return;
	}
	f = gtk_event_controller_focus_new ();
	g_signal_connect_data (f, "enter", G_CALLBACK (focus_enter),
								  hook_new (cb, userdata), hook_free, 0);
	gtk_widget_add_controller (w, f);
}

/* A GtkGestureClick gives up once the pointer travels, so a release after
   a drag-select would be lost; a drag gesture always sees the release. */
static void
drag_begin (GtkGestureDrag *g, double x, double y, hook *h)
{
	h->button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (g));
}

static void
drag_end (GtkGestureDrag *g, double ox, double oy, hook *h)
{
	GtkEventController *ec = GTK_EVENT_CONTROLLER (g);
	GtkWidget *w = gtk_event_controller_get_widget (ec);
	double sx = 0, sy = 0;

	gtk_gesture_drag_get_start_point (g, &sx, &sy);
	cur_event = gtk_event_controller_get_current_event (ec);
	cur_widget = w;
	((nx_click_cb) h->cb) (w, h->button, 1, sx + ox, sy + oy,
								  gtk_event_controller_get_current_event_state (ec), h->userdata);
	cur_event = NULL;
	cur_widget = NULL;
}

void
nx_on_release (GtkWidget *w, nx_click_cb cb, gpointer userdata)
{
	GtkGesture *g = gtk_gesture_drag_new ();
	hook *h = hook_new (cb, userdata);

	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (g), 0);
	gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (g), GTK_PHASE_CAPTURE);
	g_signal_connect (g, "drag-begin", G_CALLBACK (drag_begin), h);
	g_signal_connect_data (g, "drag-end", G_CALLBACK (drag_end), h, hook_free, 0);
	gtk_widget_add_controller (w, GTK_EVENT_CONTROLLER (g));
}

static void
motion_cb (GtkEventControllerMotion *m, double x, double y, hook *h)
{
	GtkEventController *ec = GTK_EVENT_CONTROLLER (m);

	((nx_motion_cb) h->cb) (gtk_event_controller_get_widget (ec), x, y,
									gtk_event_controller_get_current_event_state (ec), h->userdata);
}

void
nx_on_motion (GtkWidget *w, nx_motion_cb cb, gpointer userdata)
{
	GtkEventController *m = gtk_event_controller_motion_new ();
	g_signal_connect_data (m, "motion", G_CALLBACK (motion_cb),
								  hook_new (cb, userdata), hook_free, 0);
	gtk_widget_add_controller (w, m);
}

/* ---- window state / geometry ---- */

GMainLoop *
nx_main_loop (void)
{
	static GMainLoop *loop;

	if (!loop)
		loop = g_main_loop_new (NULL, FALSE);
	return loop;
}

static GtkApplication *app;

GtkApplication *
nx_app (void)
{
	return app;
}

/* every toplevel joins the app as it's created: the app id names the
   window on Wayland (the dock/taskbar entry, its icon) */
static void
window_added (GListModel *toplevels, guint pos, guint removed, guint added, gpointer d)
{
	guint i;

	for (i = pos; i < pos + added; i++)
	{
		GObject *w = g_list_model_get_item (toplevels, i);
		if (GTK_IS_WINDOW (w) && !gtk_window_get_application (GTK_WINDOW (w)))
			gtk_window_set_application (GTK_WINDOW (w), app);
		g_object_unref (w);
	}
}

void
nx_app_init (void)
{
	GError *err = NULL;

	gtk_init ();
	/* we run our own main loop: register by hand, hold so the app
	   doesn't quit when a window closes (fe_exit quits) */
	app = gtk_application_new ("io.github.nexchat", G_APPLICATION_NON_UNIQUE);
	if (!g_application_register (G_APPLICATION (app), NULL, &err))
	{
		g_warning ("application: %s", err->message);
		g_error_free (err);
		g_clear_object (&app);
		return;
	}
	g_application_hold (G_APPLICATION (app));
	g_signal_connect (gtk_window_get_toplevels (), "items-changed", G_CALLBACK (window_added), NULL);
}

static nx_window_state
toplevel_state (GtkWindow *win)
{
	GdkSurface *s = gtk_native_get_surface (GTK_NATIVE (win));
	GdkToplevelState st = s ? gdk_toplevel_get_state (GDK_TOPLEVEL (s)) : 0;
	nx_window_state r = 0;

	if (st & GDK_TOPLEVEL_STATE_MINIMIZED)
		r |= NX_WIN_ICONIFIED;
	if (st & GDK_TOPLEVEL_STATE_MAXIMIZED)
		r |= NX_WIN_MAXIMIZED;
	if (st & GDK_TOPLEVEL_STATE_FULLSCREEN)
		r |= NX_WIN_FULLSCREEN;
	return r;
}

static void
surface_state_changed (GdkSurface *s, GParamSpec *ps, hook *h)
{
	GtkWindow *win = h->userdata2;
	nx_window_state now = toplevel_state (win);
	nx_window_state old = h->button;	/* last state seen */

	h->button = now;
	if (now != old)
		((nx_window_state_cb) h->cb) (win, now ^ old, now, h->userdata);
}

static void
window_state_realize (GtkWidget *w, hook *h)
{
	GdkSurface *s = gtk_native_get_surface (GTK_NATIVE (w));

	h->button = toplevel_state (GTK_WINDOW (w));
	g_signal_connect (s, "notify::state", G_CALLBACK (surface_state_changed), h);
}

void
nx_on_window_state (GtkWidget *win, nx_window_state_cb cb, gpointer ud)
{
	hook *h = hook_new (cb, ud);

	h->userdata2 = win;
	g_signal_connect_data (win, "realize", G_CALLBACK (window_state_realize), h, hook_free, 0);
}

/* GTK4 reports no moves (the WM owns positions); a size change is all.
   On X11 a ConfigureNotify on the window's own X window is a move too, as
   GTK3's configure-event was. */
static void
window_size_notify (GtkWidget *w, GParamSpec *ps, hook *h)
{
	((nx_window_configure_cb) h->cb) (GTK_WINDOW (w), h->userdata);
}

#ifdef GDK_WINDOWING_X11
G_GNUC_BEGIN_IGNORE_DEPRECATIONS	/* GTK 4.18: X11 getters, no replacement */
static gboolean
x11_configure (GdkDisplay *d, const XEvent *xev, hook *h)
{
	GtkWidget *w = h->userdata2;
	GdkSurface *s = gtk_native_get_surface (GTK_NATIVE (w));

	if ((xev->type == ConfigureNotify || xev->type == ReparentNotify) && s &&
		 xev->xany.window == gdk_x11_surface_get_xid (s))
		((nx_window_configure_cb) h->cb) (GTK_WINDOW (w), h->userdata);
	return FALSE;		/* GDK handles it as usual */
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void
x11_configure_unhook (GtkWidget *w, hook *h)
{
	g_signal_handlers_disconnect_by_func (gtk_widget_get_display (w), x11_configure, h);
}
#endif

void
nx_on_window_configure (GtkWidget *win, nx_window_configure_cb cb, gpointer ud)
{
	hook *h = hook_new (cb, ud);

	g_signal_connect (win, "notify::default-width", G_CALLBACK (window_size_notify), h);
	g_signal_connect_data (win, "notify::default-height", G_CALLBACK (window_size_notify), h,
								  hook_free, 0);
#ifdef GDK_WINDOWING_X11
	if (GDK_IS_X11_DISPLAY (gtk_widget_get_display (win)))
	{
		h->userdata2 = win;
		g_signal_connect (gtk_widget_get_display (win), "xevent", G_CALLBACK (x11_configure), h);
		g_signal_connect (win, "destroy", G_CALLBACK (x11_configure_unhook), h);
	}
#endif
}


