/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "gtkevents.h"

typedef struct
{
	gpointer cb;
	gpointer userdata;
	guint button;		/* GTK4 release: button seen at drag-begin */
} hook;

static hook *
hook_new (gpointer cb, gpointer userdata)
{
	hook *h = g_new (hook, 1);
	h->cb = cb;
	h->userdata = userdata;
	h->button = 0;
	return h;
}

static void
hook_free (gpointer h, GClosure *c)
{
	g_free (h);
}

#if GTK_CHECK_VERSION(4, 0, 0)

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

void
nx_on_focus_in (GtkWidget *w, nx_focus_cb cb, gpointer userdata)
{
	GtkEventController *f = gtk_event_controller_focus_new ();
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

#else

/* ================= GTK3: the old "*-event" signals ================= */

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

static gboolean
button_press_sig (GtkWidget *w, GdkEventButton *ev, hook *h)
{
	int n;
	gboolean stop;

	/* GTK3 sends BUTTON_PRESS, then 2BUTTON_PRESS / 3BUTTON_PRESS on top
	   for double/triple clicks; pass the click count, same sequence */
	switch (ev->type)
	{
	case GDK_2BUTTON_PRESS: n = 2; break;
	case GDK_3BUTTON_PRESS: n = 3; break;
	case GDK_BUTTON_PRESS: n = 1; break;
	default: return FALSE;
	}
	cur_event = (GdkEvent *) ev;
	cur_widget = w;
	stop = ((nx_click_cb) h->cb) (w, ev->button, n, ev->x, ev->y, ev->state, h->userdata);
	cur_event = NULL;
	cur_widget = NULL;
	return stop;
}

void
nx_on_click (GtkWidget *w, nx_click_cb cb, gpointer userdata)
{
	gtk_widget_add_events (w, GDK_BUTTON_PRESS_MASK);
	g_signal_connect_data (w, "button-press-event", G_CALLBACK (button_press_sig),
								  hook_new (cb, userdata), hook_free, 0);
}

static gboolean
key_press_sig (GtkWidget *w, GdkEventKey *ev, hook *h)
{
	gboolean stop;
	cur_event = (GdkEvent *) ev;
	cur_widget = w;
	stop = ((nx_key_cb) h->cb) (w, ev->keyval, ev->state, h->userdata);
	cur_event = NULL;
	cur_widget = NULL;
	return stop;
}

void
nx_on_key (GtkWidget *w, nx_key_cb cb, gpointer userdata)
{
	g_signal_connect_data (w, "key-press-event", G_CALLBACK (key_press_sig),
								  hook_new (cb, userdata), hook_free, 0);
}

static gboolean
scroll_sig (GtkWidget *w, GdkEventScroll *ev, hook *h)
{
	GdkScrollDirection dir = ev->direction;

	if (dir == GDK_SCROLL_SMOOTH)
	{
		double dx, dy;
		gdk_event_get_scroll_deltas ((GdkEvent *) ev, &dx, &dy);
		if (dy < 0) dir = GDK_SCROLL_UP;
		else if (dy > 0) dir = GDK_SCROLL_DOWN;
		else if (dx < 0) dir = GDK_SCROLL_LEFT;
		else if (dx > 0) dir = GDK_SCROLL_RIGHT;
		else return FALSE;
	}
	return ((nx_scroll_cb) h->cb) (w, dir, ev->state, h->userdata);
}

void
nx_on_scroll (GtkWidget *w, nx_scroll_cb cb, gpointer userdata)
{
	gtk_widget_add_events (w, GDK_SCROLL_MASK);
	g_signal_connect_data (w, "scroll-event", G_CALLBACK (scroll_sig),
								  hook_new (cb, userdata), hook_free, 0);
}

static gboolean
enter_sig (GtkWidget *w, GdkEventCrossing *ev, hook *h)
{
	((nx_crossing_cb) h->cb) (w, TRUE, h->userdata);
	return FALSE;
}

static gboolean
leave_sig (GtkWidget *w, GdkEventCrossing *ev, hook *h)
{
	((nx_crossing_cb) h->cb) (w, FALSE, h->userdata);
	return FALSE;
}

void
nx_on_crossing (GtkWidget *w, nx_crossing_cb cb, gpointer userdata)
{
	hook *h = hook_new (cb, userdata);
	gtk_widget_add_events (w, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect (w, "enter-notify-event", G_CALLBACK (enter_sig), h);
	g_signal_connect_data (w, "leave-notify-event", G_CALLBACK (leave_sig), h, hook_free, 0);
}

static gboolean
focus_in_sig (GtkWidget *w, GdkEventFocus *ev, hook *h)
{
	((nx_focus_cb) h->cb) (w, h->userdata);
	return FALSE;
}

void
nx_on_focus_in (GtkWidget *w, nx_focus_cb cb, gpointer userdata)
{
	g_signal_connect_data (w, "focus-in-event", G_CALLBACK (focus_in_sig),
								  hook_new (cb, userdata), hook_free, 0);
}

static gboolean
button_release_sig (GtkWidget *w, GdkEventButton *ev, hook *h)
{
	gboolean stop;
	cur_event = (GdkEvent *) ev;
	cur_widget = w;
	stop = ((nx_click_cb) h->cb) (w, ev->button, 1, ev->x, ev->y, ev->state, h->userdata);
	cur_event = NULL;
	cur_widget = NULL;
	return stop;
}

void
nx_on_release (GtkWidget *w, nx_click_cb cb, gpointer userdata)
{
	gtk_widget_add_events (w, GDK_BUTTON_RELEASE_MASK);
	g_signal_connect_data (w, "button-release-event", G_CALLBACK (button_release_sig),
								  hook_new (cb, userdata), hook_free, 0);
}

static gboolean
motion_sig (GtkWidget *w, GdkEventMotion *ev, hook *h)
{
	((nx_motion_cb) h->cb) (w, ev->x, ev->y, ev->state, h->userdata);
	return FALSE;
}

void
nx_on_motion (GtkWidget *w, nx_motion_cb cb, gpointer userdata)
{
	gtk_widget_add_events (w, GDK_POINTER_MOTION_MASK);
	g_signal_connect_data (w, "motion-notify-event", G_CALLBACK (motion_sig),
								  hook_new (cb, userdata), hook_free, 0);
}

#endif
