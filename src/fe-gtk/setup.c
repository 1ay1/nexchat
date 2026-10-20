/* X-Chat
 * Copyright (C) 2004-2007 Peter Zelezny.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../common/hexchat.h"
#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "../common/text.h"
#include "../common/userlist.h"
#include "../common/util.h"
#include "../common/hexchatc.h"
#include "../common/outbound.h"
#include "fe-gtk.h"
#include "gtkutil.h"
#include "nxlist.h"
#include "maingui.h"
#include "palette.h"
#include "platform-style.h"
#include "pixmaps.h"
#include "menu.h"
#include "plugin-tray.h"
#include "notifications/notification-backend.h"

#ifdef WIN32
#include "../common/fe.h"
#endif
#include "sexy-spell-entry.h"

static void setup_live (void);


#define LABEL_INDENT 12

static GtkWidget *setup_window = NULL;
static GtkWidget *swatch[MAX_COL + 1];
static int last_selected_page = 0;
static int last_selected_row = 0; /* sound row */
static gboolean color_change;
static struct hexchatprefs setup_prefs;
static GtkWidget *cancel_button;

enum
{
	ST_END,
	ST_TOGGLE,
	ST_TOGGLR,
	ST_3OGGLE,
	ST_ENTRY,
	ST_EFONT,
	ST_EFILE,
	ST_EFOLDER,
	ST_MENU,
	ST_RADIO,
	ST_NUMBER,
	ST_HSCALE,
	ST_HEADER,
	ST_LABEL,
	ST_ALERTHEAD
};

typedef struct
{
	int type;
	char *label;
	int offset;
	char *tooltip;
	char const *const *list;
	int extra;
} setting;

#ifdef WIN32
static const char *const langsmenu[] =
{
	N_("Afrikaans"),
	N_("Albanian"),
	N_("Amharic"),
	N_("Asturian"),
	N_("Azerbaijani"),
	N_("Basque"),
	N_("Belarusian"),
	N_("Bulgarian"),
	N_("Catalan"),
	N_("Chinese (Simplified)"),
	N_("Chinese (Traditional)"),
	N_("Czech"),
	N_("Danish"),
	N_("Dutch"),
	N_("English (British)"),
	N_("English"),
	N_("Estonian"),
	N_("Finnish"),
	N_("French"),
	N_("Galician"),
	N_("German"),
	N_("Greek"),
	N_("Gujarati"),
	N_("Hindi"),
	N_("Hungarian"),
	N_("Indonesian"),
	N_("Italian"),
	N_("Japanese"),
	N_("Kannada"),
	N_("Kinyarwanda"),
	N_("Korean"),
	N_("Latvian"),
	N_("Lithuanian"),
	N_("Macedonian"),
	N_("Malay"),
	N_("Malayalam"),
	N_("Norwegian (Bokmal)"),
	N_("Norwegian (Nynorsk)"),
	N_("Polish"),
	N_("Portuguese"),
	N_("Portuguese (Brazilian)"),
	N_("Punjabi"),
	N_("Russian"),
	N_("Serbian"),
	N_("Slovak"),
	N_("Slovenian"),
	N_("Spanish"),
	N_("Swedish"),
	N_("Thai"),
	N_("Turkish"),
	N_("Ukrainian"),
	N_("Vietnamese"),
	N_("Walloon"),
	NULL
};
#endif

static const setting appearance_settings[] =
{
	{ST_HEADER,	N_("General"),0,0,0},
#ifdef WIN32
	{ST_MENU,   N_("Language:"), P_OFFINTNL(hex_gui_lang), 0, langsmenu, 0},
	{ST_EFONT,  N_("Main font:"), P_OFFSETNL(hex_text_font_main), 0, 0, sizeof prefs.hex_text_font_main},
#else
	{ST_EFONT,  N_("Font:"), P_OFFSETNL(hex_text_font), 0, 0, sizeof prefs.hex_text_font},
#endif

	{ST_HEADER,	N_("Text Box"),0,0,0},
	{ST_TOGGLE, N_("Colored nick names"), P_OFFINTNL(hex_text_color_nicks), N_("Give each person on IRC a different color"),0,0},
	{ST_TOGGLR, N_("Indent nick names"), P_OFFINTNL(hex_text_indent), N_("Make nick names right-justified"),0,0},
	{ST_TOGGLE, N_ ("Show marker line"), P_OFFINTNL (hex_text_show_marker), N_ ("Insert a red line after the last read text."), 0, 0},
	{ST_TOGGLE, N_ ("Show day separators"), P_OFFINTNL (hex_text_day_sep), N_ ("A line with the date where the day changes."), 0, 0},
	{ST_EFILE, N_ ("Background image:"), P_OFFSETNL (hex_text_background), 0, 0, sizeof prefs.hex_text_background},
	{ST_NUMBER, N_("Text zoom:"), P_OFFINTNL(hex_text_zoom), N_("Size of the chat text, in percent. Ctrl+wheel and Ctrl+=/-/0 change it."), (const char **)N_("%"), 300},

	{ST_HEADER, N_("Modern Features"), 0,0,0},
	{ST_TOGGLE, N_("Jump to latest button"), P_OFFINTNL(hex_gui_jump_button), N_("While scrolled up, show a button with the number of new lines that takes you back down."),0,0},
	{ST_TOGGLR, N_("Zoom with Ctrl+wheel and Ctrl+=/-/0"), P_OFFINTNL(hex_gui_zoom_keys),0,0,0},

	{ST_HEADER, N_("Transparency Settings"), 0,0,0},
	{ST_HSCALE, N_("Window opacity:"), P_OFFINTNL(hex_gui_transparency),0,0,0},

	{ST_HEADER,	N_("Timestamps"),0,0,0},
	{ST_TOGGLE, N_("Enable timestamps"), P_OFFINTNL(hex_stamp_text),0,0,1},
	{ST_ENTRY,  N_("Timestamp format:"), P_OFFSETNL(hex_stamp_text_format),
#ifdef WIN32
					N_("See the strftime MSDN article for details."),0,sizeof prefs.hex_stamp_text_format},
#else
					N_("See the strftime manpage for details."),0,sizeof prefs.hex_stamp_text_format},
#endif

	{ST_HEADER,	N_("Title Bar"),0,0,0},
	{ST_TOGGLE, N_("Show channel modes"), P_OFFINTNL(hex_gui_win_modes),0,0,0},
	{ST_TOGGLR, N_("Show number of users"), P_OFFINTNL(hex_gui_win_ucount),0,0,0},
	{ST_TOGGLE, N_("Show nickname"), P_OFFINTNL(hex_gui_win_nick),0,0,0},

	{ST_END, 0, 0, 0, 0, 0}
};

static const char *const tabcompmenu[] = 
{
	N_("A-Z"),
	N_("Last-spoke order"),
	NULL
};

static const char *const cursormenu[] =
{
	N_("Line"),
	N_("Thick bar"),
	N_("Block"),
	N_("Underline"),
	N_("Hollow box"),
	NULL
};

static const setting inputbox_settings[] =
{
	{ST_HEADER, N_("Input Box"),0,0,0},
	{ST_TOGGLE, N_("Use the text box font and colors"), P_OFFINTNL(hex_gui_input_style),0,0,0},
	{ST_TOGGLE, N_("Render colors and attributes"), P_OFFINTNL (hex_gui_input_attr),0,0,0},
	{ST_MENU,   N_("Cursor:"), P_OFFINTNL(hex_gui_input_cursor), 0, cursormenu, 0},
	{ST_TOGGLE, N_("Blinking cursor"), P_OFFINTNL(hex_gui_input_blink), N_("Off keeps the cursor on all the time."),0,0},
	{ST_TOGGLE, N_("Show nick box"), P_OFFINTNL(hex_gui_input_nick),0,0,1},
	{ST_TOGGLE, N_("Show user mode icon in nick box"), P_OFFINTNL(hex_gui_input_icon),0,0,0},
	{ST_NUMBER, N_("Ask before sending a paste of more than:"), P_OFFINTNL(hex_gui_input_paste_max), N_("Pasting many lines by accident floods the channel. 0 never asks."), (const char **)N_("lines."), 1000},
	{ST_TOGGLE, N_("Show commands and their usage while typing /"), P_OFFINTNL(hex_gui_input_cmdhelp), N_("A list above the input box: Up/Down pick, Tab or Enter fill in, Escape closes."),0,0},
	{ST_TOGGLE, N_("Show who is typing"), P_OFFINTNL(hex_gui_typing_show), N_("\"alice is typing...\" under the input box, on servers that pass it on."),0,0},
	{ST_TOGGLE, N_("Also say in the chat when someone starts and stops typing"), P_OFFINTNL(hex_gui_typing_chat), N_("\"alice started typing\" and \"alice stopped typing\" lines in the window, and in its log."),0,0},
	{ST_TOGGLE, N_("Let others see when I'm typing"), P_OFFINTNL(hex_gui_typing_send), N_("Tells the channel or person you're typing to, on servers that pass it on."),0,0},
	{ST_TOGGLE, N_("Show what replies answer"), P_OFFINTNL(hex_gui_replies), N_("A line like \"replying to alice: ...\" above a reply, on servers that pass replies on. Send one with /reply."),0,0},
	{ST_TOGGLE, N_("Messages of several lines (Shift+Enter)"), P_OFFINTNL(hex_gui_multiline), N_("Shift+Enter starts a new line. On servers that allow it the lines go as one message; elsewhere each is sent on its own. Takes effect on the next connect."),0,0},
	{ST_TOGGLE, N_("Spell checking"), P_OFFINTNL(hex_gui_input_spell),0,0,1},
	{ST_ENTRY,	N_("Dictionaries to use:"), P_OFFSETNL(hex_text_spell_langs),0,0,sizeof prefs.hex_text_spell_langs},
#ifdef WIN32
	{ST_LABEL,	N_("Use language codes (as in \"%LOCALAPPDATA%\\enchant\\myspell\\dicts\").\nSeparate multiple entries with commas.")},
#else
	{ST_LABEL,	N_("Use language codes. Separate multiple entries with commas.")},
#endif

	{ST_HEADER, N_("Nick Completion"),0,0,0},
	{ST_ENTRY,	N_("Nick completion suffix:"), P_OFFSETNL(hex_completion_suffix),0,0,sizeof prefs.hex_completion_suffix},
	{ST_MENU,	N_("Nick completion sorted:"), P_OFFINTNL(hex_completion_sort), 0, tabcompmenu, 0},
	{ST_NUMBER,	N_("Nick completion amount:"), P_OFFINTNL(hex_completion_amount), N_("Threshold of nicks to start listing instead of completing"), (const char **)N_("nicks."), 1000},

	{ST_END, 0, 0, 0, 0, 0}
};

static const char *const lagmenutext[] = 
{
	N_("Off"),
	N_("Graphical"),
	N_("Text"),
	N_("Both"),
	NULL
};

static const char *const ulmenutext[] = 
{
	N_("A-Z, ops first"),
	N_("A-Z"),
	N_("Z-A, ops last"),
	N_("Z-A"),
	N_("Unsorted"),
	NULL
};

static const char *const cspos[] =
{
	N_("Left (upper)"),
	N_("Left (lower)"),
	N_("Right (upper)"),
	N_("Right (lower)"),
	N_("Top"),
	N_("Bottom"),
	N_("Hidden"),
	NULL
};

static const char *const ulpos[] =
{
	N_("Left (upper)"),
	N_("Left (lower)"),
	N_("Right (upper)"),
	N_("Right (lower)"),
	NULL
};

static const setting userlist_settings[] =
{
	{ST_HEADER,	N_("User List"),0,0,0},
	{ST_TOGGLE, N_("Show hostnames in user list"), P_OFFINTNL(hex_gui_ulist_show_hosts), 0, 0, 0},
	{ST_TOGGLE, N_("Use the Text box font and colors"), P_OFFINTNL(hex_gui_ulist_style),0,0,0},
	{ST_TOGGLE, N_("Show icons for user modes"), P_OFFINTNL(hex_gui_ulist_icons), N_("Use graphical icons instead of text symbols in the user list."), 0, 0},
	{ST_TOGGLE, N_("Color nicknames in userlist"), P_OFFINTNL(hex_gui_ulist_color), N_("Will color nicknames the same as in chat."), 0, 0},
	{ST_TOGGLE, N_("Show user count in channels"), P_OFFINTNL(hex_gui_ulist_count), 0, 0, 0},
	{ST_MENU,	N_("User list sorted by:"), P_OFFINTNL(hex_gui_ulist_sort), 0, ulmenutext, 0},
	{ST_MENU,	N_("Show user list at:"), P_OFFINTNL(hex_gui_ulist_pos), 0, ulpos, 1},

	{ST_HEADER,	N_("Away Tracking"),0,0,0},
	{ST_TOGGLE,	N_("Track the away status of users and mark them in a different color"), P_OFFINTNL(hex_away_track),0,0,1},
	{ST_NUMBER, N_("On channels smaller than:"), P_OFFINTNL(hex_away_size_max),0,0,10000},

	{ST_HEADER,	N_("Action Upon Double Click"),0,0,0},
	{ST_ENTRY,	N_("Execute command:"), P_OFFSETNL(hex_gui_ulist_doubleclick), 0, 0, sizeof prefs.hex_gui_ulist_doubleclick},

	{ST_HEADER,	N_("Extra Gadgets"),0,0,0},
	{ST_MENU,	N_("Lag meter:"), P_OFFINTNL(hex_gui_lagometer), 0, lagmenutext, 0},
	{ST_MENU,	N_("Throttle meter:"), P_OFFINTNL(hex_gui_throttlemeter), 0, lagmenutext, 0},

	{ST_END, 0, 0, 0, 0, 0}
};

