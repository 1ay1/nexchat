/* nexchat
 * Copyright (C) 2026 nexchat contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* What the "/" list says about each command, for someone who has never
   used IRC. Short words, an example for everything, options explained.
   Groups are in the order they're shown; "common" ones come first when
   only "/" is typed. */

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "cmddocs.h"

#define O(f, w) { f, w }
#define END { NULL, NULL }

static const cmddoc_opt opt_bouncer[] = {
	O ("LIST", N_("what you're connected through, and its networks")),
	O ("ADD", N_("have the bouncer join a network: ADD <name> <host>[:port] [-notls]")),
	O ("DEL", N_("have the bouncer leave a network: DEL <name>")),
	O ("ON", N_("stay online on this network (networks that can do it)")),
	O ("OFF", N_("don't stay online after you quit")),
	END
};

static const cmddoc_opt opt_lastlog[] = {
	O ("-a", N_("search every open window, not just this one")),
	O ("-l", N_("search your saved logs (everything you've logged)")),
	O ("-s", N_("search the bouncer's history, also from while nexchat was closed (soju); with -a every channel")),
	O ("-m", N_("match upper/lower case exactly")),
	O ("-r", N_("the text is a regular expression")),
	O ("-h", N_("highlight what was found")),
	END
};
static const cmddoc_opt opt_clear[] = {
	O ("ALL", N_("clear every window")),
	O ("HISTORY", N_("forget what you typed (Up arrow history)")),
	O ("-50", N_("remove only the last 50 lines")),
	END
};
static const cmddoc_opt opt_close[] = {
	O ("-m", N_("close all private chats at once")),
	END
};
static const cmddoc_opt opt_server[] = {
	O ("-ssl", N_("encrypted connection (the default)")),
	O ("-insecure", N_("plain, unencrypted connection")),
	O ("-ssl-noverify", N_("encrypted, but don't check the certificate")),
	END
};
static const cmddoc_opt opt_query[] = {
	O ("-nofocus", N_("open it in the background")),
	END
};
static const cmddoc_opt opt_set[] = {
	O ("-e", N_("show the value with its escapes")),
	O ("-on", N_("turn a yes/no setting on")),
	O ("-off", N_("turn a yes/no setting off")),
	O ("-quiet", N_("don't print what changed")),
	END
};
static const cmddoc_opt opt_ignore_types[] = {
	O ("ALL", N_("everything from them")),
	O ("PRIV", N_("private messages")),
	O ("CHAN", N_("their messages in channels")),
	O ("NOTI", N_("notices")),
	O ("CTCP", N_("CTCP requests (version, ping...)")),
	O ("DCC", N_("file and chat offers")),
	O ("INVI", N_("invites")),
	O ("NOSAVE", N_("only until you quit")),
	O ("QUIET", N_("don't print a confirmation")),
	END
};
static const cmddoc_opt opt_dcc[] = {
	O ("SEND", N_("send a file: /dcc send nick file")),
	O ("GET", N_("accept a file someone offered")),
	O ("CHAT", N_("start a direct chat")),
	O ("LIST", N_("show transfers")),
	O ("CLOSE", N_("cancel a transfer")),
	END
};
static const cmddoc_opt opt_exec[] = {
	O ("-o", N_("send the output to the channel instead of just showing it")),
	END
};
static const cmddoc_opt opt_reconnect[] = {
	O ("ALL", N_("reconnect to every server")),
	O ("-ssl", N_("use an encrypted connection")),
	END
};
static const cmddoc_opt opt_notify[] = {
	O ("-n", N_("only on these networks: -n Libera.Chat,OFTC")),
	END
};
static const cmddoc_opt opt_newserver[] = {
	O ("-noconnect", N_("open the tab but don't connect yet")),
	END
};
static const cmddoc_opt opt_load[] = {
	O ("-e", N_("run each line of a text file as a command")),
	END
};

