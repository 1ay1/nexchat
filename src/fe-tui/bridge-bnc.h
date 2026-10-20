/* nexchat TUI — the bouncer's side of the bridge (bridge.h's sibling).
 *
 * The GUI's Server > Bouncer... window (fe-gtk/bncwin.c) and its Stay
 * online setup (fe-gtk/bncsetup.c), as data and actions the TUI's windows
 * use: the same core calls (common/mybouncer.c, bouncer.c, bncprobe.c),
 * so both frontends do exactly the same thing. Implemented in bouncer.c.
 * Strings returned are g_free'd by the caller (nx_core_free), unless said.
 */
#ifndef NEXCHAT_TUI_BRIDGE_BNC_H
#define NEXCHAT_TUI_BRIDGE_BNC_H

#include "bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

void nx_core_free (void *p);

/* ── our own soju (mybouncer.c) ────────────────────────────────────── */

enum { NX_MYBNC_NONE, NX_MYBNC_AVAILABLE, NX_MYBNC_SET_UP };
int  nx_bnc_state (void);
int  nx_bnc_running (void);
const char *nx_bnc_kept_by (void);          /* "systemd" / "nexchat" (static) */
const char *nx_bnc_soju_path (void);        /* static, or NULL */
/* each returns ok; *err (g_free) when not */
int  nx_bnc_setup (char **err);
int  nx_bnc_start (char **err);
void nx_bnc_stop (void);
int  nx_bnc_remove (char **err);

/* The networks the Networks tab lists (the GUI's fill_list rule: on it, or
   a favorite / auto-connect / with autojoin, or the one it was opened
   from; never a bouncer-elsewhere entry). */
typedef struct
{
	int  net;          /* index in the Network List (nx_core_net_*) */
	char *name;
	int  mine;         /* on our soju */
	int  up;           /* connected through it */
	char *state;       /* "in #a, #b" / "switched off" / "can't connect: ..." /
	                      "connects directly (host)" / "joining..." / "bouncer stopped" */
	char *who;         /* mine: "as alice (its own nick), SASL as alice", else "" */
} nx_bnc_row;
/* from: the network it was opened from (-1 none). Returns the count;
   *rows to free with nx_bnc_rows_free. */
int  nx_bnc_rows (int from, nx_bnc_row **rows);
void nx_bnc_rows_free (nx_bnc_row *rows, int n);

/* switch a network on (adopt: in the background, progress to done) / off */
typedef void (*nx_bnc_progress) (void *ud, const char *text, int done, int ok);
void nx_bnc_adopt (int net, nx_bnc_progress cb, void *ud);
int  nx_bnc_release (int net, char **err);

/* "Nick and login..." for a network on our soju */
typedef struct
{
	char *nick, *nick_from, *login, *account;   /* mybouncer_identity */
	int  same;          /* the global nick (no nick of its own) */
	char *realname, *username;
	int  has_srvpass, has_account;
	int  away, enabled;
	char *commands;
	int  has_cert;
} nx_bnc_ident;
void nx_bnc_ident_get (int net, nx_bnc_ident *out);
void nx_bnc_ident_free (nx_bnc_ident *e);
/* Save, the GUI's editor_save: nick (NULL = the same as everywhere),
   account NULL = no login, pass "" with an account = keep the one it has,
   srvpass "" = keep. Returns ok, *err a sentence for the user. */
int  nx_bnc_ident_save (int net, const char *nick, const char *account, const char *pass,
                        const char *realname, const char *username, const char *srvpass,
                        int away, int enabled, const char *commands, char **err);
/* the certificate's fingerprint, made when make and there's none */
char *nx_bnc_certfp (int net, int make, char **err);

/* Channels tab */
int  nx_bnc_owned_count (void);              /* networks on it, in list order */
int  nx_bnc_owned (int i);                    /* → Network List index */
/* "name\tstatus" lines (g_free) */
char *nx_bnc_channels (int net);
int  nx_bnc_channel_add (int net, const char *chan, char **err);
int  nx_bnc_channel_del (int net, const char *chan, char **err);
int  nx_bnc_channel_detach (int net, const char *chan, int detached, char **err);

