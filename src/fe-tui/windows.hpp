#pragma once
// The TUI's windows over the core: each owns a widgets/Panel and knows the
// bridge (bridge.h), so ui.cpp only opens them and routes events.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <maya/element/element.hpp>
#include <maya/terminal/input.hpp>

#include "inputline.hpp"
#include "panel.hpp"

namespace nx::tui {

/// Network List (servlistgui.c): the networks on the left, the selected
/// one's settings on the right, the global nicks at the top of the list.
class NetworkList {
public:
    void open();
    [[nodiscard]] bool is_open() const { return panel_.is_open(); }
    void close();

    /// Returns true when it closed (Close, Esc, or a Connect).
    bool key(const maya::KeyEvent& k);
    bool mouse(const maya::MouseEvent& m);
    void paste(std::string_view s) { panel_.paste(s); }
    [[nodiscard]] maya::Element element(int w, int h) const { return panel_.element(w, h); }

    /// A message for the status bar after an action ("" = none).
    std::string flash;
    /// Bouncer... was pressed: open the Bouncer window from this network
    /// (-1 none, -2 not pressed). The owner resets it.
    int bouncer_from = -2;

private:
    enum Id {
        F_NICK1 = 1, F_NICK2, F_NICK3, F_USER,                      // global
        F_NAME = 10, F_SERVERS, F_AUTOJOIN, F_COMMANDS,
        F_USE_GLOBAL, F_NETNICK, F_NETNICK2, F_NETREAL, F_NETUSER,
        F_LOGIN, F_PASS, F_ENCODING,
        F_SSL, F_ALLOW_INVALID, F_CYCLE, F_AUTO, F_FAVORITE, F_PROXY,
        F_SKIP,
        B_CONNECT = 100, B_ADD, B_REMOVE, B_CLOSE, B_BOUNCER,
    };
    void reload_list(int sel);
    void load(int i);
    void save();
    bool handle(Panel::Outcome o);

    Panel panel_;
    int   cur_ = -1;        // the network in the form (-1: the global user page)
};

/// Preferences (setup.c): its pages a terminal has a use for, live-applied.
class Preferences {
public:
    void open();
    [[nodiscard]] bool is_open() const { return panel_.is_open(); }
    void close();
    bool key(const maya::KeyEvent& k);
    bool mouse(const maya::MouseEvent& m);
    void paste(std::string_view s) { panel_.paste(s); }
    [[nodiscard]] maya::Element element(int w, int h) const { return panel_.element(w, h); }
    /// A pref changed since the owner last looked (re-read what it shows).
    bool changed = false;

private:
    enum { B_CLOSE = 1 };
    void load(int page);
    void apply();
    bool handle(Panel::Outcome o);
    Panel panel_;
    int   page_ = 0;
};

/// Channel List (chanlist.c): /LIST in a sortable, filterable table.
class ChannelList {
public:
    void open(std::uintptr_t net, std::string network_name);
    [[nodiscard]] bool is_open() const { return panel_.is_open(); }
    [[nodiscard]] std::uintptr_t net() const { return net_; }
    void close();
    bool key(const maya::KeyEvent& k);
    bool mouse(const maya::MouseEvent& m);
    void paste(std::string_view s);
    [[nodiscard]] maya::Element element(int w, int h) const { return panel_.element(w, h); }

    // the core's rows
    void begin();
    void add(std::string chan, int users, std::string topic);
    void end() { loading_ = false; refilter(); }

private:
    struct Row { std::string chan; int users; std::string topic; };
    enum { B_JOIN = 1, B_REFRESH, B_CLOSE };
    enum class Sort { Users, Name };
    void refilter();
    void join_selected();
    [[nodiscard]] std::vector<Rich> draw(int w, int h, bool focused) const;
    bool body_key(const maya::KeyEvent& k);
    bool handle(Panel::Outcome o);

