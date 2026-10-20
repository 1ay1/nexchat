/* nexchat TUI — the frontend the core calls (fe.h), in C.
 *
 * Every fe_* the core calls is here. None of them draws: they forward
 * what changed to the UI through bridge.h (nx_ui_*), which marks the
 * frame dirty; the GLib loop draws once when it's idle. The nx_core_*
 * functions at the bottom are the other direction, the UI asking the core.
 * See docs/tui-design.md.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <signal.h>
#include <unistd.h>
#include <glib-unix.h>
#endif

#include <glib.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/util.h"
#include "../common/fe.h"
#include "../common/text.h"
#include "../common/url.h"
#include "../common/userlist.h"
#include "../common/server.h"
#include "../common/servlist.h"
#include "../common/plugin.h"
#include "../common/batch.h"
#include "../common/dcc.h"

#include "bridge.h"
#include "bridge-bnc.h"

extern const struct prefs vars[];	/* cfgfiles.c */

static GMainLoop *main_loop;
static guint redraw_tag, out_tag, pending_tag;

/* ── the loop: the tty in GLib's main loop ─────────────────────────── */

static gboolean redraw_cb (gpointer data);

/* tools.c */
void mentions_session_gone (session *sess);
void ban_session_gone (session *sess);

/* Something changed: draw once, when the loop is next idle. */
void
ui_dirty (void)
{
	if (!redraw_tag && main_loop)
		redraw_tag = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, redraw_cb, NULL, NULL);
}

