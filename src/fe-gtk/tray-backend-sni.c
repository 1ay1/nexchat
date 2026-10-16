/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Tray icon for the GTK4 build on Linux: a StatusNotifierItem (KDE, GNOME
   with the AppIndicator extension, waybar, swaybar, xfce4-panel, ...) with
   its menu exported as com.canonical.dbusmenu, both over GDBus.

   The menu is the same nx_menu plugin-tray.c builds for GtkStatusIcon. The
   panel asks for it with AboutToShow/GetLayout; we build it then (through
   the menu callback) and keep it until the next one. Icons go out as ARGB
   pixmaps, so the blinking icons work without an icon theme. */

#include <string.h>
#include <unistd.h>
#include <gio/gio.h>
#include "tray-backend.h"

#define SNI_PATH "/StatusNotifierItem"
#define MENU_PATH "/MenuBar"
#define WATCHER "org.kde.StatusNotifierWatcher"

static const char sni_xml[] =
	"<node>"
	" <interface name='org.kde.StatusNotifierItem'>"
	"  <property name='Category' type='s' access='read'/>"
	"  <property name='Id' type='s' access='read'/>"
	"  <property name='Title' type='s' access='read'/>"
	"  <property name='Status' type='s' access='read'/>"
	"  <property name='IconName' type='s' access='read'/>"
	"  <property name='IconPixmap' type='a(iiay)' access='read'/>"
	"  <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
	"  <property name='ItemIsMenu' type='b' access='read'/>"
	"  <property name='Menu' type='o' access='read'/>"
	"  <method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
	"  <method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
	"  <method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
	"  <method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>"
	"  <signal name='NewIcon'/>"
	"  <signal name='NewToolTip'/>"
	"  <signal name='NewStatus'><arg type='s'/></signal>"
	" </interface>"
	"</node>";

static const char menu_xml[] =
	"<node>"
	" <interface name='com.canonical.dbusmenu'>"
	"  <property name='Version' type='u' access='read'/>"
	"  <property name='TextDirection' type='s' access='read'/>"
	"  <property name='Status' type='s' access='read'/>"
	"  <method name='GetLayout'>"
	"   <arg type='i' name='parentId' direction='in'/>"
	"   <arg type='i' name='recursionDepth' direction='in'/>"
	"   <arg type='as' name='propertyNames' direction='in'/>"
	"   <arg type='u' name='revision' direction='out'/>"
	"   <arg type='(ia{sv}av)' name='layout' direction='out'/>"
	"  </method>"
	"  <method name='GetGroupProperties'>"
	"   <arg type='ai' name='ids' direction='in'/>"
	"   <arg type='as' name='propertyNames' direction='in'/>"
	"   <arg type='a(ia{sv})' name='properties' direction='out'/>"
	"  </method>"
	"  <method name='GetProperty'>"
	"   <arg type='i' name='id' direction='in'/>"
	"   <arg type='s' name='name' direction='in'/>"
	"   <arg type='v' name='value' direction='out'/>"
	"  </method>"
	"  <method name='Event'>"
	"   <arg type='i' name='id' direction='in'/>"
	"   <arg type='s' name='eventId' direction='in'/>"
	"   <arg type='v' name='data' direction='in'/>"
	"   <arg type='u' name='timestamp' direction='in'/>"
	"  </method>"
	"  <method name='EventGroup'>"
	"   <arg type='a(isvu)' name='events' direction='in'/>"
	"   <arg type='ai' name='idErrors' direction='out'/>"
	"  </method>"
	"  <method name='AboutToShow'>"
	"   <arg type='i' name='id' direction='in'/>"
	"   <arg type='b' name='needUpdate' direction='out'/>"
	"  </method>"
	"  <method name='AboutToShowGroup'>"
	"   <arg type='ai' name='ids' direction='in'/>"
	"   <arg type='ai' name='updatesNeeded' direction='out'/>"
	"   <arg type='ai' name='idErrors' direction='out'/>"
	"  </method>"
	"  <signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
	"  <signal name='ItemsPropertiesUpdated'>"
	"   <arg type='a(ia{sv})'/><arg type='a(ias)'/>"
	"  </signal>"
	" </interface>"
	"</node>";