static const char *const tabwin[] =
{
	N_("Windows"),
	N_("Tabs"),
	NULL
};

static const char *const focusnewtabsmenu[] =
{
	N_("Never"),
	N_("Always"),
	N_("Only requested tabs"),
	NULL
};

static const char *const noticeposmenu[] =
{
	N_("Automatic"),
	N_("In an extra tab"),
	N_("In the front tab"),
	NULL
};

static const char *const swtype[] =
{
	N_("Tabs"),	/* 0 tabs */
	"",			/* 1 reserved */
	N_("Tree"),	/* 2 tree */
	NULL
};

static const setting tabs_settings[] =
{
	/*{ST_HEADER,	N_("Channel Switcher"),0,0,0},*/
	{ST_RADIO,  N_("Switcher type:"),P_OFFINTNL(hex_gui_tab_layout), 0, swtype, 0},
	{ST_TOGGLE, N_("Open an extra tab for server messages"), P_OFFINTNL(hex_gui_tab_server), 0, 0, 0},
	{ST_TOGGLE, N_("Open a new tab when you receive a private message"), P_OFFINTNL(hex_gui_autoopen_dialog), 0, 0, 0},
	{ST_TOGGLE, N_("Sort tabs in alphabetical order"), P_OFFINTNL(hex_gui_tab_sort), 0, 0, 0},
	{ST_TOGGLE, N_("Show icons in the channel tree"), P_OFFINTNL(hex_gui_tab_icons), 0, 0, 0},
	{ST_TOGGLE, N_("Show dotted lines in the channel tree"), P_OFFINTNL(hex_gui_tab_dots), 0, 0, 0},
	{ST_TOGGLE, N_("Scroll mouse-wheel to change tabs"), P_OFFINTNL (hex_gui_tab_scrollchans), 0, 0, 0},
	{ST_TOGGLE, N_("Middle click to close tab"), P_OFFINTNL(hex_gui_tab_middleclose), 0, 0, 0},
	{ST_TOGGLE, N_("Smaller text"), P_OFFINTNL(hex_gui_tab_small), 0, 0, 0},
	{ST_MENU,	N_("Focus new tabs:"), P_OFFINTNL(hex_gui_tab_newtofront), 0, focusnewtabsmenu, 0},
	{ST_MENU,	N_("Placement of notices:"), P_OFFINTNL(hex_irc_notice_pos), 0, noticeposmenu, 0},
	{ST_MENU,	N_("Show channel switcher at:"), P_OFFINTNL(hex_gui_tab_pos), 0, cspos, 1},
	{ST_NUMBER,	N_("Shorten tab labels to:"), P_OFFINTNL(hex_gui_tab_trunc), 0, (const char **)N_("letters."), 99},
	{ST_TOGGLE,	N_("Show the number of unread messages on tabs"), P_OFFINTNL(hex_gui_tab_unread), N_("\"#nexchat (3)\": three new messages; \"(3, 1!)\": one of them mentions you."), 0, 0},

	{ST_HEADER,	N_("Tabs or Windows"),0,0,0},
	{ST_MENU,	N_("Open channels in:"), P_OFFINTNL(hex_gui_tab_chans), 0, tabwin, 0},
	{ST_MENU,	N_("Open dialogs in:"), P_OFFINTNL(hex_gui_tab_dialogs), 0, tabwin, 0},
	{ST_MENU,	N_("Open utilities in:"), P_OFFINTNL(hex_gui_tab_utils), N_("Open DCC, Ignore, Notify etc, in tabs or windows?"), tabwin, 0},

	{ST_END, 0, 0, 0, 0, 0}
};

static const setting color_settings[] =
{
	{ST_TOGGLE, N_("Messages"), P_OFFINTNL(hex_text_stripcolor_msg), 0, 0, 0},
	{ST_TOGGLE, N_("Scrollback"), P_OFFINTNL(hex_text_stripcolor_replay), 0, 0, 0},
	{ST_TOGGLE, N_("Topic"), P_OFFINTNL(hex_text_stripcolor_topic), 0, 0, 0},

	{ST_END, 0, 0, 0, 0, 0}
};

static const char *const dccaccept[] =
{
	N_("Ask for confirmation"),
	N_("Ask for download folder"),
	N_("Save without interaction"),
	NULL
};

static const setting filexfer_settings[] =
{
	{ST_HEADER, N_("Files and Directories"), 0, 0, 0},
	{ST_MENU,	N_("Auto accept file offers:"), P_OFFINTNL(hex_dcc_auto_recv), 0, dccaccept, 0},
	{ST_EFOLDER,N_("Download files to:"), P_OFFSETNL(hex_dcc_dir), 0, 0, sizeof prefs.hex_dcc_dir},
	{ST_EFOLDER,N_("Move completed files to:"), P_OFFSETNL(hex_dcc_completed_dir), 0, 0, sizeof prefs.hex_dcc_completed_dir},
	{ST_TOGGLE, N_("Save nick name in filenames"), P_OFFINTNL(hex_dcc_save_nick), 0, 0, 0},

	{ST_HEADER,	N_("Auto Open DCC Windows"),0,0,0},
	{ST_TOGGLE, N_("Send window"), P_OFFINTNL(hex_gui_autoopen_send), 0, 0, 0},
	{ST_TOGGLE, N_("Receive window"), P_OFFINTNL(hex_gui_autoopen_recv), 0, 0, 0},
	{ST_TOGGLE, N_("Chat window"), P_OFFINTNL(hex_gui_autoopen_chat), 0, 0, 0},

	{ST_HEADER, N_("Maximum File Transfer Speeds (Byte per Second)"), 0, 0, 0},
	{ST_NUMBER,	N_("One upload:"), P_OFFINTNL(hex_dcc_max_send_cps), 
					N_("Maximum speed for one transfer"), 0, 10000000},
	{ST_NUMBER,	N_("One download:"), P_OFFINTNL(hex_dcc_max_get_cps),
					N_("Maximum speed for one transfer"), 0, 10000000},
	{ST_NUMBER,	N_("All uploads combined:"), P_OFFINTNL(hex_dcc_global_max_send_cps),
					N_("Maximum speed for all files"), 0, 10000000},
	{ST_NUMBER,	N_("All downloads combined:"), P_OFFINTNL(hex_dcc_global_max_get_cps),
					N_("Maximum speed for all files"), 0, 10000000},

	{ST_END, 0, 0, 0, 0, 0}
};

static const int balloonlist[3] =
{
	P_OFFINTNL(hex_input_balloon_chans), P_OFFINTNL(hex_input_balloon_priv), P_OFFINTNL(hex_input_balloon_hilight)
};

static const int trayblinklist[3] =
{
	P_OFFINTNL(hex_input_tray_chans), P_OFFINTNL(hex_input_tray_priv), P_OFFINTNL(hex_input_tray_hilight)
};

static const int taskbarlist[3] =
{
	P_OFFINTNL(hex_input_flash_chans), P_OFFINTNL(hex_input_flash_priv), P_OFFINTNL(hex_input_flash_hilight)
};

static const int beeplist[3] =
{
	P_OFFINTNL(hex_input_beep_chans), P_OFFINTNL(hex_input_beep_priv), P_OFFINTNL(hex_input_beep_hilight)
};

static const setting alert_settings[] =
{
	{ST_HEADER,	N_("Alerts"),0,0,0},

	{ST_ALERTHEAD},


	{ST_3OGGLE, N_("Show notifications on:"), 0, 0, (void *)balloonlist, 0},
	{ST_3OGGLE, N_("Blink tray icon on:"), 0, 0, (void *)trayblinklist, 0},
	{ST_3OGGLE, N_("Blink task bar on:"), 0, 0, (void *)taskbarlist, 0},
#ifdef WIN32
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, N_("Play the \"Instant Message Notification\" system sound upon the selected events"), (void *)beeplist, 0},
#else
#ifdef USE_LIBCANBERRA
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, N_("Play \"message-new-instant\" from the freedesktop.org sound theme upon the selected events"), (void *)beeplist, 0},
#else
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, N_("Play a GTK beep upon the selected events"), (void *)beeplist, 0},
#endif
#endif

	{ST_TOGGLE,	N_("Omit alerts when marked as being away"), P_OFFINTNL(hex_away_omit_alerts), 0, 0, 0},
	{ST_TOGGLE,	N_("Omit alerts while the window is focused"), P_OFFINTNL(hex_gui_focus_omitalerts), 0, 0, 0},

	{ST_HEADER,	N_("Tray Behavior"), 0, 0, 0},
	{ST_TOGGLE,	N_("Enable system tray icon"), P_OFFINTNL(hex_gui_tray), 0, 0, 4},
	{ST_TOGGLE,	N_("Minimize to tray"), P_OFFINTNL(hex_gui_tray_minimize), 0, 0, 0},
	{ST_TOGGLE,	N_("Close to tray"), P_OFFINTNL(hex_gui_tray_close), 0, 0, 0},
	{ST_TOGGLE,	N_("Automatically mark away/back"), P_OFFINTNL(hex_gui_tray_away), N_("Automatically change status when hiding to tray."), 0, 0},
	{ST_TOGGLE,	N_("Only show notifications when hidden or iconified"), P_OFFINTNL(hex_gui_tray_quiet), 0, 0, 0},

	{ST_HEADER,	N_("Highlighted Messages"),0,0,0},
	{ST_LABEL,	N_("Highlighted messages are ones where your nickname is mentioned, but also:"), 0, 0, 0, 1},

	{ST_ENTRY,	N_("Extra words to highlight:"), P_OFFSETNL(hex_irc_extra_hilight), 0, 0, sizeof prefs.hex_irc_extra_hilight},
	{ST_ENTRY,	N_("Nick names not to highlight:"), P_OFFSETNL(hex_irc_no_hilight), 0, 0, sizeof prefs.hex_irc_no_hilight},
	{ST_ENTRY,	N_("Nick names to always highlight:"), P_OFFSETNL(hex_irc_nick_hilight), 0, 0, sizeof prefs.hex_irc_nick_hilight},
	{ST_LABEL,	N_("Separate multiple words with commas.\nWildcards are accepted.")},

	{ST_END, 0, 0, 0, 0, 0}
};

static const setting alert_settings_nonotifications[] =
{
	{ST_HEADER,	N_("Alerts"),0,0,0},

	{ST_ALERTHEAD},
	{ST_3OGGLE, N_("Blink tray icon on:"), 0, 0, (void *)trayblinklist, 0},
#ifndef __APPLE__
	{ST_3OGGLE, N_("Blink task bar on:"), 0, 0, (void *)taskbarlist, 0},
#endif
#ifdef WIN32
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, N_("Play the \"Instant Message Notification\" system sound upon the selected events"), (void *)beeplist, 0},
#else
#ifdef USE_LIBCANBERRA
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, N_("Play \"message-new-instant\" from the freedesktop.org sound theme upon the selected events"), (void *)beeplist, 0},
#else
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, N_("Play a GTK beep upon the selected events"), (void *)beeplist, 0},
#endif
#endif

	{ST_TOGGLE,	N_("Omit alerts when marked as being away"), P_OFFINTNL(hex_away_omit_alerts), 0, 0, 0},
	{ST_TOGGLE,	N_("Omit alerts while the window is focused"), P_OFFINTNL(hex_gui_focus_omitalerts), 0, 0, 0},

	{ST_HEADER,	N_("Tray Behavior"), 0, 0, 0},
	{ST_TOGGLE,	N_("Enable system tray icon"), P_OFFINTNL(hex_gui_tray), 0, 0, 4},
	{ST_TOGGLE,	N_("Minimize to tray"), P_OFFINTNL(hex_gui_tray_minimize), 0, 0, 0},
	{ST_TOGGLE,	N_("Close to tray"), P_OFFINTNL(hex_gui_tray_close), 0, 0, 0},
	{ST_TOGGLE,	N_("Automatically mark away/back"), P_OFFINTNL(hex_gui_tray_away), N_("Automatically change status when hiding to tray."), 0, 0},

	{ST_HEADER,	N_("Highlighted Messages"),0,0,0},
	{ST_LABEL,	N_("Highlighted messages are ones where your nickname is mentioned, but also:"), 0, 0, 0, 1},

	{ST_ENTRY,	N_("Extra words to highlight:"), P_OFFSETNL(hex_irc_extra_hilight), 0, 0, sizeof prefs.hex_irc_extra_hilight},
	{ST_ENTRY,	N_("Nick names not to highlight:"), P_OFFSETNL(hex_irc_no_hilight), 0, 0, sizeof prefs.hex_irc_no_hilight},
	{ST_ENTRY,	N_("Nick names to always highlight:"), P_OFFSETNL(hex_irc_nick_hilight), 0, 0, sizeof prefs.hex_irc_nick_hilight},
	{ST_LABEL,	N_("Separate multiple words with commas.\nWildcards are accepted.")},

	{ST_END, 0, 0, 0, 0, 0}
};

