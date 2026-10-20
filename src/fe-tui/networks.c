/* nexchat TUI — the Network List's side of the bridge: servlist.c's
 * networks read and written as nx_net values (bridge.h). The GUI's
 * servlistgui.c does the same edits through its widgets.
 */

#include "config.h"

#include <string.h>

#include <glib.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/servlist.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/ignore.h"
#include "../common/notify.h"

#include "bridge.h"

/* the GUI's login dropdown, in its order (servlistgui.c login_types[]) */
static const int login_conf[] =
{
	LOGIN_DEFAULT, LOGIN_SASL,
#ifdef USE_OPENSSL
	LOGIN_SASLEXTERNAL, LOGIN_SASL_SCRAM_SHA_1, LOGIN_SASL_SCRAM_SHA_256, LOGIN_SASL_SCRAM_SHA_512,
#endif
	LOGIN_PASS, LOGIN_MSG_NICKSERV, LOGIN_NICKSERV,
#ifdef USE_OPENSSL
	LOGIN_CHALLENGEAUTH,
#endif
	LOGIN_CUSTOM,
};

static const char *const login_names[] =
{
	"Default",
	"SASL PLAIN (username + password)",
#ifdef USE_OPENSSL
	"SASL EXTERNAL (cert)",
	"SASL SCRAM-SHA-1",
	"SASL SCRAM-SHA-256",
	"SASL SCRAM-SHA-512",
#endif
	"Server password (/PASS password)",
	"NickServ (/MSG NickServ + password)",
	"NickServ (/NICKSERV + password)",
#ifdef USE_OPENSSL
	"Challenge Auth (username + password)",
#endif
	"Custom… (connect commands)",
	NULL
};

const char *const *
nx_core_login_names (void)
{
	return login_names;
}

static int
login_index (int conf)
{
	size_t i;
	for (i = 0; i < G_N_ELEMENTS (login_conf); i++)
		if (login_conf[i] == conf)
			return (int) i;
	return 0;
}

static ircnet *
net_at (int i)
{
	return g_slist_nth_data (network_list, i);
}

int
nx_core_net_count (void)
{
	return g_slist_length (network_list);
}

static char *
dup0 (const char *s)
{
	return g_strdup (s ? s : "");
}

void
nx_core_net_get (int i, nx_net *out)
{
	ircnet *net = net_at (i);
	GString *s;
	GSList *l;

	memset (out, 0, sizeof *out);
	if (!net)
		return;
	out->name = dup0 (net->name);
	out->nick = dup0 (net->nick);
	out->nick2 = dup0 (net->nick2);
	out->user = dup0 (net->user);
	out->real = dup0 (net->real);
	out->pass = dup0 (net->pass);
	out->encoding = dup0 (net->encoding);

	s = g_string_new (NULL);
	for (l = net->servlist; l; l = l->next)
		g_string_append_printf (s, "%s%s", s->len ? "\n" : "", ((ircserver *) l->data)->hostname);
	out->servers = g_string_free (s, FALSE);

	s = g_string_new (NULL);
	for (l = net->favchanlist; l; l = l->next)
	{
		favchannel *f = l->data;
		g_string_append_printf (s, "%s%s%s%s", s->len ? "\n" : "", f->name, f->key ? " " : "", f->key ? f->key : "");
	}
	out->autojoin = g_string_free (s, FALSE);

	s = g_string_new (NULL);
	for (l = net->commandlist; l; l = l->next)
		g_string_append_printf (s, "%s%s", s->len ? "\n" : "", ((commandentry *) l->data)->command);
	out->commands = g_string_free (s, FALSE);

	out->login = login_index (net->logintype);
	out->use_global = (net->flags & FLAG_USE_GLOBAL) != 0;
	out->ssl = (net->flags & FLAG_USE_SSL) != 0;
	out->cycle = (net->flags & FLAG_CYCLE) != 0;
	out->auto_connect = (net->flags & FLAG_AUTO_CONNECT) != 0;
	out->allow_invalid = (net->flags & FLAG_ALLOW_INVALID) != 0;
	out->favorite = (net->flags & FLAG_FAVORITE) != 0;
	out->proxy = (net->flags & FLAG_USE_PROXY) != 0;
}