struct _tray_backend
{
	GDBusConnection *bus;
	GDBusNodeInfo *sni_info, *menu_info;
	guint sni_reg, menu_reg, name_id, watch_id;
	char *bus_name;
	GdkPixbuf *pixbuf;
	char *tooltip;
	gboolean embedded;		/* a watcher took us */
	tray_backend_callbacks cb;
	gpointer userdata;

	nx_menu *menu;				/* the last menu built for the panel */
	GPtrArray *ids;			/* dbusmenu id -> nx_item (index 0 unused: root) */
	guint revision;
};

/* ---- icon: ARGB32, network byte order, as the spec wants ---- */

static GVariant *
pixmap_variant (GdkPixbuf *pix)
{
	GVariantBuilder b;
	int w, h, stride, ch, x, y;
	const guchar *src;
	guchar *argb;
	gboolean alpha;

	g_variant_builder_init (&b, G_VARIANT_TYPE ("a(iiay)"));
	if (!pix)
		return g_variant_builder_end (&b);
	w = gdk_pixbuf_get_width (pix);
	h = gdk_pixbuf_get_height (pix);
	stride = gdk_pixbuf_get_rowstride (pix);
	ch = gdk_pixbuf_get_n_channels (pix);
	alpha = gdk_pixbuf_get_has_alpha (pix);
	src = gdk_pixbuf_read_pixels (pix);
	argb = g_malloc ((gsize) w * h * 4);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			const guchar *p = src + y * stride + x * ch;
			guchar *d = argb + ((gsize) y * w + x) * 4;
			d[0] = alpha ? p[3] : 0xff;
			d[1] = p[0];
			d[2] = p[1];
			d[3] = p[2];
		}
	g_variant_builder_add (&b, "(ii@ay)", w, h,
								  g_variant_new_from_data (G_VARIANT_TYPE ("ay"), argb, (gsize) w * h * 4,
															  TRUE, g_free, argb));
	return g_variant_builder_end (&b);
}

/* ---- the StatusNotifierItem ---- */

static GVariant *
sni_get (GDBusConnection *c, const char *sender, const char *path, const char *iface,
			const char *prop, GError **err, gpointer data)
{
	tray_backend *tb = data;

	if (!strcmp (prop, "Category"))
		return g_variant_new_string ("Communications");
	if (!strcmp (prop, "Id"))
		return g_variant_new_string ("nexchat");
	if (!strcmp (prop, "Title"))
		return g_variant_new_string ("nexchat");
	if (!strcmp (prop, "Status"))
		return g_variant_new_string ("Active");
	if (!strcmp (prop, "IconName"))
		return g_variant_new_string ("");
	if (!strcmp (prop, "IconPixmap"))
		return pixmap_variant (tb->pixbuf);
	if (!strcmp (prop, "ToolTip"))
		return g_variant_new ("(s@a(iiay)ss)", "", pixmap_variant (NULL),
									 tb->tooltip ? tb->tooltip : "", "");
	if (!strcmp (prop, "ItemIsMenu"))
		return g_variant_new_boolean (FALSE);
	if (!strcmp (prop, "Menu"))
		return g_variant_new_object_path (MENU_PATH);
	return NULL;
}

static void
sni_call (GDBusConnection *c, const char *sender, const char *path, const char *iface,
			 const char *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data)
{
	tray_backend *tb = data;

	if (!strcmp (method, "Activate"))
	{
		if (tb->cb.activate)
			tb->cb.activate (tb, tb->userdata);
	}
	else if (!strcmp (method, "ContextMenu"))
	{
		/* panels that don't read Menu ask us to pop one: nothing to pop
		   it on here, so build it for the next GetLayout */
		if (tb->cb.menu)
			tb->cb.menu (tb, 3, GDK_CURRENT_TIME, tb->userdata);
	}
	g_dbus_method_invocation_return_value (inv, NULL);
}

