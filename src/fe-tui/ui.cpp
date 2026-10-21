// nexchat TUI — the app: state, key and mouse routing, menus, the view.
//
// Implements bridge.h's nx_ui_* (called by the C glue, fe-tui.c) and calls
// its nx_core_* back. Never includes a core header. See docs/tui-design.md.

#include <signal.h>
#include <strings.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <maya/maya.hpp>
#include <maya/screen.hpp>
#include <maya/style/schemes.hpp>

#include "../bridge.h"
#include "../bridge-bnc.h"
#include "bars.hpp"
#include "chantree.hpp"
#include "cmdpopup.hpp"
#include "chat.hpp"
#include "dialog.hpp"
#include "inputline.hpp"
#include "ircfmt.hpp"
#include "layout.hpp"
#include "menu.hpp"
#include "switcher.hpp"
#include "theme.hpp"
#include "userlist.hpp"
#include "windows.hpp"

#ifndef NX_VERSION
#define NX_VERSION ""
#endif

namespace nx::tui {
namespace {

using maya::MouseButton;
using maya::MouseEventKind;
using maya::SpecialKey;

// ── the model ───────────────────────────────────────────────────────────

struct Network {
    std::string name, nick;
    bool connected = false, connecting = false, away = false;
    double lag = -1;
};

struct Buffer {
    nx_key key = 0, net = 0;
    ChanKind kind = ChanKind::Channel;
    std::string name;
    ChatBuffer chat{5000};
    ChatView view;
    std::string topic, modes, typing;
    std::vector<UserEntry> users;
    int total = 0, ops = 0;
    Activity activity = Activity::None;
    int unread = 0;
    bool joined = true;
    std::string draft;            // what was typed here when we switched away
    std::size_t draft_cursor = 0;
    std::string reply_to;         // the msgid the next line answers ("" = none)
    // history from before the top line, collected until its batch ends
    std::vector<std::pair<std::string, std::time_t>> older;
    bool older_asked = false;     // a fetch is out (the "loading" line shows)
    bool older_none = false;      // the server has nothing older
    // the join/part line being folded (nx_ui_joinpart)
    struct JoinPart {
        LineId line = 0;
        bool first = false, pending = false;
        std::vector<std::string> joined, left;
    } joinpart;
};

// Menu action ids.
enum Action : int {
    A_None, A_Quit, A_NetList, A_Prefs, A_IgnoreList, A_FriendsList, A_Transfers, A_RawLog, A_Bouncer, A_Disconnect, A_Reconnect, A_Close, A_Away, A_ChanList, A_Nick,
    A_ToggleTree, A_ToggleUsers, A_ToggleTopic, A_ToggleMenu, A_ToggleStamps,
    A_ClearText, A_Search, A_ResetMarker, A_Copy, A_Paste, A_SelectAll,
    A_Prev, A_Next, A_NextActive, A_GoTo, A_Palette,
    A_BanList, A_Mentions, A_Plugins, A_UrlGrabber,
    A_Theme, A_ThemeNative, A_Keys, A_About,
    A_Join, A_Part, A_Topic, A_Reply, A_JumpUnread,
    // context menus
    A_NickPopupBase = 1000,      // + popup_list index
    A_PluginMenuBase = 100000,   // + /MENU item index
    A_NickQuery = 900, A_NickWhois, A_NickCopy, A_NickInsert, A_NickUnignore, A_NickReply,
    A_UrlOpen = 950, A_UrlCopy, A_UrlPreview,
    A_ChanJoin = 960, A_ChanCopy,
    A_TreeClose = 970, A_TreeFocus, A_TreeCollapse, A_TreeDisconnect, A_TreeReconnect,
    A_InputCut = 980, A_InputCopy, A_InputPaste, A_InputSelectAll, A_InputClear,
};

// Where something was drawn this frame, for the mouse.
enum class Area { None, MenuBar, Topic, Tree, TreeDivider, Strip, Chat, UsersDivider, Users, Status, Input };
struct Rect { int x = 0, y = 0, w = 0, h = 0; bool has(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

class Ui;
Ui& ui();

class Ui {
public:
    // ── the device ──────────────────────────────────────────────────────
    std::optional<maya::Screen> screen;
    bool dirty = true;

    // ── state ───────────────────────────────────────────────────────────
    std::map<nx_key, Network> nets;
    std::vector<nx_key> net_order;
    std::map<nx_key, std::unique_ptr<Buffer>> bufs;
    std::vector<nx_key> buf_order;      // creation order (kept per network)
    nx_key cur = 0;

    ChanTree tree;
    UserList users;
    InputLine input;
    Switcher switcher;
    MenuBar menubar;
    Menu ctx;                           // the open context menu
    int ctx_kind = 0;                   // what it's about (see open_*_menu)
    std::string ctx_word;               // the nick / url / channel it's about
    nx_key ctx_buf = 0;
    Dialog dialog;
    NetworkList netlist;
    Preferences prefs;
    ChannelList chanlist;
    PeopleList people;
    Transfers transfers;
    Bouncer bouncer;
    Tools tools;
    // a Raw Log buffer per network, made the first time it's opened
    std::map<nx_key, nx_key> rawlog_of;          // net → our own buffer key
    nx_key next_own_key = 1;                      // keys for UI-only buffers (odd: never a pointer)
    struct PendingDialog { int kind; std::string title, text, initial; nx_str_done sd = nullptr; nx_yes_done yd = nullptr; void* ud = nullptr; bool error = false; std::function<void()> on_yes = nullptr; };
    std::deque<PendingDialog> dialogs;  // queued behind the open one
    PendingDialog dialog_now;
    WordCompleter completer{[this](std::string_view w, bool at_start) { return complete(w, at_start); }};

    ChatStyle chat_style;
    MainLayout layout;
    bool show_menu = true, show_topic = true;
    Pane focus = Pane::Input;
    std::string clipboard;              // our own copy, for middle-click paste
    std::string toast;                  // a fe_message line in the status bar
    std::chrono::steady_clock::time_point toast_until{};
    std::string theme_name = "native";

    // geometry of the last frame
    int W = 80, H = 24;
    // the terminal window has focus (?1004 reports; assumed until told)
    bool focused = true;
    // a script changed /MENU items while the menu bar was open
    bool menus_stale = false;
    Rect r_menu, r_topic, r_tree, r_tree_div, r_strip, r_chat, r_users_div, r_users, r_status, r_input;
    // mouse state
    enum class Drag { None, Select, TreeDiv, UsersDiv, Input } drag = Drag::None;
    std::chrono::steady_clock::time_point last_click{};
    int last_click_x = -1, last_click_y = -1, clicks = 0;

    Ui() {
        input.set_completer([this](std::string_view l, std::size_t c, int s) { return completer(l, c, s); });
        const int lw = nx_core_pref_int("gui_pane_left_size");
        const int rw = nx_core_pref_int("gui_pane_right_size");
        if (lw >= 10 && lw <= 60) layout.tree_width = lw / 8 > 10 ? std::min(40, lw / 8) : layout.tree_width;
        if (rw >= 10 && rw <= 60) layout.users_width = rw / 8 > 10 ? std::min(40, rw / 8) : layout.users_width;
        chat_style.show_stamp = nx_core_pref_int("stamp_text") != 0;
        if (const char* f = nx_core_pref_str("stamp_text_format"); f && *f) chat_style.stamp_format = f;
        chat_style.indent = nx_core_pref_int("text_indent") != 0;
        chat_style.show_marker = nx_core_pref_int("text_show_marker") != 0;
        if (const char* t = nx_core_tui_get("theme"); t && *t) set_theme(t, false);
        apply_switcher_prefs();
        build_menubar();
    }

    // The GUI's switcher prefs: gui_tab_layout (0 Tabs, 2 Tree) and
    // gui_tab_pos (1/2 left, 3/4 right, 5 top, 6 bottom, 7 hidden).
    void apply_switcher_prefs() {
        const int lay = nx_core_pref_int("gui_tab_layout");
        const int pos = nx_core_pref_int("gui_tab_pos");
        using S = MainLayout::Switcher;
        if (lay == 0) layout.switcher = pos == 5 ? S::TabsTop : S::TabsBottom;   // the GUI's tabs: top or bottom
        else layout.switcher = pos == 3 || pos == 4 ? S::TreeRight : S::TreeLeft;
        if (pos == 7) layout.show_tree = false;
        layout.tab_trunc = std::max(0, nx_core_pref_int("gui_tab_trunc"));
    }

    // ── buffers ─────────────────────────────────────────────────────────
    Buffer* buf(nx_key k) { auto it = bufs.find(k); return it == bufs.end() ? nullptr : it->second.get(); }
    Buffer* current() { return buf(cur); }

    // Display order: each network's server buffer, then its channels and
    // queries in creation order (the GUI's tree).
    std::vector<nx_key> ordered() const {
        std::vector<nx_key> out;
        std::vector<nx_key> nets_seen;
        for (nx_key k : buf_order) {
            const Buffer& b = *bufs.at(k);
            if (std::find(nets_seen.begin(), nets_seen.end(), b.net) == nets_seen.end()) nets_seen.push_back(b.net);
        }
        for (nx_key n : nets_seen) {
            for (nx_key k : buf_order) if (bufs.at(k)->net == n && bufs.at(k)->kind == ChanKind::Server) out.push_back(k);
            for (nx_key k : buf_order) if (bufs.at(k)->net == n && bufs.at(k)->kind != ChanKind::Server) out.push_back(k);
        }
        return out;
    }

    std::vector<ChanEntry> entries() const {
        std::vector<ChanEntry> e;
        for (nx_key k : ordered()) {
            const Buffer& b = *bufs.at(k);
            bool connected = b.joined;
            if (b.kind == ChanKind::Server) {
                auto n = nets.find(b.net);
                connected = n != nets.end() && n->second.connected;
            }
            e.push_back({k, b.name, b.kind, b.activity, b.unread, connected});
        }
        return e;
    }

    void sync_tree() {
        std::vector<ChanEntry> e = entries();
        // keep the collapse state the user set
        for (auto& x : e)
            for (const auto& old : tree.entries())
                if (old.key == x.key) x.collapsed = old.collapsed;
        tree.set(std::move(e));
        if (cur) tree.select_key(cur);
    }

    void sync_users() {
        if (Buffer* b = current()) users.set(b->users, b->ops);
        else users.set({});
    }

    void switch_to(nx_key k, bool tell_core = true) {
        Buffer* nb = buf(k);
        if (!nb) return;
        if (Buffer* ob = current(); ob && ob != nb) {
            ob->draft = input.text();
            ob->draft_cursor = input.cursor();
            ob->chat.mark_read();
            ob->view.select_clear();
        }
        cur = k;
        nb->activity = Activity::None;
        nb->unread = 0;
        input.set(nb->draft, nb->draft_cursor);
        sync_tree();
        sync_users();
        if (find_open && find_buf != cur) find_run(0);   // the find bar follows you
        if (tell_core && !is_own(k)) nx_core_focus(k);
        if (screen) screen->set_title("nexchat: " + nb->name);
        dirty = true;
    }

    void step_buffer(int d) {
        auto o = ordered();
        if (o.empty()) return;
        auto it = std::find(o.begin(), o.end(), cur);
        long i = it == o.end() ? 0 : it - o.begin();
        const long n = static_cast<long>(o.size());
        switch_to(o[static_cast<std::size_t>(((i + d) % n + n) % n)]);
    }

    void next_active() {
        tree.select_key(cur);
        if (tree.next_active()) switch_to(*tree.selected());
    }

    // ── completion: the core decides what's a nick, channel, command ───
    std::vector<std::string> complete(std::string_view w, bool at_start) {
        std::string out(8192, '\0');
        const int n = nx_core_complete(core_key(cur), std::string(w).c_str(), at_start, out.data(), static_cast<int>(out.size()));
        std::vector<std::string> r;
        const char* p = out.data();
        for (int i = 0; i < n && *p; ++i) { r.emplace_back(p); p += std::strlen(p) + 1; }
        return r;
    }

    // ── the Raw Log: a buffer of our own under the network ────────────────────
    // Its key is odd, so it can't be a session* the core would know.
    bool is_own(nx_key k) const { return (k & 1) != 0; }

    // Close a buffer: ours we drop here; the core's it closes (part, …),
    // and it tells us with fe_session_callback.
    void close_buffer(nx_key k) {
        if (!is_own(k)) { nx_core_close(k); return; }
        for (auto it = rawlog_of.begin(); it != rawlog_of.end(); ++it)
            if (it->second == k) { rawlog_of.erase(it); break; }
        nx_ui_buffer_free(k);
    }

    // Where a command typed in buffer k runs: ours belong to their
    // network, so its server tab (or any tab on it).
    nx_key core_key(nx_key k) const {
        if (!is_own(k)) return k;
        auto b = bufs.find(k);
        if (b == bufs.end()) return 0;
        for (nx_key o : buf_order) {
            const Buffer& x = *bufs.at(o);
            if (!is_own(o) && x.net == b->second->net && x.kind == ChanKind::Server) return o;
        }
        for (nx_key o : buf_order) if (!is_own(o) && bufs.at(o)->net == b->second->net) return o;
        return 0;
    }

    void open_rawlog(nx_key net) {
        auto it = rawlog_of.find(net);
        if (it == rawlog_of.end()) {
            const nx_key k = (next_own_key += 2);
            auto b = std::make_unique<Buffer>();
            b->key = k; b->net = net; b->kind = ChanKind::Other; b->name = "Raw Log";
            b->chat.set_max_lines(5000);
            bufs[k] = std::move(b);
            buf_order.push_back(k);
            it = rawlog_of.emplace(net, k).first;
        }
        switch_to(it->second, false);
    }

    void rawlog(nx_key net, const char* line, bool out) {
        auto it = rawlog_of.find(net);
        if (it == rawlog_of.end()) return;          // only once it's been opened (the GUI's too)
        if (Buffer* b = buf(it->second)) {
            std::string l = out ? "\00304<<\t" : "\00303>>\t";
            l += line;
            b->chat.add(l, std::time(nullptr));
            dirty = true;
        }
    }

    // ── theme ───────────────────────────────────────────────────────────────────
    // The Network List index of the current buffer's network (-1 none):
    // the Bouncer window starts from it, as the GUI's does.
    int cur_net_index() {
        return nx_core_buffer_netlist_index(core_key(cur));
    }

    // A pref the window shows changed in Preferences: show it now.
    void after_prefs() {
        if (!prefs.changed) return;
        prefs.changed = false;
        prefs_seen = nx_core_prefs_version();
        apply_prefs();
    }

    // ...or anywhere else (/set, a script): the fingerprint moved.
    unsigned int prefs_seen = 0;
    void check_prefs() {
        const unsigned int v = nx_core_prefs_version();
        if (v == prefs_seen) return;
        prefs_seen = v;
        apply_prefs();
    }

    // Every pref the window draws from, read again: at start and after any
    // change, so Preferences (and /set) take effect at once.
    void apply_prefs() {
        chat_style.show_stamp = nx_core_pref_int("stamp_text") != 0;
        if (const char* f = nx_core_pref_str("stamp_text_format"); f && *f) chat_style.stamp_format = f;
        chat_style.indent = nx_core_pref_int("text_indent") != 0;
        chat_style.show_marker = nx_core_pref_int("text_show_marker") != 0;
        {
            const char* sr = nx_core_tui_get("stamp_repeat");
            chat_style.stamp_repeat = sr && (sr[0] == '1' || sr[0] == 't' || sr[0] == 'y');
            const char* br = nx_core_tui_get("brackets");
            chat_style.brackets = br && (br[0] == '1' || br[0] == 't' || br[0] == 'y');
            // on unless set off (links=0)
            auto on = [](const char* key) {
                const char* v = nx_core_tui_get(key);
                return !(v && (v[0] == '0' || v[0] == 'f' || v[0] == 'n'));
            };
            chat_style.links = on("links");
            chat_style.quiet_status = on("quiet_status");
            chat_style.quiet_own = on("quiet_own");
            chat_style.markup = on("markup");
            chat_style.my_nick = on("my_nick");
            // a blank row after pause_minutes (20) of quiet; pause_gap=0 turns it off
            const char* pm = nx_core_tui_get("pause_minutes");
            const long mins = pm && *pm ? std::max(1L, std::atol(pm)) : 20;
            chat_style.pause_gap = on("pause_gap") ? mins * 60 : 0;
            // under this many columns the chat is one column (0: never)
            const char* oc = nx_core_tui_get("one_column_below");
            chat_style.single_col_below = oc && *oc ? std::max(0, std::atoi(oc)) : 60;
            // one_column_style=inline: "16:04 alice text" per line (default: stacked)
            const char* os = nx_core_tui_get("one_column_style");
            chat_style.one_col = os && std::string_view(os) == "inline" ? ChatStyle::OneCol::Inline
                                                                        : ChatStyle::OneCol::Stacked;
        }
        for (auto& [k, b] : bufs) {
            b->chat.set_max_lines(static_cast<std::size_t>(std::max(100, nx_core_pref_int("text_max_lines"))));
            for (std::size_t i = 0; i < b->chat.size(); ++i) b->chat[i].cached_cols = -1;   // re-wrap
        }
        // the user list's order
        for (auto& [k, b] : bufs) if (b->kind == ChanKind::Channel) nx_core_users_resync(k);
        apply_switcher_prefs();
        sync_tree();
        build_menubar();
        dirty = true;
    }

    void set_theme(std::string_view name, bool save = true) {
        const maya::Theme* t = nullptr;
        if (name == "native") t = &maya::theme::native;
        for (const auto& s : maya::theme::schemes) if (name == s.name) t = s.theme;
        if (!t) return;
        theme_name = std::string(name);
        maya::app_set_theme(*t);
        if (save) nx_core_tui_set("theme", theme_name.c_str());
        dirty = true;
    }

    // ── menus ───────────────────────────────────────────────────────────
    void build_menubar() {
        auto chk = [](bool on) { return on ? 1 : 0; };
        menubar.set({
            {"nexchat", {
                {"Network List…", "^S", A_NetList},
                {"Preferences…", "", A_Prefs},
                MenuItem::sep(),
                {"Quit", "^Q", A_Quit},
            }},
            {"View", {
                {"Menu Bar", "", A_ToggleMenu, chk(show_menu)},
                {"Topic Bar", "", A_ToggleTopic, chk(show_topic)},
                {"Channel Tree", "", A_ToggleTree, chk(layout.show_tree)},
                {"User List", "^L", A_ToggleUsers, chk(layout.show_users)},
                {"Timestamps", "", A_ToggleStamps, chk(chat_style.show_stamp)},
                MenuItem::sep(),
                {"Theme", "", A_None, -1, true, theme_items()},
            }},
            {"Server", {
                {"Disconnect", "", A_Disconnect},
                {"Reconnect", "", A_Reconnect},
                {"Join a Channel…", "", A_Join},
                {"Channel List", "", A_ChanList},
                {"Bouncer…", "", A_Bouncer},
                MenuItem::sep(),
                {"Marked Away", "", A_Away, chk(cur_net() && cur_net()->away)},
                {"Change Nick…", "", A_Nick},
                MenuItem::sep(),
                {"Reply to a Message…", "Alt-R", A_Reply},
            }},
            {"Window", {
                {"Go to…", "^G", A_GoTo},
                {"Command Palette…", "^P", A_Palette},
                {"Previous", "Alt-←", A_Prev},
                {"Next", "Alt-→", A_Next},
                {"Next Active", "Alt-A", A_NextActive},
                MenuItem::sep(),
                {"Ban List", "", A_BanList},
                {"Channel List", "", A_ChanList},
                {"File Transfers", "", A_Transfers},
                {"Friends List", "", A_FriendsList},
                {"Ignore List", "", A_IgnoreList},
                {"Mentions", "", A_Mentions},
                {"Plugins and Scripts", "", A_Plugins},
                {"Raw Log", "", A_RawLog},
                {"URL Grabber", "", A_UrlGrabber},
                MenuItem::sep(),
                {"Search…", "^F", A_Search},
                {"Copy Selection", "", A_Copy},
                {"Move to Marker Line", "Alt-J", A_JumpUnread},
                {"Reset Marker Line", "", A_ResetMarker},
                {"Clear Text", "", A_ClearText},
                MenuItem::sep(),
                {"Close", "^W", A_Close},
            }},
            {"Help", {
                {"Keyboard Shortcuts", "F1", A_Keys},
                {"About", "", A_About},
            }},
        });
        auto bar = menubar.entries();
        add_plugin_menus(bar);
        menubar.set(std::move(bar));
    }

    // /MENU items from scripts: "Window/Thing" goes into Window, a new
    // top-level name becomes its own menu, deeper paths nest
    void add_plugin_menus(std::vector<MenuBar::Entry>& bar) {
        const std::size_t help_at = bar.empty() ? 0 : bar.size() - 1;   // new menus go before Help
        auto strip = [](std::string s) { s.erase(std::remove(s.begin(), s.end(), '_'), s.end()); return s; };
        for (int i = 0;; ++i) {
            const char *path = nullptr, *cmd = nullptr;
            int state = -1, enabled = 1;
            if (!nx_core_plugin_menu_get(i, &path, &cmd, &state, &enabled)) break;
            std::vector<std::string> parts;
            for (std::string_view p = path; !p.empty();) {
                const auto s = p.find('/');
                parts.push_back(strip(std::string(p.substr(0, s))));
                if (s == std::string_view::npos) break;
                p.remove_prefix(s + 1);
            }
            if (parts.empty()) continue;
            // the top level: an existing menu or a new one
            auto it = std::find_if(bar.begin(), bar.end(), [&](const MenuBar::Entry& e) { return e.title == parts[0]; });
            if (it == bar.end()) {
                it = bar.insert(bar.begin() + static_cast<long>(std::min(help_at, bar.size())), MenuBar::Entry{parts[0], {}});
            }
            if (parts.size() == 1) continue;   // just the menu itself
            std::vector<MenuItem>* level = &it->items;
            for (std::size_t d = 1; d + 1 < parts.size(); ++d) {
                auto sub = std::find_if(level->begin(), level->end(), [&](const MenuItem& m) { return m.label == parts[d]; });
                if (sub == level->end()) { level->push_back({parts[d], "", A_None}); sub = level->end() - 1; }
                level = &sub->sub;
            }
            const std::string& leaf = parts.back();
            if (leaf == "-") { level->push_back(MenuItem::sep()); continue; }
            // an item with no command is a submenu its children fill
            if (!*cmd && state < 0) {
                if (std::none_of(level->begin(), level->end(), [&](const MenuItem& m) { return m.label == leaf; }))
                    level->push_back({leaf, "", A_None});
                continue;
            }
            level->push_back({leaf, "", A_PluginMenuBase + i, state, enabled != 0});
        }
    }

    std::vector<MenuItem> theme_items() {
        std::vector<MenuItem> t;
        t.push_back({"native (the terminal's)", "", A_ThemeNative, theme_name == "native" ? 1 : 0});
        t.push_back({"Choose a scheme…", "", A_Theme});
        return t;
    }

    Network* cur_net() {
        Buffer* b = current();
        if (!b) return nullptr;
        auto it = nets.find(b->net);
        return it == nets.end() ? nullptr : &it->second;
    }

    // popup.conf labels: "_Op~gtk-go-up~" → "Op" (a GTK mnemonic, an icon name)
    static std::string popup_label(std::string_view s) {
        if (auto t = s.find('~'); t != std::string_view::npos) s = s.substr(0, t);
        std::string out;
        for (char c : s) if (c != '_') out += c;
        return out;
    }

    void open_nick_menu(nx_key b, const std::string& nick, int x, int y) {
        std::vector<MenuItem> items;
        items.push_back({nick, "", A_None, -1, false});
        items.push_back(MenuItem::sep());
        items.push_back({"Insert in Input", "", A_NickInsert});
        items.push_back({"Copy Nick", "", A_NickCopy});
        // answer their last message here (+draft/reply)
        if (nx_core_pref_int("gui_replies")) items.push_back({"Reply", "", A_NickReply});
        items.push_back(MenuItem::sep());
        // popup.conf, the GUI's nick menu (SUB / ENDSUB / SEP / TOGGLE)
        std::vector<std::vector<MenuItem>*> stack{&items};
        const int n = nx_core_popup_count();
        for (int i = 0; i < n; ++i) {
            const char *name, *cmd;
            nx_core_popup_get(i, &name, &cmd);
            std::string nm = name;
            if (!strncasecmp(name, "SUB", 3) && nm.size() == 3) {
                stack.back()->push_back({popup_label(cmd), "", A_None});
                stack.push_back(&stack.back()->back().sub);
            } else if (!strncasecmp(name, "ENDSUB", 6)) {
                if (stack.size() > 1) stack.pop_back();
            } else if (!strncasecmp(name, "SEP", 3)) {
                stack.back()->push_back(MenuItem::sep());
            } else if (!strncasecmp(name, "TOGGLE", 6)) {
                continue;   // a pref toggle; not in a TUI nick menu
            } else if (!std::strcmp(cmd, "ignore %s!*@* ALL")
                       && nx_core_ignored((nick + "!*@*").c_str())) {
                // the default Ignore item (hexchat.c), already ignored: undo it,
                // as the GUI's menu.c does
                stack.back()->push_back({"Unignore", "", A_NickUnignore});
            } else {
                stack.back()->push_back({popup_label(nm), "", A_NickPopupBase + i});
            }
        }
        // no submenu left empty, no separator first/last/doubled
        tidy(items);
        ctx_kind = 1; ctx_word = nick; ctx_buf = b;
        ctx.open(std::move(items), x, y);
    }

    static void tidy(std::vector<MenuItem>& v) {
        for (auto& it : v) if (!it.sub.empty()) tidy(it.sub);
        std::vector<MenuItem> out;
        for (auto& it : v) {
            if (it.is_sep() && (out.empty() || out.back().is_sep())) continue;
            out.push_back(std::move(it));
        }
        while (!out.empty() && out.back().is_sep()) out.pop_back();
        v = std::move(out);
    }

    void open_url_menu(const std::string& url, int x, int y) {
        ctx_kind = 2; ctx_word = url;
        std::vector<MenuItem> items{{url.size() > 40 ? url.substr(0, 39) + "…" : url, "", A_None, -1, false}, MenuItem::sep(),
                                    {"Open Link in Browser", "", A_UrlOpen}, {"Copy Link Location", "", A_UrlCopy}};
        // an image link: look at it here (fetched only now, as the GUI's hover)
        if (nx_core_pref_int("gui_url_preview") && nx_core_url_is_image(url.c_str()))
            items.push_back({"Preview Image", "", A_UrlPreview});
        ctx.open(std::move(items), x, y);
    }

    void open_chan_menu(const std::string& chan, int x, int y) {
        ctx_kind = 3; ctx_word = chan;
        ctx.open({{chan, "", A_None, -1, false}, MenuItem::sep(),
                  {"Join Channel", "", A_ChanJoin}, {"Copy Channel Name", "", A_ChanCopy}}, x, y);
    }

    void open_tree_menu(nx_key k, int x, int y) {
        Buffer* b = buf(k);
        if (!b) return;
        ctx_kind = 4; ctx_buf = k;
        std::vector<MenuItem> items{{b->name, "", A_None, -1, false}, MenuItem::sep(), {"Go to", "", A_TreeFocus}};
        if (b->kind == ChanKind::Server) {
            items.push_back({"Collapse / Expand", "", A_TreeCollapse});
            items.push_back({"Reconnect", "", A_TreeReconnect});
            items.push_back({"Disconnect", "", A_TreeDisconnect});
        }
        items.push_back(MenuItem::sep());
        items.push_back({b->kind == ChanKind::Channel ? "Part and Close" : "Close", "", A_TreeClose});
        ctx.open(std::move(items), x, y);
    }

    void open_text_menu(int x, int y) {
        Buffer* b = current();
        ctx_kind = 5;
        ctx.open({{"Copy Selection", "", A_Copy, -1, b && b->view.has_selection()},
                  {"Search…", "^F", A_Search}, {"Clear Text", "", A_ClearText},
                  {"Reset Marker Line", "", A_ResetMarker}}, x, y);
    }

    void open_input_menu(int x, int y) {
        ctx_kind = 6;
        ctx.open({{"Copy Line", "", A_InputCopy, -1, !input.text().empty()},
                  {"Cut Line", "", A_InputCut, -1, !input.text().empty()},
                  {"Paste", "", A_InputPaste, -1, !clipboard.empty()},
                  {"Clear", "", A_InputClear, -1, !input.text().empty()}}, x, y);
    }

    void copy(const std::string& s) {
        if (s.empty()) return;
        clipboard = s;
        if (screen) screen->write_clipboard(s);   // OSC 52: the system clipboard
        flash("copied " + std::to_string(s.size()) + " bytes");
    }

    void flash(std::string s) {
        toast = std::move(s);
        toast_until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }

    void command(const std::string& c) { nx_core_command(core_key(cur), c.c_str()); }

    void act(int a) {
        Buffer* b = current();
        Network* n = cur_net();
        if (a >= A_PluginMenuBase) {
            nx_core_plugin_menu_run(cur, a - A_PluginMenuBase);
            build_menubar();   // a toggle's check moved
            return;
        }
        if (a >= A_NickPopupBase) {
            const char *name, *cmd;
            nx_core_popup_get(a - A_NickPopupBase, &name, &cmd);
            nx_core_nick_command(ctx_buf ? ctx_buf : cur, cmd, ctx_word.c_str());
            return;
        }
        switch (a) {
        case A_NickUnignore:
            nx_core_nick_command(ctx_buf ? ctx_buf : cur, "unignore %s!*@*", ctx_word.c_str());
            break;
        case A_Quit: nx_core_quit(); break;
        case A_NetList: netlist.open(); break;
        case A_Prefs: prefs.open(); break;
        case A_IgnoreList: people.open(PeopleList::Kind::Ignore); break;
        case A_FriendsList: people.open(PeopleList::Kind::Friends); break;
        case A_Transfers: transfers.open(); break;
        case A_Mentions: tools.open(Tools::Kind::Mentions); break;
        case A_Plugins: tools.open(Tools::Kind::Plugins); break;
        case A_UrlGrabber: tools.open(Tools::Kind::Urls); break;
        case A_BanList:
            if (b && b->kind == ChanKind::Channel && !is_own(cur)) tools.open(Tools::Kind::Bans, core_key(cur), b->name);
            else queue_dialog({0, "Ban List", "You can only open the Ban List window while in a channel tab.", "", nullptr, nullptr, nullptr, true});
            break;
        case A_Bouncer: bouncer.open(cur_net_index()); break;
        case A_RawLog: if (b) open_rawlog(b->net); break;
        case A_Disconnect: command("discon"); break;
        case A_Reconnect: command("reconnect"); break;
        case A_Close: if (b) close_buffer(cur); break;
        case A_Away: command(n && n->away ? "back" : "away"); break;
        case A_ChanList: command("list"); break;
        case A_Join: ask_command("Join a Channel", "Channel to join:", "#", "join "); break;
        case A_Nick: ask_command("Change Nick", "New nickname:", n ? n->nick : "", "nick "); break;
        case A_Topic: if (b) ask_command("Topic", "Topic for " + b->name + ":", b->topic, "topic "); break;
        case A_ToggleTree: layout.show_tree = !layout.show_tree; break;
        case A_ToggleUsers: layout.show_users = !layout.show_users; break;
        case A_ToggleTopic: show_topic = !show_topic; break;
        case A_ToggleMenu: show_menu = !show_menu; break;
        case A_ToggleStamps: chat_style.show_stamp = !chat_style.show_stamp; break;
        case A_ClearText: if (b) b->chat.clear(); break;
        case A_ResetMarker: if (b) b->chat.clear_marker(); break;
        case A_JumpUnread:
            if (b) {
                if (auto i = b->chat.marker_index(); i && *i < b->chat.size()) b->view.reveal(b->chat, b->chat[*i].id);
                else flash("nothing unread here");
            }
            break;
        case A_Copy: if (b) copy(b->view.selection_text(b->chat)); break;
        case A_Search: open_find(); break;
        case A_Prev: step_buffer(-1); break;
        case A_Next: step_buffer(+1); break;
        case A_NextActive: next_active(); break;
        case A_GoTo: open_switcher(); break;
        case A_Palette: open_palette(); break;
        case A_ThemeNative: set_theme("native"); break;
        case A_Theme: open_theme_picker(); break;
        case A_Keys: show_keys(); break;
        case A_About:
            queue_dialog({0, "About", "nexchat " NX_VERSION " — the terminal UI\nIRC, HexChat continued; drawn with maya.", ""});
            break;
        case A_NickQuery: command("query " + ctx_word); break;
        case A_NickWhois: command("whois " + ctx_word); break;
        case A_NickCopy: copy(ctx_word); break;
        case A_NickInsert: {
            std::string t = input.text();
            if (t.empty()) t = ctx_word + nx_core_pref_str("completion_suffix") + " ";
            else t += ctx_word + " ";
            input.set(t);
            focus = Pane::Input;
            break;
        }
        case A_UrlOpen: nx_core_open_url(ctx_word.c_str()); break;
        case A_UrlCopy: copy(ctx_word); break;
        case A_UrlPreview: open_preview(ctx_word); break;
        case A_ChanJoin: command("join " + ctx_word); break;
        case A_ChanCopy: copy(ctx_word); break;
        case A_TreeFocus: switch_to(ctx_buf); break;
        case A_TreeClose: close_buffer(ctx_buf); break;
        case A_TreeCollapse: tree.select_key(ctx_buf); tree.toggle_collapse(); break;
        case A_TreeDisconnect: nx_core_command(ctx_buf, "discon"); break;
        case A_TreeReconnect: nx_core_command(ctx_buf, "reconnect"); break;
        case A_InputCopy: copy(input.text()); break;
        case A_NickReply: nx_core_command(ctx_buf ? ctx_buf : cur, ("reply " + ctx_word).c_str()); break;
        case A_Reply: if (b) open_reply_picker(core_key(cur)); break;
        case A_InputCut: copy(input.text()); input.clear(); break;
        case A_InputPaste: input.paste(clipboard); break;
        case A_InputClear: input.clear(); break;
        default: break;
        }
        build_menubar();
        dirty = true;
    }

    // ── dialogs ─────────────────────────────────────────────────────────
    // kind: 0 message, 1 confirm, 2 prompt
    void queue_dialog(PendingDialog d) {
        dialogs.push_back(std::move(d));
        if (!dialog.is_open()) next_dialog();
    }

    void next_dialog() {
        if (dialogs.empty()) return;
        dialog_now = std::move(dialogs.front());
        dialogs.pop_front();
        switch (dialog_now.kind) {
        case 0: dialog.message(dialog_now.title, dialog_now.text, dialog_now.error); break;
        case 1: dialog.confirm(dialog_now.title, dialog_now.text); break;
        default: dialog.prompt(dialog_now.title, dialog_now.text, dialog_now.initial); break;
        }
    }

    void dialog_done(Dialog::Outcome o) {
        const bool ok = o == Dialog::Outcome::Ok;
        PendingDialog d = std::move(dialog_now);
        dialog_now = {};
        const std::string value = dialog.value();
        if (d.sd) d.sd(d.ud, ok, value.c_str());
        if (d.yd) d.yd(d.ud, ok);
        if (d.on_yes && ok) d.on_yes();
        if (d.kind == 3 && ok) on_internal_prompt(d.title, value);   // our own prompts
        next_dialog();
        dirty = true;
    }

    // A prompt whose answer is a command: "/<prefix><answer>".
    std::map<std::string, std::string> internal_prompts;

    // ── find (Ctrl-F; the GUI's search bar, maingui.c) ───────────────────
    // Over the status row while open: typing finds the newest match, Enter
    // or Up an older one, Down a newer one, Esc closes. The options are
    // the GUI's prefs (text_search_*); Alt-C / Alt-R / Alt-A toggle them.
    bool find_open = false;
    InputLine find_line;
    std::string find_note;
    bool find_bad = false;
    nx_key find_buf = 0;

    ChatView::FindOpts find_opts() const {
        return {nx_core_pref_int("text_search_case_match") != 0, nx_core_pref_int("text_search_regexp") != 0,
                nx_core_pref_int("text_search_highlight_all") != 0};
    }
    void open_find() {
        if (!current()) return;
        find_open = true;
        find_line.set_prompt(Rich{"Find: ", role::title()});
        find_buf = cur;
        find_run(0);
    }
    void close_find() {
        find_open = false;
        for (auto& [k, bf] : bufs) bf->view.set_find({}, find_opts());
        find_line.clear();
        find_note.clear();
        find_bad = false;
    }
    // dir 0: the text changed (the newest match), -1 an older one, +1 newer
    void find_run(int dir) {
        Buffer* b = current();
        if (!b) return;
        if (find_buf != cur) {                        // another tab: the old one forgets
            if (Buffer* o = buf(find_buf)) o->view.set_find({}, find_opts());
            find_buf = cur;
        }
        find_bad = false;
        if (!b->view.set_find(find_line.text(), find_opts())) {
            find_note = "Not a valid regular expression.";
            find_bad = true;
            return;
        }
        if (find_line.text().empty()) { find_note.clear(); return; }
        if (!b->view.find_step(b->chat, dir)) { find_note = "No results found."; find_bad = true; return; }
        const auto [at, n] = b->view.find_count(b->chat);
        find_note = std::to_string(at) + " of " + std::to_string(n);
    }
    void toggle_find_pref(const char* name) {
        nx_core_pref_set_int(name, nx_core_pref_int(name) ? 0 : 1);
        nx_core_prefs_save();               // as the GUI's checkboxes do
        find_run(0);
    }
    // true: the bar took it
    bool find_key(const maya::KeyEvent& k) {
        using maya::SpecialKey;
        const auto* sk = std::get_if<SpecialKey>(&k.key);
        const auto* ck = std::get_if<maya::CharKey>(&k.key);
        if (sk) switch (*sk) {
            case SpecialKey::Escape: close_find(); return true;
            case SpecialKey::Enter: find_run(k.mods.shift ? +1 : -1); return true;
            case SpecialKey::Up: find_run(-1); return true;
            case SpecialKey::Down: find_run(+1); return true;
            case SpecialKey::Tab: case SpecialKey::BackTab: return true;
            case SpecialKey::PageUp: case SpecialKey::PageDown: return false;   // scroll the chat
            default: break;
        }
        if (ck && k.mods.alt && !k.mods.ctrl) {
            switch (ck->codepoint) {
            case 'c': toggle_find_pref("text_search_case_match"); return true;
            case 'r': toggle_find_pref("text_search_regexp"); return true;
            case 'a': toggle_find_pref("text_search_highlight_all"); return true;
            default: return false;
            }
        }
        if (ck && k.mods.ctrl) {
            if (ck->codepoint == 'f') return true;                 // already open
            if (ck->codepoint == 'g') { find_run(-1); return true; }  // as in a browser: the next one
            if (ck->codepoint != 'u' && ck->codepoint != 'w' && ck->codepoint != 'a' && ck->codepoint != 'e'
                && ck->codepoint != 'k' && ck->codepoint != 'h')
                return false;                                        // ^Q, ^S… stay global
        }
        const std::string before = find_line.text();
        const auto r = find_line.key(k);
        if (r == InputLine::Result::Unhandled) return false;
        if (find_line.text() != before) find_run(0);
        return true;
    }
    Rich find_bar(int w) const {
        const auto o = find_opts();
        auto opt = [](const char* label, bool on) {
            Rich r;
            r.append(on ? "[✓] " : "[ ] ", on ? role::title() : role::muted());
            r.append(label, role::muted());
            return r;
        };
        Rich right;
        if (!find_note.empty()) right.append(find_note + "  ", find_bad ? role::bad() : role::muted());
        right.append(Rich{"↑↓ ", role::muted()});
        right.append(opt("case ", o.match_case));
        right.append(opt("regex ", o.regex));
        right.append(opt("all ", o.all));
        right.append(Rich{"Esc", role::muted()});
        const int rw = display_width(right.text);
        Rich bar = find_line.row(std::max(10, w - rw - 1), true);
        bar.append(" ");
        bar.append(right);
        return bar.fit(w);
    }

    // what a key or click in the Window menu's tools wants of the UI
    struct ToolAsk { Tools::Ask a; };
    void after_tools() {
        if (!tools.flash.empty()) { flash(tools.flash); tools.flash.clear(); }
        if (!tools.copy.empty()) { copy(tools.copy); tools.copy.clear(); }
        if (tools.go_to) { if (buf(tools.go_to)) switch_to(tools.go_to); tools.go_to = 0; }
        const Tools::Ask a = tools.ask;
        tools.ask = Tools::Ask::None;
        auto* ud = new ToolAsk{a};
        switch (a) {
        case Tools::Ask::None: delete ud; break;
        case Tools::Ask::SaveUrls: {
            char* d = nx_core_addons_dir();
            std::string home = d ? d : "";
            nx_core_gfree(d);
            home = home.substr(0, home.rfind('/'));
            queue_dialog({2, "Save URLs", "Select an output filename:", home + "/url.save",
                          [](void* p, int ok, const char* t) { auto* q = static_cast<ToolAsk*>(p); ui().tools.answer(q->a, ok, t ? t : ""); ui().after_tools(); delete q; }, nullptr, ud});
            break;
        }
        case Tools::Ask::LoadPlugin: {
            char* d = nx_core_addons_dir();
            queue_dialog({2, "Load Plugin or Script", "Select a Plugin or Script to load:", std::string(d ? d : "") + "/",
                          [](void* p, int ok, const char* t) { auto* q = static_cast<ToolAsk*>(p); ui().tools.answer(q->a, ok, t ? t : ""); ui().after_tools(); delete q; }, nullptr, ud});
            nx_core_gfree(d);
            break;
        }
        case Tools::Ask::ClearBans:
            queue_dialog({1, "Ban List", "Are you sure you want to remove all listed items?", "", nullptr,
                          [](void* p, int yes) { auto* q = static_cast<ToolAsk*>(p); ui().tools.answer(q->a, yes, ""); ui().after_tools(); delete q; }, ud});
            break;
        }
    }
    void ask_command(const std::string& title, const std::string& text, const std::string& initial, std::string prefix) {
        internal_prompts[title] = std::move(prefix);
        queue_dialog({3, title, text, initial});
    }
    void on_internal_prompt(const std::string& title, const std::string& value) {
        if (title == "Theme") { set_theme(value); return; }
        auto it = internal_prompts.find(title);
        if (it != internal_prompts.end() && !value.empty()) command(it->second + value);
    }

    void open_theme_picker() {
        // The switcher over maya's schemes, live: moving through it shows
        // each theme at once; Enter keeps it, Esc goes back.
        std::vector<ChanEntry> e;
        std::vector<std::string> nets_;
        e.push_back({0, "native", ChanKind::Other});
        nets_.push_back("the terminal's colours");
        std::uintptr_t now = 0;
        for (std::size_t i = 0; i < std::size(maya::theme::schemes); ++i) {
            e.push_back({i + 1, maya::theme::schemes[i].name, ChanKind::Other});
            nets_.push_back("");
            if (theme_name == maya::theme::schemes[i].name) now = i + 1;
        }
        picking_theme = true;
        palette_open = false;
        picking_reply = false;
        switcher.title = "Theme";
        // each scheme's own colours: its background with its accent, primary
        // and text on it (the schemes are in the maya table: literal colours)
        switcher.swatch = [](std::uintptr_t k) -> Rich {
            if (k == 0) return Rich{};
            const maya::Theme& th = *maya::theme::schemes[k - 1].theme;
            const maya::Color bg = th.background;
            Rich r;
            for (const maya::LitColor* c : {&th.accent, &th.primary, &th.success, &th.warning, &th.text})
                r.append("●", Style{}.with_fg(maya::Color{*c}).with_bg(bg));
            r.append(" ", Style{}.with_bg(bg));
            return r;
        };
        theme_before = theme_name;
        switcher.open(std::move(e), std::move(nets_));
        switcher.select_key(now);
    }
    bool picking_theme = false;
    std::string theme_before;

    // ── link preview (gui_url_preview): an image link, drawn in half blocks
    // ▀ (top pixel the fg, bottom the bg), in a card over the chat. Fetched
    // when asked (the menu), never on its own.
    struct Preview {
        bool open = false, loading = false;
        std::string url, error;
        int w = 0, h = 0;                  // pixels: w wide, h tall (2 per row)
        std::vector<unsigned char> rgb;
        unsigned gen = 0;                  // a late answer for an old preview is dropped
    } preview;
    void open_preview(const std::string& url) {
        preview = Preview{true, true, url, {}, 0, 0, {}, preview.gen + 1};
        struct Ask { unsigned gen; };
        const int cols = std::clamp(W - 8, 10, 120), rows = std::clamp(H - 8, 4, 40);
        nx_core_image_fetch(url.c_str(), cols, rows,
            [](void* ud, int w, int h, const unsigned char* rgb, const char* err) {
                auto* a = static_cast<Ask*>(ud);
                Ui& u = ui();
                if (u.preview.open && u.preview.gen == a->gen) {
                    u.preview.loading = false;
                    if (rgb && w > 0 && h > 0) {
                        u.preview.w = w; u.preview.h = h;
                        u.preview.rgb.assign(rgb, rgb + static_cast<std::size_t>(w) * h * 3);
                    } else {
                        u.preview.error = err ? err : "not an image";
                    }
                    u.dirty = true;
                }
                delete a;
            }, new Ask{preview.gen});
        dirty = true;
    }
    maya::Element preview_element(int sw, int sh) const {
        using namespace maya::dsl;
        const Preview& p = preview;
        std::vector<Rich> body;
        int inner = 0;
        if (p.loading || !p.error.empty() || p.rgb.empty()) {
            const std::string t = p.loading ? "loading…" : "can't show it: " + p.error;
            inner = std::min(std::max(30, display_width(t) + 2), std::max(10, sw - 4));
            body.push_back(Rich{t, role::muted()}.fit(inner));
        } else {
            inner = std::min(p.w, std::max(1, sw - 4));
            const int rows = std::min((p.h + 1) / 2, std::max(1, sh - 4));
            auto px = [&](int x, int y) {
                if (y >= p.h) y = p.h - 1;
                const unsigned char* c = &p.rgb[(static_cast<std::size_t>(y) * p.w + x) * 3];
                return maya::Color::rgb(c[0], c[1], c[2]);
            };
            for (int r = 0; r < rows; ++r) {
                Rich row;
                for (int x = 0; x < inner; ++x)
                    row.append("▀", Style{}.with_fg(px(x, r * 2)).with_bg(px(x, r * 2 + 1)));
                body.push_back(std::move(row));
            }
        }
        const int w = inner + 4;
        std::vector<Rich> card;
        Rich top{"╭─ ", role::border()};
        std::string title = p.url;
        if (display_width(title) > w - 8) title = Rich{title}.take(std::max(1, w - 9)).text + "…";
        top.append(title, role::text());
        top.append(" ", role::border());
        std::string fill;
        for (int i = top.width(); i < w - 1; ++i) fill += "─";
        top.append(fill, role::border());
        top.append("╮", role::border());
        card.push_back(std::move(top));
        for (auto& r : body) {
            Rich row{"│ ", role::border()};
            row.append(r);
            row.append(" │", role::border());
            card.push_back(std::move(row));
        }
        Rich foot{"╰", role::border()};
        const Rich hint{" Esc close · Enter open ", role::muted()};
        std::string f2;
        for (int i = 1 + hint.width(); i < w - 2; ++i) f2 += "─";
        foot.append(f2 + "─", role::border());
        foot.append(hint);
        foot.append("╯", role::border());
        card.push_back(foot.fit(w));
        const int x = std::max(0, (sw - w) / 2), y = std::max(0, (sh - static_cast<int>(card.size())) / 2);
        return (v(stack_rows(card)) | padding(y, 0, 0, x)).build();
    }

    // what the switcher was opened for decides what a pick does
    void switcher_done(Switcher::Outcome o) {
        if (o == Switcher::Outcome::Picked) {
            if (picking_theme) set_theme(theme_of(*switcher.picked()));     // keep it (saved)
            else if (palette_open) { palette_open = false; palette_pick(*switcher.picked()); }
            else if (picking_reply) { picking_reply = false; reply_pick_done(*switcher.picked()); }
            else switch_to(*switcher.picked());
        } else if (picking_theme) {
            if (o == Switcher::Outcome::Cancelled) set_theme(theme_before, false);   // back
            else if (auto c = switcher.current()) set_theme(theme_of(*c), false);   // preview
        } else if (o == Switcher::Outcome::Cancelled) {
            picking_reply = false;
        }
    }

    // ── replies (+draft/reply) ─────────────────────────────────────────────────
    // /reply (or Alt-R): choose a recent message in a list, newest first; a
    // bar over the input says who you answer, Esc drops it, Enter sends
    bool picking_reply = false;
    std::string reply_text;   // the bar, for this frame
    std::vector<std::string> reply_ids;
    void open_reply_picker(nx_key k) {
        std::vector<ChanEntry> e;
        std::vector<std::string> right;
        reply_ids.clear();
        const char *id = nullptr, *line = nullptr;
        for (int i = 0; i < 100 && nx_core_reply_recent(k, i, &id, &line); ++i) {
            std::string l = line ? line : "";
            const auto tab = l.find('\t');
            const std::string nick = tab == std::string::npos ? "" : l.substr(0, tab);
            const std::string text = tab == std::string::npos ? l : l.substr(tab + 1);
            reply_ids.emplace_back(id);
            e.push_back({reply_ids.size() - 1, nick + ": " + text, ChanKind::Other});
            right.push_back("");
        }
        if (e.empty()) { flash("nothing here to reply to yet"); return; }
        picking_theme = false;
        palette_open = false;
        picking_reply = true;
        switcher.swatch = nullptr;
        switcher.title = "Reply to";
        switcher.open(std::move(e), std::move(right));
    }
    void reply_pick_done(std::uintptr_t i) {
        if (i < reply_ids.size()) set_reply(cur, reply_ids[i]);
    }
    void set_reply(nx_key k, std::string msgid) {
        if (Buffer* b = buf(k)) { b->reply_to = std::move(msgid); focus = Pane::Input; dirty = true; }
    }
    // the bar's text ("" = not replying)
    std::string reply_bar() {
        Buffer* b = current();
        if (!b || b->reply_to.empty()) return {};
        const char* l = nx_core_reply_lookup(core_key(cur), b->reply_to.c_str());
        std::string s = l ? l : "";
        const auto tab = s.find('\t');
        if (tab == std::string::npos) return "an earlier message";
        return s.substr(0, tab) + ": " + s.substr(tab + 1);
    }
    std::string theme_of(std::uintptr_t k) const {
        return k == 0 ? std::string("native") : std::string(maya::theme::schemes[k - 1].name);
    }

    void open_switcher() {
        std::vector<std::string> nn;
        for (auto& e : entries()) {
            const Buffer* b = buf(e.key);
            auto it = b ? nets.find(b->net) : nets.end();
            nn.push_back(it == nets.end() ? "" : it->second.name);
        }
        picking_theme = false;
        palette_open = false;
        picking_reply = false;
        switcher.swatch = nullptr;
        switcher.title = "Go to";
        switcher.open(entries(), std::move(nn));
    }

    // ── Ctrl-P: the command palette ─────────────────────────────────────────
    // Everything in one fuzzy list: each menu action ("Window › Mentions",
    // its key on the right), each tab, each /command. The key says which:
    //   kPalAction + action id, kPalCmd + index into palette_cmds, or a
    //   buffer key (even: a pointer, or ours, odd).
    static constexpr std::uintptr_t kPalAction = std::uintptr_t{1} << 60;
    static constexpr std::uintptr_t kPalCmd = std::uintptr_t{1} << 61;
    bool palette_open = false;
    std::vector<std::string> palette_cmds;
    void open_palette() {
        build_menubar();
        std::vector<ChanEntry> e;
        std::vector<std::string> right;
        // menu actions, "Menu › Item"
        std::function<void(const std::string&, const std::vector<MenuItem>&)> walk =
            [&](const std::string& path, const std::vector<MenuItem>& items) {
                for (const auto& it : items) {
                    if (it.label.empty()) continue;
                    if (!it.sub.empty()) { walk(path + it.label + " › ", it.sub); continue; }
                    if (it.action <= A_None || !it.enabled) continue;
                    std::string label = it.label;
                    if (label.size() > 3 && label.compare(label.size() - 3, 3, "…") == 0) label.erase(label.size() - 3);
                    e.push_back({kPalAction + static_cast<std::uintptr_t>(it.action), path + label, ChanKind::Other});
                    right.push_back(it.hint);
                }
            };
        for (const auto& m : menubar.entries()) walk(m.title + " › ", m.items);
        // tabs
        for (auto& en : entries()) {
            const Buffer* b = buf(en.key);
            auto it = b ? nets.find(b->net) : nets.end();
            e.push_back(en);
            right.push_back(it == nets.end() ? "" : "tab · " + it->second.name);
        }
        // /commands
        palette_cmds.clear();
        if (nx_cmd_list* l = nx_core_cmd_suggest(core_key(cur), std::string(1, nx_core_cmd_char()).c_str(), 1)) {
            for (int i = 0; i < l->n; ++i) {
                palette_cmds.push_back(l->items[i].insert);
                e.push_back({kPalCmd + palette_cmds.size() - 1, l->items[i].name, ChanKind::Other});
                right.push_back(l->items[i].desc);
            }
            nx_cmd_list_free(l);
        }
        picking_theme = false;
        palette_open = true;
        picking_reply = false;
        switcher.swatch = nullptr;
        switcher.title = "Command Palette";
        switcher.open(std::move(e), std::move(right));
    }
    void palette_pick(std::uintptr_t k) {
        if (k >= kPalCmd) {
            const std::size_t i = static_cast<std::size_t>(k - kPalCmd);
            if (i < palette_cmds.size()) {
                std::string ins = palette_cmds[i];
                for (char& c : ins) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                input.set(std::string(1, nx_core_cmd_char()) + ins + " ");
                focus = Pane::Input;
                refresh_cmdpopup();
            }
        } else if (k >= kPalAction) {
            act(static_cast<int>(k - kPalAction));
        } else {
            switch_to(k);
        }
    }

    void show_keys() {
        // inside tmux (or screen) the Alt keys may be the multiplexer's: say so first
        const bool muxed = std::getenv("TMUX") || std::getenv("STY");
        const std::string head = muxed ? "In tmux? It may keep Alt-keys for itself: Ctrl-X then the key does\n"
                                         "the same here (Ctrl-X 1 = Alt-1, Ctrl-X → = Alt-→).\n\n"
                                       : "";
        // one key per line: reads the same at any width (the dialog wraps
        // and scrolls)
        queue_dialog({0, "Keyboard Shortcuts", head +
            "Alt-1…9        buffer N\n"
            "Alt-←/→        previous / next buffer (also Ctrl-PgUp/PgDn)\n"
            "Alt-A          next active buffer\n"
            "Ctrl-G         go to…\n"
            "Ctrl-P         every command\n"
            "PgUp/PgDn      scroll\n"
            "Ctrl-Home/End  top / bottom\n"
            "Tab            complete\n"
            "Ctrl-L         user list\n"
            "Ctrl-F         find (↑↓/Enter older/newer, Alt-C case, Alt-R regex, Alt-A all)\n"
            "Alt-R          reply to a message\n"
            "Alt-J          move to the marker line (the first unread)\n"
            "Ctrl-B/I/U/R/O bold / italic / underline / reverse / reset\n"
            "Ctrl-K         colour\n"
            "Alt-K / Alt-U  kill to end / to start (readline's ^K ^U)\n"
            "F2 / F3        focus tree / users\n"
            "F10            menu\n"
            "Esc            back\n"
            "Ctrl-W         close buffer\n"
            "Ctrl-Q         quit\n"
            "Ctrl-X then a key: the Alt key, for when tmux or the window\n"
            "               manager takes Alt (Ctrl-X 1 = Alt-1, Ctrl-X → = Alt-→)\n"
            "\n"
            "Mouse: click, right-click menus, wheel, drag to select (copies), "
            "drag the │ dividers, middle-click pastes.", ""});
    }

    // ── keys ────────────────────────────────────────────────────────────
    // the leader key is waiting for its key
    bool leader_armed = false;
    // tui.conf leader= a letter for Ctrl-letter (x by default), or "none"
    char32_t leader_char() const {
        const char* v = nx_core_tui_get("leader");
        if (!v || !*v) return 'x';
        if (!strcasecmp(v, "none") || !strcmp(v, "0")) return 0;
        const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(v[0])));
        return l >= 'a' && l <= 'z' ? static_cast<char32_t>(l) : 'x';
    }
    void key(const maya::KeyEvent& k_in) {
        // The leader key (Ctrl-X, or tui.conf leader=): the next key acts as
        // if Alt were held. tmux, screen and window managers often take the
        // Alt keys (Alt-1…9, Alt-arrows); "Ctrl-X 1" still gets there.
        maya::KeyEvent k = k_in;
        {
            const auto* lc = std::get_if<maya::CharKey>(&k_in.key);
            if (leader_armed) {
                leader_armed = false;
                flash("");
                const auto* lsk = std::get_if<SpecialKey>(&k_in.key);
                if (lsk && *lsk == SpecialKey::Escape) return;               // changed your mind
                if (lc && k_in.mods.ctrl && lc->codepoint == leader_char()) {
                    // twice: the key itself, for whatever else wants it
                } else {
                    k.mods.alt = true;
                }
            } else if (lc && k_in.mods.ctrl && !k_in.mods.alt && leader_char() && lc->codepoint == leader_char()
                       && !dialog.is_open() && !user_bound(k_in)) {
                leader_armed = true;
                flash(std::string("Ctrl-") + static_cast<char>(std::toupper(static_cast<int>(leader_char())))
                      + " … (a key, as with Alt; Esc: never mind)");
                dirty = true;
                return;
            }
        }
        const auto* sk = std::get_if<SpecialKey>(&k.key);
        const auto* ck = std::get_if<maya::CharKey>(&k.key);
        const char32_t c = ck ? ck->codepoint : 0;
        Buffer* b = current();

        if (dialog.is_open()) {
            const auto o = dialog.key(k);
            if (o != Dialog::Outcome::Open) dialog_done(o);
            return;
        }
        if (preview.open) {
            // Enter opens it in the browser, anything else closes the card
            if (sk && *sk == SpecialKey::Enter) nx_core_open_url(preview.url.c_str());
            preview.open = false;
            dirty = true;
            return;
        }
        if (netlist.is_open()) {
            netlist.key(k);
            if (!netlist.flash.empty()) { flash(netlist.flash); netlist.flash.clear(); }
            if (netlist.bouncer_from != -2) { bouncer.open(netlist.bouncer_from); netlist.bouncer_from = -2; }
            return;
        }
        if (prefs.is_open()) { prefs.key(k); after_prefs(); return; }
        if (chanlist.is_open()) { chanlist.key(k); return; }
        if (people.is_open()) { people.key(k); return; }
        if (transfers.is_open()) { transfers.key(k); return; }
        if (tools.is_open()) { tools.key(k); after_tools(); return; }
        if (bouncer.is_open()) { bouncer.key(k); return; }
        if (ctx.is_open()) {
            if (ctx.key(k) == Menu::Outcome::Activated) act(ctx.activated());
            return;
        }
        if (menubar.is_open()) {
            if (menubar.key(k) == Menu::Outcome::Activated) act(menubar.activated());
            return;
        }
        if (switcher.is_open()) {
            switcher_done(switcher.key(k));
            return;
        }

        // global keys
        if (find_open && find_key(k)) return;
        if (focus == Pane::Input && cmdpop_key(k)) return;
        if (focus == Pane::Input && keybinding(k)) { refresh_cmdpopup(); return; }
        if (ck && k.mods.ctrl && !k.mods.alt) {
            switch (c) {
            case 'q': nx_core_quit(); return;
            case 's': netlist.open(); return;
            case 'f': open_find(); return;
            case 'p': open_palette(); return;
            case 'g': open_switcher(); return;
            case 'l': layout.show_users = !layout.show_users; build_menubar(); return;
            case 'w': if (b) close_buffer(cur); return;
            case 'c':
                if (b && b->view.has_selection()) { copy(b->view.selection_text(b->chat)); b->view.select_clear(); }
                else if (!input.text().empty()) input.clear();
                return;
            // the GUI's formatting keys
            case 'b': input.paste("\002"); return;
            case 'k': input.paste("\003"); return;
            case 'i': input.paste("\035"); return;
            case 'u': input.paste("\037"); return;
            case 'r': input.paste("\026"); return;
            case 'o': input.paste("\017"); return;
            default: break;
            }
        }
        if (ck && k.mods.alt && !k.mods.ctrl) {
            if (c >= '1' && c <= '9') {
                auto o = ordered();
                const std::size_t i = static_cast<std::size_t>(c - '1');
                if (i < o.size()) switch_to(o[i]);
                return;
            }
            if (c == '0') { auto o = ordered(); if (o.size() >= 10) switch_to(o[9]); return; }
            if (c == 'a') { next_active(); return; }
            if (c == 'k') { input.key(maya::KeyEvent{maya::CharKey{'k'}, {.ctrl = true}, ""}); return; }
            if (c == 'u') { input.key(maya::KeyEvent{maya::CharKey{'u'}, {.ctrl = true}, ""}); return; }
            if (c == 'f') { menubar.open(0); return; }
            if (c == 'v') { menubar.open(1); return; }
            if (c == 's') { menubar.open(2); return; }
            if (c == 'w') { menubar.open(3); return; }
            // Help is last (a script's menu can sit before it)
            if (c == 'h') { menubar.open(static_cast<int>(menubar.entries().size()) - 1); return; }
            if (c == 'r') { if (b) open_reply_picker(core_key(cur)); return; }
            if (c == 'j') { act(A_JumpUnread); return; }
        }
        if (sk) {
            switch (*sk) {
            case SpecialKey::F1: show_keys(); return;
            case SpecialKey::F10: build_menubar(); menubar.open(0); return;
            case SpecialKey::F2:
                if (layout.tabs()) { open_switcher(); return; }     // no tree to focus: Go to…
                focus = Pane::Tree; tree.select_key(cur); return;
            case SpecialKey::F3: focus = Pane::Users; return;
            case SpecialKey::Escape:
                if (b && b->view.has_selection()) b->view.select_clear();
                else if (b && !b->reply_to.empty() && focus == Pane::Input) b->reply_to.clear();   // not replying any more
                focus = Pane::Input;
                return;
            case SpecialKey::PageUp:
                if (k.mods.ctrl) step_buffer(-1);
                else if (k.mods.shift) users.move(-1);
                else if (b) b->view.page_up(b->chat);
                return;
            case SpecialKey::PageDown:
                if (k.mods.ctrl) step_buffer(+1);
                else if (k.mods.shift) users.move(+1);
                else if (b) b->view.page_down(b->chat);
                return;
            case SpecialKey::Home: if (k.mods.ctrl && b) { b->view.to_top(b->chat); return; } break;
            case SpecialKey::End:  if (k.mods.ctrl && b) { b->view.to_bottom(); return; } break;
            case SpecialKey::Left: case SpecialKey::Right:
                if (k.mods.alt) { step_buffer(*sk == SpecialKey::Left ? -1 : 1); return; }
                break;
            case SpecialKey::Up: case SpecialKey::Down:
                if (k.mods.alt) { step_buffer(*sk == SpecialKey::Up ? -1 : 1); return; }
                break;
            default: break;
            }
        }

        // the focused pane
        if (focus == Pane::Tree) {
            if (sk && (*sk == SpecialKey::Up || *sk == SpecialKey::Down)) { tree.move(*sk == SpecialKey::Up ? -1 : 1); return; }
            if (sk && *sk == SpecialKey::Enter) { if (auto s = tree.selected()) switch_to(*s); focus = Pane::Input; return; }
            if ((ck && c == ' ') || (sk && (*sk == SpecialKey::Left || *sk == SpecialKey::Right))) { tree.toggle_collapse(); return; }
            if (sk && *sk == SpecialKey::Delete) { if (auto s = tree.selected()) close_buffer(*s); return; }
        }
        if (focus == Pane::Users) {
            if (sk && (*sk == SpecialKey::Up || *sk == SpecialKey::Down)) { users.move(*sk == SpecialKey::Up ? -1 : 1); return; }
            if (sk && *sk == SpecialKey::Enter) {
                if (const UserEntry* u = users.selected()) nx_core_nick_command(cur, nx_core_ulist_doubleclick(), u->nick.c_str());
                return;
            }
            if (ck && !k.mods.ctrl && !k.mods.alt && c > ' ') { users.find(std::string(1, static_cast<char>(c))); return; }
        }

        // the input
        if (sk && *sk == SpecialKey::Enter) {
            // Shift+Enter (or Alt+Enter, where the terminal can't tell
            // Shift): a new line in the same message (gui_multiline)
            if ((k.mods.shift || k.mods.alt) && nx_core_pref_int("gui_multiline")) {
                focus = Pane::Input;
                input.newline();
                return;
            }
            submit();
            return;
        }
        focus = Pane::Input;
        input.key(k);
        refresh_cmdpopup();
    }