void
nx_core_net_free (nx_net *n)
{
	g_free (n->name); g_free (n->nick); g_free (n->nick2); g_free (n->user);
	g_free (n->real); g_free (n->pass); g_free (n->encoding);
	g_free (n->servers); g_free (n->autojoin); g_free (n->commands);
	memset (n, 0, sizeof *n);
}

/* "" → NULL, as the GUI stores an empty field */
static void
set_str (char **dst, const char *v)
{
	g_free (*dst);
	*dst = (v && *v) ? g_strdup (v) : NULL;
}

static void
set_flag (ircnet *net, guint32 flag, int on)
{
	if (on)
		net->flags |= flag;
	else
		net->flags &= ~flag;
}

void
nx_core_net_set (int i, const nx_net *in)
{
	ircnet *net = net_at (i);
	char **lines, **p;

	if (!net)
		return;
	if (in->name && *in->name)
		set_str (&net->name, in->name);
	set_str (&net->nick, in->nick);
	set_str (&net->nick2, in->nick2);
	set_str (&net->user, in->user);
	set_str (&net->real, in->real);
	set_str (&net->pass, in->pass);
	set_str (&net->encoding, in->encoding);

	while (net->servlist)
		servlist_server_remove (net, net->servlist->data);
	lines = g_strsplit (in->servers ? in->servers : "", "\n", -1);
	for (p = lines; *p; p++)
	{
		g_strstrip (*p);
		if (**p)
			servlist_server_add (net, *p);
	}
	g_strfreev (lines);

	while (net->favchanlist)
		servlist_favchan_remove (net, net->favchanlist->data);
	lines = g_strsplit (in->autojoin ? in->autojoin : "", "\n", -1);
	for (p = lines; *p; p++)
	{
		char *sp;
		g_strstrip (*p);
		if (!**p)
			continue;
		/* "#chan key" → the core's "#chan,key" */
		if ((sp = strchr (*p, ' ')))
			*sp = ',';
		servlist_favchan_add (net, *p);
	}
	g_strfreev (lines);

	while (net->commandlist)
		servlist_command_remove (net, net->commandlist->data);
	lines = g_strsplit (in->commands ? in->commands : "", "\n", -1);
	for (p = lines; *p; p++)
	{
		g_strstrip (*p);
		if (**p)
			servlist_command_add (net, *p);
	}
	g_strfreev (lines);

	if (in->login >= 0 && in->login < (int) G_N_ELEMENTS (login_conf))
		net->logintype = login_conf[in->login];
	set_flag (net, FLAG_USE_GLOBAL, in->use_global);
	set_flag (net, FLAG_USE_SSL, in->ssl);
	set_flag (net, FLAG_CYCLE, in->cycle);
	set_flag (net, FLAG_AUTO_CONNECT, in->auto_connect);
	set_flag (net, FLAG_ALLOW_INVALID, in->allow_invalid);
	set_flag (net, FLAG_FAVORITE, in->favorite);
	set_flag (net, FLAG_USE_PROXY, in->proxy);

	servlist_save ();
}

int
nx_core_net_add (const char *name)
{
	ircnet *net = servlist_net_add ((char *) (name && *name ? name : _("New Network")), "", TRUE);
	servlist_server_add (net, "newserver/6667");
	servlist_save ();
	return 0;	/* prepended */
}

void
nx_core_net_remove (int i)
{
	ircnet *net = net_at (i);
	if (!net)
		return;
	servlist_net_remove (net);
	servlist_save ();
}