static const GDBusInterfaceVTable sni_vtable = { sni_call, sni_get, NULL, { 0 } };

static void
sni_emit (tray_backend *tb, const char *signal)
{
	if (tb->bus && tb->sni_reg)
		g_dbus_connection_emit_signal (tb->bus, NULL, SNI_PATH, "org.kde.StatusNotifierItem",
												 signal, NULL, NULL);
}

/* ---- the menu as com.canonical.dbusmenu ---- */

static int
item_id (tray_backend *tb, nx_item *it)
{
	g_ptr_array_add (tb->ids, it);
	return tb->ids->len - 1;
}

static GVariant *
item_props (nx_item *it)
{
	GVariantBuilder p;
	nx_item_kind k = nx_item_get_kind (it);
	const char *label;

	g_variant_builder_init (&p, G_VARIANT_TYPE ("a{sv}"));
	if (k == NX_KIND_SEPARATOR)
	{
		g_variant_builder_add (&p, "{sv}", "type", g_variant_new_string ("separator"));
		return g_variant_builder_end (&p);
	}
	/* dbusmenu labels use "_" mnemonics like GTK's; markup isn't
	   allowed there, so markup labels go as plain text */
	label = nx_item_get_label (it);
	if (label && strchr (label, '<'))
		label = nx_item_get_text (it);
	g_variant_builder_add (&p, "{sv}", "label", g_variant_new_string (label ? label : ""));
	if (!nx_item_get_sensitive (it))
		g_variant_builder_add (&p, "{sv}", "enabled", g_variant_new_boolean (FALSE));
	if (k == NX_KIND_CHECK || k == NX_KIND_RADIO)
	{
		g_variant_builder_add (&p, "{sv}", "toggle-type",
									  g_variant_new_string (k == NX_KIND_CHECK ? "checkmark" : "radio"));
		g_variant_builder_add (&p, "{sv}", "toggle-state",
									  g_variant_new_int32 (nx_item_get_active (it) ? 1 : 0));
	}
	if (k == NX_KIND_SUBMENU)
		g_variant_builder_add (&p, "{sv}", "children-display", g_variant_new_string ("submenu"));
	return g_variant_builder_end (&p);
}

static GVariant *
layout (tray_backend *tb, nx_menu *m, int id, GVariant *props)
{
	GVariantBuilder kids;
	int i, n = m ? nx_menu_get_n_items (m) : 0;

	g_variant_builder_init (&kids, G_VARIANT_TYPE ("av"));
	for (i = 0; i < n; i++)
	{
		nx_item *it = nx_menu_get_item (m, i);
		int cid = item_id (tb, it);

		g_variant_builder_add (&kids, "v",
									  layout (tb, nx_item_get_submenu (it), cid, item_props (it)));
	}
	return g_variant_new ("(i@a{sv}av)", id, props, &kids);
}

/* the panel is about to show the menu: build a fresh one */
static void
menu_rebuild (tray_backend *tb)
{
	if (tb->cb.menu)
		tb->cb.menu (tb, 3, GDK_CURRENT_TIME, tb->userdata);
}