    // ── the command popup ("/…": fe-gtk/cmdpopup.c) ─────────────────────────
    CmdPopup cmdpop;
    bool moved_in_popup = false;   // Up/Down picked a value (Enter fills it in)
    int cmdpop_x = 0, cmdpop_y = 0, cmdpop_w = 0, cmdpop_h = 0;   // where it was drawn
    std::string cmdpop_cmd() const {
        const std::string& t = input.text();
        if (t.empty() || t[0] != nx_core_cmd_char()) return {};
        const auto sp = t.find(' ');
        return t.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
    }
    void refresh_cmdpopup() {
        const std::string cmd = cmdpop_cmd();
        if (cmd.empty() && (input.text().empty() || input.text()[0] != nx_core_cmd_char())) {
            cmdpop.close();
            cmdpop.undismiss();
            return;
        }
        if (cmdpop.dismissed_for(cmd)) return;
        cmdpop.undismiss();
        nx_cmd_list* l = nx_core_cmd_suggest(core_key(cur), input.text().c_str(), static_cast<int>(input.cursor()));
        CmdPopup::Data d;
        if (l) {
            for (int i = 0; i < l->n; ++i) {
                const nx_cmd_item& it = l->items[i];
                d.items.push_back({it.name, it.middle, it.desc, it.insert, it.heading, it.cmd});
            }
            d.args = l->args != 0;
            d.word_start = l->word_start;
            d.usage = l->usage;
            d.arg_start = l->arg_start;
            d.arg_end = l->arg_end;
            d.desc = l->desc;
            d.example = l->example;
            nx_cmd_list_free(l);
        }
        cmdpop.set(std::move(d), input.text());
        moved_in_popup = false;
        cmdpop_details();
    }
    void cmdpop_details() {
        cmdpop.details.clear();
        if (const auto* s = cmdpop.selected(); s && !cmdpop.data().args && !s->cmd.empty())
            if (char* d = nx_core_cmd_details(s->cmd.c_str())) { cmdpop.details = d; nx_core_gfree(d); }
    }
    // put the selected item in the line (cmdpopup.c fill_in)
    void cmdpop_fill() {
        const auto* it = cmdpop.selected();
        if (!it) return;
        const std::string text = input.text();
        const std::size_t cur = input.cursor();
        if (!cmdpop.data().args) {
            // the command: "/join " and what was after the name
            const auto sp = text.find(' ');
            std::string rest = sp == std::string::npos ? std::string() : text.substr(sp + 1);
            std::string ins = it->insert;
            for (char& c : ins) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string t = std::string(1, nx_core_cmd_char()) + ins + " " + rest;
            input.set(t, ins.size() + 2);
        } else {
            const std::size_t ws = std::min<std::size_t>(static_cast<std::size_t>(cmdpop.data().word_start), text.size());
            const std::string before = text.substr(0, ws);
            const std::string after = text.substr(std::min(cur, text.size()));
            const bool space_after = !after.empty() && after[0] == ' ';
            std::string t = before + it->insert + (space_after ? "" : " ") + after;
            input.set(t, before.size() + it->insert.size() + 1);
        }
        refresh_cmdpopup();
    }
    // true: the popup took the key
    bool cmdpop_key(const maya::KeyEvent& k) {
        if (!cmdpop.is_open()) return false;
        const auto* sk = std::get_if<SpecialKey>(&k.key);
        if (!sk) return false;
        switch (*sk) {
        case SpecialKey::Escape: cmdpop.dismiss(cmdpop_cmd()); return true;
        case SpecialKey::Up: if (!cmdpop.has_items()) return false; cmdpop.move(-1); moved_in_popup = true; cmdpop_details(); return true;
        case SpecialKey::Down: if (!cmdpop.has_items()) return false; cmdpop.move(+1); moved_in_popup = true; cmdpop_details(); return true;
        case SpecialKey::PageUp: if (!cmdpop.has_items()) return false; cmdpop.move(-8); moved_in_popup = true; cmdpop_details(); return true;
        case SpecialKey::PageDown: if (!cmdpop.has_items()) return false; cmdpop.move(+8); moved_in_popup = true; cmdpop_details(); return true;
        case SpecialKey::Tab:
            if (!cmdpop.has_items()) return false;
            cmdpop_fill();
            return true;
        case SpecialKey::Enter: {
            // the command list: Enter fills in the picked command, unless
            // what's typed is a whole command already (then it sends); values:
            // Enter fills in while a word is being picked, else sends
            if (cmdpop.has_items() && !cmdpop.data().args) {
                const std::string typed = cmdpop_cmd();
                const auto* s = cmdpop.selected();
                const bool whole = s && strcasecmp(s->insert.c_str(), typed.c_str()) == 0;
                bool any_whole = false;
                for (const auto& it : cmdpop.data().items) any_whole |= strcasecmp(it.insert.c_str(), typed.c_str()) == 0;
                if (!whole && !(any_whole && cmdpop.selected() == &cmdpop.data().items.front())) { cmdpop_fill(); return true; }
                cmdpop.close();
                return false;
            }
            if (cmdpop.has_items() && cmdpop.data().args) {
                const std::size_t ws = static_cast<std::size_t>(cmdpop.data().word_start);
                const std::string word = input.text().substr(std::min(ws, input.text().size()),
                                                             input.cursor() > ws ? input.cursor() - ws : 0);
                if (!word.empty() && moved_in_popup) { cmdpop_fill(); return true; }
            }
            cmdpop.close();
            return false;
        }
        default: return false;
        }
    }

