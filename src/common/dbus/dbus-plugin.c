/* dbus-plugin.c - remote access to nexchat over D-Bus (GDBus)
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

/* The same bus API HexChat had on dbus-glib: a client
   calls org.hexchat.connection.Connect on /org/hexchat/Remote, gets its
   own object path, and drives org.hexchat.plugin there. Hooks come back
   as signals on that object. */

#include <config.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include "hexchat-plugin.h"
#include "dbus-plugin.h"

#define PNAME _("remote access")
#define PDESC _("plugin for remote access using DBUS")
#define PVERSION ""

#define DBUS_OBJECT_PATH "/org/hexchat"

static const char introspection_xml[] =
	"<node>"
	"  <interface name='org.hexchat.connection'>"
	"    <method name='Connect'>"
	"      <arg type='s' name='filename' direction='in'/>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='s' name='desc' direction='in'/>"
	"      <arg type='s' name='version' direction='in'/>"
	"      <arg type='s' name='path' direction='out'/>"
	"    </method>"
	"    <method name='Disconnect'/>"
	"  </interface>"
	"  <interface name='org.hexchat.plugin'>"
	"    <method name='Command'><arg type='s' name='command' direction='in'/></method>"
	"    <method name='Print'><arg type='s' name='text' direction='in'/></method>"
	"    <method name='FindContext'>"
	"      <arg type='s' name='server' direction='in'/>"
	"      <arg type='s' name='channel' direction='in'/>"
	"      <arg type='u' name='ret_id' direction='out'/>"
	"    </method>"
	"    <method name='GetContext'><arg type='u' name='ret_id' direction='out'/></method>"
	"    <method name='SetContext'>"
	"      <arg type='u' name='id' direction='in'/>"
	"      <arg type='b' name='ret' direction='out'/>"
	"    </method>"
	"    <method name='GetInfo'>"
	"      <arg type='s' name='id' direction='in'/>"
	"      <arg type='s' name='ret_info' direction='out'/>"
	"    </method>"
	"    <method name='GetPrefs'>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='i' name='ret_type' direction='out'/>"
	"      <arg type='s' name='ret_str' direction='out'/>"
	"      <arg type='i' name='ret_int' direction='out'/>"
	"    </method>"
	"    <method name='HookCommand'>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='i' name='priority' direction='in'/>"
	"      <arg type='s' name='help_text' direction='in'/>"
	"      <arg type='i' name='return_value' direction='in'/>"
	"      <arg type='u' name='ret_id' direction='out'/>"
	"    </method>"
	"    <method name='HookServer'>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='i' name='priority' direction='in'/>"
	"      <arg type='i' name='return_value' direction='in'/>"
	"      <arg type='u' name='ret_id' direction='out'/>"
	"    </method>"
	"    <method name='HookPrint'>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='i' name='priority' direction='in'/>"
	"      <arg type='i' name='return_value' direction='in'/>"
	"      <arg type='u' name='ret_id' direction='out'/>"
	"    </method>"
	"    <method name='Unhook'><arg type='u' name='id' direction='in'/></method>"
	"    <method name='ListGet'>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='u' name='ret_id' direction='out'/>"
	"    </method>"
	"    <method name='ListNext'>"
	"      <arg type='u' name='id' direction='in'/>"
	"      <arg type='b' name='ret' direction='out'/>"
	"    </method>"
	"    <method name='ListStr'>"
	"      <arg type='u' name='id' direction='in'/>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='s' name='ret_str' direction='out'/>"
	"    </method>"
	"    <method name='ListInt'>"
	"      <arg type='u' name='id' direction='in'/>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='i' name='ret_int' direction='out'/>"
	"    </method>"
	"    <method name='ListTime'>"
	"      <arg type='u' name='id' direction='in'/>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='t' name='ret_time' direction='out'/>"
	"    </method>"
	"    <method name='ListFields'>"
	"      <arg type='s' name='name' direction='in'/>"
	"      <arg type='as' name='ret' direction='out'/>"
	"    </method>"
	"    <method name='ListFree'><arg type='u' name='id' direction='in'/></method>"
	"    <method name='EmitPrint'>"
	"      <arg type='s' name='event_name' direction='in'/>"
	"      <arg type='as' name='args' direction='in'/>"
	"      <arg type='b' name='ret' direction='out'/>"
	"    </method>"
	"    <method name='Nickcmp'>"
	"      <arg type='s' name='nick1' direction='in'/>"
	"      <arg type='s' name='nick2' direction='in'/>"
	"      <arg type='i' name='ret' direction='out'/>"
	"    </method>"
	"    <method name='Strip'>"
	"      <arg type='s' name='str' direction='in'/>"
	"      <arg type='i' name='len' direction='in'/>"
	"      <arg type='i' name='flag' direction='in'/>"
	"      <arg type='s' name='ret_str' direction='out'/>"
	"    </method>"
	"    <method name='SendModes'>"
	"      <arg type='as' name='targets' direction='in'/>"
	"      <arg type='i' name='modes_per_line' direction='in'/>"
	"      <arg type='y' name='sign' direction='in'/>"
	"      <arg type='y' name='mode' direction='in'/>"
	"    </method>"
	"    <signal name='CommandSignal'>"
	"      <arg type='as' name='word'/><arg type='as' name='word_eol'/>"
	"      <arg type='u' name='hook_id'/><arg type='u' name='context_id'/>"
	"    </signal>"
	"    <signal name='ServerSignal'>"
	"      <arg type='as' name='word'/><arg type='as' name='word_eol'/>"
	"      <arg type='u' name='hook_id'/><arg type='u' name='context_id'/>"
	"    </signal>"
	"    <signal name='PrintSignal'>"
	"      <arg type='as' name='word'/>"
	"      <arg type='u' name='hook_id'/><arg type='u' name='context_id'/>"
	"    </signal>"
	"    <signal name='UnloadSignal'/>"
	"  </interface>"
	"</node>";

