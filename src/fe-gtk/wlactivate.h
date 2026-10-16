#ifndef NEXCHAT_WLACTIVATE_H
#define NEXCHAT_WLACTIVATE_H

#include <glib.h>

struct wl_display;
struct wl_surface;

/* Ask the compositor to mark the surface as wanting attention (the
   Wayland urgency hint). FALSE if it has no xdg-activation. */
gboolean wl_request_attention (struct wl_display *dpy, struct wl_surface *surface);

#endif