    // ── keybindings.conf (the GUI's fkeys.c) ────────────────────────────────
    // The GDK keyval name of a key as the file spells it ("Page_Up", "k").
    static std::string keyval_name(const maya::KeyEvent& k) {
        if (const auto* sk = std::get_if<SpecialKey>(&k.key)) {
            switch (*sk) {
            case SpecialKey::Up: return "Up";
            case SpecialKey::Down: return "Down";
            case SpecialKey::Left: return "Left";
            case SpecialKey::Right: return "Right";
            case SpecialKey::Home: return "Home";
            case SpecialKey::End: return "End";
            case SpecialKey::PageUp: return "Page_Up";
            case SpecialKey::PageDown: return "Page_Down";
            case SpecialKey::Tab: return "Tab";
            case SpecialKey::BackTab: return "ISO_Left_Tab";
            case SpecialKey::Backspace: return "BackSpace";
            case SpecialKey::Delete: return "Delete";
            case SpecialKey::Insert: return "Insert";
            case SpecialKey::Enter: return "Return";
            case SpecialKey::Escape: return "Escape";
            default: {
                const int f = static_cast<int>(*sk) - static_cast<int>(SpecialKey::F1);
                if (f >= 0 && f < 12) return "F" + std::to_string(f + 1);
                return {};
            }
            }
        }
        const auto* ck = std::get_if<maya::CharKey>(&k.key);
        if (!ck) return {};
        static const std::map<char32_t, const char*> punct = {
            {' ', "space"}, {'`', "grave"}, {'~', "asciitilde"}, {'-', "minus"}, {'=', "equal"},
            {'[', "bracketleft"}, {']', "bracketright"}, {'\\', "backslash"}, {';', "semicolon"},
            {'\'', "apostrophe"}, {',', "comma"}, {'.', "period"}, {'/', "slash"}, {'+', "plus"},
            {'!', "exclam"}, {'@', "at"}, {'#', "numbersign"}, {'$', "dollar"}, {'%', "percent"},
            {'^', "asciicircum"}, {'&', "ampersand"}, {'*', "asterisk"}, {'(', "parenleft"},
            {')', "parenright"}, {'_', "underscore"}, {'?', "question"}, {':', "colon"},
            {'"', "quotedbl"}, {'<', "less"}, {'>', "greater"}, {'{', "braceleft"}, {'}', "braceright"},
            {'|', "bar"}};
        if (auto it = punct.find(ck->codepoint); it != punct.end()) return it->second;
        if (ck->codepoint < 0x80) return std::string(1, static_cast<char>(ck->codepoint));
        return {};
    }

