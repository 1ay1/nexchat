/* nexchat TUI — the core's side of the Window menu's tools (bridge.h):
 * URL Grabber (fe-gtk/urlgrab.c), Mentions (mentions.c), Plugins and
 * Scripts (plugingui.c) and the Ban List (banlist.c), the same logic as
 * the GUI's with the widgets left out.
 */

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/url.h"
#include "../common/tree.h"
#include "../common/server.h"
#include "../common/outbound.h"
#include "../common/modes.h"
#define PLUGIN_C
typedef struct session hexchat_context;
#include "../common/hexchat-plugin.h"
#include "../common/plugin.h"

#include "bridge.h"

void ui_dirty (void);
void mentions_session_gone (session *sess);
void ban_session_gone (session *sess);

/* ── URL Grabber ───────────────────────────────────────────────────── */

void nx_core_strfreev (char **v) { g_strfreev (v); }
void nx_core_gfree (void *p) { g_free (p); }

int
nx_core_url_enabled (void)
{
	return prefs.hex_url_grabber != 0;
}

static int
url_collect (char *url, gpointer data)
{
	g_ptr_array_add (data, g_strdup (url));
	return TRUE;
}

char **
nx_core_urls (void)
{
	GPtrArray *a = g_ptr_array_new ();
	if (url_tree)
		tree_foreach (url_tree, (tree_traverse_func *) url_collect, a);
	g_ptr_array_add (a, NULL);
	return (char **) g_ptr_array_free (a, FALSE);
}

void
nx_core_urls_clear (void)
{
	url_clear ();
}

int
nx_core_urls_save (const char *path)
{
	FILE *f;
	if (!path || !*path)
		return 0;
	/* url_save_tree says nothing when it can't: try first */
	f = g_fopen (path, "w");
	if (!f)
		return 0;
	fclose (f);
	url_save_tree (path, "w", TRUE);
	return 1;
}

/* ── Mentions ──────────────────────────────────────────────────────── */

#define MAX_MENTIONS 500

typedef struct
{
	time_t stamp;
	char *where, *who, *text;
	session *sess;
} mention;

static GQueue mentions = G_QUEUE_INIT;	/* newest first */

static void
mention_free (mention *m)
{
	g_free (m->where);
	g_free (m->who);
	g_free (m->text);
	g_free (m);
}

void
fe_mention (struct session *sess, const char *nick, const char *text, time_t stamp)
{
	mention *m;
	char *name;

	if (!sess || !sess->server)
		return;
	m = g_new0 (mention, 1);
	m->stamp = stamp ? stamp : time (NULL);
	name = sess->type == SESS_DIALOG ? g_strdup (_("private")) : g_strdup (sess->channel);
	m->where = g_strdup_printf ("%s / %s", server_get_network (sess->server, TRUE), name);
	g_free (name);
	m->who = g_strdup (nick ? nick : "");
	/* colours and bold out: the list is plain */
	m->text = strip_color (text ? text : "", -1, STRIP_ALL);
	m->sess = sess;
	g_queue_push_head (&mentions, m);
	while (g_queue_get_length (&mentions) > MAX_MENTIONS)
		mention_free (g_queue_pop_tail (&mentions));
	nx_ui_mentions_changed ();
	ui_dirty ();
}

/* a closed tab's mentions stay listed, but can't be jumped to */
void
mentions_session_gone (session *sess)
{
	GList *l;
	for (l = mentions.head; l; l = l->next)
		if (((mention *) l->data)->sess == sess)
			((mention *) l->data)->sess = NULL;
}

int
nx_core_mention_count (void)
{
	return (int) g_queue_get_length (&mentions);
}

void
nx_core_mention_get (int i, nx_mention *out)
{
	mention *m = g_queue_peek_nth (&mentions, (guint) i);
	memset (out, 0, sizeof *out);
	if (!m)
		return;
	out->stamp = m->stamp;
	out->where = m->where;
	out->who = m->who;
	out->text = m->text;
	out->buf = m->sess && is_session (m->sess) ? (nx_key) m->sess : 0;
}

void
nx_core_mentions_clear (void)
{
	g_queue_clear_full (&mentions, (GDestroyNotify) mention_free);
}

/* ── Plugins and Scripts ───────────────────────────────────────────── */

extern GSList *plugin_list;