typedef struct
{
	char *sender;			/* unique bus name of the client */
	char *path;				/* its object, /org/hexchat/<n> */
	char *filename;
	guint reg_id;
	guint reg_id2;			/* org.hexchat.connection (Disconnect) on the same path */
	guint last_hook_id;
	guint last_list_id;
	hexchat_context *context;
	GHashTable *hooks;	/* id -> HookInfo */
	GHashTable *lists;	/* id -> hexchat_list */
	void *handle;			/* its row in the plugin list */
} Client;

typedef struct
{
	guint id;
	int return_value;
	hexchat_hook *hook;
	Client *client;
} HookInfo;

typedef struct
{
	guint id;
	hexchat_context *context;
} ContextInfo;

static hexchat_plugin *ph;
static GDBusConnection *bus;
static GDBusNodeInfo *introspection;
static guint last_context_id;
static GList *contexts;
static GHashTable *clients;		/* sender -> Client */
static guint name_watch;

/* ---- contexts by id ---- */

static guint
context_list_find_id (hexchat_context *context)
{
	GList *l;

	for (l = contexts; l; l = l->next)
		if (((ContextInfo *) l->data)->context == context)
			return ((ContextInfo *) l->data)->id;
	return 0;
}

static hexchat_context *
context_list_find_context (guint id)
{
	GList *l;

	for (l = contexts; l; l = l->next)
		if (((ContextInfo *) l->data)->id == id)
			return ((ContextInfo *) l->data)->context;
	return NULL;
}

static int
open_context_cb (char *word[], void *userdata)
{
	ContextInfo *info = g_new0 (ContextInfo, 1);

	info->id = ++last_context_id;
	info->context = hexchat_get_context (ph);
	contexts = g_list_prepend (contexts, info);
	return HEXCHAT_EAT_NONE;
}

static int
close_context_cb (char *word[], void *userdata)
{
	hexchat_context *context = hexchat_get_context (ph);
	GList *l;

	for (l = contexts; l; l = l->next)
		if (((ContextInfo *) l->data)->context == context)
		{
			g_free (l->data);
			contexts = g_list_delete_link (contexts, l);
			break;
		}
	return HEXCHAT_EAT_NONE;
}

/* ---- clients ---- */

static void
hook_info_destroy (gpointer data)
{
	HookInfo *info = data;

	hexchat_unhook (ph, info->hook);
	g_free (info);
}

static void
list_info_destroy (gpointer data)
{
	hexchat_list_free (ph, data);
}

static void
client_free (gpointer data)
{
	Client *c = data;

	if (c->reg_id)
		g_dbus_connection_unregister_object (bus, c->reg_id);
	if (c->reg_id2)
		g_dbus_connection_unregister_object (bus, c->reg_id2);
	g_hash_table_destroy (c->lists);
	g_hash_table_destroy (c->hooks);
	if (c->handle)
		hexchat_plugingui_remove (ph, c->handle);
	g_free (c->sender);
	g_free (c->path);
	g_free (c->filename);
	g_free (c);
}

