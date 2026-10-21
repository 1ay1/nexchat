/* nexchat TUI — the C ABI between the core glue (fe-tui.c, C) and the UI
 * (ui/, C++). See docs/tui-design.md §2.
 *
 * The UI never sees a core struct: a buffer is a `session *` cast to an
 * opaque key, a network a `server *`. Every value the UI shows is passed
 * in by value here. The other direction (the UI asking the core to do
 * something) is the nx_core_* functions, implemented in fe-tui.c.
 */
#ifndef NEXCHAT_TUI_BRIDGE_H
#define NEXCHAT_TUI_BRIDGE_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t nx_key;   /* a session* (buffer) or server* (network) */

/* What kind of buffer a session is (SESS_* in hexchat.h). */
enum nx_buf_kind { NX_BUF_SERVER, NX_BUF_CHANNEL, NX_BUF_QUERY, NX_BUF_OTHER };

/* fe_set_tab_color levels. */
enum nx_activity { NX_ACT_NONE, NX_ACT_DATA, NX_ACT_MSG, NX_ACT_HILIGHT };

/* ── core → UI ─────────────────────────────────────────────────────── */

/* Take the terminal; nonzero on failure (not a tty). */
int  nx_ui_open (void);
void nx_ui_close (void);
/* The UI's tty input fd, and whether it has bytes waiting for output. */
int  nx_ui_input_fd (void);
int  nx_ui_output_fd (void);
int  nx_ui_wants_output (void);
/* Feed the loop's wakeups to the UI. */
void nx_ui_on_input (void);
void nx_ui_on_output (void);
void nx_ui_on_resize (void);
/* Escape timeout: >0 ms if a lone ESC is waiting to be resolved. */
int  nx_ui_pending_ms (void);
void nx_ui_on_pending (void);
/* Draw if anything changed. Returns nonzero if another frame is wanted soon. */
int  nx_ui_present (void);
/* Has anything changed since the last frame? */
int  nx_ui_dirty (void);
/* Once a second: windows that refresh themselves (the Bouncer window);
   nonzero when a frame is due. */
int  nx_ui_tick (void);
/* Something on screen is animating: call nx_ui_anim every ~120 ms
   (it steps the frame and says whether to keep going). */
int  nx_ui_animating (void);
int  nx_ui_anim (void);

void nx_ui_buffer_new (nx_key buf, nx_key net, int kind, const char *name, int focus);
void nx_ui_buffer_free (nx_key buf);
void nx_ui_buffer_rename (nx_key buf, const char *name);
void nx_ui_buffer_focus (nx_key buf);
void nx_ui_buffer_activity (nx_key buf, int activity);
void nx_ui_buffer_joined (nx_key buf, int joined);       /* 0: parted / kicked */
void nx_ui_buffer_topic (nx_key buf, const char *topic);
void nx_ui_buffer_modes (nx_key buf, const char *modes);
void nx_ui_buffer_typing (nx_key buf, const char *text);

void nx_ui_print (nx_key buf, const char *text, time_t stamp, int hilight);
void nx_ui_clear (nx_key buf, int lines);

/* the input box, for scripts: the front buffer's line, any other's draft;
   the cursor counts characters */
const char *nx_ui_input_get (nx_key buf);
int  nx_ui_input_cursor (nx_key buf);
void nx_ui_input_set (nx_key buf, const char *text);
void nx_ui_input_set_cursor (nx_key buf, int delta, int pos);

/* a buffer's lines as printed ("left\tright", raw IRC text), for /lastlog;
   NULL past the end. Valid until the next print. */
int  nx_ui_line_count (nx_key buf);
const char *nx_ui_line_get (nx_key buf, int i, time_t *stamp);

/* a join (kind 0), part (1) or quit (2) folded into one line with the ones
   just before it; 0 = print it the usual way */
int  nx_ui_joinpart (nx_key buf, int kind, const char *nick, time_t stamp);