    void check_replace() {
        if (char* r = nx_core_replace(input.text().c_str())) {
            input.set(r);
            nx_core_gfree(r);
        }
    }

    // true: a binding took the key (the TUI's own handling doesn't run)
    // a keybindings.conf entry for exactly this key (and its modifiers)
    bool user_bound(const maya::KeyEvent& k) const {
        const std::string name = keyval_name(k);
        if (name.empty()) return false;
        const int mods = (k.mods.ctrl ? NX_KMOD_CTRL : 0) | (k.mods.alt ? NX_KMOD_ALT : 0);
        for (int i = 0, n = nx_core_keybind_count(); i < n; ++i) {
            nx_keybind kb;
            nx_core_keybind_get(i, &kb);
            if (kb.key && (kb.mods & ~NX_KMOD_SHIFT) == mods && !strcasecmp(kb.key, name.c_str())) return true;
        }
        return false;
    }

    bool keybinding(const maya::KeyEvent& k) {
        const int n = nx_core_keybind_count();
        if (n == 0) {
            // no file: the GUI's defaults, which the TUI's keys are, plus its
            // Check For Replace on space (Return is in submit())
            const auto* ck = std::get_if<maya::CharKey>(&k.key);
            if (ck && ck->codepoint == ' ' && k.mods.none()) check_replace();
            return false;
        }
        const std::string name = keyval_name(k);
        if (name.empty()) return false;
        const bool letter = name.size() == 1 && std::isalpha(static_cast<unsigned char>(name[0]));
        // shift is in a letter's case, and in the punctuation itself ("!" is
        // exclam, not <Shift>1); named keys carry it as a modifier
        const bool named = std::get_if<SpecialKey>(&k.key) != nullptr;
        const int mods = (k.mods.ctrl ? NX_KMOD_CTRL : 0) | (k.mods.alt ? NX_KMOD_ALT : 0)
                       | (k.mods.shift && named ? NX_KMOD_SHIFT : 0);
        for (int i = 0; i < n; ++i) {
            nx_keybind kb;
            nx_core_keybind_get(i, &kb);
            if (!kb.key) continue;
            int want = kb.mods;
            if (!named) want &= ~NX_KMOD_SHIFT;
            const bool same = letter ? std::strlen(kb.key) == 1 && std::tolower(kb.key[0]) == std::tolower(name[0])
                                     : name == kb.key || (name == "Return" && !strcmp(kb.key, "KP_Enter"));
            if (!same || want != mods) continue;
            const int r = run_binding(kb);
            if (r == 0) return true;
            // 1: it did its part and the key goes on (Check For Replace)
        }
        return false;
    }

