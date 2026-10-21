#pragma once
// nx::tui::Menu — a dropdown / context menu, and the menu bar.
//
//   File  Edit  View  Server  Window  Help          ← MenuBar (row 0)
//   ╭──────────────────────╮
//   │ Network List…   ^S   │                        ← Menu, under its title
//   │ ──────────────────── │                           or at a right-click
//   │ ☑ Topic Bar          │
//   │ Close            ^W  │
//   ╰──────────────────────╯
//
// Items are plain data: a label, an optional key hint, a check state, a
// submenu, and an action id the owner maps back. Keyboard (↑↓ Enter Esc,
// → into a submenu, ← out, first letter to jump) and mouse (hover
// highlights, click activates, a click outside closes) both work; the
// menu returns what happened and the owner acts on it.

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <maya/element/element.hpp>
#include <maya/terminal/input.hpp>

#include "rich.hpp"

namespace nx::tui {

struct MenuItem {
    std::string label;                  // "" = a separator
    std::string hint;                   // key hint, right-aligned
    int         action = 0;             // the owner's id
    int         check = -1;             // -1 none, 0 unchecked, 1 checked
    bool        enabled = true;
    std::vector<MenuItem> sub;          // non-empty: a submenu

    static MenuItem sep() { return {}; }
    [[nodiscard]] bool is_sep() const { return label.empty(); }
};

class Menu {
public:
    enum class Outcome { Open, Activated, Closed };

    void open(std::vector<MenuItem> items, int x, int y);
    void close() { open_ = false; child_.reset(); }
    [[nodiscard]] bool is_open() const { return open_; }

    /// Would ← / → do something in here (close / open a submenu)?
    [[nodiscard]] bool wants_horizontal(bool right) const;

    Outcome key(const maya::KeyEvent& k);
    /// A mouse event in screen cells. Outside every level: Closed.
    Outcome mouse(const maya::MouseEvent& m);
    /// The action of the item activated (valid after Activated).
    [[nodiscard]] int activated() const { return activated_; }

    /// Draw over a w×h screen: every open level, positioned and clipped.
    /// The owner zstacks this over the window.
    [[nodiscard]] maya::Element element(int screen_w, int screen_h) const;

    // Geometry of the top level once drawn (for tests and mouse).
    [[nodiscard]] int x() const { return x_; }
    [[nodiscard]] int y() const { return y_; }
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const { return shown() + 2; }

private:
    void place(int screen_w, int screen_h) const;
    void step(int d);
    bool enter();   // open the selected submenu, or activate
    [[nodiscard]] int item_at(int mx, int my) const;
    [[nodiscard]] std::vector<Rich> rows() const;
    [[nodiscard]] int natural_width() const;
    [[nodiscard]] int shown() const { return std::min(static_cast<int>(items_.size()), max_rows_); }
    void keep_sel_visible() const;

    std::vector<MenuItem> items_;
    bool                  open_ = false;
    int                   sel_ = -1;
    mutable int           x_ = 0, y_ = 0;   // placed (clamped to the screen)
    // a menu bigger than the screen is cut to fit and scrolls
    mutable int           max_w_ = 1 << 20, max_rows_ = 1 << 20, top_ = 0;
    int                   want_x_ = 0, want_y_ = 0;
    int                   activated_ = 0;
    std::unique_ptr<Menu> child_;
};

/// The menu bar: titles across row 0, each opening a Menu.
class MenuBar {
public:
    struct Entry { std::string title; std::vector<MenuItem> items; };

    void set(std::vector<Entry> e) { entries_ = std::move(e); }
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    [[nodiscard]] bool is_open() const { return menu_.is_open(); }
    void open(int index);
    void close() { menu_.close(); open_ = -1; }

    Menu::Outcome key(const maya::KeyEvent& k);
    /// A mouse event; returns nullopt when it had nothing to do with the bar
    /// or its menu (so the owner handles it).
    std::optional<Menu::Outcome> mouse(const maya::MouseEvent& m);
    [[nodiscard]] int activated() const { return menu_.activated(); }

    [[nodiscard]] Rich row(int w, const std::string& right = {}) const;
    /// The open menu, positioned (zstack over the window).
    [[nodiscard]] maya::Element overlay(int screen_w, int screen_h) const { return menu_.element(screen_w, screen_h); }
    /// Which title is at column x of the bar row (-1 none).
    [[nodiscard]] int title_at(int x) const;

private:
    std::vector<Entry> entries_;
    Menu               menu_;
    int                open_ = -1;
    int                hover_ = -1;   // the title under the pointer (bar closed)
};

}  // namespace nx::tui