/* a desktop notification through the terminal (OSC 777 / 9) */
void nx_ui_notify (const char *title, const char *body);
/* the terminal window has focus (as far as it reports, ?1004) */
int  nx_ui_focused (void);
/* BEL, in order with the frames */
void nx_ui_bell (void);
/* a script added, changed or removed a /MENU item */
void nx_ui_menus_changed (void);
/* /reply on its own: choose the message to answer (msgid NULL), or reply
   to msgid; the bar over the input says so */
void nx_ui_reply_pick (nx_key buf, const char *msgid);

/* replies (+draft/reply): the recent lines of buf one can answer, newest
   first, as msgid + "nick\ttext" (colours stripped); 0 past the end */
int  nx_core_reply_recent (nx_key buf, int i, const char **msgid, const char **line);
/* who said msgid and what ("nick\ttext", stripped), NULL if gone */
const char *nx_core_reply_lookup (nx_key buf, const char *msgid);
/* send text in buf as a reply to msgid */
void nx_core_reply_send (nx_key buf, const char *msgid, const char *text);

/* The user list, as the core keeps it (sorted). */
void nx_ui_users_clear (nx_key buf);
void nx_ui_user_set (nx_key buf, const char *nick, char prefix, int away, int me, int pos);
void nx_ui_user_remove (nx_key buf, const char *nick);
void nx_ui_users_count (nx_key buf, int total, int ops);

void nx_ui_network_new (nx_key net, const char *name);
void nx_ui_network_free (nx_key net);
void nx_ui_network_name (nx_key net, const char *name);
void nx_ui_network_nick (nx_key net, const char *nick);
void nx_ui_network_state (nx_key net, int connected, int connecting);
void nx_ui_network_lag (nx_key net, double seconds);     /* <0: unknown */
void nx_ui_network_away (nx_key net, int away);

void nx_ui_message (const char *text, int error);

/* Prompts (fe_get_str / fe_get_int / fe_get_bool / fe_confirm): `done`
 * is called once with the answer (cancelled = 0). */
typedef void (*nx_str_done) (void *ud, int ok, const char *text);
void nx_ui_prompt (const char *title, const char *initial, nx_str_done done, void *ud);
typedef void (*nx_yes_done) (void *ud, int yes);
void nx_ui_confirm (const char *message, nx_yes_done done, void *ud);

/* ── UI → core ─────────────────────────────────────────────────────── */

/* Run what the user typed in buffer `buf` (text or /command). */
void nx_core_input (nx_key buf, const char *text);
/* Run a command in buffer `buf` (no history; a menu or a click). */
void nx_core_command (nx_key buf, const char *cmd);
/* The user switched to buffer `buf` (front session, marker, read). */
void nx_core_focus (nx_key buf);
/* Hand the UI buffer `buf`'s user list again (after a sort change). */
void nx_core_users_resync (nx_key buf);
/* Close buffer `buf` (part / close query / disconnect server). */
void nx_core_close (nx_key buf);
/* Quit nexchat. */
void nx_core_quit (void);
/* Completion candidates for `word` in `buf` (nicks, channels, commands),
 * written into `out` as NUL-separated strings, double-NUL terminated;
 * returns the count. */
int  nx_core_complete (nx_key buf, const char *word, int at_start, char *out, int outlen);
/* Expand a popup/userlist command for `nick` (%s %n %a …) and run it. */
void nx_core_nick_command (nx_key buf, const char *cmd, const char *nick);
/* The nick menu (popup.conf): `n` entries of name/cmd pairs. */
int  nx_core_popup_count (void);
void nx_core_popup_get (int i, const char **name, const char **cmd);
/* Menu items scripts added with /MENU (main menu only): its path
   ("Sub/Item", the leading menu dropped), command, toggle state (-1 = not
   a toggle), enabled. Returns 0 past the end. */
int  nx_core_plugin_menu_get (int i, const char **path, const char **cmd, int *state, int *enabled);
/* run the i'th one (a toggle flips) */
void nx_core_plugin_menu_run (nx_key buf, int i);
/* hex_gui_ulist_doubleclick */
const char *nx_core_ulist_doubleclick (void);
/* Classify a word as the core's url.c does: 0 none, 1 url, 2 channel,
 * 3 host, 4 host6, 5 email, 6 nick. */