static const setting alert_settings_unity[] =
{
	{ST_HEADER,	N_("Alerts"),0,0,0},

	{ST_ALERTHEAD},
	{ST_3OGGLE, N_("Show notifications on:"), 0, 0, (void *)balloonlist, 0},
	{ST_3OGGLE, N_("Blink task bar on:"), 0, 0, (void *)taskbarlist, 0},
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, 0, (void *)beeplist, 0},

	{ST_TOGGLE,	N_("Omit alerts when marked as being away"), P_OFFINTNL(hex_away_omit_alerts), 0, 0, 0},
	{ST_TOGGLE,	N_("Omit alerts while the window is focused"), P_OFFINTNL(hex_gui_focus_omitalerts), 0, 0, 0},

	{ST_HEADER,	N_("Highlighted Messages"),0,0,0},
	{ST_LABEL,	N_("Highlighted messages are ones where your nickname is mentioned, but also:"), 0, 0, 0, 1},

	{ST_ENTRY,	N_("Extra words to highlight:"), P_OFFSETNL(hex_irc_extra_hilight), 0, 0, sizeof prefs.hex_irc_extra_hilight},
	{ST_ENTRY,	N_("Nick names not to highlight:"), P_OFFSETNL(hex_irc_no_hilight), 0, 0, sizeof prefs.hex_irc_no_hilight},
	{ST_ENTRY,	N_("Nick names to always highlight:"), P_OFFSETNL(hex_irc_nick_hilight), 0, 0, sizeof prefs.hex_irc_nick_hilight},
	{ST_LABEL,	N_("Separate multiple words with commas.\nWildcards are accepted.")},

	{ST_END, 0, 0, 0, 0, 0}
};

static const setting alert_settings_unityandnonotifications[] =
{
	{ST_HEADER, N_("Alerts"), 0, 0, 0},

	{ST_ALERTHEAD},
	{ST_3OGGLE, N_("Blink task bar on:"), 0, 0, (void *)taskbarlist, 0},
	{ST_3OGGLE, N_("Make a beep sound on:"), 0, 0, (void *)beeplist, 0},

	{ST_TOGGLE, N_("Omit alerts when marked as being away"), P_OFFINTNL (hex_away_omit_alerts), 0, 0, 0},
	{ST_TOGGLE, N_("Omit alerts while the window is focused"), P_OFFINTNL (hex_gui_focus_omitalerts), 0, 0, 0},

	{ST_HEADER, N_("Highlighted Messages"), 0, 0, 0},
	{ST_LABEL, N_("Highlighted messages are ones where your nickname is mentioned, but also:"), 0, 0, 0, 1},

	{ST_ENTRY, N_("Extra words to highlight:"), P_OFFSETNL (hex_irc_extra_hilight), 0, 0, sizeof prefs.hex_irc_extra_hilight},
	{ST_ENTRY, N_("Nick names not to highlight:"), P_OFFSETNL (hex_irc_no_hilight), 0, 0, sizeof prefs.hex_irc_no_hilight},
	{ST_ENTRY, N_("Nick names to always highlight:"), P_OFFSETNL (hex_irc_nick_hilight), 0, 0, sizeof prefs.hex_irc_nick_hilight},
	{ST_LABEL, N_("Separate multiple words with commas.\nWildcards are accepted.")},

	{ST_END, 0, 0, 0, 0, 0}
};

static const setting general_settings[] =
{
	{ST_HEADER,	N_("Default Messages"),0,0,0},
	{ST_ENTRY,	N_("Quit:"), P_OFFSETNL(hex_irc_quit_reason), 0, 0, sizeof prefs.hex_irc_quit_reason},
	{ST_ENTRY,	N_("Leave channel:"), P_OFFSETNL(hex_irc_part_reason), 0, 0, sizeof prefs.hex_irc_part_reason},
	{ST_ENTRY,	N_("Away:"), P_OFFSETNL(hex_away_reason), 0, 0, sizeof prefs.hex_away_reason},

	{ST_HEADER,	N_("Away"),0,0,0},
	{ST_TOGGLE,	N_("Show away once"), P_OFFINTNL(hex_away_show_once), N_("Show identical away messages only once."), 0, 0},
	{ST_TOGGLE,	N_("Automatically unmark away"), P_OFFINTNL(hex_away_auto_unmark), N_("Unmark yourself as away before sending messages."), 0, 0},

	{ST_HEADER,	N_("Miscellaneous"),0,0,0},
	{ST_TOGGLE,	N_("Display MODEs in raw form"), P_OFFINTNL(hex_irc_raw_modes), 0, 0, 0},
	{ST_TOGGLE,	N_("WHOIS on notify"), P_OFFINTNL(hex_notify_whois_online), N_("Sends a /WHOIS when a user comes online in your notify list."), 0, 0},
	{ST_TOGGLE,	N_("Hide join and part messages"), P_OFFINTNL(hex_irc_conf_mode), N_("Hide channel join/part messages by default."), 0, 0},
	{ST_NUMBER,	N_("Only show joins and parts of people who spoke in the last:"), P_OFFINTNL(hex_irc_smart_filter), N_("Hide joins, parts and quits of everyone else. 0 shows them all."), (const char **)N_("minutes."), 1440},
	{ST_TOGGLE,	N_("Hide nick change messages"), P_OFFINTNL(hex_irc_hide_nickchange), 0, 0, 0},

	{ST_END, 0, 0, 0, 0, 0}
};

static const char *const bantypemenu[] = 
{
	N_("*!*@*.host"),
	N_("*!*@domain"),
	N_("*!*user@*.host"),
	N_("*!*user@domain"),
	NULL
};

static const setting advanced_settings[] =
{
	{ST_HEADER,	N_("Auto Copy Behavior"),0,0,0},
	{ST_TOGGLE, N_("Automatically copy selected text"), P_OFFINTNL(hex_text_autocopy_text),
					N_("Copy selected text to clipboard when left mouse button is released. "
						"Otherwise, Ctrl+Shift+C will copy the "
						"selected text to the clipboard."), 0, 0},
	{ST_TOGGLE, N_("Automatically include timestamps"), P_OFFINTNL(hex_text_autocopy_stamp),
					N_("Automatically include timestamps in copied lines of text. Otherwise, "
						"include timestamps if the Shift key is held down while selecting."), 0, 0},
	{ST_TOGGLE, N_("Automatically include color information"), P_OFFINTNL(hex_text_autocopy_color),
					N_("Automatically include color information in copied lines of text.  "
						"Otherwise, include color information if the Ctrl key is held down "
						"while selecting."), 0, 0},

	{ST_HEADER,	N_("Miscellaneous"), 0, 0, 0},
	{ST_ENTRY,  N_("Real name:"), P_OFFSETNL(hex_irc_real_name), 0, 0, sizeof prefs.hex_irc_real_name},
#ifdef WIN32
	{ST_ENTRY,  N_("Alternative fonts:"), P_OFFSETNL(hex_text_font_alternative), N_("Separate multiple entries with commas without spaces before or after."), 0, sizeof prefs.hex_text_font_alternative},
#endif
	{ST_TOGGLE,	N_("Display lists in compact mode"), P_OFFINTNL(hex_gui_compact), N_("Use less spacing between user list/channel tree rows."), 0, 0},
	{ST_TOGGLE,	N_("Use server time if supported"), P_OFFINTNL(hex_irc_cap_server_time), N_("Display timestamps obtained from server if it supports the time-server extension."), 0, 0},
	{ST_TOGGLE,	N_("Get missed messages from the server on join"), P_OFFINTNL(hex_irc_chathistory), N_("On servers and bouncers with chat history (soju, Ergo), show what was said while you were away when you join."), 0, 0},
	{ST_TOGGLE,	N_("Automatically reconnect to servers on disconnect"), P_OFFINTNL(hex_net_auto_reconnect), 0, 0, 1},
	{ST_NUMBER,	N_("Auto reconnect delay:"), P_OFFINTNL(hex_net_reconnect_delay), 0, 0, 9999},
	{ST_NUMBER,	N_("Auto join delay:"), P_OFFINTNL(hex_irc_join_delay), 0, 0, 9999},
	{ST_MENU,	N_("Ban Type:"), P_OFFINTNL(hex_irc_ban_type), N_("Attempt to use this banmask when banning or quieting. (requires irc_who_join)"), bantypemenu, 0},

	{ST_END, 0, 0, 0, 0, 0}
};

static const setting logging_settings[] =
{
	{ST_HEADER,	N_("Logging"),0,0,0},
	{ST_TOGGLE,	N_("Display scrollback from previous session"), P_OFFINTNL(hex_text_replay), 0, 0, 0},
	{ST_NUMBER,	N_("Scrollback lines:"), P_OFFINTNL(hex_text_max_lines),0,0,100000},
	{ST_TOGGLE,	N_("Enable logging of conversations to disk"), P_OFFINTNL(hex_irc_logging), 0, 0, 0},
	{ST_ENTRY,	N_("Log filename:"), P_OFFSETNL(hex_irc_logmask), 0, 0, sizeof prefs.hex_irc_logmask},
	{ST_LABEL,	N_("%s=Server %c=Channel %n=Network.")},

	{ST_HEADER,	N_("Timestamps"),0,0,0},
	{ST_TOGGLE,	N_("Insert timestamps in logs"), P_OFFINTNL(hex_stamp_log), 0, 0, 1},
	{ST_ENTRY,	N_("Log timestamp format:"), P_OFFSETNL(hex_stamp_log_format), 0, 0, sizeof prefs.hex_stamp_log_format},
#ifdef WIN32
	{ST_LABEL,	N_("See the strftime MSDN article for details.")},
#else
	{ST_LABEL,	N_("See the strftime manpage for details.")},
#endif

	{ST_HEADER,	N_("URLs"),0,0,0},
	{ST_TOGGLE,	N_("Enable logging of URLs to disk"), P_OFFINTNL(hex_url_logging), 0, 0, 0},
	{ST_TOGGLE,	N_("Enable URL grabber"), P_OFFINTNL(hex_url_grabber), 0, 0, 1},
	{ST_NUMBER,	N_("Maximum number of URLs to grab:"), P_OFFINTNL(hex_url_grabber_limit), 0, 0, 9999},

	{ST_END, 0, 0, 0, 0, 0}
};

static const char *const proxytypes[] =
{
	N_("(Disabled)"),
	N_("Wingate"),
	N_("SOCKS4"),
	N_("SOCKS5"),
	N_("HTTP"),
	N_("Auto"),
	NULL
};

static const char *const proxyuse[] =
{
	N_("All connections"),
	N_("IRC server only"),
	N_("DCC only"),
	NULL
};

static const setting network_settings[] =
{
	{ST_HEADER,	N_("Your Address"), 0, 0, 0, 0},
	{ST_ENTRY,	N_("Bind to:"), P_OFFSETNL(hex_net_bind_host), 0, 0, sizeof prefs.hex_net_bind_host},
	{ST_LABEL,	N_("Only useful for computers with multiple addresses.")},

	{ST_HEADER, N_("File Transfers"), 0, 0, 0},
	{ST_TOGGLE, N_("Get my address from the IRC server"), P_OFFINTNL(hex_dcc_ip_from_server),
					N_("Asks the IRC server for your real address. Use this if you have a 192.168.*.* address!"), 0, 0},
	{ST_ENTRY,	N_("DCC IP address:"), P_OFFSETNL(hex_dcc_ip),
					N_("Claim you are at this address when offering files."), 0, sizeof prefs.hex_dcc_ip},
	{ST_NUMBER,	N_("First DCC listen port:"), P_OFFINTNL(hex_dcc_port_first), 0, 0, 65535},
	{ST_NUMBER,	N_("Last DCC listen port:"), P_OFFINTNL(hex_dcc_port_last), 0, 
		(const char **)N_("!Leave ports at zero for full range."), 65535},

	{ST_HEADER,	N_("Proxy Server"), 0, 0, 0, 0},
	{ST_ENTRY,	N_("Hostname:"), P_OFFSETNL(hex_net_proxy_host), 0, 0, sizeof prefs.hex_net_proxy_host},
	{ST_NUMBER,	N_("Port:"), P_OFFINTNL(hex_net_proxy_port), 0, 0, 65535},
	{ST_MENU,	N_("Type:"), P_OFFINTNL(hex_net_proxy_type), 0, proxytypes, 0},
	{ST_MENU,	N_("Use proxy for:"), P_OFFINTNL(hex_net_proxy_use), 0, proxyuse, 0},

	{ST_HEADER,	N_("Proxy Authentication"), 0, 0, 0, 0},
	{ST_TOGGLE,	N_("Use authentication (HTTP or SOCKS5 only)"), P_OFFINTNL(hex_net_proxy_auth), 0, 0, 0},
	{ST_ENTRY,	N_("Username:"), P_OFFSETNL(hex_net_proxy_user), 0, 0, sizeof prefs.hex_net_proxy_user},
	{ST_ENTRY,	N_("Password:"), P_OFFSETNL(hex_net_proxy_pass), 0, GINT_TO_POINTER(1), sizeof prefs.hex_net_proxy_pass},

	{ST_END, 0, 0, 0, 0, 0}
};