static GVariant *
root_props (void)
{
	GVariantBuilder p;

	g_variant_builder_init (&p, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (&p, "{sv}", "children-display", g_variant_new_string ("submenu"));
	return g_variant_builder_end (&p);
}

static void
menu_call (GDBusConnection *c, const char *sender, const char *path, const char *iface,
			  const char *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data)
{
	tray_backend *tb = data;

	if (!strcmp (method, "GetLayout"))
	{
		GVariant *l;

		if (!tb->menu)
			menu_rebuild (tb);
		g_ptr_array_set_size (tb->ids, 1);
		/* the whole tree every time; the menu is small */
		l = layout (tb, tb->menu, 0, root_props ());
		g_dbus_method_invocation_return_value (inv, g_variant_new ("(u@(ia{sv}av))", tb->revision, l));
	}
	else if (!strcmp (method, "AboutToShow"))
	{
		gint32 id;

		g_variant_get (params, "(i)", &id);
		if (id == 0)
		{
			menu_rebuild (tb);
			g_dbus_method_invocation_return_value (inv, g_variant_new ("(b)", TRUE));
		}
		else
			g_dbus_method_invocation_return_value (inv, g_variant_new ("(b)", FALSE));
	}
	else if (!strcmp (method, "AboutToShowGroup"))
	{
		menu_rebuild (tb);
		g_dbus_method_invocation_return_value (inv, g_variant_new ("(@ai@ai)",
							g_variant_new_array (G_VARIANT_TYPE_INT32, NULL, 0),
							g_variant_new_array (G_VARIANT_TYPE_INT32, NULL, 0)));
	}
	else if (!strcmp (method, "Event") || !strcmp (method, "EventGroup"))
	{
		gint32 id;
		const char *ev;

		if (!strcmp (method, "Event"))
		{
			g_variant_get (params, "(i&svu)", &id, &ev, NULL, NULL);
			g_dbus_method_invocation_return_value (inv, NULL);
		}
		else
		{
			GVariantIter *it;

			g_variant_get (params, "(a(isvu))", &it);
			if (!g_variant_iter_next (it, "(i&svu)", &id, &ev, NULL, NULL))
				id = -1, ev = "";
			g_variant_iter_free (it);
			g_dbus_method_invocation_return_value (inv, g_variant_new ("(@ai)",
								g_variant_new_array (G_VARIANT_TYPE_INT32, NULL, 0)));
		}
		if (!strcmp (ev, "clicked") && id > 0 && (guint) id < tb->ids->len)
			nx_item_activate (g_ptr_array_index (tb->ids, id));
	}
	else if (!strcmp (method, "GetGroupProperties"))
	{
		GVariantBuilder b;
		GVariantIter *ids;
		gint32 id;

		g_variant_get (params, "(aias)", &ids, NULL);
		g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ia{sv})"));
		while (g_variant_iter_next (ids, "i", &id))
			if (id > 0 && (guint) id < tb->ids->len)
				g_variant_builder_add (&b, "(i@a{sv})", id, item_props (g_ptr_array_index (tb->ids, id)));
		g_variant_iter_free (ids);
		g_dbus_method_invocation_return_value (inv, g_variant_new ("(@a(ia{sv}))", g_variant_builder_end (&b)));
	}
	else if (!strcmp (method, "GetProperty"))
		g_dbus_method_invocation_return_error (inv, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
															"use GetGroupProperties");
	else
		g_dbus_method_invocation_return_error (inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "%s", method);
}

static GVariant *
menu_get (GDBusConnection *c, const char *sender, const char *path, const char *iface,
			 const char *prop, GError **err, gpointer data)
{
	if (!strcmp (prop, "Version"))
		return g_variant_new_uint32 (3);
	if (!strcmp (prop, "TextDirection"))
		return g_variant_new_string ("ltr");
	if (!strcmp (prop, "Status"))
		return g_variant_new_string ("normal");
	return NULL;
}

static const GDBusInterfaceVTable menu_vtable = { menu_call, menu_get, NULL, { 0 } };

/* ---- registering with the watcher ---- */

static void
register_done (GObject *src, GAsyncResult *res, gpointer data)
{
	tray_backend *tb = data;
	GVariant *r = g_dbus_connection_call_finish (G_DBUS_CONNECTION (src), res, NULL);

	tb->embedded = r != NULL;
	if (r)
		g_variant_unref (r);
	if (tb->cb.embedded_changed)
		tb->cb.embedded_changed (tb, tb->embedded, tb->userdata);
}

static void
watcher_appeared (GDBusConnection *c, const char *name, const char *owner, gpointer data)
{
	tray_backend *tb = data;

	g_dbus_connection_call (c, WATCHER, "/StatusNotifierWatcher", WATCHER,
									"RegisterStatusNotifierItem", g_variant_new ("(s)", tb->bus_name),
									NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, register_done, tb);
}