static void
emit (Client *c, const char *signal, GVariant *params)
{
	g_dbus_connection_emit_signal (bus, c->sender, c->path, "org.hexchat.plugin",
											 signal, params, NULL);
}

/* word[] from a hook, as "as": HexChat's arrays end at the first empty slot */
static GVariant *
strv_variant (char *word[])
{
	GVariantBuilder b;
	int i;

	g_variant_builder_init (&b, G_VARIANT_TYPE ("as"));
	for (i = 0; word && word[i] && word[i][0]; i++)
		g_variant_builder_add (&b, "s", word[i]);
	return g_variant_builder_end (&b);
}

static int
server_hook_cb (char *word[], char *word_eol[], void *userdata)
{
	HookInfo *info = userdata;

	info->client->context = hexchat_get_context (ph);
	emit (info->client, "ServerSignal",
			g_variant_new ("(@as@asuu)", strv_variant (word + 1), strv_variant (word_eol + 1),
								info->id, context_list_find_id (info->client->context)));
	return info->return_value;
}

static int
command_hook_cb (char *word[], char *word_eol[], void *userdata)
{
	HookInfo *info = userdata;

	info->client->context = hexchat_get_context (ph);
	emit (info->client, "CommandSignal",
			g_variant_new ("(@as@asuu)", strv_variant (word + 1), strv_variant (word_eol + 1),
								info->id, context_list_find_id (info->client->context)));
	return info->return_value;
}

static int
print_hook_cb (char *word[], void *userdata)
{
	HookInfo *info = userdata;

	info->client->context = hexchat_get_context (ph);
	emit (info->client, "PrintSignal",
			g_variant_new ("(@asuu)", strv_variant (word + 1),
								info->id, context_list_find_id (info->client->context)));
	return info->return_value;
}

static HookInfo *
hook_new (Client *c, int return_value)
{
	HookInfo *info = g_new0 (HookInfo, 1);

	info->client = c;
	info->return_value = return_value;
	info->id = ++c->last_hook_id;
	return info;
}

/* ---- org.hexchat.plugin ---- */

/* the client's context, current for the plugin API; FALSE if it's gone */
static gboolean
use_context (Client *c)
{
	return hexchat_set_context (ph, c->context);
}