static const cmddoc docs[] = {
	/* ---- Talking ---- */
	{ "MSG", N_("Talking"), N_("Send a private message"), "<nick> <message>",
	  "/msg alice are you around?", N_("Only that person sees it. It opens in its own tab if you've chatted before."), NULL, 1 },
	{ "QUERY", N_("Talking"), N_("Open a private chat with someone"), "<nick> [message]",
	  "/query alice", N_("Opens a tab just for the two of you."), opt_query, 1 },
	{ "ME", N_("Talking"), N_("Say what you're doing"), "<action>",
	  "/me waves hello", N_("Shows as \"* you waves hello\"."), NULL, 1 },
	{ "DELETE", N_("Talking"), N_("Take back your last message"), "[nick [reason]]",
	  "/delete", N_("Everyone sees that it was deleted. Operators can delete someone else's last one. Only on servers that allow it."), NULL, 0 },
	{ "REPLY", N_("Talking"), N_("Answer someone's last message"), "<nick> <message>",
	  "/reply alice sounds good", N_("Clients that show replies point to what you answered. Elsewhere it's sent as \"alice: message\"."), NULL, 0 },
	{ "SAY", N_("Talking"), N_("Send text as it is, even if it starts with /"), "<text>",
	  "/say /join is how you join", NULL, NULL, 0 },
	{ "NOTICE", N_("Talking"), N_("Send a notice (a message that isn't answered automatically)"), "<nick or #channel> <message>",
	  "/notice alice the meeting moved", NULL, NULL, 0 },
	{ "AWAY", N_("Talking"), N_("Mark yourself away"), "[reason]",
	  "/away lunch, back at 2", N_("People who message you get your reason. /back to return."), NULL, 1 },
	{ "BACK", N_("Talking"), N_("Mark yourself back"), "",
	  "/back", NULL, NULL, 1 },
	{ "NICK", N_("Talking"), N_("Change your name"), "<new nick>",
	  "/nick alice_away", N_("Letters, numbers and _-[]{}\\|^ only, no spaces."), NULL, 1 },

	/* ---- Channels ---- */
	{ "JOIN", N_("Channels"), N_("Go into a channel"), "<#channel> [password]",
	  "/join #linux", N_("Channels start with #. Some need a password (a key)."), NULL, 1 },
	{ "PART", N_("Channels"), N_("Leave this channel"), "[#channel] [reason]",
	  "/part bye all", N_("Leaves the channel you're in, or the one you name."), NULL, 1 },
	{ "CYCLE", N_("Channels"), N_("Leave and come straight back"), "[#channel]",
	  "/cycle", NULL, NULL, 0 },
	{ "TOPIC", N_("Channels"), N_("See or change the channel's topic"), "[new topic]",
	  "/topic Welcome! Rules: be nice", N_("Without text it shows the topic. Changing it may need channel operator rights."), NULL, 1 },
	{ "LIST", N_("Channels"), N_("Find channels on this server"), "",
	  "/list", N_("Opens the channel list, where you can search and join."), NULL, 1 },
	{ "NAMES", N_("Channels"), N_("List who is in the channel"), "[#channel]",
	  "/names", NULL, NULL, 0 },
	{ "INVITE", N_("Channels"), N_("Invite someone into a channel"), "<nick> [#channel]",
	  "/invite alice #private", NULL, NULL, 0 },
	{ "CLOSE", N_("Channels"), N_("Close this tab"), "[-m]",
	  "/close", N_("Leaves the channel and closes its tab."), opt_close, 0 },

	/* ---- People ---- */
	{ "WHOIS", N_("People"), N_("See who someone is"), "<nick>",
	  "/whois alice", N_("Shows their name, where they connect from, their channels and how long they've been idle."), NULL, 1 },
	{ "IGNORE", N_("People"), N_("Stop seeing someone's messages"), "<nick or mask> <what> [options]",
	  "/ignore spammer ALL", N_("Use their nick or a mask like *!*@example.com. /unignore undoes it."), opt_ignore_types, 1 },
	{ "UNIGNORE", N_("People"), N_("See someone's messages again"), "<nick or mask>",
	  "/unignore spammer", NULL, NULL, 0 },
	{ "NOTIFY", N_("People"), N_("Get told when someone comes online"), "[nick]",
	  "/notify alice", N_("Without a nick it shows your list."), opt_notify, 0 },
	{ "PING", N_("People"), N_("Check how fast someone answers"), "<nick or #channel>",
	  "/ping alice", NULL, NULL, 0 },
	{ "CTCP", N_("People"), N_("Ask someone's client something"), "<nick> <VERSION|TIME|PING>",
	  "/ctcp alice VERSION", N_("VERSION asks which program they use."), NULL, 0 },
	{ "DNS", N_("People"), N_("Look up someone's address"), "<nick or host>",
	  "/dns alice", NULL, NULL, 0 },

	/* ---- Moderating ---- */
	{ "KICK", N_("Moderating"), N_("Remove someone from the channel"), "<nick> [reason]",
	  "/kick spammer stop flooding", N_("Needs channel operator (@) rights. They can come back unless also banned."), NULL, 1 },
	{ "BAN", N_("Moderating"), N_("Stop someone from joining"), "<nick or mask>",
	  "/ban spammer", N_("Needs channel operator rights. /unban lifts it."), NULL, 0 },
	{ "KICKBAN", N_("Moderating"), N_("Ban, then remove someone"), "<nick> [reason]",
	  "/kickban spammer bye", NULL, NULL, 0 },
	{ "UNBAN", N_("Moderating"), N_("Let a banned person join again"), "<nick or mask>",
	  "/unban *!*@example.com", NULL, NULL, 0 },
	{ "OP", N_("Moderating"), N_("Make someone a channel operator (@)"), "<nick>",
	  "/op alice", N_("Operators can kick, ban and change the topic."), NULL, 0 },
	{ "DEOP", N_("Moderating"), N_("Take channel operator (@) away"), "<nick>",
	  "/deop alice", NULL, NULL, 0 },
	{ "VOICE", N_("Moderating"), N_("Let someone talk in a moderated channel (+)"), "<nick>",
	  "/voice alice", NULL, NULL, 0 },
	{ "DEVOICE", N_("Moderating"), N_("Take voice (+) away"), "<nick>",
	  "/devoice alice", NULL, NULL, 0 },
	{ "MODE", N_("Moderating"), N_("Change channel or user settings (modes)"), "<#channel or nick> <+/-modes> [values]",
	  "/mode #mychan +m", N_("+m only voiced people talk, +i invite only, +k sets a password, +t only ops change the topic."), NULL, 0 },

	/* ---- Connection ---- */
	{ "SERVER", N_("Connection"), N_("Connect to a server or network"), "<network or address> [port] [password]",
	  "/server irc.libera.chat", N_("Use a name from your network list (Libera.Chat) or an address."), opt_server, 1 },
	{ "NEWSERVER", N_("Connection"), N_("Connect in a new tab, keeping this one"), "<address> [port]",
	  "/newserver irc.oftc.net", NULL, opt_newserver, 0 },
	{ "RECONNECT", N_("Connection"), N_("Connect to this server again"), "[ALL]",
	  "/reconnect", NULL, opt_reconnect, 0 },
	{ "DISCON", N_("Connection"), N_("Disconnect from this server"), "",
	  "/discon", N_("The tab stays open; /reconnect to come back."), NULL, 0 },
	{ "QUIT", N_("Connection"), N_("Disconnect, with a goodbye message"), "[message]",
	  "/quit see you tomorrow", NULL, NULL, 1 },
	{ "ID", N_("Connection"), N_("Log in to your registered nick (NickServ)"), "<password>",
	  "/id mypassword", N_("Only you see the password. Better: set SASL in the network list."), NULL, 0 },
	{ "GHOST", N_("Connection"), N_("Kick an old connection that's using your nick"), "<nick> <password>",
	  "/ghost alice mypassword", NULL, NULL, 0 },
	{ "BOUNCER", N_("Connection"), N_("Stay online when nexchat is closed"), "[LIST|ADD|DEL|ON|OFF]",
	  "/bouncer add libera irc.libera.chat", N_("Through a bouncer (soju, ZNC) or a network that keeps you on. Network List > Stay online sets it up."), opt_bouncer, 1 },

	/* ---- This window ---- */
	{ "CLEAR", N_("This window"), N_("Clear the text in this window"), "[ALL|HISTORY|-number]",
	  "/clear", NULL, opt_clear, 1 },
	{ "LASTLOG", N_("This window"), N_("Search the chat"), "[options] <text>",
	  "/lastlog -a release", N_("Matching lines open in a (lastlog) tab."), opt_lastlog, 1 },
	{ "SETTAB", N_("This window"), N_("Rename this tab"), "<name>",
	  "/settab work", NULL, NULL, 0 },
	{ "CHARSET", N_("This window"), N_("Change the text encoding for this server"), "[encoding]",
	  "/charset UTF-8", N_("Only if letters look garbled."), NULL, 0 },

	/* ---- Files ---- */
	{ "DCC", N_("Files"), N_("Send or receive files, or chat directly"), "<SEND|GET|CHAT|LIST|CLOSE> ...",
	  "/dcc send alice ~/photo.jpg", N_("Direct between you and them, not through the server."), opt_dcc, 0 },

	/* ---- Settings and help ---- */
	{ "SET", N_("Settings"), N_("See or change a setting"), "[options] <setting> [value]",
	  "/set gui_tab_unread on", N_("Without a value it shows the setting. Most of these are also in Settings > Preferences."), opt_set, 0 },
	{ "HELP", N_("Settings"), N_("Help about a command"), "[command]",
	  "/help join", N_("Without a name it lists every command."), NULL, 1 },

	/* ---- Advanced ---- */
	{ "QUOTE", N_("Advanced"), N_("Send a raw line to the server"), "<IRC line>",
	  "/quote WHO #mychan", N_("For things nexchat has no command for."), NULL, 0 },
	{ "EXEC", N_("Advanced"), N_("Run a program on your computer"), "[-o] <command>",
	  "/exec uptime", NULL, opt_exec, 0 },
	{ "LOAD", N_("Advanced"), N_("Load a plugin or script"), "<file>",
	  "/load ~/script.py", NULL, opt_load, 0 },
	{ "UNLOAD", N_("Advanced"), N_("Unload a plugin or script"), "<name>",
	  "/unload script.py", NULL, NULL, 0 },
	{ "ALLCHAN", N_("Advanced"), N_("Run a command in every channel"), "<command>",
	  "/allchan say I'm back", NULL, NULL, 0 },
	{ "ALLSERV", N_("Advanced"), N_("Run a command on every server"), "<command>",
	  "/allserv away lunch", NULL, NULL, 0 },

	{ NULL }
};

const cmddoc *
cmddoc_find (const char *name)
{
	int i;

	for (i = 0; docs[i].name; i++)
		if (docs[i].group && !g_ascii_strcasecmp (docs[i].name, name))
			return &docs[i];
	return NULL;
}

const cmddoc *
cmddoc_all (void)
{
	return docs;
}

/* the groups in the order they're first seen in docs[] */
int
cmddoc_group_rank (const char *group)
{
	const char *seen[32];
	int n = 0, i, k;

	for (i = 0; docs[i].name; i++)
	{
		for (k = 0; k < n; k++)
			if (!strcmp (seen[k], docs[i].group))
				break;
		if (k == n && n < 32)
			seen[n++] = docs[i].group;
		if (!strcmp (docs[i].group, group))
			return k;
	}
	return n;
}
