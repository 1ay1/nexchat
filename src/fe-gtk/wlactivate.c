/* nexchat
 * Copyright (C) 2025 nexchat contributors.
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

/* Urgency on Wayland: there is no hint, so do what the protocol offers.
   An xdg-activation token with no serial can't steal focus; activating
   with it makes the compositor mark the window urgent instead. */

#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "xdg-activation-v1-client-protocol.h"
#include "wlactivate.h"

static void
reg_global (void *data, struct wl_registry *reg, uint32_t name,
				const char *iface, uint32_t version)
{
	struct xdg_activation_v1 **act = data;

	if (!strcmp (iface, xdg_activation_v1_interface.name))
		*act = wl_registry_bind (reg, name, &xdg_activation_v1_interface, 1);
}

static void
reg_global_remove (void *data, struct wl_registry *reg, uint32_t name)
{
}

static const struct wl_registry_listener reg_listener = {
	reg_global, reg_global_remove
};

static void
token_done (void *data, struct xdg_activation_token_v1 *tok, const char *token)
{
	*(char **) data = strdup (token);
}

static const struct xdg_activation_token_v1_listener token_listener = {
	token_done
};

gboolean
wl_request_attention (struct wl_display *dpy, struct wl_surface *surface)
{
	/* our own queue, so GDK's dispatching never sees these objects */
	struct wl_event_queue *q;
	struct wl_display *wrap;
	struct wl_registry *reg;
	struct xdg_activation_v1 *act = NULL;
	struct xdg_activation_token_v1 *tok;
	char *token = NULL;

	if (!dpy || !surface)
		return FALSE;

	q = wl_display_create_queue (dpy);
	wrap = wl_proxy_create_wrapper (dpy);
	wl_proxy_set_queue ((struct wl_proxy *) wrap, q);
	reg = wl_display_get_registry (wrap);
	wl_registry_add_listener (reg, &reg_listener, &act);
	wl_display_roundtrip_queue (dpy, q);

	if (act)
	{
		tok = xdg_activation_v1_get_activation_token (act);
		xdg_activation_token_v1_add_listener (tok, &token_listener, &token);
		xdg_activation_token_v1_set_surface (tok, surface);
		xdg_activation_token_v1_commit (tok);
		while (!token && wl_display_roundtrip_queue (dpy, q) >= 0)
			;
		xdg_activation_token_v1_destroy (tok);
		if (token)
			xdg_activation_v1_activate (act, token, surface);
		xdg_activation_v1_destroy (act);
		wl_display_flush (dpy);
	}

	free (token);
	wl_registry_destroy (reg);
	wl_proxy_wrapper_destroy (wrap);
	wl_event_queue_destroy (q);
	return act != NULL;
}