static GVariant *
plugin_call (Client *c, const char *m, GVariant *p)
{
	const char *s1, *s2;
	gint32 i1, i2;
	guint32 u;

	if (!strcmp (m, "Command"))
	{
		g_variant_get (p, "(&s)", &s1);
		if (use_context (c))
			hexchat_command (ph, s1);
		return NULL;
	}
	if (!strcmp (m, "Print"))
	{
		g_variant_get (p, "(&s)", &s1);
		if (use_context (c))
			hexchat_print (ph, s1);
		return NULL;
	}
	if (!strcmp (m, "FindContext"))
	{
		g_variant_get (p, "(&s&s)", &s1, &s2);
		return g_variant_new ("(u)", context_list_find_id (
			hexchat_find_context (ph, *s1 ? s1 : NULL, *s2 ? s2 : NULL)));
	}
	if (!strcmp (m, "GetContext"))
		return g_variant_new ("(u)", context_list_find_id (c->context));
	if (!strcmp (m, "SetContext"))
	{
		hexchat_context *ctx;

		g_variant_get (p, "(u)", &u);
		ctx = context_list_find_context (u);
		if (ctx)
			c->context = ctx;
		return g_variant_new ("(b)", ctx != NULL);
	}
	if (!strcmp (m, "GetInfo"))
	{
		const char *info = NULL;

		g_variant_get (p, "(&s)", &s1);
		/* win_ptr is a pointer, not a string */
		if (use_context (c) && strcmp (s1, "win_ptr") != 0 && strcmp (s1, "gtkwin_ptr") != 0)
			info = hexchat_get_info (ph, s1);
		return g_variant_new ("(s)", info ? info : "");
	}
	if (!strcmp (m, "GetPrefs"))
	{
		const char *str = NULL;
		int type = 0, val = 0;

		g_variant_get (p, "(&s)", &s1);
		if (use_context (c))
			type = hexchat_get_prefs (ph, s1, &str, &val);
		return g_variant_new ("(isi)", type, str && type == 1 ? str : "", val);
	}
	if (!strcmp (m, "HookCommand"))
	{
		HookInfo *info;

		g_variant_get (p, "(&si&si)", &s1, &i1, &s2, &i2);
		info = hook_new (c, i2);
		info->hook = hexchat_hook_command (ph, s1, i1, command_hook_cb, s2, info);
		g_hash_table_insert (c->hooks, GUINT_TO_POINTER (info->id), info);
		return g_variant_new ("(u)", info->id);
	}
	if (!strcmp (m, "HookServer") || !strcmp (m, "HookPrint"))
	{
		HookInfo *info;

		g_variant_get (p, "(&sii)", &s1, &i1, &i2);
		info = hook_new (c, i2);
		if (m[4] == 'S')
			info->hook = hexchat_hook_server (ph, s1, i1, server_hook_cb, info);
		else
			info->hook = hexchat_hook_print (ph, s1, i1, print_hook_cb, info);
		g_hash_table_insert (c->hooks, GUINT_TO_POINTER (info->id), info);
		return g_variant_new ("(u)", info->id);
	}
	if (!strcmp (m, "Unhook"))
	{
		g_variant_get (p, "(u)", &u);
		g_hash_table_remove (c->hooks, GUINT_TO_POINTER (u));
		return NULL;
	}
	if (!strcmp (m, "ListGet"))
	{
		hexchat_list *xlist = NULL;

		g_variant_get (p, "(&s)", &s1);
		if (use_context (c))
			xlist = hexchat_list_get (ph, s1);
		if (!xlist)
			return g_variant_new ("(u)", 0);
		g_hash_table_insert (c->lists, GUINT_TO_POINTER (++c->last_list_id), xlist);
		return g_variant_new ("(u)", c->last_list_id);
	}
	if (!strcmp (m, "ListNext"))
	{
		hexchat_list *xlist;

		g_variant_get (p, "(u)", &u);
		xlist = g_hash_table_lookup (c->lists, GUINT_TO_POINTER (u));
		return g_variant_new ("(b)", xlist ? hexchat_list_next (ph, xlist) : FALSE);
	}
	if (!strcmp (m, "ListStr"))
	{
		hexchat_list *xlist;
		const char *str = NULL;

		g_variant_get (p, "(u&s)", &u, &s1);
		xlist = g_hash_table_lookup (c->lists, GUINT_TO_POINTER (u));
		/* "context" is a pointer: ListInt gives its id */
		if (xlist && strcmp (s1, "context") != 0)
			str = hexchat_list_str (ph, xlist, s1);
		return g_variant_new ("(s)", str ? str : "");
	}
	if (!strcmp (m, "ListInt"))
	{
		hexchat_list *xlist;
		int val = -1;

		g_variant_get (p, "(u&s)", &u, &s1);
		xlist = g_hash_table_lookup (c->lists, GUINT_TO_POINTER (u));
		if (xlist && !strcmp (s1, "context"))
			val = context_list_find_id ((hexchat_context *) hexchat_list_str (ph, xlist, s1));
		else if (xlist)
			val = hexchat_list_int (ph, xlist, s1);
		return g_variant_new ("(i)", val);
	}
	if (!strcmp (m, "ListTime"))
	{
		hexchat_list *xlist;

		g_variant_get (p, "(u&s)", &u, &s1);
		xlist = g_hash_table_lookup (c->lists, GUINT_TO_POINTER (u));
		return g_variant_new ("(t)", xlist ? (guint64) hexchat_list_time (ph, xlist, s1)
													 : (guint64) -1);
	}
	if (!strcmp (m, "ListFields"))
	{
		const char * const *f;

		g_variant_get (p, "(&s)", &s1);
		f = hexchat_list_fields (ph, s1);
		return g_variant_new ("(^as)", f ? f : (const char * const *) (const char *[]) { NULL });
	}
	if (!strcmp (m, "ListFree"))
	{
		g_variant_get (p, "(u)", &u);
		g_hash_table_remove (c->lists, GUINT_TO_POINTER (u));
		return NULL;
	}
	if (!strcmp (m, "EmitPrint"))
	{
		const char *argv[4] = { NULL, NULL, NULL, NULL };
		const char **args;
		gboolean ret;
		int i;

		g_variant_get (p, "(&s^a&s)", &s1, &args);
		for (i = 0; i < 4 && args[i]; i++)
			argv[i] = args[i];
		ret = use_context (c) &&
				hexchat_emit_print (ph, s1, argv[0], argv[1], argv[2], argv[3], NULL);
		g_free (args);
		return g_variant_new ("(b)", ret);
	}
	if (!strcmp (m, "Nickcmp"))
	{
		g_variant_get (p, "(&s&s)", &s1, &s2);
		use_context (c);
		return g_variant_new ("(i)", hexchat_nickcmp (ph, s1, s2));
	}
	if (!strcmp (m, "Strip"))
	{
		char *str;
		GVariant *r;

		g_variant_get (p, "(&sii)", &s1, &i1, &i2);
		str = hexchat_strip (ph, s1, i1, i2);
		r = g_variant_new ("(s)", str ? str : "");
		hexchat_free (ph, str);
		return r;
	}
	if (!strcmp (m, "SendModes"))
	{
		const char **targets;
		guchar sign, mode;

		g_variant_get (p, "(^a&siyy)", &targets, &i1, &sign, &mode);
		if (use_context (c))
			hexchat_send_modes (ph, targets, g_strv_length ((char **) targets), i1, sign, mode);
		g_free (targets);
		return NULL;
	}
	return NULL;
}