static gboolean
out_cb (gint fd, GIOCondition cond, gpointer data)
{
	nx_ui_on_output ();
	if (!nx_ui_wants_output ())
	{
		out_tag = 0;
		if (nx_ui_dirty ())
			ui_dirty ();
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
pending_cb (gpointer data)
{
	pending_tag = 0;
	nx_ui_on_pending ();
	ui_dirty ();
	return G_SOURCE_REMOVE;
}

static gboolean anim_cb (gpointer data);
static guint anim_tag;

static gboolean
redraw_cb (gpointer data)
{
	redraw_tag = 0;
	/* a widget wants another frame soon (none of ours animate yet; maya's
	   may): ask again in a frame's time */
	if (nx_ui_present ())
		redraw_tag = g_timeout_add (16, redraw_cb, NULL);
	/* the tty took only part of the frame: push the rest when it can */
	if (nx_ui_wants_output () && !out_tag)
		out_tag = g_unix_fd_add (nx_ui_output_fd (), G_IO_OUT, out_cb, NULL);
	/* a spinner or typing dots on screen: frames keep coming, ~8/s */
	if (nx_ui_animating () && !anim_tag)
		anim_tag = g_timeout_add (120, anim_cb, NULL);
	return G_SOURCE_REMOVE;
}

static gboolean
anim_cb (gpointer data)
{
	if (!nx_ui_anim ())
	{
		anim_tag = 0;
		return G_SOURCE_REMOVE;
	}
	ui_dirty ();
	return G_SOURCE_CONTINUE;
}

static gboolean
in_cb (gint fd, GIOCondition cond, gpointer data)
{
	if (cond & (G_IO_HUP | G_IO_ERR))
	{
		hexchat_exit ();
		return G_SOURCE_REMOVE;
	}
	nx_ui_on_input ();
	{
		const int ms = nx_ui_pending_ms ();
		if (ms > 0 && !pending_tag)
			pending_tag = g_timeout_add (ms, pending_cb, NULL);
	}
	if (nx_ui_dirty ())
		ui_dirty ();
	return G_SOURCE_CONTINUE;
}

static gboolean
winch_cb (gpointer data)
{
	nx_ui_on_resize ();
	ui_dirty ();
	return G_SOURCE_CONTINUE;
}

/* ── startup ───────────────────────────────────────────────────────── */

static char *arg_cfgdir = NULL;
static gint arg_show_autoload = 0;
static gint arg_show_config = 0;
static gint arg_show_version = 0;
static gint arg_tui = 0;

static const GOptionEntry gopt_entries[] =
{
 {"no-auto",	'a', 0, G_OPTION_ARG_NONE,	&arg_dont_autoconnect, N_("Don't auto connect to servers"), NULL},
 {"cfgdir",	'd', 0, G_OPTION_ARG_STRING,	&arg_cfgdir, N_("Use a different config directory"), "PATH"},
 {"no-plugins",	'n', 0, G_OPTION_ARG_NONE,	&arg_skip_plugins, N_("Don't auto load any plugins"), NULL},
 {"plugindir",	'p', 0, G_OPTION_ARG_NONE,	&arg_show_autoload, N_("Show plugin/script auto-load directory"), NULL},
 {"configdir",	'u', 0, G_OPTION_ARG_NONE,	&arg_show_config, N_("Show user config directory"), NULL},
 {"tui",	 0,  G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE,	&arg_tui, N_("Run in the terminal (this is the terminal UI)"), NULL},
 {"url",	 0,  G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,	&arg_url, N_("Open an irc://server:port/channel URL"), "URL"},
 {"version",	'v', 0, G_OPTION_ARG_NONE,	&arg_show_version, N_("Show version information"), NULL},
 {G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_STRING_ARRAY, &arg_urls, N_("Open an irc://server:port/channel?key URL"), "URL"},
 {NULL}
};

int
fe_args (int argc, char *argv[])
{
	GError *error = NULL;
	GOptionContext *context;

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, gopt_entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);
	if (error)
	{
		if (error->message)
			fprintf (stderr, "%s\n", error->message);
		return 1;
	}
	g_option_context_free (context);

	if (arg_show_version)
	{
		printf (PACKAGE_NAME " " PACKAGE_VERSION "\n");
		return 0;
	}
	if (arg_show_autoload)
	{
#ifdef USE_PLUGIN
		printf ("%s\n", HEXCHATLIBDIR);
		return 0;
#else
		printf (PACKAGE_NAME " was built without plugin support\n");
		return 1;
#endif
	}
	if (arg_show_config)
	{
		printf ("%s\n", get_xdir ());
		return 0;
	}
	if (arg_cfgdir)
	{
		g_free (xdir);
		xdir = g_strdup (arg_cfgdir);
		if (xdir[0] && xdir[strlen (xdir) - 1] == '/')
			xdir[strlen (xdir) - 1] = 0;
		g_free (arg_cfgdir);
	}
	return -1;
}

void
fe_init (void)
{
	/* the pref accessors read the same fields as the struct (tests) */
	if (g_getenv ("NX_TUI_SELFTEST"))
	{
		int bad = 0;
		bad += nx_core_pref_int ("text_max_lines") != prefs.hex_text_max_lines;
		bad += nx_core_pref_int ("stamp_text") != (int) prefs.hex_stamp_text;
		bad += nx_core_pref_int ("gui_ulist_sort") != prefs.hex_gui_ulist_sort;
		bad += nx_core_pref_int ("gui_tab_newtofront") != prefs.hex_gui_tab_newtofront;
		bad += strcmp (nx_core_pref_str ("stamp_text_format"), prefs.hex_stamp_text_format) != 0;
		bad += strcmp (nx_core_pref_str ("irc_nick1"), prefs.hex_irc_nick1) != 0;
		fprintf (stderr, "nx-selftest prefs: %s\n", bad ? "FAIL" : "ok");
		if (bad)
			exit (3);
	}
	/* one window, tabs: the TUI is the GUI's tabbed main window */
	prefs.hex_gui_tab_server = 1;
	if (nx_ui_open () != 0)
	{
		fprintf (stderr, "nexchat --tui needs a terminal\n");
		exit (1);
	}
}

static gboolean
tick_cb (gpointer data)
{
	if (nx_ui_tick ())
		ui_dirty ();
	return G_SOURCE_CONTINUE;
}

void
fe_main (void)
{
	main_loop = g_main_loop_new (NULL, FALSE);
	g_unix_fd_add (nx_ui_input_fd (), G_IO_IN | G_IO_HUP | G_IO_ERR, in_cb, NULL);
#ifndef WIN32
	g_unix_signal_add (SIGWINCH, winch_cb, NULL);
#endif
	g_timeout_add_seconds (1, tick_cb, NULL);
	ui_dirty ();
	g_main_loop_run (main_loop);
	nx_ui_close ();
}

void
fe_cleanup (void)
{
}

void
fe_exit (void)
{
	if (main_loop)
		g_main_loop_quit (main_loop);
	else
		exit (0);
}

/* ── timers and sockets: GLib's, as every frontend ─────────────────── */

int
fe_timeout_add (int interval, void *callback, void *userdata)
{
	return g_timeout_add (interval, (GSourceFunc) callback, userdata);
}

int
fe_timeout_add_seconds (int interval, void *callback, void *userdata)
{
	return g_timeout_add_seconds (interval, (GSourceFunc) callback, userdata);
}

void
fe_timeout_remove (int tag)
{
	g_source_remove (tag);
}

void
fe_input_remove (int tag)
{
	g_source_remove (tag);
}

int
fe_input_add (int sok, int flags, void *func, void *data)
{
	int tag, type = 0;
	GIOChannel *channel;

#ifdef G_OS_WIN32
	if (flags & FIA_FD)
		channel = g_io_channel_win32_new_fd (sok);
	else
		channel = g_io_channel_win32_new_socket (sok);
#else
	channel = g_io_channel_unix_new (sok);
#endif
	if (flags & FIA_READ)
		type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
	if (flags & FIA_WRITE)
		type |= G_IO_OUT | G_IO_ERR;
	if (flags & FIA_EX)
		type |= G_IO_PRI;
	tag = g_io_add_watch (channel, type, (GIOFunc) func, data);
	g_io_channel_unref (channel);
	return tag;
}

void
fe_idle_add (void *func, void *data)
{
	g_idle_add (func, data);
}

/* ── sessions → buffers ────────────────────────────────────────────── */

static int
buf_kind (session *sess)
{
	switch (sess->type)
	{
	case SESS_SERVER:  return NX_BUF_SERVER;
	case SESS_CHANNEL: return NX_BUF_CHANNEL;
	case SESS_DIALOG:  return NX_BUF_QUERY;
	default:           return NX_BUF_OTHER;
	}
}

static const char *
buf_name (session *sess)
{
	if (sess->type == SESS_SERVER)
		return server_get_network (sess->server, TRUE);
	return sess->channel[0] ? sess->channel : server_get_network (sess->server, TRUE);
}

static void
focus_sess (session *sess)
{
	current_sess = sess;
	current_tab = sess;
	sess->server->front_session = sess;
	if (sess->server->server_session == NULL || sess->server->server_session->type != SESS_SERVER)
		sess->server->server_session = sess;
	/* read: its activity clears, as when a tab is clicked */
	sess->tab_state = TAB_STATE_NONE;
	sess->unread = sess->unread_hilight = 0;
	lastact_update (sess);
	nx_ui_buffer_focus ((nx_key) sess);
	nx_ui_buffer_activity ((nx_key) sess, NX_ACT_NONE);
}

void
fe_new_server (struct server *serv)
{
	nx_ui_network_new ((nx_key) serv, server_get_network (serv, TRUE));
	ui_dirty ();
}

void
fe_new_window (struct session *sess, int focus)
{
	if (!sess->server->front_session)
		sess->server->front_session = sess;
	if (!sess->server->server_session)
		sess->server->server_session = sess;

	/* the GUI's rule (maingui.c): focus the new tab on the first one, or
	   when asked and gui_tab_newtofront is "only asked" (2), or always (1) */
	const int want = !current_tab
		|| (prefs.hex_gui_tab_newtofront == 2 && focus)
		|| prefs.hex_gui_tab_newtofront == 1;

	nx_ui_buffer_new ((nx_key) sess, (nx_key) sess->server, buf_kind (sess), buf_name (sess), want);
	if (want)
		focus_sess (sess);
	if (sess->type == SESS_CHANNEL)
		nx_ui_buffer_joined ((nx_key) sess, 1);
	ui_dirty ();
}

void
fe_close_window (struct session *sess)
{
	/* the GUI closes the tab, and the tab's destroy tells the core */
	session_free (sess);
}

void
fe_session_callback (struct session *sess)
{
	/* the core is freeing sess: forget it */
	mentions_session_gone (sess);
	ban_session_gone (sess);
	nx_ui_buffer_free ((nx_key) sess);
	if (current_tab == sess)
		current_tab = NULL;
	ui_dirty ();
}

void
fe_server_callback (struct server *serv)
{
	nx_ui_network_free ((nx_key) serv);
	ui_dirty ();
}

void
fe_ctrl_gui (session *sess, fe_gui_action action, int arg)
{
	switch (action)
	{
	case FE_GUI_FOCUS:
	case FE_GUI_SHOW:
		focus_sess (sess);
		break;
	case FE_GUI_FLASH:
		fe_flash_window (sess);
		break;
	case FE_GUI_COLOR:
		fe_set_tab_color (sess, arg);
		break;
	default:
		break;
	}
	ui_dirty ();
}

int
fe_gui_info (session *sess, int info_type)
{
	if (info_type == 0)		/* window status: we're always "active" while on screen */
		return 1;
	return -1;
}

void *
fe_gui_info_ptr (session *sess, int info_type)
{
	return NULL;
}

void
fe_set_tab_color (struct session *sess, tabcolor col)
{
	int act = NX_ACT_NONE;
	switch (col & ~FE_COLOR_ALLFLAGS)
	{
	case FE_COLOR_NEW_DATA:    act = NX_ACT_DATA; break;
	case FE_COLOR_NEW_MSG:     act = NX_ACT_MSG; break;
	case FE_COLOR_NEW_HILIGHT: act = NX_ACT_HILIGHT; break;
	}
	if (sess == current_tab)
		act = NX_ACT_NONE;
	nx_ui_buffer_activity ((nx_key) sess, act);
	ui_dirty ();
}

void
fe_set_channel (struct session *sess)
{
	nx_ui_buffer_rename ((nx_key) sess, buf_name (sess));
	ui_dirty ();
}

void
fe_set_title (struct session *sess)
{
	ui_dirty ();
}

void
fe_set_nonchannel (struct session *sess, int state)
{
	if (sess->type == SESS_CHANNEL)
		nx_ui_buffer_joined ((nx_key) sess, state);
	ui_dirty ();
}

void
fe_clear_channel (struct session *sess)
{
	nx_ui_buffer_topic ((nx_key) sess, "");
	nx_ui_buffer_modes ((nx_key) sess, "");
	nx_ui_users_clear ((nx_key) sess);
	if (sess->type == SESS_CHANNEL)
		nx_ui_buffer_joined ((nx_key) sess, 0);
	ui_dirty ();
}

void
fe_set_topic (struct session *sess, char *topic, char *stripped_topic)
{
	nx_ui_buffer_topic ((nx_key) sess, topic ? topic : "");
	ui_dirty ();
}

static void
update_modes (session *sess)
{
	nx_ui_buffer_modes ((nx_key) sess, sess->current_modes ? sess->current_modes : "");
	ui_dirty ();
}

void
fe_update_mode_buttons (struct session *sess, char mode, char sign)
{
	update_modes (sess);
}

void
fe_update_channel_key (struct session *sess)
{
	update_modes (sess);
}

void
fe_update_channel_limit (struct session *sess)
{
	update_modes (sess);
}

void
fe_typing_changed (session *sess)
{
	char *t = typing_text (sess);
	nx_ui_buffer_typing ((nx_key) sess, t ? t : "");
	g_free (t);
	ui_dirty ();
}

/* ── text ──────────────────────────────────────────────────────────── */

void
fe_print_text (struct session *sess, char *text, time_t stamp, gboolean no_activity)
{
	/* the core may print several lines in one call */
	char *start = text, *nl;
	const int hilight = (sess->tab_state & TAB_STATE_NEW_HILIGHT) && !no_activity && sess != current_tab;

	while ((nl = strchr (start, '\n')))
	{
		*nl = 0;
		nx_ui_print ((nx_key) sess, start, stamp ? stamp : time (NULL), hilight);
		*nl = '\n';
		start = nl + 1;
	}
	if (*start)
		nx_ui_print ((nx_key) sess, start, stamp ? stamp : time (NULL), hilight);

	if (!no_activity)
	{
		if (sess == current_tab)
			fe_set_tab_color (sess, FE_COLOR_NONE);
		else if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
			fe_set_tab_color (sess, FE_COLOR_NEW_HILIGHT);
		else if (sess->tab_state & TAB_STATE_NEW_MSG)
			fe_set_tab_color (sess, FE_COLOR_NEW_MSG);
		else
			fe_set_tab_color (sess, FE_COLOR_NEW_DATA);
	}
	ui_dirty ();
}

void
fe_text_clear (struct session *sess, int lines)
{
	nx_ui_clear ((nx_key) sess, lines);
	ui_dirty ();
}

gboolean
fe_print_joinpart (struct session *sess, int kind, const char *nick, time_t stamp)
{
	return FALSE;
}

gboolean
fe_reply_pick (struct session *sess, const char *msgid)
{
	return FALSE;
}

void
fe_message (char *msg, int flags)
{
	nx_ui_message (msg, (flags & FE_MSG_ERROR) != 0);
	ui_dirty ();
}

void
fe_beep (session *sess)
{
	fputc (7, stdout);
	fflush (stdout);
}

void
fe_flash_window (struct session *sess)
{
	fe_beep (sess);
}

/* ── the user list ─────────────────────────────────────────────────── */

/* The user list in the GUI's order (gui_ulist_sort, userlistgui.c):
   0 by rank then name (the default), 1 by name, 2/3 those reversed,
   anything else the core's own order. */
static gint
ulist_cmp (gconstpointer a, gconstpointer b, gpointer serv)
{
	const int s = prefs.hex_gui_ulist_sort;
	int r = (s == 0 || s == 2)
		? nick_cmp_az_ops (serv, (struct User *) a, (struct User *) b)
		: nick_cmp_alpha ((struct User *) a, (struct User *) b, serv);
	return s >= 2 ? -r : r;
}

static void
users_resync (session *sess)
{
	GList *list, *l;
	int pos = 0;

	nx_ui_users_clear ((nx_key) sess);
	list = g_list_reverse (userlist_double_list (sess));	/* it prepends */
	if (prefs.hex_gui_ulist_sort <= 3)
		list = g_list_sort_with_data (list, ulist_cmp, sess->server);
	for (l = list; l; l = l->next)
	{
		struct User *u = l->data;
		nx_ui_user_set ((nx_key) sess, u->nick, u->prefix[0], u->away && prefs.hex_away_track, u->me, pos++);
	}
	g_list_free (list);
	if (g_getenv ("NX_TUI_TRACE"))
		g_printerr ("users_resync %s: %d users (total %d)\n", sess->channel, pos, sess->total);
	nx_ui_users_count ((nx_key) sess, sess->total, sess->ops);
	ui_dirty ();
}

void
fe_userlist_insert (struct session *sess, struct User *newuser, gboolean sel)
{
	users_resync (sess);
}

int
fe_userlist_remove (struct session *sess, struct User *user)
{
	users_resync (sess);
	return 0;
}

void
fe_userlist_rehash (struct session *sess, struct User *user)
{
	users_resync (sess);
}

void
fe_userlist_update (session *sess, struct User *user)
{
	users_resync (sess);
}

void
fe_userlist_numbers (struct session *sess)
{
	nx_ui_users_count ((nx_key) sess, sess->total, sess->ops);
	ui_dirty ();
}

void
fe_userlist_clear (struct session *sess)
{
	nx_ui_users_clear ((nx_key) sess);
	ui_dirty ();
}

void
fe_userlist_set_selected (struct session *sess)
{
}

void
fe_userlist_hide (session *sess)
{
}

void
fe_uselect (struct session *sess, char *word[], int do_clear, int scroll_to)
{
}

/* ── networks ──────────────────────────────────────────────────────── */

static void
net_state (server *serv)
{
	nx_ui_network_name ((nx_key) serv, server_get_network (serv, TRUE));
	nx_ui_network_nick ((nx_key) serv, serv->nick);
	nx_ui_network_state ((nx_key) serv, serv->connected, serv->connecting);
	nx_ui_network_away ((nx_key) serv, serv->is_away);
	if (serv->server_session)
		nx_ui_buffer_rename ((nx_key) serv->server_session, buf_name (serv->server_session));
	ui_dirty ();
}

void
fe_set_nick (struct server *serv, char *newnick)
{
	net_state (serv);
}

void
fe_change_nick (struct server *serv, char *nick, char *newnick)
{
	session *sess = find_dialog (serv, nick);
	if (sess)
		nx_ui_buffer_rename ((nx_key) sess, newnick);
	ui_dirty ();
}

void
fe_set_lag (server *serv, long lag)
{
	if (lag == -1)
	{
		if (!serv->lag_sent)
			return;
		lag = make_ping_time () - serv->lag_sent;
	}
	nx_ui_network_lag ((nx_key) serv, lag / 1000.0);
	ui_dirty ();
}

void
fe_set_throttle (server *serv)
{
}

void
fe_set_away (server *serv)
{
	net_state (serv);
}

void
fe_server_event (server *serv, int type, int arg)
{
	net_state (serv);
}

void
fe_progressbar_start (struct session *sess)
{
	if (sess->server)
		nx_ui_network_state ((nx_key) sess->server, sess->server->connected, 1);
	ui_dirty ();
}

void
fe_progressbar_end (struct server *serv)
{
	net_state (serv);
}

void
fe_bouncer_changed (server *serv)
{
	/* what the bouncer has for us changed: the window shows it */
	nx_ui_bouncer_changed ();
	net_state (serv);
}

/* ── prompts ───────────────────────────────────────────────────────── */

typedef struct
{
	void *cb;
	void *ud;
	int kind;	/* 0 str, 1 int, 2 bool */
} prompt_ctx;

static void
prompt_done (void *p, int ok, const char *text)
{
	prompt_ctx *c = p;
	switch (c->kind)
	{
	case 0:
		((void (*) (int, char *, void *)) c->cb) (!ok, (char *) text, c->ud);
		break;
	case 1:
		((void (*) (int, int, void *)) c->cb) (!ok, text ? atoi (text) : 0, c->ud);
		break;
	case 3:
		/* fe_get_file's contract: each file, then NULL once it's over */
		if (ok && text && *text)
			((void (*) (void *, char *)) c->cb) (c->ud, (char *) text);
		((void (*) (void *, char *)) c->cb) (c->ud, NULL);
		break;
	}
	g_free (c);
	ui_dirty ();
}

static void
bool_done (void *p, int yes)
{
	prompt_ctx *c = p;
	((void (*) (int, void *)) c->cb) (yes, c->ud);
	g_free (c);
	ui_dirty ();
}

void
fe_get_str (char *prompt, char *def, void *callback, void *ud)
{
	prompt_ctx *c = g_new0 (prompt_ctx, 1);
	c->cb = callback; c->ud = ud; c->kind = 0;
	nx_ui_prompt (prompt, def ? def : "", prompt_done, c);
	ui_dirty ();
}

void
fe_get_int (char *prompt, int def, void *callback, void *ud)
{
	char buf[32];
	prompt_ctx *c = g_new0 (prompt_ctx, 1);
	c->cb = callback; c->ud = ud; c->kind = 1;
	g_snprintf (buf, sizeof buf, "%d", def);
	nx_ui_prompt (prompt, buf, prompt_done, c);
	ui_dirty ();
}

void
fe_get_bool (char *title, char *prompt, void *callback, void *userdata)
{
	prompt_ctx *c = g_new0 (prompt_ctx, 1);
	c->cb = callback; c->ud = userdata; c->kind = 2;
	nx_ui_confirm (prompt, bool_done, c);
	ui_dirty ();
}

typedef struct
{
	void (*yes) (void *);
	void (*no) (void *);
	void *ud;
} confirm_ctx;

static void
confirm_done (void *p, int yes)
{
	confirm_ctx *c = p;
	if (yes && c->yes)
		c->yes (c->ud);
	else if (!yes && c->no)
		c->no (c->ud);
	g_free (c);
	ui_dirty ();
}

void
fe_confirm (const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud)
{
	confirm_ctx *c = g_new0 (confirm_ctx, 1);
	c->yes = yesproc; c->no = noproc; c->ud = ud;
	nx_ui_confirm (message, confirm_done, c);
	ui_dirty ();
}

void
fe_get_file (const char *title, char *initial,
				 void (*callback) (void *userdata, char *file), void *userdata, int flags)
{
	/* a path prompt (the TUI has no file chooser) */
	prompt_ctx *p = g_new0 (prompt_ctx, 1);
	p->cb = (void *) callback; p->ud = userdata; p->kind = 3;
	nx_ui_prompt (title, initial ? initial : "", prompt_done, p);
	ui_dirty ();
}

/* ── the Channel List (/LIST) ───────────────────────────────────────── */

static GHashTable *chanlist_open;	/* server* → 1 while its list window is up */

int
fe_is_chanwindow (struct server *serv)
{
	return chanlist_open && g_hash_table_contains (chanlist_open, serv);
}

void
fe_add_chan_list (struct server *serv, char *chan, char *users, char *topic)
{
	nx_ui_chanlist_add ((nx_key) serv, chan, atoi (users), topic ? topic : "");
	ui_dirty ();
}

void
fe_chan_list_end (struct server *serv)
{
	nx_ui_chanlist_end ((nx_key) serv);
	ui_dirty ();
}

void
fe_open_chan_list (server *serv, char *filter, int do_refresh)
{
	if (!chanlist_open)
		chanlist_open = g_hash_table_new (NULL, NULL);
	g_hash_table_add (chanlist_open, serv);
	nx_ui_chanlist_begin ((nx_key) serv);
	if (do_refresh && serv->connected)
		serv->p_list_channels (serv, filter ? filter : "", 1);
	ui_dirty ();
}

/* the UI closed its Channel List: /LIST replies go back to the server tab */
void
nx_core_chanlist_closed (nx_key net)
{
	if (chanlist_open)
		g_hash_table_remove (chanlist_open, (void *) net);
}

void
nx_core_chanlist_refresh (nx_key net, const char *filter)
{
	server *serv = (server *) net;
	GSList *l;
	for (l = serv_list; l; l = l->next)
		if (l->data == serv && serv->connected)
		{
			fe_open_chan_list (serv, (char *) filter, TRUE);
			return;
		}
}

void
nx_core_join (nx_key net, const char *chan)
{
	server *serv = (server *) net;
	GSList *l;
	for (l = serv_list; l; l = l->next)
		if (l->data == serv && serv->connected)
		{
			serv->p_join (serv, (char *) chan, "");
			return;
		}
}

nx_key
nx_core_buffer_net (nx_key buf)
{
	session *sess = (session *) buf;
	return sess && is_session (sess) ? (nx_key) sess->server : 0;
}

int
nx_core_buffer_netlist_index (nx_key buf)
{
	session *sess = (session *) buf;
	if (!sess || !is_session (sess) || !sess->server->network)
		return -1;
	return g_slist_index (network_list, sess->server->network);
}

/* ── everything the TUI doesn't have (yet): see the design, §6 ─────── */

void
fe_add_rawlog (struct server *serv, char *text, int len, int outbound)
{
	char **lines = g_strsplit (text, "\r\n", 0), **p;
	for (p = lines; *p && **p; p++)
		nx_ui_rawlog ((nx_key) serv, *p, outbound);
	g_strfreev (lines);
}

/* ── File Transfers ─────────────────────────────────────────────────── */

void fe_dcc_add (struct DCC *dcc) { nx_ui_dcc_changed (); ui_dirty (); }
void fe_dcc_update (struct DCC *dcc) { nx_ui_dcc_changed (); ui_dirty (); }
void fe_dcc_remove (struct DCC *dcc) { nx_ui_dcc_changed (); ui_dirty (); }

int
nx_core_dcc_count (void)
{
	return g_slist_length (dcc_list);
}

void
nx_core_dcc_get (int i, nx_dcc *out)
{
	struct DCC *d = g_slist_nth_data (dcc_list, i);
	memset (out, 0, sizeof *out);
	if (!d)
		return;
	out->id = (nx_key) d;
	out->kind = d->type == TYPE_SEND ? 0 : d->type == TYPE_RECV ? 1 : 2;
	out->state = d->dccstat;
	out->nick = d->nick ? d->nick : "";
	out->file = d->file ? d->file : "";
	out->size = d->size;
	out->pos = d->type == TYPE_SEND ? d->ack : d->pos;
	out->cps = d->cps;
	out->eta = d->cps > 0 && d->size > out->pos ? (long) ((d->size - out->pos) / d->cps) : -1;
}

static struct DCC *
dcc_of (nx_key id)
{
	return g_slist_find (dcc_list, (void *) id) ? (struct DCC *) id : NULL;
}

void
nx_core_dcc_accept (nx_key id)
{
	struct DCC *d = dcc_of (id);
	if (d && d->type == TYPE_RECV && d->dccstat == STAT_QUEUED)
		dcc_get (d);
	else if (d && (d->type == TYPE_CHATRECV) && d->dccstat == STAT_QUEUED)
		dcc_get (d);
}

void
nx_core_dcc_abort (nx_key id)
{
	struct DCC *d = dcc_of (id);
	if (d)
		dcc_abort (d->serv ? d->serv->front_session : current_sess, d);
}

void
nx_core_dcc_resume (nx_key id)
{
	struct DCC *d = dcc_of (id);
	if (d && d->type == TYPE_RECV && d->dccstat == STAT_QUEUED)
		dcc_resume (d);
}

/* Clear: forget the finished ones (done, failed, aborted), as the GUI's */
void
nx_core_dcc_clear (void)
{
	GSList *l = dcc_list;
	while (l)
	{
		struct DCC *d = l->data;
		l = l->next;
		if (d->dccstat == STAT_DONE || d->dccstat == STAT_FAILED || d->dccstat == STAT_ABORTED)
			dcc_abort (d->serv ? d->serv->front_session : current_sess, d);
	}
}
void fe_notify_update (char *name) { nx_ui_lists_changed (); ui_dirty (); }
void fe_notify_ask (char *name, char *networks) {}
int fe_dcc_open_recv_win (int passive) { return nx_ui_open_transfers (passive); }
int fe_dcc_open_send_win (int passive) { return nx_ui_open_transfers (passive); }
int fe_dcc_open_chat_win (int passive) { return nx_ui_open_transfers (passive); }
void fe_dcc_send_filereq (struct session *sess, char *nick, int maxcps, int passive) {}
void fe_url_add (const char *text) { nx_ui_url_added (); ui_dirty (); }
void fe_pluginlist_update (void) { nx_ui_plugins_changed (); ui_dirty (); }
void fe_buttons_update (struct session *sess) {}
void fe_dlgbuttons_update (struct session *sess) {}
void fe_ignore_update (int level) { nx_ui_lists_changed (); ui_dirty (); }
void fe_lastlog (session *sess, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags) {}
int fe_lastlog_append (session *sess, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags) { return 0; }
void fe_serverlist_open (session *sess) { nx_ui_open_network_list (); ui_dirty (); }
char *fe_get_inputbox_contents (struct session *sess) { return NULL; }
int fe_get_inputbox_cursor (struct session *sess) { return 0; }
void fe_set_inputbox_contents (struct session *sess, char *text) {}
void fe_set_inputbox_cursor (struct session *sess, int delta, int pos) {}
void fe_menu_del (menu_entry *me) {}
char *fe_menu_add (menu_entry *me) { return NULL; }
void fe_menu_update (menu_entry *me) {}
void fe_tray_set_flash (const char *filename1, const char *filename2, int timeout) {}
void fe_tray_set_file (const char *filename) {}
void fe_tray_set_icon (feicon icon) {}
void fe_tray_set_tooltip (const char *text) {}
const char *fe_get_default_font (void) { return NULL; }

void
fe_open_url (const char *url)
{
	nx_core_open_url (url);
}

/* ── the UI asking the core ────────────────────────────────────────── */

static session *
sess_of (nx_key buf)
{
	session *s = (session *) buf;
	return s && is_session (s) ? s : current_sess;
}

void
nx_core_input (nx_key buf, const char *text)
{
	session *sess = sess_of (buf);
	char *copy;
	if (!sess)
		return;
	copy = g_strdup (text);
	handle_multiline (sess, copy, TRUE, FALSE);
	g_free (copy);
	ui_dirty ();
}

void
nx_core_command (nx_key buf, const char *cmd)
{
	session *sess = sess_of (buf);
	char *copy;
	if (!sess)
		return;
	copy = g_strdup (cmd);
	if (*copy == '!')
		hexchat_exec (copy + 1);
	else
		handle_command (sess, copy, TRUE);
	g_free (copy);
	ui_dirty ();
}

void
nx_core_focus (nx_key buf)
{
	session *sess = sess_of (buf);
	if (sess)
		focus_sess (sess);
	ui_dirty ();
}

void
nx_core_users_resync (nx_key buf)
{
	session *sess = (session *) buf;
	if (sess && is_session (sess))
		users_resync (sess);
}

void
nx_core_close (nx_key buf)
{
	session *sess = sess_of (buf);
	if (!sess)
		return;
	if (sess_list && !sess_list->next)
	{
		hexchat_exit ();
		return;
	}
	fe_close_window (sess);
	ui_dirty ();
}

void
nx_core_quit (void)
{
	hexchat_exit ();
}

static int
complete_add (char **p, int *left, const char *s)
{
	const int n = strlen (s) + 1;
	if (n + 1 > *left)
		return 0;
	memcpy (*p, s, n);
	*p += n;
	*left -= n;
	return 1;
}

int
nx_core_complete (nx_key buf, const char *word, int at_start, char *out, int outlen)
{
	session *sess = sess_of (buf);
	char *p = out;
	int left = outlen, count = 0, i;
	const size_t wl = strlen (word);
	GSList *l;

	if (!sess || outlen < 2)
		return 0;
	out[0] = out[1] = 0;

	if (word[0] == prefs.hex_input_command_char[0])
	{
		/* commands: the core's, the user's, the plugins' */
		for (i = 0; xc_cmds[i].name; i++)
			if (!g_ascii_strncasecmp (xc_cmds[i].name, word + 1, wl - 1))
			{
				char tmp[128];
				g_snprintf (tmp, sizeof tmp, "%c%s", prefs.hex_input_command_char[0], xc_cmds[i].name);
				g_ascii_strdown (tmp, -1);
				count += complete_add (&p, &left, tmp);
			}
		for (l = command_list; l; l = l->next)
		{
			struct popup *pop = l->data;
			if (!g_ascii_strncasecmp (pop->name, word + 1, wl - 1))
			{
				char tmp[128];
				g_snprintf (tmp, sizeof tmp, "%c%s", prefs.hex_input_command_char[0], pop->name);
				count += complete_add (&p, &left, tmp);
			}
		}
	}
	else if (sess->server->chantypes && strchr (sess->server->chantypes, word[0]))
	{
		for (l = sess_list; l; l = l->next)
		{
			session *s = l->data;
			if (s->type == SESS_CHANNEL && s->channel[0] && !rfc_ncasecmp (s->channel, (char *) word, wl))
				count += complete_add (&p, &left, s->channel);
		}
	}
	else
	{
		GList *users = userlist_double_list (sess), *u;
		for (u = users; u; u = u->next)
		{
			struct User *user = u->data;
			if (!user->me && !rfc_ncasecmp (user->nick, (char *) word, wl))
				count += complete_add (&p, &left, user->nick);
		}
		g_list_free (users);
		if (sess->type == SESS_DIALOG && !rfc_ncasecmp (sess->channel, (char *) word, wl))
			count += complete_add (&p, &left, sess->channel);
	}
	*p = 0;
	return count;
}

void
nx_core_nick_command (nx_key buf, const char *cmd, const char *nick)
{
	session *sess = sess_of (buf);
	char *buf2;
	char *host = _("Host unknown");
	char *account = _("Account unknown");
	struct User *user;
	int len;

	if (!sess)
		return;
	user = userlist_find (sess, nick);
	if (user)
	{
		if (user->hostname && strchr (user->hostname, '@'))
			host = strchr (user->hostname, '@') + 1;
		if (user->account)
			account = user->account;
	}
	len = strlen (cmd) + strlen (nick) * 2 + 512;
	buf2 = g_malloc (len);
	auto_insert (buf2, len, (unsigned char *) cmd, 0, 0, (char *) nick, sess->channel, "",
					 server_get_network (sess->server, TRUE), host,
					 sess->server->nick, (char *) nick, account);
	nx_core_command ((nx_key) sess, buf2);
	g_free (buf2);
}

int
nx_core_popup_count (void)
{
	return g_slist_length (popup_list);
}

void
nx_core_popup_get (int i, const char **name, const char **cmd)
{
	struct popup *pop = g_slist_nth_data (popup_list, i);
	*name = pop ? pop->name : "";
	*cmd = pop ? pop->cmd : "";
}

const char *
nx_core_ulist_doubleclick (void)
{
	return prefs.hex_gui_ulist_doubleclick;
}

int
nx_core_word_type (nx_key buf, const char *word)
{
	session *sess = sess_of (buf);
	int t = url_check_word (word);
	if (t)
		return t;
	if (sess && userlist_find (sess, word))
		return WORD_NICK;
	return 0;
}

void
nx_core_open_url (const char *url)
{
#ifdef __APPLE__
	const char *argv[] = { "open", url, NULL };
#elif defined (WIN32)
	const char *argv[] = { "explorer", url, NULL };
#else
	const char *argv[] = { "xdg-open", url, NULL };
#endif
	g_spawn_async (NULL, (char **) argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL
						| G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

/* A pref by its hexchat.conf name, through the core's own table. */
static const struct prefs *
pref_find (const char *name)
{
	int i;
	for (i = 0; vars[i].name; i++)
		if (!strcmp (vars[i].name, name))
			return &vars[i];
	return NULL;
}

int
nx_core_pref_int (const char *name)
{
	const struct prefs *p = pref_find (name);
	if (!p || p->type == TYPE_STR)
		return 0;
	return *((int *) &prefs + p->offset);	/* int prefs index in ints (STRUCT_OFFSET_INT) */
}

const char *
nx_core_pref_str (const char *name)
{
	const struct prefs *p = pref_find (name);
	if (!p || p->type != TYPE_STR)
		return "";
	return (char *) &prefs + p->offset;
}

void
nx_core_pref_set_str (const char *name, const char *value)
{
	const struct prefs *p = pref_find (name);
	if (!p || p->type != TYPE_STR)
		return;
	g_strlcpy ((char *) &prefs + p->offset, value, p->len);
	if (p->after_update)
		p->after_update ();
}

void
nx_core_pref_set_int (const char *name, int value)
{
	const struct prefs *p = pref_find (name);
	if (!p || p->type == TYPE_STR)
		return;
	*((int *) &prefs + p->offset) = p->type == TYPE_BOOL ? (value != 0) : value;
	if (p->after_update)
		p->after_update ();
}

void
nx_core_prefs_save (void)
{
	save_config ();
}

/* a fingerprint of every pref: changes when anything does (Preferences,
   /set, a script), so the UI can re-read what it draws from */
unsigned int
nx_core_prefs_version (void)
{
	const unsigned char *p = (const unsigned char *) &prefs;
	unsigned int h = 2166136261u;
	size_t i;
	for (i = 0; i < sizeof prefs; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

/* tui.conf: [tui] key=value, the TUI's own settings */
static GKeyFile *tui_conf;

static char *
tui_conf_path (void)
{
	return g_build_filename (get_xdir (), "tui.conf", NULL);
}

const char *
nx_core_tui_get (const char *key)
{
	static char *last;
	if (!tui_conf)
	{
		char *path = tui_conf_path ();
		tui_conf = g_key_file_new ();
		g_key_file_load_from_file (tui_conf, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
		g_free (path);
	}
	g_free (last);
	last = g_key_file_get_string (tui_conf, "tui", key, NULL);
	return last ? last : "";
}

void
nx_core_tui_set (const char *key, const char *value)
{
	char *path;
	(void) nx_core_tui_get (key);	/* loads it */
	g_key_file_set_string (tui_conf, "tui", key, value);
	path = tui_conf_path ();
	g_key_file_save_to_file (tui_conf, path, NULL);
	g_free (path);
}