    // fkeys.c's return values: 0 handled, 1 not (the key carries on)
    int run_binding(const nx_keybind& kb) {
        Buffer* b = current();
        const std::string d1 = kb.d1 ? kb.d1 : "", d2 = kb.d2 ? kb.d2 : "";
        switch (kb.action) {
        case NX_KA_COMMAND:
            if (d1.empty()) return 1;
            nx_core_run_typed(core_key(cur), d1.c_str());
            return 0;
        case NX_KA_PAGE: {
            if (d1.empty()) return 1;
            if (!strcasecmp(d1.c_str(), "auto")) {
                const nx_key k = nx_core_lastact();
                if (!k || !buf(k)) return 1;
                switch_to(k);
                return 0;
            }
            char* end = nullptr;
            const long num = std::strtol(d1.c_str(), &end, 10);
            if (!end || *end) return 1;
            if (!d2.empty()) { step_buffer(static_cast<int>(num)); return 0; }   // relative
            auto o = ordered();
            if (num < 1 || static_cast<std::size_t>(num) > o.size()) return 1;
            switch_to(o[static_cast<std::size_t>(num - 1)]);
            return 0;
        }
        case NX_KA_INSERT: if (!d1.empty()) input.paste(d1); return 0;
        case NX_KA_SCROLL:
            if (!b) return 0;
            if (!strcasecmp(d1.c_str(), "Top")) b->view.to_top(b->chat);
            else if (!strcasecmp(d1.c_str(), "Bottom")) b->view.to_bottom();
            else if (!strcasecmp(d1.c_str(), "Up")) b->view.page_up(b->chat);
            else if (!strcasecmp(d1.c_str(), "Down")) b->view.page_down(b->chat);
            else if (d1 == "+1") b->view.scroll(b->chat, -1);
            else if (d1 == "-1") b->view.scroll(b->chat, 1);
            return 0;
        case NX_KA_SET_BUFFER: if (d1.empty()) return 1; input.set(d1); return 0;
        case NX_KA_HISTORY_UP: input.key(maya::KeyEvent{SpecialKey::Up, {}, ""}); return 0;
        case NX_KA_HISTORY_DOWN: input.key(maya::KeyEvent{SpecialKey::Down, {}, ""}); return 0;
        case NX_KA_COMPLETE: {
            const bool back = !strcasecmp(d1.c_str(), "Previous") || !strcasecmp(d1.c_str(), "Up");
            input.key(maya::KeyEvent{back ? SpecialKey::BackTab : SpecialKey::Tab, {}, ""});
            return 0;
        }
        case NX_KA_NICK_SELECT: users.move(!strcasecmp(d1.c_str(), "Up") ? -1 : 1); return 0;
        case NX_KA_REPLACE: check_replace(); return 1;
        case NX_KA_PUSH_HISTORY:
            if (!input.text().empty()) { input.commit_history(input.text()); input.clear(); }
            return 0;
        // moving tabs: the tree is in the core's order, it doesn't move
        default: return 1;
        }
    }

    void submit() {
        Buffer* b = current();
        cmdpop.close();
        cmdpop.undismiss();
        if (nx_core_keybind_count() == 0) check_replace();   // the GUI's default Return binding
        std::string line = input.text();
        if (line.empty()) return;
        input.commit_history(line);
        input.clear();
        if (b) b->view.to_bottom();
        // a reply chosen: this line answers it (a /command goes on as one;
        // "//text" is text that starts with a slash)
        const char cc = nx_core_cmd_char();
        if (b && !b->reply_to.empty() && line.find('\n') == std::string::npos
            && !(line[0] == cc && (line.size() < 2 || line[1] != cc))) {
            const std::string id = std::move(b->reply_to);
            b->reply_to.clear();
            nx_core_reply_send(core_key(cur), id.c_str(), line[0] == cc ? line.c_str() + 1 : line.c_str());
            return;
        }
        nx_core_input(core_key(cur), line.c_str());
        check_prefs();   // a /set shows at once
    }