static void
client_method (GDBusConnection *conn, const char *sender, const char *path,
					const char *iface, const char *method, GVariant *params,
					GDBusMethodInvocation *inv, gpointer data);

static const GDBusInterfaceVTable client_vtable = { client_method, NULL, NULL, { 0 } };

static Client *
client_connect (const char *sender, const char *filename, const char *name,
					 const char *desc, const char *version)
{
	static guint count;
	Client *c = g_new0 (Client, 1);
	GError *err = NULL;

	c->sender = g_strdup (sender);
	c->path = g_strdup_printf (DBUS_OBJECT_PATH "/%u", count++);
	c->filename = g_path_get_basename (filename);
	c->hooks = g_hash_table_new_full (NULL, NULL, NULL, hook_info_destroy);
	c->lists = g_hash_table_new_full (NULL, NULL, NULL, list_info_destroy);
	c->handle = hexchat_plugingui_add (ph, c->filename, name, desc, version, NULL);
	c->reg_id = g_dbus_connection_register_object (bus, c->path,
						g_dbus_node_info_lookup_interface (introspection, "org.hexchat.plugin"),
						&client_vtable, c, NULL, &err);
	if (!c->reg_id)
	{
		g_warning ("dbus: %s", err->message);
		g_clear_error (&err);
	}
	c->reg_id2 = g_dbus_connection_register_object (bus, c->path,
						g_dbus_node_info_lookup_interface (introspection, "org.hexchat.connection"),
						&client_vtable, c, NULL, &err);
	if (!c->reg_id2)
	{
		g_warning ("dbus: %s", err->message);
		g_error_free (err);
	}
	g_hash_table_insert (clients, c->sender, c);
	return c;
}

/* ---- method dispatch ---- */

static void
client_method (GDBusConnection *conn, const char *sender, const char *path,
					const char *iface, const char *method, GVariant *params,
					GDBusMethodInvocation *inv, gpointer data)
{
	Client *c = data;

	/* each client drives only its own object */
	if (strcmp (sender, c->sender) != 0)
	{
		g_dbus_method_invocation_return_error (inv, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
															"not your object");
		return;
	}
	if (!strcmp (iface, "org.hexchat.connection") && !strcmp (method, "Disconnect"))
	{
		g_dbus_method_invocation_return_value (inv, NULL);
		g_hash_table_remove (clients, sender);
		return;
	}
	g_dbus_method_invocation_return_value (inv, plugin_call (c, method, params));
}

static void
remote_method (GDBusConnection *conn, const char *sender, const char *path,
					const char *iface, const char *method, GVariant *params,
					GDBusMethodInvocation *inv, gpointer data)
{
	Client *c;

	if (!strcmp (method, "Connect"))
	{
		const char *filename, *name, *desc, *version;

		g_variant_get (params, "(&s&s&s&s)", &filename, &name, &desc, &version);
		c = g_hash_table_lookup (clients, sender);
		if (!c)
			c = client_connect (sender, filename, name, desc, version);
		g_dbus_method_invocation_return_value (inv, g_variant_new ("(s)", c->path));
	}
	else if (!strcmp (method, "Disconnect"))
	{
		g_dbus_method_invocation_return_value (inv, NULL);
		g_hash_table_remove (clients, sender);
	}
	else
	{
		/* `nexchat -e` calls org.hexchat.plugin.Command straight on
		   /org/hexchat/Remote, in whatever tab is in front */
		Client tmp = { 0 };

		tmp.context = hexchat_find_context (ph, NULL, NULL);
		tmp.sender = (char *) sender;
		tmp.hooks = tmp.lists = NULL;
		if (!strcmp (iface, "org.hexchat.plugin") &&
			 (!strcmp (method, "Command") || !strcmp (method, "Print")))
			g_dbus_method_invocation_return_value (inv, plugin_call (&tmp, method, params));
		else
			g_dbus_method_invocation_return_error (inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
																"Connect first");
	}
}

