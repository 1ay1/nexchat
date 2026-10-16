/* example.c - example of a D-Bus client for nexchat (GDBus)
 * Copyright (C) 2006 Claessens Xavier
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
 *
 * Claessens Xavier
 * xclaesse@gmail.com
 */

/* build: cc example.c $(pkg-config --cflags --libs gio-2.0)
   then type "/test arg1 arg2" in nexchat */

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#define DBUS_SERVICE "io.github.nexchat.service"
#define DBUS_REMOTE "/org/hexchat/Remote"
#define DBUS_REMOTE_CONNECTION_INTERFACE "org.hexchat.connection"
#define DBUS_REMOTE_PLUGIN_INTERFACE "org.hexchat.plugin"

static guint command_id;
static guint server_id;

static void
write_error (const char *message, GError **error)
{
	if (error == NULL || *error == NULL)
		return;
	g_printerr ("%s: %s\n", message, (*error)->message);
	g_clear_error (error);
}

static void
on_signal (GDBusProxy *proxy, const char *sender, const char *signal,
			  GVariant *params, gpointer data)
{
	GError *error = NULL;

	if (!strcmp (signal, "UnloadSignal"))
	{
		g_print ("Good bye !\n");
		exit (EXIT_SUCCESS);
	}
	else if (!strcmp (signal, "ServerSignal"))
	{
		const char **word_eol;
		guint hook_id;

		g_variant_get (params, "(@as^a&suu)", NULL, &word_eol, &hook_id, NULL);
		if (hook_id == server_id && word_eol[0])
			g_print ("message: %s\n", word_eol[0]);
		g_free (word_eol);
	}
	else if (!strcmp (signal, "CommandSignal"))
	{
		const char **word_eol;
		guint hook_id;

		g_variant_get (params, "(@as^a&suu)", NULL, &word_eol, &hook_id, NULL);
		if (hook_id == command_id)
		{
			/* now "/test blah" again gives "Unknown command" */
			g_dbus_proxy_call_sync (proxy, "Unhook", g_variant_new ("(u)", hook_id),
											G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
			write_error ("Failed to complete unhook", &error);
			g_print ("test command received: %s\n", word_eol[0] && word_eol[1] ? word_eol[1] : "");
			g_dbus_proxy_call_sync (proxy, "Print", g_variant_new ("(s)", "test command succeed"),
											G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
			write_error ("Failed to complete Print", &error);
		}
		g_free (word_eol);
	}
}

int
main (int argc, char **argv)
{
	GDBusConnection *bus;
	GDBusProxy *remote;
	GVariant *r;
	char *path;
	GError *error = NULL;

	/* one connection for everything: the remote object belongs to the
	   bus name that called Connect */
	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (!bus)
	{
		write_error ("Couldn't connect to session bus", &error);
		return EXIT_FAILURE;
	}
	remote = g_dbus_proxy_new_sync (bus, G_DBUS_PROXY_FLAGS_NONE, NULL,
											 DBUS_SERVICE, DBUS_REMOTE,
											 DBUS_REMOTE_CONNECTION_INTERFACE, NULL, &error);
	if (!remote)
	{
		write_error ("Couldn't connect to nexchat", &error);
		return EXIT_FAILURE;
	}
	r = g_dbus_proxy_call_sync (remote, "Connect",
										 g_variant_new ("(ssss)", argv[0], "example",
															 "Example of a D-Bus client", "1.0"),
										 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (!r)
	{
		write_error ("Failed to complete Connect", &error);
		return EXIT_FAILURE;
	}
	g_variant_get (r, "(s)", &path);
	g_variant_unref (r);
	g_object_unref (remote);

	remote = g_dbus_proxy_new_sync (bus, G_DBUS_PROXY_FLAGS_NONE, NULL,
											 DBUS_SERVICE, path,
											 DBUS_REMOTE_PLUGIN_INTERFACE, NULL, &error);
	g_free (path);
	if (!remote)
	{
		write_error ("Failed to get the plugin object", &error);
		return EXIT_FAILURE;
	}

	r = g_dbus_proxy_call_sync (remote, "HookCommand",
										 g_variant_new ("(sisi)", "test", 0, "Simple D-BUS example", 1),
										 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (!r)
	{
		write_error ("Failed to complete HookCommand", &error);
		return EXIT_FAILURE;
	}
	g_variant_get (r, "(u)", &command_id);
	g_variant_unref (r);
	g_print ("Command hook id=%d\n", command_id);

	r = g_dbus_proxy_call_sync (remote, "HookServer", g_variant_new ("(sii)", "RAW LINE", 0, 0),
										 G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	if (!r)
	{
		write_error ("Failed to complete HookServer", &error);
		return EXIT_FAILURE;
	}
	g_variant_get (r, "(u)", &server_id);
	g_variant_unref (r);
	g_print ("Server hook id=%d\n", server_id);

	g_signal_connect (remote, "g-signal", G_CALLBACK (on_signal), NULL);

	/* now type "/test arg1 arg2 ..." in nexchat */
	g_main_loop_run (g_main_loop_new (NULL, FALSE));
	return EXIT_SUCCESS;
}