int  nx_core_word_type (nx_key buf, const char *word);
void nx_core_open_url (const char *url);
/* Prefs the UI reads (nick column width, stamps …). */
int  nx_core_pref_int (const char *name);
const char *nx_core_pref_str (const char *name);
void nx_core_pref_set_str (const char *name, const char *value);
/* Set an int/bool pref by its hexchat.conf name (and run its hook). */
void nx_core_pref_set_int (const char *name, int value);
/* Write hexchat.conf (save_config). */
void nx_core_prefs_save (void);
/* Changes whenever any pref does (Preferences, /set, scripts). */
unsigned int nx_core_prefs_version (void);
/* The TUI's own settings (tui.conf in the config dir; the theme lives
   here, never in the GUI's files). get returns "" when unset. */
const char *nx_core_tui_get (const char *key);
void nx_core_tui_set (const char *key, const char *value);

/* ── the Network List (servlist.c's networks) ──────────────────────── */

/* One network as the editor sees it. Lists are '\n'-separated:
   servers "host/port" ("/+port" = TLS, the core's own syntax), autojoin
   "#chan key" per line, connect commands one per line. Strings are
   owned by the caller of nx_core_net_get (free with nx_core_net_free). */
typedef struct
{
	char *name, *nick, *nick2, *user, *real, *pass, *encoding;
	char *servers, *autojoin, *commands;
	int login;           /* index into nx_core_login_names */
	int use_global;      /* FLAG_USE_GLOBAL: the global nick/user */
	int ssl, cycle, auto_connect, allow_invalid, favorite, proxy;
} nx_net;

int  nx_core_net_count (void);
void nx_core_net_get (int i, nx_net *out);
void nx_core_net_free (nx_net *n);
/* Write `in` over network i (servlist_save()s). */
void nx_core_net_set (int i, const nx_net *in);
/* New network at the top, named `name`; returns its index. */
int  nx_core_net_add (const char *name);
void nx_core_net_remove (int i);
/* Connect to network i (in a free server tab, or a new one). */
void nx_core_net_connect (int i);
/* The login methods, in the GUI's dropdown order; NULL-terminated. */
const char *const *nx_core_login_names (void);
/* The global user info (Network List's top fields, in hexchat.conf). */
void nx_core_user_get (char **nick1, char **nick2, char **nick3, char **user);
void nx_core_user_set (const char *nick1, const char *nick2, const char *nick3, const char *user);
/* hex_gui_slist_skip, and whether to show the list at startup */
int  nx_core_slist_skip (void);
void nx_core_slist_skip_set (int skip);

/* ── core → UI: open the Network List (fe_serverlist_open) ─────────── */
void nx_ui_open_network_list (void);

/* ── the Channel List (/LIST): fe_add_chan_list rows ───────────────── */
void nx_ui_chanlist_begin (nx_key net);                  /* a /LIST started */
void nx_ui_chanlist_add (nx_key net, const char *chan, int users, const char *topic);
void nx_ui_chanlist_end (nx_key net);
/* Ask network `net` (a server*) for its channels (/LIST, filter may be ""). */
void nx_core_chanlist_refresh (nx_key net, const char *filter);
/* The Channel List window closed: /LIST output goes to the server tab. */
void nx_core_chanlist_closed (nx_key net);
/* Join `chan` on network `net`. */
void nx_core_join (nx_key net, const char *chan);
/* The network a buffer is on. */
nx_key nx_core_buffer_net (nx_key buf);
/* The Network List index of the buffer's network entry (-1: none). */
int nx_core_buffer_netlist_index (nx_key buf);

/* ── Ignore List (ignore.c) and Friends List (notify.c) ──────────────── */
/* ignore flags, ignore.h's IG_*: */
enum { NX_IG_PRIV = 1, NX_IG_NOTI = 2, NX_IG_CHAN = 4, NX_IG_CTCP = 8, NX_IG_INVI = 16,
       NX_IG_UNIG = 32, NX_IG_DCC = 128 };