static const setting identd_settings[] =
{
	{ST_HEADER, N_("Identd Server"), 0, 0, 0, 0},
	{ST_TOGGLE, N_("Enabled"), P_OFFINTNL(hex_identd_server), N_("Server will respond with the networks username"), 0, 1},
	{ST_NUMBER,	N_("Port:"), P_OFFINTNL(hex_identd_port), N_("You must have permissions to listen on this port. "
												   "If not 113 (0 defaults to this) then you must configure port-forwarding."), 0, 65535},

	{ST_END, 0, 0, 0, 0, 0}
};

#define setup_get_str(pr,set) (((char *)pr)+set->offset)
#define setup_get_int(pr,set) *(((int *)pr)+set->offset)
#define setup_get_int3(pr,off) *(((int *)pr)+off) 

#define setup_set_int(pr,set,num) *((int *)pr+set->offset)=num
#define setup_set_str(pr,set,str) strcpy(((char *)pr)+set->offset,str)


static void
setup_3oggle_cb (GtkWidget *but, unsigned int *setting)
{
	*setting = nx_toggle_get_active (but);
	setup_live ();
}

static void
setup_headlabel (GtkWidget *tab, int row, int col, char *text)
{
	GtkWidget *label;
	char buf[128];
	char *sp;

	g_snprintf (buf, sizeof (buf), "<b><span size=\"smaller\">%s</span></b>", text);
	sp = strchr (buf + 17, ' ');
	if (sp)
		*sp = '\n';

	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), buf);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (tab, label, col, col + 1, row, row + 1, 0, 0, 4, 0);
}

static void
setup_create_alert_header (GtkWidget *tab, int row, const setting *set)
{
	setup_headlabel (tab, row, 3, _("Channel Message"));
	setup_headlabel (tab, row, 4, _("Private Message"));
	setup_headlabel (tab, row, 5, _("Highlighted Message"));
}

/* a check box with no label (the alerts grid) */
static GtkWidget *
setup_lone_check (void)
{
	GtkWidget *wid = gtk_check_button_new ();
	gtk_widget_add_css_class (wid, "nx-lone");
	return wid;
}

/* makes 3 toggles side-by-side */

