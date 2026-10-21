// nexchat TUI — Preferences (see windows.hpp): setup.c's pages, the ones
// a terminal has a use for, over the same prefs by their hexchat.conf
// names. Like the GUI's, it applies as you go and saves on close.

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "bridge.h"
#include "windows.hpp"

namespace nx::tui {

namespace {

enum class T { Head, Toggle, Text, Number, Menu, Label, TuiToggle };
struct Setting {
    T kind;
    const char* label;
    const char* pref = nullptr;              // hexchat.conf name
    std::vector<const char*> choices = {};   // Menu
};
struct Page { const char* name; std::vector<Setting> settings; };

// setup.c's pages, in its order. Left out on purpose: fonts, background
// image, zoom, opacity, tray, notifications, spell check, icons, nick box —
// the terminal owns those.
const std::vector<Page>& pages() {
    static const std::vector<Page> p = {
        {"Appearance", {
            {T::Head, "Text Box"},
            {T::Toggle, "Colored nick names", "text_color_nicks"},
            {T::Toggle, "Indent nick names", "text_indent"},
            {T::Toggle, "Show marker line", "text_show_marker"},
            {T::Toggle, "Show day separators", "text_day_sep"},
            {T::Head, "Timestamps"},
            {T::Toggle, "Enable timestamps", "stamp_text"},
            {T::Text, "Timestamp format", "stamp_text_format"},
            {T::TuiToggle, "Repeat the time on every line", "stamp_repeat=0"},
            {T::TuiToggle, "Nicks in <brackets>", "brackets=0"},
            {T::TuiToggle, "Joins, modes and server text in a quieter colour", "quiet_status=1"},
            {T::TuiToggle, "My own nick quieter than others'", "quiet_own=1"},
            {T::TuiToggle, "Show *bold*, _italic_ and `code` as typed", "markup=1"},
            {T::TuiToggle, "My nick in colour where others mention it", "my_nick=1"},
            {T::TuiToggle, "A blank line where the talk paused", "pause_gap=1"},
            {T::TuiToggle, "Narrow terminal: nick and time above the text", "one_column_stacked=1"},
            {T::Head, "Terminal"},
            {T::TuiToggle, "Underline what's under the mouse (restart)", "hover=1"},
            {T::TuiToggle, "Links you can click (OSC 8)", "links=1"},
        }},
        {"Input box", {
            {T::Head, "Input Box"},
            {T::Toggle, "Show commands while typing /", "gui_input_cmdhelp"},
            {T::Number, "Ask before pasting more than (lines)", "gui_input_paste_max"},
            {T::Toggle, "Show who is typing", "gui_typing_show"},
            {T::Toggle, "Say in the chat when someone types", "gui_typing_chat"},
            {T::Toggle, "Let others see when I'm typing", "gui_typing_send"},
            {T::Toggle, "Show what replies answer", "gui_replies"},
            {T::Head, "Nick Completion"},
            {T::Text, "Nick completion suffix", "completion_suffix"},
            {T::Menu, "Nick completion sorted", "completion_sort", {"A-Z", "Last-spoke order"}},
            {T::Number, "Nick completion amount", "completion_amount"},
        }},
        {"User list", {
            {T::Head, "User List"},
            {T::Menu, "User list sorted by", "gui_ulist_sort", {"A-Z, ops first", "A-Z", "Z-A, ops last", "Z-A", "Unsorted"}},
            {T::Head, "Away Tracking"},
            {T::Toggle, "Track away users and mark them", "away_track"},
            {T::Number, "On channels smaller than", "away_size_max"},
            {T::Head, "Action Upon Double Click"},
            {T::Text, "Execute command", "gui_ulist_doubleclick"},
        }},
        {"Channel switcher", {
            {T::Head, "Channel Switcher"},
            {T::Menu, "Switcher type", "gui_tab_layout", {"Tabs", "Tree", "Tree"}},
            {T::Menu, "Show channel switcher at", "gui_tab_pos",
             {"Left", "Left (upper)", "Left (lower)", "Right (upper)", "Right (lower)", "Top", "Bottom", "Hidden"}},
            {T::Toggle, "Open a new tab for private messages", "gui_autoopen_dialog"},
            {T::Toggle, "Sort tabs in alphabetical order", "gui_tab_sort"},
            {T::Toggle, "Middle click to close tab", "gui_tab_middleclose"},
            {T::Menu, "Focus new tabs", "gui_tab_newtofront", {"Never", "Always", "Only requested tabs"}},
            {T::Menu, "Placement of notices", "irc_notice_pos", {"Automatic", "In an extra tab", "In the front tab"}},
            {T::Number, "Shorten tab labels to", "gui_tab_trunc"},
            {T::Toggle, "Show the number of unread messages", "gui_tab_unread"},
        }},
        {"General", {
            {T::Head, "Default Messages"},
            {T::Text, "Quit", "irc_quit_reason"},
            {T::Text, "Leave channel", "irc_part_reason"},
            {T::Text, "Away", "away_reason"},
            {T::Head, "Away"},
            {T::Toggle, "Show away once", "away_show_once"},
            {T::Toggle, "Automatically unmark away", "away_auto_unmark"},
            {T::Head, "Miscellaneous"},
            {T::Toggle, "Display MODEs in raw form", "irc_raw_modes"},
            {T::Toggle, "WHOIS on notify", "notify_whois_online"},
            {T::Toggle, "Hide join and part messages", "irc_conf_mode"},
            {T::Number, "Only show joins/parts of who spoke in (min)", "irc_smart_filter"},
            {T::Toggle, "Group joins and parts into one line", "irc_group_joinpart"},
            {T::Toggle, "Hide nick change messages", "irc_hide_nickchange"},
        }},
        {"Alerts", {
            {T::Head, "Alerts"},
            {T::Toggle, "Beep on channel messages", "input_beep_chans"},
            {T::Toggle, "Beep on private messages", "input_beep_priv"},
            {T::Toggle, "Beep on highlights", "input_beep_hilight"},
            {T::Toggle, "Omit alerts when marked away", "away_omit_alerts"},
            {T::Head, "Highlighted Messages"},
            {T::Label, "Your nick, and also (comma-separated, wildcards ok):"},
            {T::Text, "Extra words to highlight", "irc_extra_hilight"},
            {T::Text, "Nick names not to highlight", "irc_no_hilight"},
            {T::Text, "Nick names to always highlight", "irc_nick_hilight"},
        }},
        {"Logging", {
            {T::Head, "Logging"},
            {T::Toggle, "Display scrollback from previous session", "text_replay"},
            {T::Number, "Scrollback lines", "text_max_lines"},
            {T::Toggle, "Enable logging of conversations to disk", "irc_logging"},
            {T::Text, "Log filename", "irc_logmask"},
            {T::Label, "%s=Server %c=Channel %n=Network."},
            {T::Head, "Timestamps"},
            {T::Toggle, "Insert timestamps in logs", "stamp_log"},
            {T::Text, "Log timestamp format", "stamp_log_format"},
            {T::Head, "URLs"},
            {T::Toggle, "Enable logging of URLs to disk", "url_logging"},
            {T::Toggle, "Enable URL grabber", "url_grabber"},
            {T::Number, "Maximum number of URLs to grab", "url_grabber_limit"},
        }},
        {"Advanced", {
            {T::Head, "Auto Copy Behavior"},
            {T::Toggle, "Automatically copy selected text", "text_autocopy_text"},
            {T::Toggle, "Automatically include timestamps", "text_autocopy_stamp"},
            {T::Toggle, "Automatically include color information", "text_autocopy_color"},
            {T::Head, "Miscellaneous"},
            {T::Text, "Real name", "irc_real_name"},
            {T::Toggle, "Use server time if supported", "irc_cap_server_time"},
            {T::Toggle, "Get missed messages on join", "irc_chathistory"},
            {T::Toggle, "Reconnect automatically on disconnect", "net_auto_reconnect"},
            {T::Number, "Auto reconnect delay", "net_reconnect_delay"},
            {T::Number, "Auto join delay", "irc_join_delay"},
            {T::Menu, "Ban Type", "irc_ban_type", {"*!*@*.host", "*!*@domain", "*!*user@*.host", "*!*user@domain"}},
        }},
        {"Network setup", {
            {T::Head, "Your Address"},
            {T::Text, "Bind to", "net_bind_host"},
            {T::Head, "File Transfers"},
            {T::Toggle, "Get my address from the IRC server", "dcc_ip_from_server"},
            {T::Text, "DCC IP address", "dcc_ip"},
            {T::Number, "First DCC listen port", "dcc_port_first"},
            {T::Number, "Last DCC listen port", "dcc_port_last"},
            {T::Head, "Proxy Server"},
            {T::Text, "Hostname", "net_proxy_host"},
            {T::Number, "Port", "net_proxy_port"},
            {T::Menu, "Type", "net_proxy_type", {"(Disabled)", "Wingate", "SOCKS4", "SOCKS5", "HTTP", "Auto"}},
            {T::Menu, "Use proxy for", "net_proxy_use", {"All connections", "IRC server only", "DCC only"}},
            {T::Toggle, "Use authentication (HTTP / SOCKS5)", "net_proxy_auth"},
            {T::Text, "Username", "net_proxy_user"},
            {T::Text, "Password", "net_proxy_pass"},
        }},
        {"File transfers", {
            {T::Head, "Files and Directories"},
            {T::Menu, "Auto accept file offers", "dcc_auto_recv", {"No: list them", "Ask for confirmation", "Save without asking"}},
            {T::Text, "Download files to", "dcc_dir"},
            {T::Text, "Move completed files to", "dcc_completed_dir"},
            {T::Toggle, "Save nick name in filenames", "dcc_save_nick"},
            {T::Head, "Maximum Speeds (bytes per second, 0 = no limit)"},
            {T::Number, "One upload", "dcc_max_send_cps"},
            {T::Number, "One download", "dcc_max_get_cps"},
            {T::Number, "All uploads combined", "dcc_global_max_send_cps"},
            {T::Number, "All downloads combined", "dcc_global_max_get_cps"},
        }},
        {"Identd", {
            {T::Head, "Identd Server"},
            {T::Toggle, "Enabled", "identd_server"},
            {T::Number, "Port", "identd_port"},
        }},
    };
    return p;
}

}  // namespace

void Preferences::open() {
    std::vector<std::string> names;
    for (const auto& p : pages()) names.push_back(p.name);
    panel_.list_width = 20;
    panel_.set_list(std::move(names), page_, "Settings");
    panel_.set_buttons({{"Close", B_CLOSE}});
    load(page_);
    panel_.open("Preferences");
    panel_.set_part(Panel::Part::List);
    panel_.status = "Changes apply right away · Tab moves between the lists";
}

void Preferences::close() {
    nx_core_prefs_save();
    panel_.close();
}

void Preferences::load(int page) {
    page_ = page;
    std::vector<Field> fields;
    int id = 0;
    for (const Setting& s : pages()[static_cast<std::size_t>(page)].settings) {
        Field f;
        f.label = s.label;
        f.id = id++;
        switch (s.kind) {
        case T::Head:   f.kind = Field::Kind::Heading; break;
        case T::Label:  f.kind = Field::Kind::Heading; break;
        case T::Toggle: f.kind = Field::Kind::Toggle; f.on = nx_core_pref_int(s.pref) != 0; break;
        case T::TuiToggle: {
            // tui.conf: unset is the default given as "1"/"0" in pref after a '='
            f.kind = Field::Kind::Toggle;
            const std::string_view spec = s.pref;
            const auto eq = spec.find('=');
            const std::string key{spec.substr(0, eq)};
            const char* v = nx_core_tui_get(key.c_str());
            const char d = eq == std::string_view::npos ? '0' : spec[eq + 1];
            const char c = v && *v ? v[0] : d;
            f.on = c == '1' || c == 't' || c == 'y';
            break;
        }
        case T::Text:
            f.kind = std::string_view(s.pref).find("pass") != std::string_view::npos ? Field::Kind::Password : Field::Kind::Text;
            f.text = nx_core_pref_str(s.pref);
            break;
        case T::Number: f.kind = Field::Kind::Text; f.text = std::to_string(nx_core_pref_int(s.pref)); break;
        case T::Menu:
            f.kind = Field::Kind::Choice;
            for (const char* c : s.choices) f.choices.push_back(c);
            f.choice = std::clamp(nx_core_pref_int(s.pref), 0, static_cast<int>(f.choices.size()) - 1);
            break;
        }
        fields.push_back(std::move(f));
    }
    panel_.form.set(std::move(fields));
    panel_.form.label_width = 40;
}

// Write the form back to the prefs, live (the GUI's Preferences do too).
void Preferences::apply() {
    const auto& settings = pages()[static_cast<std::size_t>(page_)].settings;
    for (const Field& f : panel_.form.fields()) {
        const Setting& s = settings[static_cast<std::size_t>(f.id)];
        if (!s.pref) continue;
        switch (s.kind) {
        case T::Toggle: nx_core_pref_set_int(s.pref, f.on); break;
        case T::TuiToggle: {
            const std::string_view spec = s.pref;
            const std::string key{spec.substr(0, spec.find('='))};
            nx_core_tui_set(key.c_str(), f.on ? "1" : "0");
            break;
        }
        case T::Text: nx_core_pref_set_str(s.pref, f.text.c_str()); break;
        case T::Number: {
            char* end = nullptr;
            const long v = std::strtol(f.text.c_str(), &end, 10);
            if (end && end != f.text.c_str()) nx_core_pref_set_int(s.pref, static_cast<int>(v));
            break;
        }
        case T::Menu: nx_core_pref_set_int(s.pref, f.choice); break;
        default: break;
        }
    }
    changed = true;
}

bool Preferences::handle(Panel::Outcome o) {
    switch (o) {
    case Panel::Outcome::Closed: apply(); nx_core_prefs_save(); return true;
    case Panel::Outcome::Picked: apply(); load(panel_.list_sel()); return false;
    case Panel::Outcome::Changed: apply(); return false;
    case Panel::Outcome::Activated: panel_.set_part(Panel::Part::Body); return false;
    case Panel::Outcome::Button:
        if (panel_.button() == B_CLOSE) { apply(); close(); return true; }
        return false;
    default: return false;
    }
}

bool Preferences::key(const maya::KeyEvent& k) { return handle(panel_.key(k)); }
bool Preferences::mouse(const maya::MouseEvent& m) { return handle(panel_.mouse(m)); }

}  // namespace nx::tui