static hexchat_plugin *
plugin_nth (int i)
{
	GSList *l;
	for (l = plugin_list; l; l = l->next)
	{
		hexchat_plugin *pl = l->data;
		if (pl->version[0] == 0)	/* the GUI lists only these */
			continue;
		if (i-- == 0)
			return pl;
	}
	return NULL;
}

int
nx_core_plugin_count (void)
{
	int n = 0;
	while (plugin_nth (n))
		n++;
	return n;
}

void
nx_core_plugin_get (int i, nx_plugin *out)
{
	hexchat_plugin *pl = plugin_nth (i);
	memset (out, 0, sizeof *out);
	if (!pl)
		return;
	out->name = pl->name;
	out->version = pl->version;
	out->file = pl->filename ? file_part (pl->filename) : "";
	out->desc = pl->desc;
	out->path = pl->filename ? pl->filename : "";
}

static void
plugin_command (const char *verb, const char *path)
{
	char *buf = strchr (path, ' ') ? g_strdup_printf ("%s \"%s\"", verb, path)
											 : g_strdup_printf ("%s %s", verb, path);
	handle_command (current_sess, buf, FALSE);
	g_free (buf);
}

int
nx_core_plugin_unload (const char *name, const char *path)
{
	if (!current_sess || !path)
		return 0;
	if (g_str_has_suffix (path, "." PLUGIN_SUFFIX))
	{
		char *n = g_strdup (name);
		int r = plugin_kill (n, FALSE);
		g_free (n);
		return r == 2;
	}
	/* let python.so or perl.so handle it */
	plugin_command ("UNLOAD", path);
	return 0;
}

void
nx_core_plugin_load (const char *path, int reload)
{
	if (current_sess && path && *path)
		plugin_command (reload ? "RELOAD" : "LOAD", path);
}

char *
nx_core_addons_dir (void)
{
	return g_build_filename (get_xdir (), "addons", NULL);
}

/* ── Ban List ──────────────────────────────────────────────────────── */

#define RPL_BANLIST 367
#define RPL_ENDOFBANLIST 368
#define RPL_INVITELIST 346
#define RPL_ENDOFINVITELIST 347
#define RPL_EXCEPTLIST 348
#define RPL_ENDOFEXCEPTLIST 349
#define RPL_QUIETLIST 728
#define RPL_ENDOFQUIETLIST 729

static const struct
{
	const char *name, *type;
	char letter;
	int code, endcode;
} ban_modes[NX_BAN_KINDS] = {
	{ N_("Bans"), N_("Ban"), 'b', RPL_BANLIST, RPL_ENDOFBANLIST },
	{ N_("Exempts"), N_("Exempt"), 'e', RPL_EXCEPTLIST, RPL_ENDOFEXCEPTLIST },
	{ N_("Invites"), N_("Invite"), 'I', RPL_INVITELIST, RPL_ENDOFINVITELIST },
	{ N_("Quiets"), N_("Quiet"), 'q', RPL_QUIETLIST, RPL_ENDOFQUIETLIST },
};

/* the lists asked for and not ended yet, per session with a window open */
static GHashTable *ban_pending;

const char *nx_core_ban_kind_name (int k) { return k >= 0 && k < NX_BAN_KINDS ? _(ban_modes[k].name) : ""; }
const char *nx_core_ban_kind_type (int k) { return k >= 0 && k < NX_BAN_KINDS ? _(ban_modes[k].type) : ""; }

/* chanmodes' first group (list modes) has the letter */
static gboolean
listmode (server *serv, char letter)
{
	const char *cm = serv->chanmodes;
	for (; cm && *cm && *cm != ','; cm++)
		if (*cm == letter)
			return TRUE;
	return FALSE;
}

/* banlist.c's supports_*: what the server has (capable), what anyone can
   list (readable), what an op can (writeable) */
static void
ban_bits (session *sess, int *capable, int *readable, int *writeable)
{
	server *serv = sess->server;
	*capable = *readable = *writeable = 1 << 0;		/* bans: everywhere */
	if (serv->have_except || listmode (serv, 'e'))
	{
		*capable |= 1 << 1;
		*writeable |= 1 << 1;
	}
	if (serv->have_invite || listmode (serv, 'I'))
	{
		*capable |= 1 << 2;
		*writeable |= 1 << 2;
	}
	if (listmode (serv, 'q'))
	{
		*capable |= 1 << 3;
		*readable |= 1 << 3;
		*writeable |= 1 << 3;
	}
}