static void
setup_create_3oggle (GtkWidget *tab, int row, const setting *set)
{
	GtkWidget *label, *wid;
	int *offsets = (int *)set->list;

	label = gtk_label_new (_(set->label));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	if (set->tooltip)
	{
		gtk_widget_set_tooltip_text (label, _(set->tooltip));
	}
	gtkutil_grid_attach (tab, label, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	wid = setup_lone_check ();
	nx_toggle_set_active ((GtkWidget *) (wid),
											setup_get_int3 (&setup_prefs, offsets[0]));
	g_signal_connect (G_OBJECT (wid), "toggled",
							G_CALLBACK (setup_3oggle_cb), ((int *)&setup_prefs) + offsets[0]);
	gtkutil_grid_attach (tab, wid, 3, 4, row, row + 1, 0, 0, 0, 0);

	wid = setup_lone_check ();
	nx_toggle_set_active ((GtkWidget *) (wid),
											setup_get_int3 (&setup_prefs, offsets[1]));
	g_signal_connect (G_OBJECT (wid), "toggled",
							G_CALLBACK (setup_3oggle_cb), ((int *)&setup_prefs) + offsets[1]);
	gtkutil_grid_attach (tab, wid, 4, 5, row, row + 1, 0, 0, 0, 0);

	wid = setup_lone_check ();
	nx_toggle_set_active ((GtkWidget *) (wid),
											setup_get_int3 (&setup_prefs, offsets[2]));
	g_signal_connect (G_OBJECT (wid), "toggled",
							G_CALLBACK (setup_3oggle_cb), ((int *)&setup_prefs) + offsets[2]);
	gtkutil_grid_attach (tab, wid, 5, 6, row, row + 1, 0, 0, 0, 0);
}

static void
setup_toggle_cb (GtkWidget *but, const setting *set)
{
	GtkWidget *label, *disable_wid;

	setup_set_int (&setup_prefs, set, nx_toggle_get_active (but));
	setup_live ();

	/* does this toggle also enable/disable another widget? */
	disable_wid = g_object_get_data (G_OBJECT (but), "nxt");
	if (disable_wid)
	{
		gtk_widget_set_sensitive (disable_wid, nx_toggle_get_active (but));
		label = g_object_get_data (G_OBJECT (disable_wid), "lbl");
		gtk_widget_set_sensitive (label, nx_toggle_get_active (but));
	}
}

static void
setup_toggle_sensitive_cb (GtkWidget *but, GtkWidget *wid)
{
	gtk_widget_set_sensitive (wid, nx_toggle_get_active (but));
}

static void
setup_create_toggleR (GtkWidget *tab, int row, const setting *set)
{
	GtkWidget *wid;

	wid = gtk_check_button_new_with_label (_(set->label));
	nx_toggle_set_active ((GtkWidget *) (wid),
											setup_get_int (&setup_prefs, set));
	g_signal_connect (G_OBJECT (wid), "toggled",
							G_CALLBACK (setup_toggle_cb), (gpointer)set);
	if (set->tooltip)
		gtk_widget_set_tooltip_text (wid, _(set->tooltip));
	gtkutil_grid_attach (tab, wid, 4, 5, row, row + 1,
							GU_EXPAND | GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
}

static GtkWidget *
setup_create_toggleL (GtkWidget *tab, int row, const setting *set)
{
	GtkWidget *wid;

	wid = gtk_check_button_new_with_label (_(set->label));
	nx_toggle_set_active ((GtkWidget *) (wid),
											setup_get_int (&setup_prefs, set));
	g_signal_connect (G_OBJECT (wid), "toggled",
							G_CALLBACK (setup_toggle_cb), (gpointer)set);
	if (set->tooltip)
		gtk_widget_set_tooltip_text (wid, _(set->tooltip));
	gtkutil_grid_attach (tab, wid, 2, row==6 ? 6 : 4, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	return wid;
}

static GtkWidget *
setup_create_italic_label (char *text)
{
	GtkWidget *label;
	char buf[256];

	label = gtk_label_new (NULL);
	g_snprintf (buf, sizeof (buf), "<i><span size=\"smaller\">%s</span></i>", text);
	gtk_label_set_markup (GTK_LABEL (label), buf);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);

	return label;
}

static void
setup_spin_cb (GtkSpinButton *spin, const setting *set)
{
	setup_set_int (&setup_prefs, set, gtk_spin_button_get_value_as_int (spin));
	setup_live ();
}

static GtkWidget *
setup_create_spin (GtkWidget *table, int row, const setting *set)
{
	GtkWidget *label, *wid, *rbox;
	char *text;

	label = gtk_label_new (_(set->label));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (table, label, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	rbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtkutil_grid_attach (table, rbox, 3, 4, row, row + 1,
							GU_EXPAND | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	gtk_widget_set_halign (rbox, GTK_ALIGN_START);
	gtk_widget_set_valign (rbox, GTK_ALIGN_CENTER);

	wid = gtk_spin_button_new_with_range (0, set->extra, 1);
	g_object_set_data (G_OBJECT (wid), "lbl", label);
	if (set->tooltip)
		gtk_widget_set_tooltip_text (wid, _(set->tooltip));
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (wid),
										setup_get_int (&setup_prefs, set));
	g_signal_connect (G_OBJECT (wid), "value_changed",
							G_CALLBACK (setup_spin_cb), (gpointer)set);
	nx_box_pack_start (rbox, wid, 0, 0, 0);

	if (set->list)
	{
		text = _((char *)set->list);
		if (text[0] == '!')
			label = setup_create_italic_label (text + 1);
		else
			label = gtk_label_new (text);
		nx_box_pack_start (rbox, label, 0, 0, 6);
	}

	return wid;
}

static gint
setup_apply_trans (int *tag)
{
	prefs.hex_gui_transparency = setup_prefs.hex_gui_transparency;
	gtk_widget_set_opacity (GTK_WIDGET (current_sess->gui->window),
							(prefs.hex_gui_transparency / 255.));

	/* mg_update_xtext (current_sess->gui->xtext); */
	*tag = 0;
	return 0;
}

static void
setup_hscale_cb (GtkScale *wid, const setting *set)
{
	static int tag = 0;

	setup_set_int (&setup_prefs, set, (int) gtk_range_get_value (GTK_RANGE (wid)));
	setup_live ();

	if (tag == 0)
	{
		tag = g_idle_add ((GSourceFunc) setup_apply_trans, &tag);
	}
}

static void
setup_create_hscale (GtkWidget *table, int row, const setting *set)
{
	GtkWidget *wid;

	wid = gtk_label_new (_(set->label));
	gtk_label_set_xalign (GTK_LABEL (wid), 0.0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	wid = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0., 255., 1.);
	gtk_scale_set_draw_value (GTK_SCALE (wid), TRUE);
	gtk_scale_set_value_pos (GTK_SCALE (wid), GTK_POS_RIGHT);
	gtk_range_set_value (GTK_RANGE (wid), setup_get_int (&setup_prefs, set));
	g_signal_connect (G_OBJECT(wid), "value_changed",
							G_CALLBACK (setup_hscale_cb), (gpointer)set);
	gtkutil_grid_attach (table, wid, 3, 6, row, row + 1,
							GU_EXPAND | GU_FILL, GU_FILL, 0, 0);

#ifndef WIN32 /* Windows always supports this */
	/* Only used for transparency currently */
	if (!nx_widget_is_composited (current_sess->gui->window))
		gtk_widget_set_sensitive (wid, FALSE);
#endif
}


static GtkWidget *proxy_user; 	/* username GtkEntry */
static GtkWidget *proxy_pass; 	/* password GtkEntry */

static void
setup_menu_cb (GtkWidget *cbox, gpointer data)
{
	const setting *set = data;
	int n = nx_combo_get_active (cbox);

	/* set the prefs.<field> */
	setup_set_int (&setup_prefs, set, n + set->extra);
	setup_live ();

	if (set->list == proxytypes)
	{
		/* only HTTP and SOCKS5 can use a username/pass */
		gtk_widget_set_sensitive (proxy_user, (n == 3 || n == 4 || n == 5));
		gtk_widget_set_sensitive (proxy_pass, (n == 3 || n == 4 || n == 5));
	}
}

static void
setup_radio_cb (GtkWidget *item, const setting *set)
{
	if (nx_toggle_get_active ((GtkWidget *) (item)))
	{
		int n = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "n"));
		/* set the prefs.<field> */
		setup_set_int (&setup_prefs, set, n);
		setup_live ();
	}
}

static int
setup_create_radio (GtkWidget *table, int row, const setting *set)
{
	GtkWidget *wid, *hbox;
	int i;
	const char **text = (const char **)set->list;
	GtkWidget *group;

	wid = gtk_label_new (_(set->label));
	gtk_label_set_xalign (GTK_LABEL (wid), 0.0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtkutil_grid_attach (table, hbox, 3, 4, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);

	i = 0;
	group = NULL;
	while (text[i])
	{
		if (text[i][0] != 0)
		{
			wid = nx_radio_button_new (group, _(text[i]));
			/*if (set->tooltip)
				gtk_widget_set_tooltip_text (wid, _(set->tooltip));*/
			group = wid;
			gtkutil_container_add (GTK_WIDGET (hbox), wid);
			if (i == setup_get_int (&setup_prefs, set))
				nx_toggle_set_active ((GtkWidget *) (wid), TRUE);
			g_object_set_data (G_OBJECT (wid), "n", GINT_TO_POINTER (i));
			g_signal_connect (G_OBJECT (wid), "toggled",
									G_CALLBACK (setup_radio_cb), (gpointer)set);
		}
		i++;
		row++;
	}

	return i;
}

/*
static const char *id_strings[] =
{
	"",
	"*",
	"%C4*%C18%B%B",
	"%U"
};

static void
setup_id_menu_cb (GtkWidget *item, char *dest)
{
	int n = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "n"));

	strcpy (dest, id_strings[n]);
	setup_live ();
}

static void
setup_create_id_menu (GtkWidget *table, char *label, int row, char *dest)
{
	GtkWidget *wid, *menu, *item;
	int i, def = 0;
	static const char *text[] =
	{
		("(disabled)"),
		("A star (*)"),
		("A red star (*)"),
		("Underlined")
	};

	wid = gtk_label_new (label);
	gtk_label_set_xalign (GTK_LABEL (wid), 0.0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	wid = gtk_option_menu_new ();
	menu = gtk_menu_new ();

	for (i = 0; i < 4; i++)
	{
		if (strcmp (id_strings[i], dest) == 0)
		{
			def = i;
			break;
		}
	}

	i = 0;
	while (text[i])
	{
		item = gtk_menu_item_new_with_label (_(text[i]));
		g_object_set_data (G_OBJECT (item), "n", GINT_TO_POINTER (i));

		nx_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate",
								G_CALLBACK (setup_id_menu_cb), dest);
		i++;
	}

	gtk_option_menu_set_menu (GTK_OPTION_MENU (wid), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (wid), def);

	gtkutil_grid_attach (table, wid, 3, 4, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
}

*/

static void
setup_create_menu (GtkWidget *table, int row, const setting *set)
{
	GtkWidget *wid, *cbox, *box;
	const char **text = (const char **)set->list;
	int i, n;
	const char **tr;

	wid = gtk_label_new (_(set->label));
	gtk_label_set_xalign (GTK_LABEL (wid), 0.0); gtk_label_set_yalign (GTK_LABEL (wid), 0.5);
	gtkutil_grid_attach (table, wid, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	for (n = 0; text[n]; n++);
	tr = g_new0 (const char *, n + 1);
	for (i = 0; i < n; i++)
		tr[i] = _(text[i]);
	cbox = nx_combo_new (tr);
	g_free (tr);

	nx_combo_set_active (cbox, setup_get_int (&setup_prefs, set) - set->extra);
	nx_combo_on_changed (cbox, setup_menu_cb, (gpointer)set);

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	nx_box_pack_start (box, cbox, 0, 0, 0);
	gtkutil_grid_attach (table, box, 3, 4, row, row + 1,
							GU_EXPAND | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
}

static void
setup_filereq_cb (GtkWidget *entry, char *file)
{
	if (file)
	{
		if (file[0])
			nx_entry_set_text (entry, file);
	}
}

static void
setup_browsefile_cb (GtkWidget *button, GtkWidget *entry)
{
	/* used for background image only */
	char *filter;
	int filter_type;

#ifdef WIN32
	filter = "*png;*.tiff;*.gif;*.jpeg;*.jpg";
	filter_type = FRF_EXTENSIONS;
#else
	filter = "image/*";
	filter_type = FRF_MIMETYPES;
#endif
	gtkutil_file_req (GTK_WINDOW (setup_window), _("Select an Image File"), setup_filereq_cb,
					entry, NULL, filter, filter_type|FRF_RECENTLYUSED|FRF_MODAL);
}

/* GtkFontSelectionDialog is GTK2's picker and is gone in GTK4. The
   chooser looks different (a searchable list with a preview instead of
   three columns), but it returns the same Pango font string that goes into
   text_font, so nothing stored changes. Listed in ROADMAP section 9. */
static void
setup_font_chosen (GObject *src, GAsyncResult *res, gpointer data)
{
	GtkWidget *entry = data;
	PangoFontDescription *fd = gtk_font_dialog_choose_font_finish (GTK_FONT_DIALOG (src), res, NULL);

	if (fd)
	{
		char *font_name = pango_font_description_to_string (fd);
		/* the entry's "changed" stores it and applies it live */
		if (GTK_IS_WIDGET (entry))
			nx_entry_set_text (entry, font_name);
		g_free (font_name);
		pango_font_description_free (fd);
	}
	g_object_unref (entry);
}

static void
setup_browsefolder_cb (GtkWidget *button, GtkEntry *entry)
{
	gtkutil_file_req (GTK_WINDOW (setup_window), _("Select Download Folder"), setup_filereq_cb, entry, (char*)nx_entry_get_text (entry), NULL, FRF_CHOOSEFOLDER|FRF_MODAL);
}

static void
setup_browsefont_cb (GtkWidget *button, GtkWidget *entry)
{
	GtkFontDialog *dialog = gtk_font_dialog_new ();
	PangoFontDescription *cur = NULL;

	gtk_font_dialog_set_title (dialog, _("Select font"));
	gtk_font_dialog_set_modal (dialog, TRUE);
	if (nx_entry_get_text (entry)[0])
		cur = pango_font_description_from_string (nx_entry_get_text (entry));
	gtk_font_dialog_choose_font (dialog, GTK_WINDOW (setup_window), cur, NULL,
										 setup_font_chosen, g_object_ref (entry));
	if (cur)
		pango_font_description_free (cur);
	g_object_unref (dialog);
}

static void
setup_entry_cb (GtkEntry *entry, setting *set)
{
	int size;
	int pos;
	unsigned char *p = (unsigned char*)nx_entry_get_text (entry);
	int len = strlen (p);

	/* need to truncate? */
	if (len >= set->extra)
	{
		len = pos = 0;
		while (1)
		{
			size = g_utf8_skip [*p];
			len += size;
			p += size;
			/* truncate to "set->extra" BYTES */
			if (len >= set->extra)
			{
				gtk_editable_delete_text (GTK_EDITABLE (entry), pos, -1);
				break;
			}
			pos++;
		}
	}
	else
	{
		setup_set_str (&setup_prefs, set, nx_entry_get_text (entry));
		setup_live ();
	}
}

static void
setup_create_label (GtkWidget *table, int row, const setting *set)
{
	gtkutil_grid_attach (table, setup_create_italic_label (_(set->label)),
							set->extra ? 1 : 3, 5, row, row + 1, GU_FILL,
							GU_SHRINK | GU_FILL, 0, 0);
}

static GtkWidget *
setup_create_entry (GtkWidget *table, int row, const setting *set)
{
	GtkWidget *label;
	GtkWidget *wid, *bwid;

	label = gtk_label_new (_(set->label));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (table, label, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	wid = gtk_entry_new ();
	g_object_set_data (G_OBJECT (wid), "lbl", label);
	if (set->list)
		gtk_entry_set_visibility (GTK_ENTRY (wid), FALSE);
	if (set->tooltip)
		gtk_widget_set_tooltip_text (wid, _(set->tooltip));
	gtk_entry_set_max_length (GTK_ENTRY (wid), set->extra - 1);
	nx_entry_set_text (wid, setup_get_str (&setup_prefs, set));
	g_signal_connect (G_OBJECT (wid), "changed",
							G_CALLBACK (setup_entry_cb), (gpointer)set);

	if (set->offset == P_OFFSETNL(hex_net_proxy_user))
		proxy_user = wid;
	if (set->offset == P_OFFSETNL(hex_net_proxy_pass))
		proxy_pass = wid; 

	/* only http and Socks5 can auth */
	if ( (set->offset == P_OFFSETNL(hex_net_proxy_pass) ||
			set->offset == P_OFFSETNL(hex_net_proxy_user)) &&
	     (setup_prefs.hex_net_proxy_type != 4 && setup_prefs.hex_net_proxy_type != 3 && setup_prefs.hex_net_proxy_type != 5) )
		gtk_widget_set_sensitive (wid, FALSE);

	if (set->type == ST_ENTRY)
		gtkutil_grid_attach (table, wid, 3, 6, row, row + 1,
								GU_EXPAND | GU_FILL, GU_FILL, 0, 0);
	else
	{
		gtkutil_grid_attach (table, wid, 3, 5, row, row + 1,
								GU_EXPAND | GU_FILL, GU_SHRINK, 0, 0);
		bwid = gtk_button_new_with_label (_("Browse..."));
		gtkutil_grid_attach (table, bwid, 5, 6, row, row + 1,
								GU_SHRINK | GU_FILL, GU_FILL, 0, 0);
		if (set->type == ST_EFILE)
			g_signal_connect (G_OBJECT (bwid), "clicked",
									G_CALLBACK (setup_browsefile_cb), wid);
		if (set->type == ST_EFONT)
			g_signal_connect (G_OBJECT (bwid), "clicked",
									G_CALLBACK (setup_browsefont_cb), wid);
		if (set->type == ST_EFOLDER)
			g_signal_connect (G_OBJECT (bwid), "clicked",
									G_CALLBACK (setup_browsefolder_cb), wid);
	}

	return wid;
}

static void
setup_create_header (GtkWidget *table, int row, char *labeltext)
{
	GtkWidget *label;
	char buf[128];

	if (row == 0)
		g_snprintf (buf, sizeof (buf), "<b>%s</b>", _(labeltext));
	else
		g_snprintf (buf, sizeof (buf), "\n<b>%s</b>", _(labeltext));

	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), buf);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (table, label, 0, 4, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 5);
}

static void
setup_create_button (GtkWidget *table, int row, char *label, GCallback callback)
{
	GtkWidget *but = gtk_button_new_with_label (label);
	gtkutil_grid_attach (table, but, 2, 3, row, row + 1,
					GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 5);
	g_signal_connect (G_OBJECT (but), "clicked", callback, NULL);
}

static GtkWidget *
setup_create_frame (void)
{
	GtkWidget *tab;

	tab = gtk_grid_new ();
	nx_set_border_width (tab, 6);
	gtk_grid_set_row_spacing (GTK_GRID (tab), 2);
	gtk_grid_set_column_spacing (GTK_GRID (tab), 3);

	return tab;
}

static void
open_data_cb (GtkWidget *button, gpointer data)
{
	fe_open_url (get_xdir ());
}

static GtkWidget *
setup_create_page (const setting *set)
{
	int i, row, do_disable;
	GtkWidget *tab;
	GtkWidget *wid = NULL, *parentwid = NULL;

	tab = setup_create_frame ();
	nx_set_border_width (tab, 6);

	i = row = do_disable = 0;
	while (set[i].type != ST_END)
	{
		switch (set[i].type)
		{
		case ST_HEADER:
			setup_create_header (tab, row, set[i].label);
			break;
		case ST_EFONT:
		case ST_ENTRY:
		case ST_EFILE:
		case ST_EFOLDER:
			wid = setup_create_entry (tab, row, &set[i]);
			break;
		case ST_TOGGLR:
			row--;
			setup_create_toggleR (tab, row, &set[i]);
			break;
		case ST_TOGGLE:
			wid = setup_create_toggleL (tab, row, &set[i]);
			if (set[i].extra)
				do_disable = set[i].extra;
			break;
		case ST_3OGGLE:
			setup_create_3oggle (tab, row, &set[i]);
			break;
		case ST_MENU:
			setup_create_menu (tab, row, &set[i]);
			break;
		case ST_RADIO:
			row += setup_create_radio (tab, row, &set[i]);
			break;
		case ST_NUMBER:
			wid = setup_create_spin (tab, row, &set[i]);
			break;
		case ST_HSCALE:
			setup_create_hscale (tab, row, &set[i]);
			break;
		case ST_LABEL:
			setup_create_label (tab, row, &set[i]);
			break;
		case ST_ALERTHEAD:
			setup_create_alert_header (tab, row, &set[i]);
		}

		if (do_disable)
		{
			if (GTK_IS_WIDGET (parentwid))
			{
				g_signal_connect (G_OBJECT (parentwid), "toggled", G_CALLBACK(setup_toggle_sensitive_cb), wid);
				gtk_widget_set_sensitive (wid, nx_toggle_get_active ((GtkWidget *) (parentwid)));
				do_disable--;
				if (!do_disable)
					parentwid = NULL;
			}
			else
				parentwid = wid;
		}

		i++;
		row++;
	}

	if (set == logging_settings)
	{
		setup_create_button (tab, row, _("Open Data Folder"), G_CALLBACK(open_data_cb));
	}

	return tab;
}

/* GTK2 tinted each palette button with a GtkStyle bg; GTK3 ignores that.
   Give the button its own provider so only that button changes. */
static void
setup_color_button_paint (GtkWidget *button, const GdkRGBA *col)
{
	/* per-widget providers are deprecated: one display-wide provider holds
	   a rule per swatch, each button carries its own class */
	static GtkCssProvider *prov;
	static GHashTable *rules;	/* class -> rule */
	char *cls = g_object_get_data (G_OBJECT (button), "hexchat-color-class");
	GString *all;
	GHashTableIter it;
	gpointer v;

	if (!prov)
	{
		prov = gtk_css_provider_new ();
		rules = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		gtk_style_context_add_provider_for_display (gtk_widget_get_display (button),
				GTK_STYLE_PROVIDER (prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}
	if (!cls)
	{
		static int n;
		cls = g_strdup_printf ("nx-swatch-%d", n++);
		g_object_set_data_full (G_OBJECT (button), "hexchat-color-class", cls, g_free);
		gtk_widget_add_css_class (button, cls);
	}
	g_hash_table_insert (rules, g_strdup (cls), g_strdup_printf (
		"button.%s { background-image: none; background-color: rgb(%d,%d,%d);"
		" color: %s; padding: 0; min-width: 18px; min-height: 18px; border-radius: 0; }\n",
		cls, C8R (*col), C8G (*col), C8B (*col),
		/* GTK3's theme: white digits on everything but near-white swatches */
		(0.299 * col->red + 0.587 * col->green + 0.114 * col->blue) < 0.85 ? "white" : "black"));
	all = g_string_new (NULL);
	g_hash_table_iter_init (&it, rules);
	while (g_hash_table_iter_next (&it, NULL, &v))
		g_string_append (all, v);
	nx_css_provider_load (prov, all->str);
	g_string_free (all, TRUE);
}

/* GTK4's GtkColorDialog: the system-look picker, the same RGB back, so
   colors.conf is unaffected */
static void
setup_color_chosen (GObject *src, GAsyncResult *res, gpointer data)
{
	GtkWidget *button = data;
	GdkRGBA *picked = gtk_color_dialog_choose_rgba_finish (GTK_COLOR_DIALOG (src), res, NULL);
	GdkRGBA *col = g_object_get_data (G_OBJECT (button), "c");

	if (picked && col && gtk_widget_get_root (button))
	{
		color_change = TRUE;
		*col = *picked;
		col->alpha = 1.0;	/* the palette has no transparency */
		setup_color_button_paint (button, col);
		setup_live ();
	}
	if (picked)
		gdk_rgba_free (picked);
	g_object_unref (button);
}

static void
setup_color_cb (GtkWidget *button, gpointer userdata)
{
	GtkColorDialog *dialog = gtk_color_dialog_new ();
	GdkRGBA *color = &colors[GPOINTER_TO_INT (userdata)];

	gtk_color_dialog_set_title (dialog, _("Select color"));
	gtk_color_dialog_set_modal (dialog, TRUE);
	gtk_color_dialog_set_with_alpha (dialog, FALSE);
	g_object_set_data (G_OBJECT (button), "c", color);
	gtk_color_dialog_choose_rgba (dialog, GTK_WINDOW (setup_window), color, NULL,
											setup_color_chosen, g_object_ref (button));
	g_object_unref (dialog);
}

static void
setup_create_color_button (GtkWidget *table, int num, int row, int col)
{
	GtkWidget *but;
	char buf[64];

	if (num > 31)
		strcpy (buf, "<span size=\"x-small\"> </span>");
	else
						/* 12345678901 23456789 01  23456789 */
		sprintf (buf, "<span size=\"x-small\">%d</span>", num);
	but = gtk_button_new_with_label (" ");
	gtk_label_set_markup (GTK_LABEL (nx_bin_get_child (but)), buf);
	/* win32 build uses this to turn off themeing */
	g_object_set_data (G_OBJECT (but), "hexchat-color", (gpointer)1);
	gtkutil_grid_attach (table, but, col, col+1, row, row+1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, 0, 0);
	g_signal_connect (G_OBJECT (but), "clicked",
							G_CALLBACK (setup_color_cb), GINT_TO_POINTER (num));
	setup_color_button_paint (but, &colors[num]);
	swatch[num] = but;
}

/* the scheme list: the short one (HexChat, native, the light/dark pairs)
   unless "All schemes" is ticked or the current one is only in the long
   one. Rows know their index in palette_scheme_id(). */
static GtkWidget *scheme_dd, *scheme_all;
static gboolean scheme_filling;

static void
setup_scheme_cb (GtkWidget *dd, gpointer userdata)
{
	guint i = gtk_drop_down_get_selected (GTK_DROP_DOWN (dd));
	GListModel *m = gtk_drop_down_get_model (GTK_DROP_DOWN (dd));
	GObject *item;
	char *id;
	int n;

	if (scheme_filling || i == GTK_INVALID_LIST_POSITION || !(item = g_list_model_get_item (m, i)))
		return;
	id = palette_scheme_id (GPOINTER_TO_INT (g_object_get_data (item, "i")));
	g_object_unref (item);
	if (strcmp (id, palette_scheme ()))
	{
		palette_set_scheme (id);
		color_change = TRUE;
		for (n = 0; n <= MAX_COL; n++)
			if (swatch[n])
				setup_color_button_paint (swatch[n], &colors[n]);
		setup_live ();
	}
	g_free (id);
}

static void
setup_scheme_fill (gboolean all)
{
	GtkStringList *names = gtk_string_list_new (NULL);
	const char *cur = palette_scheme ();
	int i, row = 0, sel = -1, n = all ? palette_scheme_count () : palette_scheme_short_count ();

	for (i = 0; i < n; i++)
	{
		char *id = palette_scheme_id (i), *label_text = palette_scheme_label (i);
		GObject *item;

		gtk_string_list_append (names, label_text);
		item = g_list_model_get_item (G_LIST_MODEL (names), row);
		g_object_set_data (item, "i", GINT_TO_POINTER (i));
		g_object_unref (item);
		if (!strcmp (id, cur))
			sel = row;
		row++;
		g_free (id);
		g_free (label_text);
	}
	/* a single scheme picked before: it's in the long list only */
	if (sel < 0 && !all)
	{
		g_object_unref (names);
		gtk_check_button_set_active (GTK_CHECK_BUTTON (scheme_all), TRUE);
		return;
	}
	scheme_filling = TRUE;
	gtk_drop_down_set_model (GTK_DROP_DOWN (scheme_dd), G_LIST_MODEL (names));
	gtk_drop_down_set_selected (GTK_DROP_DOWN (scheme_dd), sel < 0 ? 0 : sel);
	scheme_filling = FALSE;
	g_object_unref (names);
}

static void
setup_scheme_all_cb (GtkCheckButton *b, gpointer d)
{
	setup_scheme_fill (gtk_check_button_get_active (b));
}

static gboolean
setup_scheme_pick (gpointer d)
{
	const char *want = g_getenv ("NEXCHAT_PREFS_PICK");
	GListModel *m;
	guint i;

	if (!scheme_dd)
		return G_SOURCE_REMOVE;
	m = gtk_drop_down_get_model (GTK_DROP_DOWN (scheme_dd));
	for (i = 0; i < g_list_model_get_n_items (m); i++)
	{
		GtkStringObject *s = g_list_model_get_item (m, i);
		gboolean hit = g_str_has_prefix (gtk_string_object_get_string (s), want);
		g_object_unref (s);
		if (hit)
		{
			gtk_drop_down_set_selected (GTK_DROP_DOWN (scheme_dd), i);
			break;
		}
	}
	return G_SOURCE_REMOVE;
}

/* Up/Down (and Left/Right, Home/End, Page Up/Down) on the closed picker
   step through the schemes, each applied as it's reached: try them
   one after another without opening the list. Space/Enter still open it. */
static gboolean
setup_scheme_key (GtkEventControllerKey *k, guint key, guint code, GdkModifierType mods, gpointer d)
{
	GtkDropDown *dd = GTK_DROP_DOWN (scheme_dd);
	guint n = g_list_model_get_n_items (gtk_drop_down_get_model (dd));
	guint cur = gtk_drop_down_get_selected (dd);
	int to;

	if (!n || (mods & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_META_MASK | GDK_SUPER_MASK)))
		return FALSE;
	if (cur == GTK_INVALID_LIST_POSITION)
		cur = 0;
	switch (key)
	{
	case GDK_KEY_Up: case GDK_KEY_KP_Up: case GDK_KEY_Left: case GDK_KEY_KP_Left:
		to = (int) cur - 1; break;
	case GDK_KEY_Down: case GDK_KEY_KP_Down: case GDK_KEY_Right: case GDK_KEY_KP_Right:
		to = (int) cur + 1; break;
	case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:
		to = (int) cur - 10; break;
	case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
		to = (int) cur + 10; break;
	case GDK_KEY_Home: case GDK_KEY_KP_Home:
		to = 0; break;
	case GDK_KEY_End: case GDK_KEY_KP_End:
		to = (int) n - 1; break;
	default:
		return FALSE;
	}
	to = CLAMP (to, 0, (int) n - 1);
	if ((guint) to != cur)
		gtk_drop_down_set_selected (dd, to);	/* "notify::selected" applies it */
	return TRUE;
}

/* the scheme list, typing filters it */
static gboolean
setup_scheme_keys (gpointer data)
{
	char **keys = data;
	static int i;

	if (!scheme_dd)
	{
		g_strfreev (keys);
		return G_SOURCE_REMOVE;
	}
	gtk_widget_grab_focus (scheme_dd);
	if (!keys[i])
	{
		GListModel *m = gtk_drop_down_get_model (GTK_DROP_DOWN (scheme_dd));
		GtkStringObject *s = g_list_model_get_item (m, gtk_drop_down_get_selected (GTK_DROP_DOWN (scheme_dd)));
		g_printerr ("prefs-scheme: %s | %s\n", s ? gtk_string_object_get_string (s) : "", palette_scheme ());
		g_clear_object (&s);
		g_strfreev (keys);
		i = 0;
		return G_SOURCE_REMOVE;
	}
	{
		guint kv = gdk_keyval_from_name (keys[i]);
		GList *l, *ctrls = NULL;
		GListModel *cm = gtk_widget_observe_controllers (scheme_dd);
		guint j;
		for (j = 0; j < g_list_model_get_n_items (cm); j++)
		{
			GObject *c = g_list_model_get_item (cm, j);
			if (GTK_IS_EVENT_CONTROLLER_KEY (c))
				ctrls = g_list_prepend (ctrls, c);
			else
				g_object_unref (c);
		}
		g_object_unref (cm);
		for (l = ctrls; l; l = l->next)
		{
			gboolean handled = FALSE;
			g_signal_emit_by_name (l->data, "key-pressed", kv, 0, 0, &handled);
			if (handled)
				break;
		}
		g_list_free_full (ctrls, g_object_unref);
	}
	i++;
	return G_SOURCE_CONTINUE;
}

/* the scheme list, typing filters it */
static GtkWidget *
setup_create_scheme_row (void)
{
	GtkWidget *box, *label;
	GtkEventController *keys;

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	nx_set_border_width (box, 6);
	label = gtk_label_new_with_mnemonic (_("Color _scheme:"));
	nx_box_pack_start (box, label, FALSE, FALSE, 0);

	scheme_dd = gtk_drop_down_new (NULL,
			gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string"));
	gtk_drop_down_set_enable_search (GTK_DROP_DOWN (scheme_dd), TRUE);
	gtk_drop_down_set_search_match_mode (GTK_DROP_DOWN (scheme_dd), GTK_STRING_FILTER_MATCH_MODE_SUBSTRING);
	gtk_widget_set_valign (scheme_dd, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text (scheme_dd, _("Colors the whole window and the text; you can still change any color below. "
													"Up/Down tries the next one."));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), scheme_dd);
	nx_box_pack_start (box, scheme_dd, FALSE, FALSE, 0);
	/* capture: before GtkDropDown's own Up/Down opens the popover */
	keys = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
	g_signal_connect (keys, "key-pressed", G_CALLBACK (setup_scheme_key), NULL);
	gtk_widget_add_controller (scheme_dd, keys);
	gtk_widget_set_focusable (scheme_dd, TRUE);

	scheme_all = gtk_check_button_new_with_mnemonic (_("_All schemes"));
	gtk_widget_set_tooltip_text (scheme_all, _("Every one of the 800+ terminal schemes, not just the ones that follow light/dark"));
	gtk_widget_set_valign (scheme_all, GTK_ALIGN_CENTER);
	nx_box_pack_start (box, scheme_all, FALSE, FALSE, 0);
	g_signal_connect (scheme_all, "toggled", G_CALLBACK (setup_scheme_all_cb), NULL);

	setup_scheme_fill (FALSE);
	nx_combo_on_changed (scheme_dd, setup_scheme_cb, NULL);
	/* tests: NEXCHAT_PREFS_PICK=Label picks that row, as a click would */
	if (g_getenv ("NEXCHAT_PREFS_PICK"))
		g_timeout_add (1200, setup_scheme_pick, NULL);
	/* tests: NEXCHAT_PREFS_KEYS="Down Down Up End" presses those keys on
	   the focused picker (through its key controller) */
	if (g_getenv ("NEXCHAT_PREFS_KEYS"))
		g_timeout_add (1500, setup_scheme_keys, g_strsplit (g_getenv ("NEXCHAT_PREFS_KEYS"), " ", -1));
	return box;
}

static void
setup_create_other_colorR (char *text, int num, int row, GtkWidget *tab)
{
	GtkWidget *label;

	label = gtk_label_new (text);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (tab, label, 5, 9, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);
	setup_create_color_button (tab, num, row, 9);
}

static void
setup_create_other_color (char *text, int num, int row, GtkWidget *tab)
{
	GtkWidget *label;

	label = gtk_label_new (text);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (tab, label, 2, 3, row, row + 1,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);
	setup_create_color_button (tab, num, row, 3);
}

static GtkWidget *
setup_create_color_page (void)
{
	GtkWidget *tab, *box, *label;
	int i;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	nx_set_border_width (box, 6);

	gtkutil_container_add (GTK_WIDGET (box), setup_create_scheme_row ());

	tab = gtk_grid_new ();
	nx_set_border_width (tab, 6);
	gtk_grid_set_row_spacing (GTK_GRID (tab), 2);
	gtk_grid_set_column_spacing (GTK_GRID (tab), 3);
	gtkutil_container_add (GTK_WIDGET (box), tab);

	setup_create_header (tab, 0, N_("Text Colors"));

	label = gtk_label_new (_("mIRC colors:"));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (tab, label, 2, 3, 1, 2,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	for (i = 0; i < 16; i++)
		setup_create_color_button (tab, i, 1, i+3);

	label = gtk_label_new (_("Local colors:"));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (tab, label, 2, 3, 2, 3,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0);

	for (i = 16; i < 32; i++)
		setup_create_color_button (tab, i, 2, (i+3) - 16);

	setup_create_other_color (_("Foreground:"), COL_FG, 3, tab);
	setup_create_other_colorR (_("Background:"), COL_BG, 3, tab);

	setup_create_header (tab, 5, N_("Selected Text"));

	setup_create_other_color (_("Foreground:"), COL_MARK_FG, 6, tab);
	setup_create_other_colorR (_("Background:"), COL_MARK_BG, 6, tab);

	setup_create_header (tab, 8, N_("Interface Colors"));

	setup_create_other_color (_("New data:"), COL_NEW_DATA, 9, tab);
	setup_create_other_colorR (_("Marker line:"), COL_MARKER, 9, tab);
	setup_create_other_color (_("New message:"), COL_NEW_MSG, 10, tab);
	setup_create_other_colorR (_("Away user:"), COL_AWAY, 10, tab);
	setup_create_other_color (_("Highlight:"), COL_HILIGHT, 11, tab);
	setup_create_other_colorR (_("Spell checker:"), COL_SPELL, 11, tab);

	setup_create_header (tab, 15, N_("Color Stripping"));

	/* label = gtk_label_new (_("Strip colors from:"));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtkutil_grid_attach (tab, label, 2, 3, 16, 17,
							GU_SHRINK | GU_FILL, GU_SHRINK | GU_FILL, LABEL_INDENT, 0); */

	for (i = 0; i < 3; i++)
	{
		setup_create_toggleL (tab, i + 16, &color_settings[i]);
	}

	return box;
}

/* === GLOBALS for sound GUI === */

static GtkWidget *sndfile_entry;
static int ignore_changed = FALSE;

extern struct text_event te[]; /* text.c */
extern char *sound_files[];

static void
setup_snd_populate (GtkWidget *view)
{
	nx_store *store = nx_view_get_store (view);
	nx_row *row;
	int i;

	for (i = NUM_XP-1; i >= 0; i--)
	{
		row = nx_store_prepend (store, NULL);
		nx_row_set (row, 0, te[i].name, 1, sound_files[i] ? sound_files[i] : "", 2, i, -1);
		if (i == last_selected_row)
		{
			nx_view_select (view, row);
			nx_view_scroll_to (view, row, NX_SCROLL_CENTER);
			nx_view_set_cursor (view, row, NULL);
		}
	}
}

static int
setup_snd_get_selected (GtkWidget *view, nx_row **row)
{
	*row = nx_view_get_selected (view);
	if (!*row)
		return -1;
	return nx_row_get_int (*row, 2);
}

static void
setup_snd_row_cb (GtkWidget *view, gpointer user_data)
{
	int n;
	nx_row *row;

	n = setup_snd_get_selected (view, &row);
	if (n == -1)
		return;
	last_selected_row = n;

	ignore_changed = TRUE;
	if (sound_files[n])
		nx_entry_set_text (sndfile_entry, sound_files[n]);
	else
		nx_entry_set_text (sndfile_entry, "");
	ignore_changed = FALSE;
}

static void
setup_snd_filereq_cb (GtkWidget *entry, char *file)
{
	if (file)
	{
		if (file[0])
		{
			/* Use just the filename if the given sound file is in the default <config>/sounds directory.
			 * We're comparing absolute paths so this won't work in portable mode which uses a relative path.
			 */
			if (!strcmp (g_path_get_dirname (file), g_build_filename (get_xdir (), HEXCHAT_SOUND_DIR, NULL)))
			{
				nx_entry_set_text (entry, g_path_get_basename (file));
			}
			else
			{
				nx_entry_set_text (entry, file);
			}
		}
	}
}

static void
setup_snd_browse_cb (GtkWidget *button, GtkEntry *entry)
{
	char *sounds_dir = g_build_filename (get_xdir (), HEXCHAT_SOUND_DIR, NULL);
	char *filter = NULL;
	int filter_type;
#ifdef WIN32 /* win32 only supports wav, others could support anything */
	filter = "*.wav";
	filter_type = FRF_EXTENSIONS;
#else
	filter = "audio/*";
	filter_type = FRF_MIMETYPES;
#endif

	gtkutil_file_req (GTK_WINDOW (setup_window), _("Select a sound file"), setup_snd_filereq_cb, entry,
						sounds_dir, filter, FRF_MODAL|FRF_FILTERISINITIAL|filter_type);
	g_free (sounds_dir);
}

static void
setup_snd_play_cb (GtkWidget *button, GtkEntry *entry)
{
	sound_play (nx_entry_get_text (entry), FALSE);
}

static void
setup_snd_changed_cb (GtkEntry *ent, GtkWidget *view)
{
	int n;
	nx_row *row;

	if (ignore_changed)
		return;

	n = setup_snd_get_selected (view, &row);
	if (n == -1)
		return;

	/* get the new sound file */
	g_free (sound_files[n]);
	sound_files[n] = g_strdup (nx_entry_get_text (ent));

	/* update the TreeView list */
	nx_row_set (row, 1, sound_files[n], -1);

	/* sound.conf: saved with the rest */
	setup_live ();
}

static GtkWidget *
setup_create_sound_page (void)
{
	GtkWidget *vbox1;
	GtkWidget *vbox2;
	GtkWidget *sound_tree;
	GtkWidget *table1;
	GtkWidget *sound_label;
	GtkWidget *sound_browse;
	GtkWidget *sound_play;
	nx_store *store;

	vbox1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	nx_set_border_width (vbox1, 6);
	nx_widget_show (vbox1);

	vbox2 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	nx_widget_show (vbox2);
	gtkutil_container_add (GTK_WIDGET (vbox1), vbox2);

	store = nx_store_new (FALSE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
	sound_tree = nx_view_new_scrolled (vbox2, store);
	g_object_unref (store);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (gtk_widget_get_parent (sound_tree)),
											  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	nx_scrolled_window_set_shadow_in (gtk_widget_get_parent (sound_tree), TRUE);
	nx_view_add_column (sound_tree, _("Event"), NX_COL_TEXT, 0);
	nx_view_add_column (sound_tree, _("Sound file"), NX_COL_TEXT, 1);
	nx_view_set_search_column (sound_tree, 0);
	setup_snd_populate (sound_tree);
	nx_view_on_select (sound_tree, setup_snd_row_cb, NULL);
	nx_widget_show (sound_tree);

	table1 = gtk_grid_new ();
	nx_widget_show (table1);
	nx_box_pack_start (vbox2, table1, FALSE, TRUE, 8);
	gtk_grid_set_row_spacing (GTK_GRID (table1), 2);
	gtk_grid_set_column_spacing (GTK_GRID (table1), 4);

	sound_label = gtk_label_new_with_mnemonic (_("Sound file:"));
	nx_widget_show (sound_label);
	gtkutil_grid_attach (table1, sound_label, 0, 1, 0, 1,
							(GU_FILL),
							(0), 0, 0);
	gtk_label_set_xalign (GTK_LABEL (sound_label), 0); gtk_label_set_yalign (GTK_LABEL (sound_label), 0.5);

	sndfile_entry = gtk_entry_new ();
	g_signal_connect (G_OBJECT (sndfile_entry), "changed",
							G_CALLBACK (setup_snd_changed_cb), sound_tree);
	nx_widget_show (sndfile_entry);
	gtkutil_grid_attach (table1, sndfile_entry, 0, 1, 1, 2,
							(GU_EXPAND | GU_FILL),
							(0), 0, 0);

	sound_browse = gtk_button_new_with_mnemonic (_("_Browse..."));
	g_signal_connect (G_OBJECT (sound_browse), "clicked",
							G_CALLBACK (setup_snd_browse_cb), sndfile_entry);
	nx_widget_show (sound_browse);
	gtkutil_grid_attach (table1, sound_browse, 1, 2, 1, 2,
							(GU_FILL),
							(0), 0, 0);

#ifdef NX_STOCK_MEDIA_PLAY
	sound_play = gtkutil_button_new_from_stock (NX_STOCK_MEDIA_PLAY);
#else
	sound_play = gtk_button_new_with_mnemonic (_("_Play"));
#endif
	g_signal_connect (G_OBJECT (sound_play), "clicked",
							G_CALLBACK (setup_snd_play_cb), sndfile_entry);
	nx_widget_show (sound_play);
	gtkutil_grid_attach (table1, sound_play, 2, 3, 1, 2,
							(GU_FILL),
							(0), 0, 0);

	setup_snd_row_cb (sound_tree, NULL);

	return vbox1;
}

static void
setup_add_page (const char *title, GtkWidget *book, GtkWidget *tab)
{
	GtkWidget *label, *vvbox, *viewport;
	GtkScrolledWindow *sw;
	char buf[128];

	vvbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	/* label */
	label = gtk_label_new (NULL);
	g_snprintf (buf, sizeof (buf), "<b><big>%s</big></b>", _(title));
	gtk_label_set_markup (GTK_LABEL (label), buf);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0); gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	/* was gtk_misc_set_padding (2, 1) */
	gtk_widget_set_margin_start (label, 2); gtk_widget_set_margin_end (label, 2);
	gtk_widget_set_margin_top (label, 1); gtk_widget_set_margin_bottom (label, 1);
	nx_box_pack_start (vvbox, label, FALSE, FALSE, 2);

	gtkutil_container_add (GTK_WIDGET (vvbox), tab);

	sw = GTK_SCROLLED_WINDOW(nx_scrolled_window_new ());
	nx_scrolled_window_set_shadow_in (sw, TRUE);
	gtk_scrolled_window_set_policy (sw, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtkutil_container_add (GTK_WIDGET (sw), vvbox);

	viewport = nx_bin_get_child (sw);
	nx_viewport_set_no_shadow (viewport);

	gtk_notebook_append_page (GTK_NOTEBOOK (book), GTK_WIDGET(sw), NULL);
}

static const char *const cata[] =
{
	N_("Interface"),
		N_("Appearance"),
		N_("Input box"),
		N_("User list"),
		N_("Channel switcher"),
		N_("Colors"),
		NULL,
	N_("Chatting"),
		N_("General"),
		N_("Alerts"),
		N_("Sounds"),
		N_("Logging"),
		N_("Advanced"),
		NULL,
	N_("Network"),
		N_("Network setup"),
		N_("File transfers"),
		N_("Identd"),
		NULL,
	NULL
};

static GtkWidget *
setup_create_pages (GtkWidget *box)
{
	GtkWidget *book;
	GtkWindow *win = GTK_WINDOW(nx_widget_get_toplevel (box));

	book = gtk_notebook_new ();

	setup_add_page (cata[1], book, setup_create_page (appearance_settings));
	setup_add_page (cata[2], book, setup_create_page (inputbox_settings));
	setup_add_page (cata[3], book, setup_create_page (userlist_settings));
	setup_add_page (cata[4], book, setup_create_page (tabs_settings));
	setup_add_page (cata[5], book, setup_create_color_page ());

	setup_add_page (cata[8], book, setup_create_page (general_settings));

	if (!gtkutil_tray_icon_supported (win) && !notification_backend_supported ())
	{
		setup_add_page (cata[9], book, setup_create_page (alert_settings_unityandnonotifications));
	}
	else if (!gtkutil_tray_icon_supported (win))
	{
		setup_add_page (cata[9], book, setup_create_page (alert_settings_unity));
	}
	else if (!notification_backend_supported ())
	{
		setup_add_page (cata[9], book, setup_create_page (alert_settings_nonotifications));
	}
	else
	{
		setup_add_page (cata[9], book, setup_create_page (alert_settings));
	}

	setup_add_page (cata[10], book, setup_create_sound_page ());
	setup_add_page (cata[11], book, setup_create_page (logging_settings));
	setup_add_page (cata[12], book, setup_create_page (advanced_settings));

	setup_add_page (cata[15], book, setup_create_page (network_settings));
	setup_add_page (cata[16], book, setup_create_page (filexfer_settings));
	setup_add_page (cata[17], book, setup_create_page (identd_settings));

	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (book), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (book), FALSE);
	gtkutil_container_add (GTK_WIDGET (box), book);

	return book;
}

static void
setup_tree_cb (GtkWidget *view, gpointer book)
{
	nx_row *row = nx_view_get_selected (view);
	int page;

	if (row)
	{
		page = nx_row_get_int (row, 1);
		if (page != -1)
		{
			gtk_notebook_set_current_page (GTK_NOTEBOOK (book), page);
			last_selected_page = page;
		}
	}
}

/* the category headings can't be chosen, only the pages under them */
static gboolean
setup_tree_select_filter (nx_row *row, gpointer data)
{
	return nx_row_parent (row) != NULL;
}

static void
setup_create_tree (GtkWidget *box, GtkWidget *book)
{
	GtkWidget *tree;
	GtkWidget *frame;
	nx_store *model;
	nx_row *row, *child, *sel_row = NULL;
	int i, page;

	model = nx_store_new (TRUE, 2, G_TYPE_STRING, G_TYPE_INT);

	/* tests: NEXCHAT_PREFS_PAGE=N opens on page N */
	if (g_getenv ("NEXCHAT_PREFS_PAGE"))
		last_selected_page = atoi (g_getenv ("NEXCHAT_PREFS_PAGE"));

	i = 0;
	page = 0;
	do
	{
		row = nx_store_append (model, NULL);
		nx_row_set (row, 0, _(cata[i]), 1, -1, -1);
		i++;

		do
		{
			child = nx_store_append (model, row);
			nx_row_set (child, 0, _(cata[i]), 1, page, -1);
			if (page == last_selected_page)
				sel_row = child;
			page++;
			i++;
		} while (cata[i]);

		i++;

	} while (cata[i]);

	tree = nx_view_new (model);
	g_object_unref (model);
	nx_view_set_select_mode (tree, GTK_SELECTION_BROWSE);
	nx_view_set_select_filter (tree, setup_tree_select_filter, NULL);
	nx_view_on_select (tree, setup_tree_cb, book);
	nx_view_add_column (tree, _("Categories"), NX_COL_TEXT, 0);
	nx_view_set_search_column (tree, 0);
	nx_view_expand_all (tree);

	frame = gtk_frame_new (NULL);
	gtkutil_container_add (GTK_WIDGET (frame), tree);
	nx_box_pack_start (box, frame, 0, 0, 0);
	nx_box_reorder_child (box, frame, 0);

	if (sel_row)
		nx_view_select (tree, sel_row);
}

static void
setup_apply_to_sess (session_gui *gui)
{
	mg_update_xtext (gui->xtext);

	if (prefs.hex_gui_ulist_buttons)
		nx_widget_show (gui->button_box);
	else
		nx_widget_hide (gui->button_box);

	/* update active languages */
	sexy_spell_entry_deactivate_language((SexySpellEntry *)gui->input_box,NULL);
	sexy_spell_entry_activate_default_languages((SexySpellEntry *)gui->input_box);

	sexy_spell_entry_set_checked ((SexySpellEntry *)gui->input_box, prefs.hex_gui_input_spell);
	sexy_spell_entry_set_parse_attributes ((SexySpellEntry *)gui->input_box, prefs.hex_gui_input_attr);
	sexy_spell_entry_set_cursor ((SexySpellEntry *)gui->input_box, prefs.hex_gui_input_cursor, prefs.hex_gui_input_blink);
}

static void
unslash (char *dir)
{
	if (dir[0])
	{
		int len = strlen (dir) - 1;
#ifdef WIN32
		if (dir[len] == '/' || dir[len] == '\\')
#else
		if (dir[len] == '/')
#endif
			dir[len] = 0;
	}
}

void
setup_apply_real (int new_pix, int do_ulist, int do_layout, int do_identd)
{
	GSList *list;
	session *sess;
	int done_main = FALSE;

	/* remove trailing slashes */
	unslash (prefs.hex_dcc_dir);
	unslash (prefs.hex_dcc_completed_dir);

	g_mkdir (prefs.hex_dcc_dir, 0700);
	g_mkdir (prefs.hex_dcc_completed_dir, 0700);

	if (new_pix)
	{
		if (channelwin_pix)
			g_object_unref (channelwin_pix);
		/* live: a half-typed path just loads nothing, no popup */
		channelwin_pix = prefs.hex_text_background[0] && g_file_test (prefs.hex_text_background, G_FILE_TEST_IS_REGULAR)
			? pixmap_load_from_file (prefs.hex_text_background) : NULL;
	}

	if (!setup_window)
		fe_check_font ();
	/* the scheme decides light or dark for GTK's own drawing too */
	platform_style_sync_scheme ();
	fe_apply_css ();

	list = sess_list;
	while (list)
	{
		sess = list->data;
		if (sess->gui->is_tab)
		{
			/* only apply to main tabwindow once */
			if (!done_main)
			{
				done_main = TRUE;
				setup_apply_to_sess (sess->gui);
			}
		} else
		{
			setup_apply_to_sess (sess->gui);
		}

		log_open_or_close (sess);

		if (do_ulist)
			userlist_rehash (sess);
		mg_update_tab_label (sess);

		list = list->next;
	}

	mg_apply_setup ();
	tray_apply_setup ();
	hexchat_reinit_timers ();

	if (do_layout)
		menu_change_layout ();

	if (do_identd)
		handle_command (current_sess, "IDENTD reload", FALSE);
}

static GtkWidget *setup_note;	/* the line next to Close: restart needed, etc. */
static gboolean setup_needs_restart;

static void
setup_apply (struct hexchatprefs *pr)
{
#ifdef WIN32
	PangoFontDescription *old_desc;
	PangoFontDescription *new_desc;
	char buffer[4 * FONTNAMELEN + 1];
#endif
	int new_pix = FALSE;
	int noapply = FALSE;
	int do_ulist = FALSE;
	int do_layout = FALSE;
	int do_identd = FALSE;
	const char *note = NULL;

	if (strcmp (pr->hex_text_background, prefs.hex_text_background) != 0)
		new_pix = TRUE;

#define DIFF(a) (pr->a != prefs.a)

#ifdef WIN32
	if (DIFF (hex_gui_lang))
		noapply = TRUE;
#endif
	if (DIFF (hex_gui_compact))
		noapply = TRUE;
	if (DIFF (hex_gui_input_icon))
		noapply = TRUE;
	if (DIFF (hex_gui_input_nick))
		noapply = TRUE;
	if (DIFF (hex_gui_lagometer))
		noapply = TRUE;
	if (DIFF (hex_gui_tab_icons))
		noapply = TRUE;
	if (DIFF (hex_gui_tab_server))
		noapply = TRUE;
	if (DIFF (hex_gui_tab_small))
		noapply = TRUE;
	if (DIFF (hex_gui_tab_sort))
		noapply = TRUE;
	if (DIFF (hex_gui_tab_trunc))
		noapply = TRUE;
	if (DIFF (hex_gui_throttlemeter))
		noapply = TRUE;
	if (DIFF (hex_gui_ulist_count))
		noapply = TRUE;
	if (DIFF (hex_gui_ulist_icons))
		noapply = TRUE;
	if (DIFF (hex_gui_ulist_show_hosts))
		noapply = TRUE;
	if (DIFF (hex_gui_ulist_style))
		noapply = TRUE;
	if (DIFF (hex_gui_ulist_sort))
		noapply = TRUE;
	if (DIFF (hex_gui_input_style) && prefs.hex_gui_input_style == TRUE)
		noapply = TRUE; /* Requires restart to *disable* */

	if (DIFF (hex_gui_tab_dots))
		do_layout = TRUE;
	if (DIFF (hex_gui_tab_layout))
		do_layout = TRUE;

	if (DIFF (hex_identd_server) || DIFF (hex_identd_port))
		do_identd = TRUE;

	if (color_change || (DIFF (hex_gui_ulist_color)) || (DIFF (hex_away_size_max)) || (DIFF (hex_away_track)))
		do_ulist = TRUE;

	if ((pr->hex_gui_tab_pos == 5 || pr->hex_gui_tab_pos == 6) &&
		 pr->hex_gui_tab_layout == 2 && pr->hex_gui_tab_pos != prefs.hex_gui_tab_pos)
		note = _("The tree can't go on the top or bottom: switch to Tabs first.");

	/* format cannot be blank, there is already a setting for this */
	if (pr->hex_stamp_text_format[0] == 0)
	{
		pr->hex_stamp_text = 0;
		strcpy (pr->hex_stamp_text_format, prefs.hex_stamp_text_format);
	}
	/* while it's being typed: kept as it was until it has something */
	if (pr->hex_irc_real_name[0] == 0)
	{
		strcpy (pr->hex_irc_real_name, prefs.hex_irc_real_name[0] ? prefs.hex_irc_real_name : "realname");
		note = _("The real name can't be empty; the old one is kept.");
	}

	memcpy (&prefs, pr, sizeof (prefs));

#ifdef WIN32
	/* merge hex_font_main and hex_font_alternative into hex_font_normal */
	old_desc = pango_font_description_from_string (prefs.hex_text_font_main);
	sprintf (buffer, "%s,%s", pango_font_description_get_family (old_desc), prefs.hex_text_font_alternative);
	new_desc = pango_font_description_from_string (buffer);
	pango_font_description_set_weight (new_desc, pango_font_description_get_weight (old_desc));
	pango_font_description_set_style (new_desc, pango_font_description_get_style (old_desc));
	pango_font_description_set_size (new_desc, pango_font_description_get_size (old_desc));
	sprintf (prefs.hex_text_font, "%s", pango_font_description_to_string (new_desc));

	/* FIXME this is not required after pango_font_description_from_string()
	g_free (old_desc);
	g_free (new_desc);
	*/
#endif

	if (prefs.hex_irc_real_name[0] == 0)
	{
		fe_message (_("The Real name option cannot be left blank. Falling back to \"realname\"."), FE_MSG_WARN);
		strcpy (prefs.hex_irc_real_name, "realname");
	}
	
	setup_apply_real (new_pix, do_ulist, do_layout, do_identd);
	color_change = FALSE;

	if (noapply)
		setup_needs_restart = TRUE;
	if (!note && setup_needs_restart)
		note = _("Some of these take full effect after a restart.");
#ifndef WIN32
	if (!note && prefs.hex_dcc_auto_recv == 2 && !strcmp ((char *) g_get_home_dir (), prefs.hex_dcc_dir))
		note = _("Careful: files are accepted into your home folder (someone could send a .bash_profile).");
#endif
	if (setup_note)
		gtk_label_set_text (GTK_LABEL (setup_note), note ? note : "");
}

/* ---- live: every change is applied and saved a moment after it's made ---- */

static guint live_tag;

static gboolean
setup_live_now (gpointer d)
{
	live_tag = 0;
	if (!setup_window)
		return G_SOURCE_REMOVE;
	setup_apply (&setup_prefs);
	/* what was fixed up (an empty real name...) shows in the fields' copy */
	memcpy (&setup_prefs, &prefs, sizeof prefs);
	save_config ();
	palette_save ();
	sound_save ();
	return G_SOURCE_REMOVE;
}

/* soon, not at once: typing in a field, dragging a slider, scrolling
   through schemes all settle first */
static void
setup_live (void)
{
	if (live_tag)
		g_source_remove (live_tag);
	live_tag = g_timeout_add (250, setup_live_now, NULL);
}

static gboolean
setup_press_ok (gpointer b)
{
	g_signal_emit_by_name (b, "clicked");
	return G_SOURCE_REMOVE;
}

static GtkWidget *
setup_window_open (void)
{
	GtkWidget *wid, *win, *vbox, *hbox, *hbbox;
	char buf[128];

	g_snprintf(buf, sizeof(buf), _("Preferences - %s"), _(DISPLAY_NAME));
	win = gtkutil_window_new (buf, "prefs", 0, 600, 2);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
	nx_set_border_width (vbox, 6);
	gtkutil_container_add (GTK_WIDGET (win), vbox);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	gtkutil_container_add (GTK_WIDGET (vbox), hbox);

	setup_create_tree (hbox, setup_create_pages (hbox));

	/* changes apply as they're made: a note on the left, Close on the right */
	hbbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	nx_box_pack_end (vbox, hbbox, FALSE, FALSE, 0);

	setup_note = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (setup_note), 0.0);
	gtk_label_set_wrap (GTK_LABEL (setup_note), TRUE);
	gtk_widget_add_css_class (setup_note, "dim-label");
	gtk_widget_set_hexpand (setup_note, TRUE);
	gtk_box_append (GTK_BOX (hbbox), setup_note);

	cancel_button = wid = gtk_button_new_with_mnemonic (_("_Close"));
	gtk_widget_set_tooltip_text (wid, _("Changes are applied and saved as you make them"));
	g_signal_connect_swapped (G_OBJECT (wid), "clicked",
							 G_CALLBACK (gtk_window_destroy), win);
	gtk_box_append (GTK_BOX (hbbox), wid);
	gtk_window_set_default_widget (GTK_WINDOW (win), wid);
	/* tests: NEXCHAT_PREFS_OK=1 closes it once it's up */
	if (g_getenv ("NEXCHAT_PREFS_OK"))
		g_timeout_add (1500, setup_press_ok, wid);

	nx_widget_show_all (win);

	return win;
}

static void
setup_close_cb (GtkWidget *win, GtkWidget **swin)
{
	*swin = NULL;
	memset (swatch, 0, sizeof swatch);
	setup_note = NULL;
	scheme_dd = scheme_all = NULL;
	/* the last change, if it hadn't gone through yet */
	if (live_tag)
	{
		g_source_remove (live_tag);
		live_tag = 0;
		setup_apply (&setup_prefs);
		save_config ();
		palette_save ();
	}
	if (setup_needs_restart)
		fe_message (_("Some settings you changed take full effect after a restart."), FE_MSG_INFO);
}

void
setup_open (void)
{
	if (setup_window)
	{
		gtk_window_present (GTK_WINDOW (setup_window));
		return;
	}

	memcpy (&setup_prefs, &prefs, sizeof (prefs));

	color_change = FALSE;
	setup_needs_restart = FALSE;
	setup_window = setup_window_open ();

	g_signal_connect (G_OBJECT (setup_window), "destroy",
							G_CALLBACK (setup_close_cb), &setup_window);
}