    void paste(const std::string& s) {
        if (dialog.is_open()) { dialog.paste(s); return; }
        if (netlist.is_open()) { netlist.paste(s); return; }
        if (prefs.is_open()) { prefs.paste(s); return; }
        if (chanlist.is_open()) { chanlist.paste(s); return; }
        if (people.is_open()) { people.paste(s); return; }
        if (bouncer.is_open()) { bouncer.paste(s); return; }
        // A paste of several lines: with gui_multiline it goes in the input
        // (↵ between the lines) to look over and send with Enter; else it
        // goes as several lines, asking first past gui_input_paste_max, as
        // the GUI does.
        if (s.find('\n') != std::string::npos) {
            std::string all = s;
            while (!all.empty() && (all.back() == '\n' || all.back() == '\r')) all.pop_back();
            if (nx_core_pref_int("gui_multiline")) {
                input.keep_newlines = true;
                input.paste(all);
                input.keep_newlines = false;
                return;
            }
            if (input.text().empty()) {
                int lines = 1;
                for (std::size_t i = 0; i + 1 < all.size(); ++i)
                    if (all[i] == '\n' && all[i + 1] != '\n') ++lines;
                const int max = nx_core_pref_int("gui_input_paste_max");
                const nx_key to = core_key(cur);
                if (max > 0 && lines > max && all[0] != nx_core_cmd_char()) {
                    Buffer* b = current();
                    PendingDialog d{1, "Paste", "Send " + std::to_string(lines) + " lines to "
                                        + (b ? b->name : std::string("the server")) + "?"};
                    d.on_yes = [all, to] { nx_core_input(to, all.c_str()); };
                    queue_dialog(std::move(d));
                    return;
                }
                nx_core_input(to, all.c_str());
                return;
            }
        }
        input.paste(s);
    }

    // ── mouse ───────────────────────────────────────────────────────────
    Area area_at(int x, int y) const {
        if (show_menu && r_menu.has(x, y)) return Area::MenuBar;
        if (r_topic.has(x, y)) return Area::Topic;
        if (r_tree.has(x, y)) return Area::Tree;
        if (r_tree_div.has(x, y)) return Area::TreeDivider;
        if (r_strip.has(x, y)) return Area::Strip;
        if (r_chat.has(x, y)) return Area::Chat;
        if (r_users_div.has(x, y)) return Area::UsersDivider;
        if (r_users.has(x, y)) return Area::Users;
        if (r_status.has(x, y)) return Area::Status;
        if (r_input.has(x, y)) return Area::Input;
        return Area::None;
    }

    // ── hover ────────────────────────────────────────────────────────────────
    // The pointer moved: what's under it, if a click would do something with
    // it, is underlined (a link, nick or channel in the chat, a tab, a
    // nick in the list), and a divider brightens. Only redraws on change.
    Area hover_area = Area::None;
    void hover(int x, int y) {
        Buffer* b = current();
        const Area a = area_at(x, y);
        bool changed = a != hover_area;
        hover_area = a;
        // the chat: a link, nick or channel word
        if (b) {
            std::optional<ChatView::Hit> h;
            bool linkish = false;
            if (a == Area::Chat) {
                h = b->view.hit(x - r_chat.x, y - r_chat.y);
                if (h) {
                    auto w = ChatView::word_at(b->chat, *h);
                    const int type = w ? nx_core_word_type(cur, w->text.c_str()) : 0;
                    linkish = w && (h->in_left || (type >= 1 && type <= 6));
                }
            }
            changed |= b->view.set_hover(b->chat, h, linkish);
        }
        std::optional<std::uintptr_t> tk;
        if (a == Area::Tree) tk = tree.key_at_row(y - r_tree.y);
        else if (a == Area::Strip) tk = tree.key_at_col(x);
        changed |= tree.set_hover(tk);
        changed |= users.set_hover(a == Area::Users ? users.nick_at_row(y - r_users.y) : std::nullopt);
        const auto sh = a == Area::Status ? status_at(x) : StatusBar::Hot::None;
        changed |= sh != status_hot;
        status_hot = sh;
        // the pointer's shape (OSC 22, kitty/foot/WezTerm/Ghostty; ignored
        // elsewhere): a hand on what clicks, ↔ on a divider, I-beam on text
        const char* shape = "default";
        if (a == Area::TreeDivider || a == Area::UsersDivider) shape = "ew-resize";
        else if (tk || (a == Area::Users && users.nick_at_row(y - r_users.y)) || sh != StatusBar::Hot::None
                 || a == Area::MenuBar) shape = "pointer";
        else if (a == Area::Chat) shape = b && b->view.hovering() ? "pointer" : "text";
        else if (a == Area::Input) shape = "text";
        set_pointer(shape);
        if (changed) dirty = true;
    }
    std::string pointer_now = "default";
    // animation (spinner, typing dots): a frame every ~120 ms while something
    // moves, nothing otherwise
    int anim_frame = 0;
    bool animating = false;
    void set_pointer(const char* shape) {
        if (!screen || pointer_now == shape) return;
        pointer_now = shape;
        screen->emit_host_sequence(std::string("\x1b]22;") + shape + "\x1b\\");
    }
    StatusBar::Hot status_hot = StatusBar::Hot::None;
    // the status row drawn last (its spans): a copy, as view() makes a new one
    std::vector<std::pair<std::pair<int, int>, StatusBar::Hot>> status_spans;
    StatusBar::Hot status_at(int x) const {
        for (const auto& [span, h] : status_spans)
            if (x >= span.first && x < span.second) return h;
        return StatusBar::Hot::None;
    }

    void mouse(const maya::MouseEvent& m) {
        const int x = m.x.value, y = m.y.value;
        Buffer* b = current();

        if (dialog.is_open()) {
            const auto o = dialog.mouse(m);
            if (o != Dialog::Outcome::Open) dialog_done(o);
            return;
        }
        if (preview.open) {
            if (m.kind == maya::MouseEventKind::Press) { preview.open = false; dirty = true; }
            return;
        }
        if (netlist.is_open()) {
            netlist.mouse(m);
            if (!netlist.flash.empty()) { flash(netlist.flash); netlist.flash.clear(); }
            if (netlist.bouncer_from != -2) { bouncer.open(netlist.bouncer_from); netlist.bouncer_from = -2; }
            return;
        }
        if (prefs.is_open()) { prefs.mouse(m); after_prefs(); return; }
        if (chanlist.is_open()) { chanlist.mouse(m); return; }
        if (people.is_open()) { people.mouse(m); return; }
        if (transfers.is_open()) { transfers.mouse(m); return; }
        if (tools.is_open()) { tools.mouse(m); after_tools(); return; }
        if (bouncer.is_open()) { bouncer.mouse(m); return; }
        if (ctx.is_open()) {
            const auto o = ctx.mouse(m);
            if (o == Menu::Outcome::Activated) act(ctx.activated());
            // a click outside closes the menu AND does nothing else
            return;
        }
        if (show_menu || menubar.is_open()) {
            maya::MouseEvent mm = m;
            if (!show_menu) mm.y = maya::Rows{y + 1};
            if (auto o = menubar.mouse(mm)) {
                if (*o == Menu::Outcome::Activated) act(menubar.activated());
                return;
            }
        }
        if (switcher.is_open()) {
            switcher_done(switcher.mouse(m, x - r_chat.x - layout.switcher_at_x, y - r_chat.y - layout.switcher_at_y));
            return;
        }

        // hover (no button down, no drag): underline what a click would act on
        if (cmdpop_h > 0 && x >= cmdpop_x && x < cmdpop_x + cmdpop_w && y >= cmdpop_y && y < cmdpop_y + cmdpop_h) {
            // the command popup: hover marks a row, a click fills it in
            if (m.kind == MouseEventKind::Move && m.button == MouseButton::None) { cmdpop.hover_row(y - cmdpop_y); return; }
            if (m.button == MouseButton::ScrollUp || m.button == MouseButton::ScrollDown) {
                cmdpop.move(m.button == MouseButton::ScrollUp ? -1 : 1);
                cmdpop_details();
                return;
            }
            if (m.kind == MouseEventKind::Press && m.button == MouseButton::Left) {
                if (cmdpop.item_at_row(y - cmdpop_y)) cmdpop_fill();
                return;
            }
            return;
        }
        if (drag == Drag::None && m.kind == MouseEventKind::Move && m.button == MouseButton::None) {
            hover(x, y);
            return;
        }

        // multi-click detection (double / triple)
        const auto now = std::chrono::steady_clock::now();
        if (m.kind == MouseEventKind::Press && m.button == MouseButton::Left) {
            if (now - last_click < std::chrono::milliseconds(400) && std::abs(x - last_click_x) <= 1 && y == last_click_y) ++clicks;
            else clicks = 1;
            last_click = now; last_click_x = x; last_click_y = y;
        }

        // drags in progress
        if (drag != Drag::None) {
            if (m.kind == MouseEventKind::Move) {
                switch (drag) {
                case Drag::TreeDiv:
                    layout.tree_width = layout.switcher == MainLayout::Switcher::TreeRight
                                            ? std::clamp(W - 1 - x, 8, W / 2) : std::clamp(x, 8, W / 2);
                    break;
                case Drag::UsersDiv: layout.users_width = std::clamp(W - x - 1, 8, W / 2); break;
                case Drag::Select:
                    if (b) {
                        // drag past the top/bottom scrolls
                        if (y < r_chat.y) b->view.scroll(b->chat, 1);
                        if (y >= r_chat.y + r_chat.h) b->view.scroll(b->chat, -1);
                        const int cy = std::clamp(y - r_chat.y, 0, r_chat.h - 1);
                        if (auto h = b->view.hit(x - r_chat.x, cy)) b->view.select_extend(*h);
                    }
                    break;
                default: break;
                }
                return;
            }
            if (m.kind == MouseEventKind::Release) {
                if (drag == Drag::Select && b && b->view.has_selection())
                    copy(b->view.selection_text(b->chat));      // autocopy, as xtext
                drag = Drag::None;
                return;
            }
        }

        const Area a = area_at(x, y);
        const bool wheel_up = m.button == MouseButton::ScrollUp, wheel_dn = m.button == MouseButton::ScrollDown;
        const bool left = m.button == MouseButton::Left && m.kind == MouseEventKind::Press;
        const bool right = m.button == MouseButton::Right && m.kind == MouseEventKind::Press;
        const bool middle = m.button == MouseButton::Middle && m.kind == MouseEventKind::Press;

        switch (a) {
        case Area::Topic:
            if (left && clicks >= 2) act(A_Topic);
            else if (right && b) {
                ctx_kind = 7;
                ctx.open({{"Edit Topic…", "", A_Topic}, {"Copy Topic", "", A_InputCopy, -1, false}}, x, y + 1);
            }
            break;
        case Area::Tree: {
            const int row = y - r_tree.y;
            if (wheel_up || wheel_dn) { tree.move(wheel_up ? -1 : 1); break; }
            auto k = tree.key_at_row(row);
            if (!k) break;
            if (left) {
                const Buffer* tb = buf(*k);
                // the ▾/▸ of a network toggles it
                if (tb && tb->kind == ChanKind::Server && x - r_tree.x <= 2) { tree.select_key(*k); tree.toggle_collapse(); }
                else switch_to(*k);
                focus = Pane::Input;
            } else if (right) {
                open_tree_menu(*k, x, y);
            } else if (middle) {
                close_buffer(*k);
            }
            break;
        }
        case Area::TreeDivider:
            if (left && clicks >= 2) { layout.show_tree = false; build_menubar(); }
            else if (left) drag = Drag::TreeDiv;
            break;
        case Area::UsersDivider:
            if (left && clicks >= 2) { layout.show_users = false; build_menubar(); }
            else if (left) drag = Drag::UsersDiv;
            break;
        case Area::Strip: {
            // the GUI's tabs: wheel steps (gui_tab_scrollchans), click picks,
            // right-click its menu, middle-click closes (gui_tab_middleclose)
            if (wheel_up || wheel_dn) { step_buffer(wheel_up ? -1 : 1); break; }
            auto k = tree.key_at_col(x);
            if (!k) break;
            if (left) { switch_to(*k); focus = Pane::Input; }
            else if (right) open_tree_menu(*k, x, y);
            else if (middle && nx_core_pref_int("gui_tab_middleclose")) close_buffer(*k);
            break;
        }
        case Area::Users: {
            const int row = y - r_users.y;
            if (wheel_up || wheel_dn) { users.move(wheel_up ? -3 : 3); break; }
            auto nick = users.nick_at_row(row);
            if (!nick) break;
            users.select_nick(*nick);
            if (left && clicks >= 2) nx_core_nick_command(cur, nx_core_ulist_doubleclick(), nick->c_str());
            else if (left) focus = Pane::Users;
            else if (right) open_nick_menu(cur, *nick, x, y);
            break;
        }
        case Area::Chat: {
            if (!b) break;
            if (wheel_up) { b->view.scroll(b->chat, 3); break; }
            if (wheel_dn) { b->view.scroll(b->chat, -3); break; }
            auto h = b->view.hit(x - r_chat.x, y - r_chat.y);
            if (middle) { if (!clipboard.empty()) input.paste(clipboard); focus = Pane::Input; break; }
            if (!h) { if (left) b->view.select_clear(); if (right) open_text_menu(x, y); break; }
            auto word = ChatView::word_at(b->chat, *h);
            const int type = word ? nx_core_word_type(cur, word->text.c_str()) : 0;
            const bool is_nick = word && (h->in_left || type == 6);
            const bool is_url = word && (type == 1 || type == 3 || type == 4 || type == 5);
            const bool is_chan = word && type == 2;
            if (right) {
                if (b->view.has_selection()) open_text_menu(x, y);
                else if (is_nick) open_nick_menu(cur, word->text, x, y);
                else if (is_url) open_url_menu(word->text, x, y);
                else if (is_chan) open_chan_menu(word->text, x, y);
                else open_text_menu(x, y);
                break;
            }
            if (!left) break;
            if (clicks == 2) {
                if (is_nick) { nx_core_nick_command(cur, nx_core_ulist_doubleclick(), word->text.c_str()); break; }
                b->view.select_word(b->chat, *h);
                copy(b->view.selection_text(b->chat));
                break;
            }
            if (clicks >= 3) { b->view.select_line(b->chat, *h); copy(b->view.selection_text(b->chat)); break; }
            // a single click: open a link (Ctrl+click when the GUI wants a modifier)
            if (is_url && (nx_core_pref_int("gui_url_mod") == 0 || m.mods.ctrl)) { nx_core_open_url(word->text.c_str()); break; }
            if (is_chan && m.mods.ctrl) { command("join " + word->text); break; }
            if (is_nick) users.select_nick(word->text);
            b->view.select_begin(*h);
            drag = Drag::Select;
            break;
        }
        case Area::Status: {
            if (!left) break;
            switch (status_at(x)) {
            case StatusBar::Hot::Below: if (b) b->view.to_bottom(); break;
            case StatusBar::Hot::Network: netlist.open(); break;
            case StatusBar::Hot::Hint0: build_menubar(); menubar.open(0); break;
            case StatusBar::Hot::Hint1: open_palette(); break;
            case StatusBar::Hot::Hint2: show_keys(); break;
            default:
                if (x < W / 2) { if (Network* n = cur_net()) command(n->away ? "back" : "away"); }
                break;
            }
            break;
        }
        case Area::Input:
            if (middle) { if (!clipboard.empty()) input.paste(clipboard); }
            else if (right) open_input_menu(x, y - 5);   // above the framed input
            else if (wheel_up || wheel_dn) input.key(maya::KeyEvent{wheel_up ? SpecialKey::Up : SpecialKey::Down, {}, ""});
            else if (left) {
                // place the caret: the column past the prompt
                const int col = x - r_input.x - display_width(input_prompt_text());
                std::size_t at = 0;
                int w = 0;
                const std::string& t = input.text();
                while (at < t.size() && w < col) { w += glyph_width(t, at); at += utf8_len(static_cast<unsigned char>(t[at])); }
                input.set(t, at);
            }
            focus = Pane::Input;
            break;
        default:
            break;
        }
    }