int  nx_core_ignore_count (void);
const char *nx_core_ignore_get (int i, int *flags);
/* add or replace `mask` (an empty-flags add removes it); saved */
void nx_core_ignore_set (const char *mask, int flags);
void nx_core_ignore_remove (const char *mask);
/* mask is on the ignore list (ignore_exists) */
int  nx_core_ignored (const char *mask);

int  nx_core_notify_count (void);
/* name, its networks ("" = all), and whether it's online on any server */
const char *nx_core_notify_get (int i, const char **networks, int *online, long *seen);
void nx_core_notify_add (const char *name, const char *networks);
void nx_core_notify_remove (const char *name);
/* the core changed the lists (fe_ignore_update, fe_notify_update) */
void nx_ui_lists_changed (void);

/* ── File Transfers (dcc.c) ───────────────────────────────────────────── */
/* kind: 0 send, 1 receive, 2 chat; state: 0 queued, 1 active, 2 failed,
   3 done, 4 connecting, 5 aborted (dcc.h's dcc_state) */
typedef struct
{
	nx_key id;            /* the struct DCC* */
	int kind, state;
	const char *nick, *file;
	unsigned long long size, pos;
	double cps;           /* bytes per second, now */
	long eta;             /* seconds, -1 unknown */
} nx_dcc;
int  nx_core_dcc_count (void);
void nx_core_dcc_get (int i, nx_dcc *out);
/* accept (receive) / resume / abort / remove a finished one */
void nx_core_dcc_accept (nx_key id);
void nx_core_dcc_abort (nx_key id);
void nx_core_dcc_resume (nx_key id);
void nx_core_dcc_clear (void);
/* the core added / changed / removed a transfer */
void nx_ui_dcc_changed (void);
/* open the File Transfers window; returns whether it was already open
   (fe_dcc_open_recv_win's contract). passive: don't take the focus. */
int  nx_ui_open_transfers (int passive);

/* ── Raw Log (fe_add_rawlog): every line to and from a server ──────── */
void nx_ui_rawlog (nx_key net, const char *line, int outbound);

/* ── the command popup (fe-gtk/cmdpopup.c), on common/cmdinfo.c ───────── */
/* One suggestion: name (bold), middle column, a description (dim), what
   replaces the word being typed, a group heading above it (or ""), and the
   command a row is about. All owned by the list (nx_cmd_list_free). */
typedef struct
{
	char *name, *middle, *desc, *insert, *heading, *cmd;
} nx_cmd_item;
typedef struct
{
	nx_cmd_item *items;
	int n;
	int args;          /* the items are values for an argument (else commands) */
	int word_start;    /* byte offset of the word they replace */
	char *usage;       /* "/KICK <nick> [reason]" (args), "" otherwise */
	int arg_start, arg_end;  /* the current argument in usage, bytes (-1: none) */
	char *desc;        /* the command's one-line description */
	char *example;     /* "/join #linux", or "" */
} nx_cmd_list;
/* What to show for `text` with the caret at byte `cursor` in `buf`; NULL
   when it isn't a command being typed (or gui_input_cmdhelp is off). */
nx_cmd_list *nx_core_cmd_suggest (nx_key buf, const char *text, int cursor);
void nx_cmd_list_free (nx_cmd_list *l);
/* what one command does, its example and options, for the details line
   (nx_core_gfree it) */
char *nx_core_cmd_details (const char *cmd);
/* the command char ("/") */
char nx_core_cmd_char (void);

/* ── keybindings.conf (fe-gtk/fkeys.c's file, read the same way) ─────── */
/* the GUI's actions, in its order (key_actions[]) */
enum nx_key_action
{
	NX_KA_COMMAND, NX_KA_PAGE, NX_KA_INSERT, NX_KA_SCROLL, NX_KA_SET_BUFFER,
	NX_KA_HISTORY_UP, NX_KA_HISTORY_DOWN, NX_KA_COMPLETE, NX_KA_NICK_SELECT,
	NX_KA_REPLACE, NX_KA_TAB_LEFT, NX_KA_TAB_RIGHT, NX_KA_FAMILY_LEFT,
	NX_KA_FAMILY_RIGHT, NX_KA_PUSH_HISTORY
};
#define NX_KMOD_CTRL  1
#define NX_KMOD_ALT   2
#define NX_KMOD_SHIFT 4
/* a key: a GDK keyval name as the file has it ("Page_Up", "k", "F9") */
typedef struct
{
	const char *key;     /* owned by the core */
	int mods;            /* NX_KMOD_* */
	int action;
	const char *d1, *d2; /* NULL when unset; \n not yet expanded */
} nx_keybind;
/* 0 when there is no keybindings.conf (the TUI's own keys stand) */
int  nx_core_keybind_count (void);
void nx_core_keybind_get (int i, nx_keybind *out);
/* replace.conf on the last word of `text` (Check For Replace): the new
   line, or NULL if nothing changes (nx_core_gfree it) */