static void
watcher_vanished (GDBusConnection *c, const char *name, gpointer data)
{
	tray_backend *tb = data;

	if (tb->embedded)
	{
		tb->embedded = FALSE;
		if (tb->cb.embedded_changed)
			tb->cb.embedded_changed (tb, tb->embedded, tb->userdata);
	}
}

/* ---- tray_backend ---- */

tray_backend *
tray_backend_new (GdkPixbuf *icon, const tray_backend_callbacks *cb, gpointer userdata)
{
	static int count;
	tray_backend *tb;
	GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

	if (!bus)
		return NULL;
	tb = g_new0 (tray_backend, 1);
	tb->bus = bus;
	tb->cb = *cb;
	tb->userdata = userdata;
	tb->pixbuf = icon;
	tb->ids = g_ptr_array_new ();
	g_ptr_array_add (tb->ids, NULL);
	tb->revision = 1;
	tb->sni_info = g_dbus_node_info_new_for_xml (sni_xml, NULL);
	tb->menu_info = g_dbus_node_info_new_for_xml (menu_xml, NULL);
	tb->sni_reg = g_dbus_connection_register_object (bus, SNI_PATH, tb->sni_info->interfaces[0],
																	 &sni_vtable, tb, NULL, NULL);
	tb->menu_reg = g_dbus_connection_register_object (bus, MENU_PATH, tb->menu_info->interfaces[0],
																	  &menu_vtable, tb, NULL, NULL);
	/* the name the spec asks for; the watcher is told it */
	tb->bus_name = g_strdup_printf ("org.kde.StatusNotifierItem-%d-%d", (int) getpid (), ++count);
	tb->name_id = g_bus_own_name_on_connection (bus, tb->bus_name, G_BUS_NAME_OWNER_FLAGS_NONE,
															  NULL, NULL, NULL, NULL);
	tb->watch_id = g_bus_watch_name_on_connection (bus, WATCHER, G_BUS_NAME_WATCHER_FLAGS_NONE,
																  watcher_appeared, watcher_vanished, tb, NULL);
	return tb;
}

void
tray_backend_free (tray_backend *tb)
{
	if (!tb)
		return;
	g_bus_unwatch_name (tb->watch_id);
	g_bus_unown_name (tb->name_id);
	g_dbus_connection_unregister_object (tb->bus, tb->sni_reg);
	g_dbus_connection_unregister_object (tb->bus, tb->menu_reg);
	g_dbus_node_info_unref (tb->sni_info);
	g_dbus_node_info_unref (tb->menu_info);
	if (tb->menu)
		nx_menu_free (tb->menu);
	g_ptr_array_free (tb->ids, TRUE);
	g_free (tb->bus_name);
	g_free (tb->tooltip);
	g_object_unref (tb->bus);
	g_free (tb);
}

void
tray_backend_set_icon (tray_backend *tb, GdkPixbuf *icon)
{
	tb->pixbuf = icon;
	sni_emit (tb, "NewIcon");
}

GdkPixbuf *
tray_backend_get_icon (tray_backend *tb)
{
	return tb->pixbuf;
}

void
tray_backend_set_tooltip (tray_backend *tb, const char *text)
{
	g_free (tb->tooltip);
	tb->tooltip = g_strdup (text);
	sni_emit (tb, "NewToolTip");
}

gboolean
tray_backend_is_embedded (tray_backend *tb)
{
	return tb->embedded;
}

/* plugin-tray.c built the menu; the panel shows it: keep it for GetLayout
   and Event, and tell the panel it changed */
void
tray_backend_show_menu (tray_backend *tb, nx_menu *menu, guint button, guint32 time)
{
	if (tb->menu)
		nx_menu_free (tb->menu);
	tb->menu = menu;
	g_ptr_array_set_size (tb->ids, 1);
	tb->revision++;
	g_dbus_connection_emit_signal (tb->bus, NULL, MENU_PATH, "com.canonical.dbusmenu",
											 "LayoutUpdated", g_variant_new ("(ui)", tb->revision, 0), NULL);
}