    std::string input_prompt_text() {
        Network* n = cur_net();
        return " " + (n && !n->nick.empty() ? n->nick : std::string("nexchat")) + " ❯ ";
    }

    // ── the frame ───────────────────────────────────────────────────────
    maya::Element view() {
        using namespace maya::dsl;
        Buffer* b = current();
        Network* n = cur_net();
        const int w = W, h = H;

        // the input's prompt is the nick, like the GUI's nick button, then ❯
        {
            const std::string p = input_prompt_text();
            const std::size_t arrow = p.rfind("❯");
            Rich pr;
            pr.append(p.substr(0, arrow), n && n->away ? role::away() : role::title());
            pr.append(p.substr(arrow), role::muted());
            input.set_prompt(std::move(pr));
        }
        if (b) {
            const char* what = b->kind == ChanKind::Channel ? "Message "
                             : b->kind == ChanKind::Query ? "Message " : "";
            input.set_placeholder(*what ? std::string(what) + b->name : "Type a /command");
        }

        StatusBar status;
        if (n) {
            status.nick = n->nick;
            status.network = n->name;
            status.lag = n->connected ? n->lag : -1;
            status.away = n->away;
            status.connecting = n->connecting;
        }
        if (b) {
            // "alice is typing…": the dots are drawn, animated
            std::string ty = b->typing;
            for (const char* tail : {"…", "..."})
                if (ty.size() >= std::strlen(tail) && ty.compare(ty.size() - std::strlen(tail), std::string::npos, tail) == 0)
                    ty.erase(ty.size() - std::strlen(tail));
            status.typing_live = ty;
            status.lines_below = static_cast<int>(b->view.lines_below(b->chat));
        }
        if (!toast.empty() && std::chrono::steady_clock::now() < toast_until) status.typing = toast;
        status.frame = anim_frame;
        animating = status.animating();
        status.hints = {"F10 menu", "^P commands", "F1 keys"};
        status.hot = hover_area == Area::Status ? status_hot : StatusBar::Hot::None;

        TopicBar topic;
        if (b) {
            topic.channel = b->name; topic.modes = b->modes; topic.topic = b->topic;
            // the count shows here when the user list (which has it) doesn't
            const bool users_shown = layout.panes(W, b->kind == ChanKind::Channel).users;
            if (!users_shown) topic.users = b->total ? b->total : static_cast<int>(b->users.size());
        }
        // a short screen keeps its rows for the chat: no topic bar under 16
        const bool has_topic = show_topic && b && b->kind == ChanKind::Channel && h >= 16;

        MainParts parts;
        parts.topic = has_topic ? &topic : nullptr;
        parts.tree = &tree;
        parts.buffer = b ? &b->chat : nullptr;
        parts.view = b ? &b->view : nullptr;
        chat_style.nick = n ? n->nick : std::string{};
        parts.chat_style = &chat_style;
        parts.users = b && b->kind == ChanKind::Channel ? &users : nullptr;
        parts.status = &status;
        // an almost-empty tab's card
        MainParts::Welcome welcome;
        if (b) {
            if (b->kind == ChanKind::Channel) {
                welcome.name = b->name;
                welcome.what = b->topic.empty() ? "A channel" : "";
                welcome.hints = {"Say hello — or type / for commands", "Ctrl-P finds anything"};
            } else if (b->kind == ChanKind::Query) {
                welcome.name = b->name;
                welcome.what = "A private conversation";
                welcome.hints = {"Only the two of you see this"};
            } else if (b->kind == ChanKind::Server) {
                welcome.name = b->name;
                welcome.what = n && n->connected ? "Connected" : "The network’s own messages";
                welcome.hints = {"/join #channel to go in one · Ctrl-S the networks", "F1 every key · Ctrl-P every command"};
            }
            parts.welcome = &welcome;
        }
        (void)status.row(w);                 // lays out the clickable spans
        status_spans = status.spans_;
        Rich find_r;
        if (find_open) {
            find_r = find_bar(w);
            parts.find = &find_r;
        }
        parts.input = &input;
        reply_text = reply_bar();
        parts.reply = &reply_text;
        parts.switcher = &switcher;
        parts.hover_div = hover_area == Area::TreeDivider || drag == Drag::TreeDiv ? MainParts::Div::Tree
                        : hover_area == Area::UsersDivider || drag == Drag::UsersDiv ? MainParts::Div::Users
                        : MainParts::Div::None;
        // nothing in the window is focused while a popup has the keys
        parts.focus = (dialog.is_open() || ctx.is_open() || menubar.is_open() || switcher.is_open())
                          ? Pane::Input : focus;

        // geometry (mirrors MainLayout's arithmetic, for the mouse)
        const int top = show_menu ? 1 : 0;
        const bool strip_on = layout.tabs() && layout.show_tree;
        const bool strip_top = strip_on && layout.switcher == MainLayout::Switcher::TabsTop;
        const int topic_rows = has_topic ? 2 : 0;
        const int body_y = top + topic_rows + (strip_top ? 2 : 0);
        const int body_h = std::max(0, h - top - topic_rows - (strip_on ? 2 : 0) - MainLayout::kBottomRows);
        const auto pn = layout.panes(w, parts.users != nullptr);
        const bool tree_on = pn.tree;
        const bool tree_right = layout.switcher == MainLayout::Switcher::TreeRight;
        const bool users_on = pn.users;
        const int tw = pn.tw, uw = pn.uw;
        const int chat_x = tree_on && !tree_right ? tw + 1 : 0;
        const int chat_w = pn.chat_w;
        r_menu = {0, 0, show_menu ? w : 0, show_menu ? 1 : 0};
        r_topic = {0, top + (strip_top ? 2 : 0), has_topic ? w : 0, has_topic ? 1 : 0};
        if (tree_right) {
            r_tree = {w - tw, body_y, tw, tree_on ? body_h : 0};
            r_tree_div = {w - tw - 1, body_y, tree_on ? 1 : 0, body_h};
        } else {
            r_tree = {0, body_y, tw, tree_on ? body_h : 0};
            r_tree_div = {tw, body_y, tree_on ? 1 : 0, body_h};
        }
        r_strip = {0, strip_top ? top : body_y + body_h + 1, strip_on ? w : 0, strip_on ? 1 : 0};
        r_chat = {chat_x, body_y, chat_w, body_h};
        r_users_div = {chat_x + chat_w, body_y, users_on ? 1 : 0, body_h};
        r_users = {chat_x + chat_w + 1, body_y, uw, users_on ? body_h : 0};
        r_status = {0, h - 4, w, 1};
        r_input = {1, h - 2, std::max(1, w - 2), 1};   // inside its frame (rows h-3..h-1)

        std::vector<maya::Element> rows;
        if (show_menu) {
            char clock[16];
            const std::time_t t = std::time(nullptr);
            std::strftime(clock, sizeof clock, "%H:%M", std::localtime(&t));
            rows.push_back(menubar.row(w, clock).element());
        }
        rows.push_back(layout.element(parts, w, h - top));
        maya::Element window = (v(std::move(rows)) | height(h)).build();

        std::vector<maya::Element> layers{std::move(window)};
        if (cmdpop.is_open() && focus == Pane::Input && !find_open) {
            // over the chat, its bottom on the input's frame, as wide as the
            // chat area (at least 64 columns), as tall as it needs
            const int pw = std::clamp(std::max(r_chat.w, 64), 20, w);
            const int px = std::clamp(r_chat.x, 0, std::max(0, w - pw));
            const int bottom = h - MainLayout::kBottomRows;   // the rule over the status row
            auto pr = cmdpop.rows(pw, std::max(3, std::min(bottom - top - 1, 20)));
            cmdpop_y = bottom - static_cast<int>(pr.size());
            cmdpop_x = px;
            cmdpop_h = static_cast<int>(pr.size());
            cmdpop_w = pw;
            layers.push_back((v(stack_rows(pr)) | padding(std::max(cmdpop_y, 0), 0, 0, px)).build());
        } else {
            cmdpop_h = 0;
        }
        if (menubar.is_open()) layers.push_back(menubar.overlay(w, h));
        if (ctx.is_open()) layers.push_back(ctx.element(w, h));
        if (netlist.is_open()) layers.push_back(netlist.element(w, h));
        if (prefs.is_open()) layers.push_back(prefs.element(w, h));
        if (chanlist.is_open()) layers.push_back(chanlist.element(w, h));
        if (people.is_open()) layers.push_back(people.element(w, h));
        if (transfers.is_open()) layers.push_back(transfers.element(w, h));
        if (tools.is_open()) layers.push_back(tools.element(w, h));
        if (bouncer.is_open()) layers.push_back(bouncer.element(w, h));
        if (preview.open) layers.push_back(preview_element(w, h));
        if (dialog.is_open()) layers.push_back(dialog.element(w, h));
        if (layers.size() == 1) return std::move(layers[0]);
        return maya::detail::zstack(std::move(layers));
    }

    bool present() {
        if (!screen) return false;
        if (menus_stale && !menubar.is_open()) { menus_stale = false; build_menubar(); }
        const auto sz = screen->size();
        W = sz.width.value; H = sz.height.value;
        if (!screen->ready() || screen->pending_output()) return false;
        const maya::Presented f = screen->present([&] { return view(); });
        dirty = f.redraw_now;
        // scrolled up to the first line: ask the server for what came before
        if (Buffer* b = current(); b && !b->older_asked && !b->older_none && b->view.lines_below(b->chat) > 0
                                   && b->view.shows_top(b->chat)) {
            std::time_t oldest = 0;
            for (std::size_t i = 0; i < b->chat.size() && !oldest; ++i) oldest = b->chat[i].stamp;
            if (nx_core_history_older(core_key(cur), oldest)) { b->older_asked = true; flash("loading older messages…"); }
        }
        return f.redraw_now || f.redraw_at.has_value()
            || (!toast.empty() && std::chrono::steady_clock::now() < toast_until + std::chrono::milliseconds(50));
    }
};

std::unique_ptr<Ui> g_ui;

Ui& ui() {
    if (!g_ui) g_ui = std::make_unique<Ui>();
    return *g_ui;
}

ChanKind kind_of(int k) {
    switch (k) {
    case NX_BUF_SERVER: return ChanKind::Server;
    case NX_BUF_CHANNEL: return ChanKind::Channel;
    case NX_BUF_QUERY: return ChanKind::Query;
    default: return ChanKind::Other;
    }
}

}  // namespace
}  // namespace nx::tui

// ── bridge.h ────────────────────────────────────────────────────────────

using namespace nx::tui;