void
nx_core_net_connect (int i)
{
	ircnet *net = net_at (i);
	session *use = NULL;
	GSList *l;

	if (!net)
		return;
	/* the GUI's choice (servlistgui.c servlist_connect_cb): a tab already on
	   this network that isn't connected, else the front tab if it's an
	   unconnected, empty server tab, else a new one */
	for (l = sess_list; l; l = l->next)
	{
		session *s = l->data;
		if (s->server->network == net)
		{
			use = s->server->connected ? NULL : s;
			break;
		}
	}
	if (!use && current_sess && !current_sess->server->connected
		 && !current_sess->server->connecting && current_sess->server->server_session
		 && current_sess->server->server_session->channel[0] == 0
		 && current_sess->server->network == NULL)
		use = current_sess;
	servlist_connect (use, net, TRUE);
}

void
nx_core_user_get (char **nick1, char **nick2, char **nick3, char **user)
{
	*nick1 = g_strdup (prefs.hex_irc_nick1);
	*nick2 = g_strdup (prefs.hex_irc_nick2);
	*nick3 = g_strdup (prefs.hex_irc_nick3);
	*user = g_strdup (prefs.hex_irc_user_name);
}

void
nx_core_user_set (const char *nick1, const char *nick2, const char *nick3, const char *user)
{
	char *sp;
	safe_strcpy (prefs.hex_irc_nick1, nick1, sizeof (prefs.hex_irc_nick1));
	safe_strcpy (prefs.hex_irc_nick2, nick2, sizeof (prefs.hex_irc_nick2));
	safe_strcpy (prefs.hex_irc_nick3, nick3, sizeof (prefs.hex_irc_nick3));
	safe_strcpy (prefs.hex_irc_user_name, user, sizeof (prefs.hex_irc_user_name));
	/* no spaces in a user name (servlistgui.c does the same) */
	if ((sp = strchr (prefs.hex_irc_user_name, ' ')))
		*sp = 0;
	save_config ();
}

int
nx_core_slist_skip (void)
{
	return prefs.hex_gui_slist_skip;
}

void
nx_core_slist_skip_set (int skip)
{
	prefs.hex_gui_slist_skip = skip ? 1 : 0;
	save_config ();
}

/* ── Ignore List ────────────────────────────────────────────────────── */

int
nx_core_ignore_count (void)
{
	return g_slist_length (ignore_list);
}

const char *
nx_core_ignore_get (int i, int *flags)
{
	struct ignore *ig = g_slist_nth_data (ignore_list, i);
	*flags = ig ? (int) ig->type : 0;
	return ig ? ig->mask : "";
}

void
nx_core_ignore_set (const char *mask, int flags)
{
	if (!mask || !*mask)
		return;
	if (!(flags & ~IG_NOSAVE))
		ignore_del ((char *) mask, NULL);
	else
		ignore_add ((char *) mask, flags, TRUE);
	ignore_save ();
}

void
nx_core_ignore_remove (const char *mask)
{
	ignore_del ((char *) mask, NULL);
	ignore_save ();
}

int
nx_core_ignored (const char *mask)
{
	return ignore_exists ((char *) mask) != NULL;
}

/* ── Friends List ───────────────────────────────────────────────────── */

int
nx_core_notify_count (void)
{
	return g_slist_length (notify_list);
}

const char *
nx_core_notify_get (int i, const char **networks, int *online, long *seen)
{
	struct notify *n = g_slist_nth_data (notify_list, i);
	GSList *l;

	*online = 0;
	*seen = 0;
	*networks = n && n->networks ? n->networks : "";
	if (!n)
		return "";
	for (l = n->server_list; l; l = l->next)
	{
		struct notify_per_server *ps = l->data;
		if (ps->ison)
			*online = 1;
		if (ps->lastseen > *seen)
			*seen = ps->lastseen;
	}
	return n->name;
}

void
nx_core_notify_add (const char *name, const char *networks)
{
	if (!name || !*name)
		return;
	notify_adduser ((char *) name, (networks && *networks) ? (char *) networks : NULL);
	notify_save ();
}

void
nx_core_notify_remove (const char *name)
{
	notify_deluser ((char *) name);
	notify_save ();
}