/* Access tab */
int  nx_bnc_port (void);
const char *nx_bnc_user (void);
char *nx_bnc_password (void);
int  nx_bnc_set_password (const char *pw, char **err);
int  nx_bnc_remote (void);
int  nx_bnc_set_remote (int on, char **err);
int  nx_bnc_remote_port (void);
char *nx_bnc_remote_certfp (void);
int  nx_bnc_can_systemd (void);
int  nx_bnc_set_systemd (int on, char **err);

/* Storage tab */
int  nx_bnc_logs_as_files (void);
int  nx_bnc_set_logs_as_files (int on, char **err);
char *nx_bnc_title (void);
int  nx_bnc_set_title (const char *t, char **err);
char *nx_bnc_log_tail (int lines);

/* ── bouncers elsewhere (bouncer.c): one card each ─────────────────── */

typedef struct
{
	char *id, *name, *host, *nick, *state, *error;
} nx_rnet;
typedef struct
{
	int  net;            /* Network List index of the entry */
	char *name, *where;  /* "irc.example.org:6697" / "irc://host:6667", "as alice" user */
	char *user;
	int  live;           /* logged in (soju / ZNC) */
	int  connecting;     /* a connection, not in yet */
	int  soju;           /* soju (else ZNC) */
	char *error;         /* the last login error, or NULL */
	int  n;              /* its networks */
	nx_rnet *nets;
} nx_rbnc;
int  nx_rbnc_list (nx_rbnc **out);
void nx_rbnc_list_free (nx_rbnc *l, int n);
/* a key that changes when anything a card shows changes (the 3 s tick) */
char *nx_rbnc_key (void);
void nx_rbnc_connect (int net);        /* Connect / Disconnect */
void nx_rbnc_forget (int net);
/* on the live connection of entry net */
int  nx_rbnc_add (int net, const char *name, const char *addr, char **err);
int  nx_rbnc_del (int net, const char *id, char **err);
int  nx_rbnc_change (int net, const char *id, const char *nick, const char *username,
                     const char *realname, const char *pass, char **err);
int  nx_rbnc_sasl (int net, const char *netname, const char *account, const char *password, char **err);
/* the network's tab ("Open"): focuses it, or connects it */
void nx_rbnc_open (int net, const char *netname);

/* ── Stay online setup (bncsetup.c) ────────────────────────────────── */

/* what this computer has (bncdetect): text lines for the choose page and
   whether ours / a local soju can be used. done once, on the main loop */
typedef struct
{
	int  ours;          /* nexchat's soju exists */
	int  soju;          /* soju installed */
	char *soju_version;
	char *soju_failed;  /* the system service fails: why, or NULL */
	int  znc;
	int  n_listen;      /* something answering locally that isn't ours */
	char **listen;      /* "soju on 127.0.0.1:6697 (TLS)" ... NULL-terminated */
	char **listen_addr; /* the address to fill in for each */
} nx_found;
typedef void (*nx_found_cb) (void *ud, const nx_found *f);
void nx_bnc_detect (nx_found_cb cb, void *ud);

/* the probe: kind ("soju", "znc", "persist", "plain") or error */
typedef struct
{
	const char *kind, *error, *networks;
	int cert_untrusted;
	const char *cert_why;
} nx_probe;
typedef void (*nx_probe_cb) (void *ud, const nx_probe *r);
/* addr as typed ("host", "host:port", "ircs://", "irc://"); 0 if it
   doesn't parse (cb not called) */
int  nx_bnc_probe (const char *addr, const char *user, const char *pass, nx_probe_cb cb, void *ud);
/* Connect on the bouncer page: the entry (new, or net's) written the way
   that bouncer wants and connected; networks: "name host" lines to add on
   it once in, plus other_host (as typed, named other_name or its host).
   Returns the entry's index, -1 with *err. */
int  nx_bnc_setup_bouncer (int from, const char *addr, const char *user, const char *pass,
                           int trust_cert, const char *networks, const char *other_host,
                           const char *other_name, char **err);
/* Connect on the persist page: net logs in with account (SASL) and stays on */
int  nx_bnc_setup_persist (int net, const char *account, const char *password, char **err);
/* the networks offered on the networks page */
int  nx_bnc_popular (int i, const char **name, const char **host);

/* the core changed something bouncer-related (fe_bouncer_changed) */
void nx_ui_bouncer_changed (void);
/* open the Bouncer window from network `from` (-1 none) */
void nx_ui_open_bouncer (int from);

#ifdef __cplusplus
}
#endif

#endif