extern "C" {

int nx_ui_open(void) {
    Ui& u = ui();
    // hover underlines need bare-motion reports (mode 1003); a terminal
    // that handles them badly: hover=0 in tui.conf
    const char* hv = nx_core_tui_get("hover");
    const bool hover = !(hv && *hv && (hv[0] == '0' || hv[0] == 'f' || hv[0] == 'n'));
    auto s = maya::Screen::open({.title = "nexchat", .mouse = true, .hover_motion = hover});
    if (!s) return 1;
    u.screen.emplace(std::move(*s));
    u.dirty = true;
    return 0;
}

void nx_ui_close(void) {
    if (g_ui && g_ui->screen) g_ui->set_pointer("default");
    if (g_ui) g_ui->screen.reset();
}

int nx_ui_input_fd(void) { return ui().screen ? static_cast<int>(ui().screen->input_handle()) : -1; }
int nx_ui_output_fd(void) { return ui().screen ? static_cast<int>(ui().screen->output_handle()) : -1; }
int nx_ui_wants_output(void) { return ui().screen && ui().screen->pending_output(); }

void nx_ui_on_input(void) {
    Ui& u = ui();
    if (!u.screen) return;
    auto evs = u.screen->read();
    if (!evs) return;
    if (!evs->empty()) u.dirty = true;   // frame acks come back empty: no redraw
    for (auto& ev : *evs) {
        if (auto* k = std::get_if<maya::KeyEvent>(&ev)) u.key(*k);
        else if (auto* m = std::get_if<maya::MouseEvent>(&ev)) {
            // maya reports cells 1-based (as the terminal does); everything
            // here is 0-based, the way frames are laid out.
            maya::MouseEvent z = *m;
            z.x = maya::Columns{m->x.value - 1};
            z.y = maya::Rows{m->y.value - 1};
            if (std::getenv("NX_TUI_TRACE"))
                std::fprintf(stderr, "mouse b=%d k=%d x=%d y=%d\n", static_cast<int>(z.button),
                             static_cast<int>(z.kind), z.x.value, z.y.value);
            u.mouse(z);
            u.dirty = true;   // hover states change on bare motion too
        }
        else if (auto* p = std::get_if<maya::PasteEvent>(&ev)) u.paste(p->content);
        else if (auto* f = std::get_if<maya::FocusEvent>(&ev)) u.focused = f->focused;
        else if (std::holds_alternative<maya::ResizeEvent>(ev)) u.screen->on_resize();
    }
}

void nx_ui_on_output(void) { if (ui().screen) (void)ui().screen->flush(); }
void nx_ui_on_resize(void) { if (ui().screen) ui().screen->on_resize(); ui().dirty = true; }
int nx_ui_pending_ms(void) { return ui().screen && ui().screen->has_pending_input() ? 30 : 0; }

void nx_ui_on_pending(void) {
    Ui& u = ui();
    if (!u.screen) return;
    for (auto& ev : u.screen->resolve_pending_input())
        if (auto* k = std::get_if<maya::KeyEvent>(&ev)) { u.key(*k); u.dirty = true; }
}

int nx_ui_present(void) { return ui().present() ? 1 : 0; }
int nx_ui_dirty(void) { return ui().dirty ? 1 : 0; }

void nx_ui_buffer_new(nx_key k, nx_key net, int kind, const char* name, int focus) {
    Ui& u = ui();
    auto b = std::make_unique<Buffer>();
    b->key = k; b->net = net; b->kind = kind_of(kind); b->name = name ? name : "";
    const int max = nx_core_pref_int("text_max_lines");
    b->chat.set_max_lines(max > 0 ? static_cast<std::size_t>(max) : 5000);
    u.bufs[k] = std::move(b);
    u.buf_order.push_back(k);
    if (!u.nets.count(net)) { u.nets[net] = {}; u.net_order.push_back(net); }
    if (focus || !u.cur) u.switch_to(k, false);
    u.sync_tree();
    u.dirty = true;
}

void nx_ui_buffer_free(nx_key k) {
    Ui& u = ui();
    if (u.tools.is_open(Tools::Kind::Bans) && u.tools.ban_buf() == k) u.tools.close();
    if (!u.bufs.count(k)) return;
    auto o = u.ordered();
    auto it = std::find(o.begin(), o.end(), k);
    nx_key next = 0;
    if (it != o.end()) {
        if (it + 1 != o.end()) next = *(it + 1);
        else if (it != o.begin()) next = *(it - 1);
    }
    u.bufs.erase(k);
    u.buf_order.erase(std::remove(u.buf_order.begin(), u.buf_order.end(), k), u.buf_order.end());
    if (u.cur == k) { u.cur = 0; if (next) u.switch_to(next); }
    u.sync_tree();
    u.dirty = true;
}

void nx_ui_buffer_rename(nx_key k, const char* name) {
    if (Buffer* b = ui().buf(k)) { b->name = name ? name : ""; ui().sync_tree(); ui().dirty = true; }
}

void nx_ui_buffer_focus(nx_key k) {
    if (ui().cur != k) ui().switch_to(k, false);
}

void nx_ui_buffer_activity(nx_key k, int a) {
    Ui& u = ui();
    Buffer* b = u.buf(k);
    if (!b) return;
    const Activity na = a == NX_ACT_HILIGHT ? Activity::Hilight : a == NX_ACT_MSG ? Activity::Message
                      : a == NX_ACT_DATA ? Activity::Data : Activity::None;
    if (k == u.cur) { b->activity = Activity::None; b->unread = 0; }
    else {
        if (na >= Activity::Message && na >= b->activity) ++b->unread;
        b->activity = std::max(b->activity, na);
        if (na == Activity::None) { b->activity = na; b->unread = 0; }
    }
    u.sync_tree();
    u.dirty = true;
}

void nx_ui_buffer_joined(nx_key k, int joined) {
    if (Buffer* b = ui().buf(k)) { b->joined = joined != 0; ui().sync_tree(); ui().dirty = true; }
}
void nx_ui_buffer_topic(nx_key k, const char* t) { if (Buffer* b = ui().buf(k)) { b->topic = t ? t : ""; ui().dirty = true; } }
void nx_ui_buffer_modes(nx_key k, const char* m) { if (Buffer* b = ui().buf(k)) { b->modes = m ? m : ""; ui().dirty = true; } }
void nx_ui_buffer_typing(nx_key k, const char* t) { if (Buffer* b = ui().buf(k)) { b->typing = t ? t : ""; ui().dirty = true; } }

void nx_ui_print(nx_key k, const char* text, time_t stamp, int kind) {
    if (Buffer* b = ui().buf(k)) {
        if (nx_core_pref_int("text_day_sep")) b->chat.add_day_if_new(stamp);
        using K = ChatLine::Kind;
        const K lk = kind & NX_LINE_ACTION ? K::Action
                   : kind & NX_LINE_OWN    ? K::Own
                   : kind & NX_LINE_MSG    ? K::Message
                   : kind & NX_LINE_NOTICE ? K::Notice : K::Status;
        b->chat.add(text ? text : "", stamp, (kind & NX_LINE_HILIGHT) != 0, lk);
        // the line a join/part run folds into
        if (b->joinpart.pending) { b->joinpart.pending = false; b->joinpart.line = b->chat[b->chat.size() - 1].id; }
        ui().dirty = true;
    }
}

void nx_ui_clear(nx_key k, int lines) { if (Buffer* b = ui().buf(k)) { b->chat.trim(lines); ui().dirty = true; } }

int nx_ui_focused(void) { return ui().focused ? 1 : 0; }
void nx_ui_bell(void) { if (ui().screen) ui().screen->emit_host_sequence("\a"); }
void nx_ui_reply_pick(nx_key k, const char* msgid) {
    Ui& u = ui();
    if (msgid && *msgid) u.set_reply(k, msgid);
    else if (k == u.cur) u.open_reply_picker(k);
    u.dirty = true;
}

void nx_ui_print_older(nx_key k, const char* text, time_t stamp) {
    if (Buffer* b = ui().buf(k)) b->older.emplace_back(text ? text : "", stamp);
}

void nx_ui_older_done(nx_key k, int n) {
    Buffer* b = ui().buf(k);
    if (!b) return;
    b->older_asked = false;
    if (n <= 0 || b->older.empty()) { b->older_none = true; b->older.clear(); ui().dirty = true; return; }
    // newest of the batch first, each on top of the last
    for (auto it = b->older.rbegin(); it != b->older.rend(); ++it)
        if (!b->chat.add_older(it->first, it->second)) { b->older_none = true; break; }
    b->older.clear();
    ui().dirty = true;
}

void nx_ui_menus_changed(void) {
    // rebuilt on the next tick: the core calls this before it frees the
    // entry (and its children), and not while a menu is open either
    ui().menus_stale = true;
    ui().dirty = true;
}

int nx_ui_line_count(nx_key k) {
    Buffer* b = ui().buf(k);
    return b ? static_cast<int>(b->chat.size()) : 0;
}

const char* nx_ui_line_get(nx_key k, int i, time_t* stamp) {
    static std::string out;
    Buffer* b = ui().buf(k);
    if (!b || i < 0 || static_cast<std::size_t>(i) >= b->chat.size()) return nullptr;
    const ChatLine& l = b->chat[static_cast<std::size_t>(i)];
    if (l.day) return "";
    out = l.left.empty() ? l.right : l.left + "\t" + l.right;
    if (stamp) *stamp = l.stamp;
    return out.c_str();
}

// Joins, parts and quits in a row fold into one line (the GUI's
// fe_print_joinpart): "→ alice, bob joined · ← carol left". Anything else
// printed in between starts a new one.
int nx_ui_joinpart(nx_key k, int kind, const char* nick, time_t stamp) {
    Buffer* b = ui().buf(k);
    if (!b || !nick || !*nick) return 0;
    auto& jp = b->joinpart;
    const bool ours = jp.line && !b->chat.empty() && b->chat[b->chat.size() - 1].id == jp.line;
    if (!ours) {
        // the first of a run prints as the event itself
        jp = {};
        jp.first = true;
    }
    auto drop = [](std::vector<std::string>& v, const char* n) {
        auto it = std::find_if(v.begin(), v.end(), [&](const std::string& s) { return !strcasecmp(s.c_str(), n); });
        if (it == v.end()) return false;
        v.erase(it);
        return true;
    };
    if (kind == 0) { drop(jp.left, nick); drop(jp.joined, nick); jp.joined.emplace_back(nick); }
    else if (!drop(jp.joined, nick)) { drop(jp.left, nick); jp.left.emplace_back(nick); }
    if (jp.first && jp.joined.size() + jp.left.size() == 1) {
        // one event prints as usual (reason, host, colours); nx_ui_print
        // records its line so the next one replaces it
        jp.first = false;
        jp.pending = true;
        return 0;
    }
    jp.first = false;
    auto list = [](const std::vector<std::string>& v) {
        std::string s;
        const std::size_t shown = std::min<std::size_t>(v.size(), 12);
        for (std::size_t i = 0; i < shown; ++i) { if (i) s += ", "; s += v[i]; }
        if (v.size() > shown) s += " and " + std::to_string(v.size() - shown) + " more";
        return s;
    };
    std::string t = "*\t";
    if (!jp.joined.empty()) t += "\00303→\017 " + list(jp.joined) + " joined";
    if (!jp.left.empty()) {
        if (!jp.joined.empty()) t += "\u00a0\00314·\017  ";
        t += "\00305←\017 " + list(jp.left) + " left";
    }
    if (jp.joined.empty() && jp.left.empty()) t += std::string("\00314") + nick + " came and went";
    if (ours) b->chat.replace_back(t);
    else { b->chat.add(t, stamp, false, ChatLine::Kind::Status); jp.line = b->chat[b->chat.size() - 1].id; }
    ui().dirty = true;
    return 1;
}

// a desktop notification through the terminal: OSC 777 (VTE, foot,
// WezTerm, Ghostty) or OSC 9 (iTerm2, kitty, Windows Terminal); inside tmux
// wrapped for passthrough
void nx_ui_notify(const char* title, const char* body) {
    Ui& u = ui();
    if (!u.screen) return;
    auto clean = [](const char* s, std::size_t max) {
        std::string o;
        for (const char* p = s ? s : ""; *p && o.size() < max; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c < 0x20 || c == 0x7f) continue;          // no ESC/BEL/IRC codes
            o += (c == ';') ? ',' : static_cast<char>(c);   // ; splits OSC 777 fields
        }
        return o;
    };
    const std::string t = clean(title, 80), b = clean(body, 240);
    auto env = [](const char* k) { const char* v = std::getenv(k); return std::string(v ? v : ""); };
    const std::string term = env("TERM"), prog = env("TERM_PROGRAM");
    const bool osc777 = !env("VTE_VERSION").empty() || term.rfind("foot", 0) == 0
                        || prog == "WezTerm" || prog == "ghostty" || term == "xterm-ghostty";
    std::string seq = osc777 ? "\x1b]777;notify;" + t + ";" + b + "\x1b\\"
                             : "\x1b]9;" + t + (b.empty() ? "" : ": " + b) + "\x1b\\";
    if (!env("TMUX").empty()) {
        std::string w = "\x1bPtmux;";
        for (char c : seq) { w += c; if (c == '\x1b') w += '\x1b'; }
        seq = w + "\x1b\\";
    }
    u.screen->emit_host_sequence(seq);
}

// the input box, for scripts (fe_get/set_inputbox_*): the line on screen for
// the front buffer, the remembered draft for any other; the cursor counts
// characters, as the GUI's does
namespace {
std::size_t cp_to_byte(const std::string& s, int cp) {
    std::size_t i = 0;
    for (int n = 0; i < s.size() && n < cp; ++n) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    }
    return i;
}
int byte_to_cp(const std::string& s, std::size_t b) {
    int n = 0;
    for (std::size_t i = 0; i < b && i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}
}  // namespace

const char* nx_ui_input_get(nx_key k) {
    Ui& u = ui();
    if (k == u.cur) return u.input.text().c_str();
    if (Buffer* b = u.buf(k)) return b->draft.c_str();
    return "";
}
int nx_ui_input_cursor(nx_key k) {
    Ui& u = ui();
    if (k == u.cur) return byte_to_cp(u.input.text(), u.input.cursor());
    if (Buffer* b = u.buf(k)) return byte_to_cp(b->draft, b->draft_cursor);
    return 0;
}
void nx_ui_input_set(nx_key k, const char* text) {
    Ui& u = ui();
    std::string t = text ? text : "";
    if (k == u.cur) u.input.set(std::move(t));
    else if (Buffer* b = u.buf(k)) { b->draft_cursor = t.size(); b->draft = std::move(t); }
    u.dirty = true;
}
void nx_ui_input_set_cursor(nx_key k, int delta, int pos) {
    Ui& u = ui();
    if (k == u.cur) {
        const std::string& s = u.input.text();
        const int p = std::max(0, pos + (delta ? byte_to_cp(s, u.input.cursor()) : 0));
        u.input.set(s, cp_to_byte(s, p));
    } else if (Buffer* b = u.buf(k)) {
        const int p = std::max(0, pos + (delta ? byte_to_cp(b->draft, b->draft_cursor) : 0));
        b->draft_cursor = cp_to_byte(b->draft, p);
    }
    u.dirty = true;
}

void nx_ui_users_clear(nx_key k) {
    if (Buffer* b = ui().buf(k)) { b->users.clear(); b->total = b->ops = 0; if (k == ui().cur) ui().sync_users(); ui().dirty = true; }
}

void nx_ui_user_set(nx_key k, const char* nick, char prefix, int away, int me, int pos) {
    Buffer* b = ui().buf(k);
    if (!b) return;
    UserEntry e{nick ? nick : "", prefix == ' ' ? '\0' : prefix, away != 0, me != 0};
    if (pos >= 0 && static_cast<std::size_t>(pos) <= b->users.size()) b->users.insert(b->users.begin() + pos, std::move(e));
    else b->users.push_back(std::move(e));
    if (k == ui().cur) ui().sync_users();
    ui().dirty = true;
}

void nx_ui_user_remove(nx_key k, const char* nick) {
    Buffer* b = ui().buf(k);
    if (!b) return;
    std::erase_if(b->users, [&](const UserEntry& e) { return e.nick == nick; });
    if (k == ui().cur) ui().sync_users();
    ui().dirty = true;
}

void nx_ui_users_count(nx_key k, int total, int ops) {
    if (Buffer* b = ui().buf(k)) { b->total = total; b->ops = ops; if (k == ui().cur) ui().sync_users(); ui().dirty = true; }
}

void nx_ui_network_new(nx_key n, const char* name) {
    Ui& u = ui();
    if (!u.nets.count(n)) u.net_order.push_back(n);
    u.nets[n].name = name ? name : "";
    u.dirty = true;
}
void nx_ui_network_free(nx_key n) {
    Ui& u = ui();
    u.nets.erase(n);
    u.net_order.erase(std::remove(u.net_order.begin(), u.net_order.end(), n), u.net_order.end());
    u.dirty = true;
}
void nx_ui_network_name(nx_key n, const char* name) { ui().nets[n].name = name ? name : ""; ui().dirty = true; }
void nx_ui_network_nick(nx_key n, const char* nick) { ui().nets[n].nick = nick ? nick : ""; ui().dirty = true; }
void nx_ui_network_state(nx_key n, int c, int cing) {
    auto& net = ui().nets[n];
    net.connected = c != 0; net.connecting = cing != 0;
    ui().sync_tree();
    ui().dirty = true;
}
void nx_ui_network_lag(nx_key n, double s) { ui().nets[n].lag = s; ui().dirty = true; }
void nx_ui_network_away(nx_key n, int away) { ui().nets[n].away = away != 0; ui().build_menubar(); ui().dirty = true; }

void nx_ui_message(const char* text, int error) {
    Ui& u = ui();
    std::string t = text ? strip_irc(text) : "";
    if (error) u.queue_dialog({0, "nexchat", t, "", nullptr, nullptr, nullptr, true});
    else u.flash(t);
    u.dirty = true;
}

void nx_ui_prompt(const char* title, const char* initial, nx_str_done done, void* ud) {
    ui().queue_dialog({2, "nexchat", title ? title : "", initial ? initial : "", done, nullptr, ud});
}

void nx_ui_confirm(const char* message, nx_yes_done done, void* ud) {
    ui().queue_dialog({1, "nexchat", message ? message : "", "", nullptr, done, ud});
}

void nx_ui_open_network_list(void) {
    ui().netlist.open();
    ui().dirty = true;
}

void nx_ui_chanlist_begin(nx_key net) {
    Ui& u = ui();
    if (!u.chanlist.is_open() || u.chanlist.net() != net) {
        auto it = u.nets.find(net);
        u.chanlist.open(net, it == u.nets.end() ? "" : it->second.name);
    }
    u.chanlist.begin();
    u.dirty = true;
}

void nx_ui_chanlist_add(nx_key net, const char* chan, int users, const char* topic) {
    Ui& u = ui();
    if (u.chanlist.is_open() && u.chanlist.net() == net) u.chanlist.add(chan ? chan : "", users, topic ? topic : "");
    u.dirty = true;
}

void nx_ui_chanlist_end(nx_key net) {
    Ui& u = ui();
    if (u.chanlist.is_open() && u.chanlist.net() == net) u.chanlist.end();
    u.dirty = true;
}

void nx_ui_lists_changed(void) {
    if (ui().people.is_open()) ui().people.reload();
    ui().dirty = true;
}

void nx_ui_dcc_changed(void) {
    if (ui().transfers.is_open()) ui().transfers.reload();
    ui().dirty = true;
}

void nx_ui_url_added(void) {
    if (ui().tools.is_open(Tools::Kind::Urls)) ui().tools.reload();
    ui().dirty = true;
}
void nx_ui_mentions_changed(void) {
    if (ui().tools.is_open(Tools::Kind::Mentions)) ui().tools.reload();
    ui().dirty = true;
}
void nx_ui_plugins_changed(void) {
    if (ui().tools.is_open(Tools::Kind::Plugins)) ui().tools.reload();
    ui().dirty = true;
}
void nx_ui_ban_add(nx_key buf, int kind, const char* mask, const char* who, const char* when) {
    ui().tools.ban_add(buf, kind, mask ? mask : "", who ? who : "", when ? when : "");
    ui().dirty = true;
}
void nx_ui_ban_end(nx_key buf) {
    ui().tools.ban_end(buf);
    ui().dirty = true;
}

int nx_ui_open_transfers(int passive) {
    Ui& u = ui();
    const bool was = u.transfers.is_open();
    // passive (an offer arriving): don't throw a window over what you're doing
    if (!was && !passive) u.transfers.open();
    u.dirty = true;
    return was ? 1 : 0;
}

void nx_ui_rawlog(nx_key net, const char* line, int outbound) {
    ui().rawlog(net, line ? line : "", outbound != 0);
}

void nx_ui_bouncer_changed(void) { ui().dirty = true; }

void nx_ui_open_bouncer(int from) {
    ui().bouncer.open(from);
    ui().dirty = true;
}

/* the 1 s tick from the loop: windows that refresh by themselves */
int nx_ui_animating(void) { return ui().animating ? 1 : 0; }
int nx_ui_anim(void) {
    Ui& u = ui();
    if (!u.animating) return 0;
    ++u.anim_frame;
    u.dirty = true;
    return 1;
}

int nx_ui_tick(void) {
    Ui& u = ui();
    if (u.bouncer.tick()) u.dirty = true;
    if (u.menus_stale && !u.menubar.is_open()) { u.menus_stale = false; u.build_menubar(); u.dirty = true; }
    u.check_prefs();
    return u.dirty ? 1 : 0;
}

}  // extern "C"
