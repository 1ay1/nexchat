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
};

struct MainLayout {
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

    /// The window at an explicit size. Everything in `p` is read now; the
    /// element holds no reference to it.
    [[nodiscard]] maya::Element element(const MainParts& p, int w, int h) const;
    /// The window filling its slot. `p`'s pointees must outlive the frame
    /// (they're read at paint time, when the slot's size is known).
    [[nodiscard]] maya::Element element(const MainParts& p) const;
};

}  // namespace nx::tui
