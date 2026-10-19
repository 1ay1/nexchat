/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Toolkit-independent input hooks (phase 4, step 4).
 *
 * GTK3 delivers input as GdkEvent* through "*-event" signals; GTK4 through
 * event controllers (GtkGestureClick, GtkEventControllerKey/Scroll/
 * Motion/Focus) with no GdkEvent struct to read fields from. Handlers here
 * take plain values, so the same handler works under both; only
 * gtkevents.c knows how each GTK delivers them.
 *
 * Return TRUE from a handler to stop the event (GTK3: return TRUE from the
 * signal; GTK4: claim the gesture / return TRUE from the controller). */

#ifndef NEXCHAT_GTKEVENTS_H
#define NEXCHAT_GTKEVENTS_H

#include <gtk/gtk.h>

/* mouse button pressed. n_press: 1 single, 2 double (GDK_2BUTTON_PRESS),
   3 triple. x/y widget-relative. */
typedef gboolean (*nx_click_cb) (GtkWidget *w, guint button, int n_press,
											double x, double y, GdkModifierType state,
											gpointer userdata);
/* key pressed. Same return meaning as a GTK3 key-press handler. */
typedef gboolean (*nx_key_cb) (GtkWidget *w, guint keyval, GdkModifierType state,
										 gpointer userdata);
/* wheel: dir is GDK_SCROLL_UP/DOWN/LEFT/RIGHT; smooth deltas are mapped to
   those, one call per step, as GTK3 did for non-smooth wheels */
typedef gboolean (*nx_scroll_cb) (GtkWidget *w, GdkScrollDirection dir,
											 GdkModifierType state, gpointer userdata);
/* pointer entered (TRUE) or left (FALSE) the widget */
typedef void (*nx_crossing_cb) (GtkWidget *w, gboolean entered, gpointer userdata);
/* keyboard focus came in */
typedef void (*nx_focus_cb) (GtkWidget *w, gpointer userdata);
/* pointer moved; x/y widget-relative (outside the widget while a button
   pressed in it is held, as with GTK3's implicit grab) */
typedef void (*nx_motion_cb) (GtkWidget *w, double x, double y,
										GdkModifierType state, gpointer userdata);

void nx_on_click (GtkWidget *w, nx_click_cb cb, gpointer userdata);
void nx_on_key (GtkWidget *w, nx_key_cb cb, gpointer userdata);
void nx_on_scroll (GtkWidget *w, nx_scroll_cb cb, gpointer userdata);

/* smooth scrolling for content that follows the fingers: dx/dy are pixels
   when unit is GDK_SCROLL_UNIT_SURFACE (trackpads, Magic Mouse), steps
   when GDK_SCROLL_UNIT_WHEEL (wheels); decel gets the momentum's velocity
   (pixels/s) after a flick */
typedef gboolean (*nx_smooth_scroll_cb) (GtkWidget *w, double dx, double dy, GdkScrollUnit unit,
													  GdkModifierType state, gpointer userdata);
typedef void (*nx_decelerate_cb) (GtkWidget *w, double vx, double vy, gpointer userdata);
void nx_on_smooth_scroll (GtkWidget *w, nx_smooth_scroll_cb cb, nx_decelerate_cb decel, gpointer userdata);
void nx_on_crossing (GtkWidget *w, nx_crossing_cb cb, gpointer userdata);
void nx_on_focus_in (GtkWidget *w, nx_focus_cb cb, gpointer userdata);
/* button released (n_press is always 1); the return value only matters
   on GTK3 */
void nx_on_release (GtkWidget *w, nx_click_cb cb, gpointer userdata);
void nx_on_motion (GtkWidget *w, nx_motion_cb cb, gpointer userdata);

/* The event currently being handled, as a GdkEvent*, for the few callers
   that still need one (menu popups positioned at the pointer). NULL
   outside a handler. */
GdkEvent *nx_current_event (void);
/* the widget that event arrived on (the one the hook is on) */
GtkWidget *nx_current_event_widget (void);

#endif