static const GDBusInterfaceVTable remote_vtable = { remote_method, NULL, NULL, { 0 } };

/* the client left the bus: drop its object, hooks and lists */
static void
name_owner_changed (GDBusConnection *conn, const char *sender, const char *path,
						  const char *iface, const char *signal, GVariant *params, gpointer data)
{
	const char *name, *old_owner, *new_owner;

	g_variant_get (params, "(&s&s&s)", &name, &old_owner, &new_owner);
	if (!*new_owner)
		g_hash_table_remove (clients, name);
}

/* /unload of a script name a remote client registered: tell it to go */
static gboolean
client_has_filename (gpointer key, gpointer value, gpointer name)
{
	return !strcmp (((Client *) value)->filename, name);
}

static int
unload_plugin_cb (char *word[], char *word_eol[], void *userdata)
{
	Client *c = g_hash_table_find (clients, client_has_filename, word[2]);

	if (!c)
		return HEXCHAT_EAT_NONE;
	emit (c, "UnloadSignal", NULL);
	return HEXCHAT_EAT_ALL;
}

static gboolean
init_dbus (void)
{
	GError *err = NULL;
	GDBusInterfaceInfo *conn_iface;
	GVariant *r;

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &err);
	if (!bus)
	{
		hexchat_printf (ph, _("Couldn't connect to session bus: %s\n"), err->message);
		g_error_free (err);
		return FALSE;
	}
	introspection = g_dbus_node_info_new_for_xml (introspection_xml, NULL);

	/* the remote object answers both interfaces: Connect, and the
	   `nexchat -e` Command shortcut */
	conn_iface = g_dbus_node_info_lookup_interface (introspection, "org.hexchat.connection");
	if (!g_dbus_connection_register_object (bus, DBUS_OBJECT_PATH "/Remote", conn_iface,
														 &remote_vtable, NULL, NULL, &err) ||
		 !g_dbus_connection_register_object (bus, DBUS_OBJECT_PATH "/Remote",
							g_dbus_node_info_lookup_interface (introspection, "org.hexchat.plugin"),
							&remote_vtable, NULL, NULL, &err))
	{
		hexchat_printf (ph, _("Failed to acquire %s: %s\n"), DBUS_SERVICE, err->message);
		g_error_free (err);
		return FALSE;
	}

	/* the name last: a client that sees it can call at once */
	r = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
												"org.freedesktop.DBus", "RequestName",
												g_variant_new ("(su)", DBUS_SERVICE, 0x1 /* ALLOW_REPLACEMENT */),
												G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
	if (!r)
	{
		hexchat_printf (ph, _("Failed to acquire %s: %s\n"), DBUS_SERVICE, err->message);
		g_error_free (err);
		return FALSE;
	}
	g_variant_unref (r);

	name_watch = g_dbus_connection_signal_subscribe (bus, "org.freedesktop.DBus",
						"org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus",
						NULL, G_DBUS_SIGNAL_FLAGS_NONE, name_owner_changed, NULL, NULL);
	return TRUE;
}

int
dbus_plugin_init (hexchat_plugin *plugin_handle, char **plugin_name,
						char **plugin_desc, char **plugin_version, char *arg)
{
	ph = plugin_handle;
	*plugin_name = PNAME;
	*plugin_desc = PDESC;
	*plugin_version = PVERSION;

	clients = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, client_free);
	if (init_dbus ())
	{
		hexchat_hook_print (ph, "Open Context", HEXCHAT_PRI_NORM, open_context_cb, NULL);
		hexchat_hook_print (ph, "Close Context", HEXCHAT_PRI_NORM, close_context_cb, NULL);
		hexchat_hook_command (ph, "unload", HEXCHAT_PRI_HIGHEST, unload_plugin_cb, NULL, NULL);
	}
	return TRUE;
}