static session *
ban_sess (nx_key buf)
{
	session *sess = (session *) buf;
	return sess && is_session (sess) && sess->type == SESS_CHANNEL && sess->channel[0] ? sess : NULL;
}

int
nx_core_ban_is_op (nx_key buf)
{
	session *sess = ban_sess (buf);
	return sess && sess->me && (sess->me->op || sess->me->hop);
}

int
nx_core_ban_capable (nx_key buf)
{
	int c, r, w;
	session *sess = ban_sess (buf);
	if (!sess)
		return 0;
	ban_bits (sess, &c, &r, &w);
	return c;
}

int
nx_core_ban_checkable (nx_key buf)
{
	int c, r, w;
	session *sess = ban_sess (buf);
	if (!sess)
		return 0;
	ban_bits (sess, &c, &r, &w);
	return nx_core_ban_is_op (buf) ? w : r;
}

int
nx_core_ban_refresh (nx_key buf, int kinds)
{
	session *sess = ban_sess (buf);
	char tbuf[256];
	int i;

	if (!sess || !sess->server->connected)
		return 0;
	if (!ban_pending)
		ban_pending = g_hash_table_new (NULL, NULL);
	kinds &= nx_core_ban_checkable (buf);
	g_hash_table_insert (ban_pending, sess, GINT_TO_POINTER (kinds));
	if (!kinds)
		nx_ui_ban_end (buf);
	for (i = 0; i < NX_BAN_KINDS; i++)
		if (kinds & 1 << i)
		{
			g_snprintf (tbuf, sizeof tbuf, "quote mode %s +%c", sess->channel, ban_modes[i].letter);
			handle_command (sess, tbuf, FALSE);
		}
	return 1;
}

void
nx_core_ban_remove (nx_key buf, const char *const *masks, const int *kinds, int n)
{
	session *sess = ban_sess (buf);
	char tbuf[2048];
	char **word;
	int k, i, m;

	if (!sess || n <= 0)
		return;
	word = g_new (char *, n);
	for (k = 0; k < NX_BAN_KINDS; k++)
	{
		for (i = m = 0; i < n; i++)
			if (kinds[i] == k)
				word[m++] = (char *) masks[i];
		if (m)
			send_channel_modes (sess, tbuf, word, 0, m, '-', ban_modes[k].letter, 0);
	}
	g_free (word);
}

void
nx_core_ban_closed (nx_key buf)
{
	if (ban_pending)
		g_hash_table_remove (ban_pending, (gpointer) buf);
}

void
ban_session_gone (session *sess)
{
	nx_core_ban_closed ((nx_key) sess);
}

static int
pending_of (session *sess, gboolean *open)
{
	gpointer v = NULL;
	*open = ban_pending && g_hash_table_lookup_extended (ban_pending, sess, NULL, &v);
	return GPOINTER_TO_INT (v);
}

/* TRUE: the window took it (it isn't printed in the tab) */
gboolean
fe_add_ban_list (struct session *sess, char *mask, char *who, char *when, int rplcode)
{
	gboolean open;
	int i, pending = pending_of (sess, &open);

	for (i = 0; i < NX_BAN_KINDS; i++)
		if (ban_modes[i].code == rplcode)
			break;
	if (!open || i == NX_BAN_KINDS || !(pending & 1 << i))
		return FALSE;
	nx_ui_ban_add ((nx_key) sess, i, mask, who, when);
	ui_dirty ();
	return TRUE;
}

gboolean
fe_ban_list_end (struct session *sess, int rplcode)
{
	gboolean open;
	int i, pending = pending_of (sess, &open);

	for (i = 0; i < NX_BAN_KINDS; i++)
		if (ban_modes[i].endcode == rplcode)
			break;
	if (!open || i == NX_BAN_KINDS || !(pending & 1 << i))
		return FALSE;
	pending &= ~(1 << i);
	g_hash_table_insert (ban_pending, sess, GINT_TO_POINTER (pending));
	if (!pending)
		nx_ui_ban_end ((nx_key) sess);
	ui_dirty ();
	return TRUE;
}
