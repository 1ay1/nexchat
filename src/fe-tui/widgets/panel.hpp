#pragma once
// nx::tui::Panel — a large modal window over the TUI: the Network List,
// Preferences, the Channel List, transfers, the ban/ignore/friends lists.
//
//   ╭─ Network List ─────────────────────────────────────────────── ✕ ─╮
//   │ ╭ list ───────────╮ ╭ form / table ──────────────────────────────╮│
//   │ │ Libera          │ │ Nick name     alice                        ││
//   │ │ OFTC            │ │ Use SSL       [✓]                           ││
//   │ │ …               │ │ …                                           ││
//   │ ╰─────────────────╯ ╰─────────────────────────────────────────────╯│
//   │                                [ Add ] [ Remove ] [ Connect ] [Close]│
//   ╰──────────────────────────────────────────────────────────────────╯
//
// The panel is chrome only: a title, an optional left list (Panel::List),
// a right body (a Form, or rows the owner draws), and a button row. Focus
// cycles list → body → buttons with Tab; the mouse picks anything. What a
// button or a list pick MEANS is the owner's.

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include <maya/element/element.hpp>
#include <maya/terminal/input.hpp>

#include "form.hpp"
#include "rich.hpp"

namespace nx::tui {

class Panel {
public:
    enum class Outcome { None, Closed, Button, Picked, Changed, Activated };
    enum class Part { List, Body, Buttons };

    struct Button { std::string label; int id; bool enabled = true; };

    void open(std::string title) { title_ = std::move(title); open_ = true; part_ = list_.empty() && !has_list_ ? Part::Body : Part::List; }
    void close() { open_ = false; }
    [[nodiscard]] bool is_open() const { return open_; }
    [[nodiscard]] const std::string& title() const { return title_; }

    // the left list (none: the body is the whole width)
    void set_list(std::vector<std::string> items, int sel = 0, std::string heading = {});
    void no_list() { has_list_ = false; list_.clear(); if (part_ == Part::List) part_ = Part::Body; }
    [[nodiscard]] int list_sel() const { return list_sel_; }
    void set_list_sel(int i) { list_sel_ = std::clamp(i, 0, std::max(0, static_cast<int>(list_.size()) - 1)); }
    int list_width = 24;

    // the body: a Form, or custom rows (Table-like) drawn by `draw`
    Form form;
    bool body_is_form = true;
    std::function<std::vector<Rich>(int w, int h, bool focused)> draw;
    std::function<bool(const maya::KeyEvent&)> body_key;          // custom body's keys
    std::function<bool(int x, int y, bool dbl, bool right)> body_click;
    /// the pointer moved over the body (x, y in it; inside false when it left)
    std::function<void(int x, int y, bool inside)> body_hover;

    void set_buttons(std::vector<Button> b) { buttons_ = std::move(b); }
    [[nodiscard]] int button() const { return button_; }

    std::string status;   // a line under the body (hints, counts)

    Outcome key(const maya::KeyEvent& k);
    Outcome mouse(const maya::MouseEvent& m);
    void paste(std::string_view s) { if (part_ == Part::Body && body_is_form) form.paste(s); }

    [[nodiscard]] maya::Element element(int screen_w, int screen_h) const;
    [[nodiscard]] Part part() const { return part_; }
    void set_part(Part p) { part_ = p; }

private:
    bool open_ = false;
    std::string title_;
    bool has_list_ = false;
    std::vector<std::string> list_;
    std::string list_heading_;
    int list_sel_ = 0;
    mutable int list_top_ = 0;
    std::vector<Button> buttons_;
    int button_ = 0, button_focus_ = 0;
    Part part_ = Part::Body;
    // the last frame's geometry
    mutable int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    mutable int list_x_ = 0, list_y_ = 0, list_w_ = 0, list_h_ = 0;
    mutable int body_x_ = 0, body_y_ = 0, body_w_ = 0, body_h_ = 0;
    mutable int buttons_y_ = 0, close_x_ = 0;
    mutable std::vector<std::pair<int, int>> button_x_;   // [x0, x1) per button
    std::chrono::steady_clock::time_point last_click_{};
    int last_y_ = -1;
    int hover_list_ = -1, hover_button_ = -1;
    bool hover_close_ = false;
};

}  // namespace nx::tui
