#pragma once
// nx::tui::MainLayout — the main window, composed from the widgets.
//
//   ┌ topic bar ───────────────────────────────────────────┐
//   │ chan tree │ chat view                     │ user list │
//   │           │                               │           │
//   ├ status bar ──────────────────────────────────────────┤
//   └ input line ──────────────────────────────────────────┘
//
// Responsive, the way a terminal needs it: below `wide` columns the user
// list goes, below `narrow` the channel tree goes too (Ctrl-G's switcher
// and Alt-←/→ still reach every buffer). A thin │ separates panes. The
// switcher, when open, floats over the chat.
//
// The layout borrows everything; it holds no state of its own beyond the
// thresholds, so the owner decides what's focused and visible.

#include <maya/element/element.hpp>

#include "bars.hpp"
#include "chantree.hpp"
#include "chat.hpp"
#include "inputline.hpp"
#include "switcher.hpp"
#include "userlist.hpp"

namespace nx::tui {

enum class Pane : std::uint8_t { Input, Tree, Users };

struct MainParts {
    const TopicBar*   topic = nullptr;     // null: no topic row (server tab)
    const ChanTree*   tree = nullptr;
    const ChatBuffer* buffer = nullptr;
    const ChatView*   view = nullptr;
    const ChatStyle*  chat_style = nullptr;
    const UserList*   users = nullptr;     // null: not a channel
    const StatusBar*  status = nullptr;
    const InputLine*  input = nullptr;
    const Switcher*   switcher = nullptr;  // drawn when open
    Pane              focus = Pane::Input;
    const Rich*       find = nullptr;      // the find bar: drawn over the status row
    /// the divider under the pointer (draggable): drawn in the accent
    enum class Div { None, Tree, Users } hover_div = Div::None;
    /// a quiet card in an almost-empty tab: its name, what it is, hints
    struct Welcome { std::string name, what; std::vector<std::string> hints; };
    const Welcome* welcome = nullptr;
    const std::string* reply = nullptr;    // replying to ("nick: text"), shown on the input frame
};

struct MainLayout {
    /// Rows under the body: the rule, the status row, the framed input (3).
    static constexpr int kBottomRows = 5;
    int tree_width = 18;
    int users_width = 18;
    int wide = 100;     // show the user list at or above this width
    int narrow = 60;    // show the tree at or above this width
    bool show_tree = true;
    bool show_users = true;
    /// The switcher as a strip of tabs (gui_tab_layout 0) instead of the
    /// tree: at the top (gui_tab_pos 5), else at the bottom. The tree on
    /// the right (tab_pos 3/4) is drawn on the right.
    enum class Switcher { TreeLeft, TreeRight, TabsTop, TabsBottom };
    Switcher switcher = Switcher::TreeLeft;
    int tab_trunc = 20;
    [[nodiscard]] bool tabs() const { return switcher == Switcher::TabsTop || switcher == Switcher::TabsBottom; }

    /// Which side panes show at width w, and how wide: the one answer for
    /// drawing and for the mouse. has_users: the buffer has a user list.
    struct Panes { bool tree = false, users = false; int tw = 0, uw = 0, chat_w = 0; };
    [[nodiscard]] Panes panes(int w, bool has_users) const {
        Panes p;
        p.tree = !tabs() && show_tree && w >= narrow;
        p.users = show_users && has_users && w >= wide;
        auto grow = [w](int set) { return w >= 160 ? std::max(set, std::min(w / 9, 26)) : set; };
        p.tw = p.tree ? grow(tree_width) : 0;
        p.uw = p.users ? grow(users_width) : 0;
        // the chat keeps ~60 columns: the user list goes first, then the
        // tree (Ctrl-G and Alt-←/→ still switch tabs without it)
        if (p.users && w - p.tw - p.uw - 2 < 60) { p.users = false; p.uw = 0; }
        if (p.tree && w - p.tw - p.uw - 1 < 56) { p.tree = false; p.tw = 0; }
        p.chat_w = std::max(1, w - p.tw - p.uw - (p.tree ? 1 : 0) - (p.users ? 1 : 0));
        return p;
    }

    /// The window at an explicit size. Everything in `p` is read now; the
    /// element holds no reference to it.
    [[nodiscard]] maya::Element element(const MainParts& p, int w, int h) const;
    /// The window filling its slot. `p`'s pointees must outlive the frame
    /// (they're read at paint time, when the slot's size is known).
    [[nodiscard]] maya::Element element(const MainParts& p) const;
    /// Where the switcher card was drawn, relative to the chat area's
    /// top-left (for the mouse).
    mutable int switcher_at_x = 0, switcher_at_y = 0;
};

}  // namespace nx::tui