    Panel panel_;
    std::uintptr_t net_ = 0;
    std::string network_;
    std::vector<Row> rows_;
    std::vector<std::size_t> shown_;
    std::size_t sel_ = 0;
    mutable int top_ = 0;
    Sort sort_ = Sort::Users;
    bool loading_ = false;
    InputLine filter_;
    int min_users_ = 0;
};

/// Ignore List (ignoregui.c) and Friends List (notifygui.c): a table of
/// entries with Add / Remove, and for ignores a toggle per type.
class PeopleList {
public:
    enum class Kind { Ignore, Friends };
    void open(Kind k);
    [[nodiscard]] bool is_open() const { return panel_.is_open(); }
    void close() { panel_.close(); }
    bool key(const maya::KeyEvent& k);
    bool mouse(const maya::MouseEvent& m);
    void paste(std::string_view s) { panel_.paste(s); }
    [[nodiscard]] maya::Element element(int w, int h) const { return panel_.element(w, h); }
    /// The core changed the list: re-read it.
    void reload();

private:
    enum { B_ADD = 1, B_REMOVE, B_CLOSE, F_MASK = 100, F_NETS, F_TYPE0 = 200 };
    void load(int i);
    void reload_rows();
    void save();
    bool handle(Panel::Outcome o);
    Panel panel_;
    Kind  kind_ = Kind::Ignore;
    std::vector<std::string> names_;   // the masks / nicks, by list row
    int   cur_ = -1;
    bool  editing_ = false;            // our own change is going through the core
};

/// File Transfers (dccgui.c): every transfer with progress, speed and ETA;
/// Accept, Resume, Abort, Clear.
class Transfers {
public:
    void open();
    [[nodiscard]] bool is_open() const { return panel_.is_open(); }
    void close() { panel_.close(); }
    bool key(const maya::KeyEvent& k);
    bool mouse(const maya::MouseEvent& m);
    [[nodiscard]] maya::Element element(int w, int h) const { return panel_.element(w, h); }
    void reload();

private:
    enum { B_ACCEPT = 1, B_RESUME, B_ABORT, B_CLEAR, B_CLOSE };
    struct Row { std::uintptr_t id; int kind, state; std::string nick, file;
                 unsigned long long size, pos; double cps; long eta; };
    [[nodiscard]] std::vector<Rich> draw(int w, int h, bool focused) const;
    bool body_key(const maya::KeyEvent& k);
    bool handle(Panel::Outcome o);
    void buttons();
    Panel panel_;
    std::vector<Row> rows_;
    std::size_t sel_ = 0;
    mutable int top_ = 0;
};

/// Server > Bouncer... (fe-gtk/bncwin.c) and its Stay online setup
/// (bncsetup.c): bouncerwin.cpp.
class BouncerWin;
class Bouncer {
public:
    Bouncer();
    ~Bouncer();
    /// from: the network it's opened from (-1 none)
    void open(int from);
    /// straight to Stay online setup (the Network List's Bouncer... does the window)
    void open_setup(int from);
    [[nodiscard]] bool is_open() const;
    void close();
    void key(const maya::KeyEvent& k);
    void mouse(const maya::MouseEvent& m);
    void paste(std::string_view s);
    /// the 3 s refresh: true when what it shows changed
    bool tick();
    [[nodiscard]] maya::Element element(int w, int h) const;

private:
    std::unique_ptr<BouncerWin> w_;
};

/// The Window menu's tables (tools.cpp): URL Grabber (urlgrab.c),
/// Mentions (mentions.c), Plugins and Scripts (plugingui.c), Ban List
/// (banlist.c). One is open at a time, like every panel.
class Tools {
public:
    enum class Kind { Urls, Mentions, Plugins, Bans };
    /// Bans: buf is the channel's buffer, name its title
    void open(Kind k, std::uintptr_t buf = 0, std::string name = {});
    [[nodiscard]] bool is_open() const { return panel_.is_open(); }
    [[nodiscard]] bool is_open(Kind k) const { return panel_.is_open() && kind_ == k; }
    void close();
    void key(const maya::KeyEvent& k);
    void mouse(const maya::MouseEvent& m);
    void paste(std::string_view) {}
    [[nodiscard]] maya::Element element(int w, int h) const { return panel_.element(w, h); }
    /// the core changed what it shows
    void reload();
    void ban_add(std::uintptr_t buf, int kind, std::string mask, std::string who, std::string when);
    void ban_end(std::uintptr_t buf);
    [[nodiscard]] std::uintptr_t ban_buf() const { return buf_; }

    // what the UI does once a key or click is through (cleared by it)
    std::string flash, copy;
    std::uintptr_t go_to = 0;           // Mentions: switch to this buffer
    enum class Ask { None, SaveUrls, LoadPlugin, ClearBans };
    Ask ask = Ask::None;
    /// the answer to `ask` (a path, or yes)
    void answer(Ask a, bool ok, const std::string& text);

private:
    enum { B_A = 1, B_B, B_C, B_D, B_E, B_CLOSE };
    struct Row { std::vector<std::string> cells; std::uintptr_t key = 0; int kind = 0; bool marked = false; };
    struct Col { std::string title; int width; };   // width 0: the rest
    void buttons();
    void press(int b);
    void activate();
    void status();
    bool body_key(const maya::KeyEvent& k);
    std::vector<Rich> draw(int w, int h, bool focused) const;
    void refresh_bans();
    void remove_bans(bool keep_marked);
    [[nodiscard]] std::vector<std::size_t> marked_or_cursor() const;
    Panel panel_;
    Kind kind_ = Kind::Urls;
    std::vector<Col> cols_;
    std::vector<Row> rows_;
    std::size_t sel_ = 0;
    mutable int top_ = 0;
    // the Ban List
    std::uintptr_t buf_ = 0;
    std::string name_;
    int checked_ = 1, capable_ = 1, checkable_ = 1;
    bool loading_ = false, op_ = false;
};

}  // namespace nx::tui