char *nx_core_replace (const char *text);
/* the most important activity first (lastact; Change Page "auto") */
nx_key nx_core_lastact (void);
/* run d1 as typed, \n separating lines (Run Command) */
void nx_core_run_typed (nx_key buf, const char *d1);

/* ── URL Grabber (urlgrab.c): the URLs seen, newest first ──────────── */
/* 0 when url_grabber is off (the window says so) */
int  nx_core_url_enabled (void);
/* the grabbed URLs, oldest first, NULL-terminated (nx_core_strfreev it) */
char **nx_core_urls (void);
void nx_core_strfreev (char **v);
void nx_core_gfree (void *p);
void nx_core_urls_clear (void);
/* write the list to path (url_save_tree); 0 on failure */
int  nx_core_urls_save (const char *path);
/* the core grabbed one (fe_url_add) */
void nx_ui_url_added (void);

/* ── Mentions (mentions.c): highlights and private messages ────────── */
typedef struct
{
	time_t stamp;
	const char *where, *who, *text;   /* owned by the core */
	nx_key buf;                        /* 0 once its tab is closed */
} nx_mention;
/* newest first */
int  nx_core_mention_count (void);
void nx_core_mention_get (int i, nx_mention *out);
void nx_core_mentions_clear (void);
void nx_ui_mentions_changed (void);

/* ── Plugins and Scripts (plugingui.c) ─────────────────────────────── */
typedef struct
{
	const char *name, *version, *file, *desc, *path;   /* owned by the core */
} nx_plugin;
int  nx_core_plugin_count (void);
void nx_core_plugin_get (int i, nx_plugin *out);
/* Unload: a .so is killed (1 = it refused), a script goes to its loader */
int  nx_core_plugin_unload (const char *name, const char *path);
/* LOAD / RELOAD path in the current tab */
void nx_core_plugin_load (const char *path, int reload);
/* addons/ in the config dir (nx_core_gfree it) */
char *nx_core_addons_dir (void);
void nx_ui_plugins_changed (void);

/* ── Ban List (banlist.c): +b +e +I +q of a channel ────────────────── */
/* the kinds: 0 Bans, 1 Exempts, 2 Invites, 3 Quiets */
#define NX_BAN_KINDS 4
const char *nx_core_ban_kind_name (int kind);   /* "Bans"… */
const char *nx_core_ban_kind_type (int kind);   /* "Ban"… */
/* bits of the kinds the channel's server has / you can list (op or not) */
int  nx_core_ban_capable (nx_key buf);
int  nx_core_ban_checkable (nx_key buf);
/* you're an op (or halfop) there: Remove / Crop / Clear work */
int  nx_core_ban_is_op (nx_key buf);
/* ask for the lists of the kinds in `kinds` (bits); 0 if not connected.
   The entries come back through nx_ui_ban_add, then nx_ui_ban_end. */
int  nx_core_ban_refresh (nx_key buf, int kinds);
/* -mode them: masks[i] of kind kinds[i] */
void nx_core_ban_remove (nx_key buf, const char *const *masks, const int *kinds, int n);
/* the window on buf closed: the core's replies print in the tab again */
void nx_core_ban_closed (nx_key buf);
void nx_ui_ban_add (nx_key buf, int kind, const char *mask, const char *who, const char *when);
void nx_ui_ban_end (nx_key buf);

#ifdef __cplusplus
}
#endif

#endif
