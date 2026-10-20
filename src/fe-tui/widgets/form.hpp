#pragma once
// nx::tui::Form — a column of labelled fields: the body of a settings
// dialog (Network List's edit pane, Preferences).
//
//   Nick name     [ alice           ]       ← Text
//   Use SSL       [✓]                       ← Toggle
//   Login method  ‹ SASL PLAIN       ›      ← Choice (←/→ or click cycles)
//   Servers       irc.libera.chat/+6697     ← List (one entry per line,
//                 + add…                       Enter edits, Del removes)
//
// Fields are plain data the owner reads back after an edit. Tab / ↑↓ move
// between fields, the focused one takes the keys. Mouse: click focuses a
// field (and toggles / cycles), the wheel scrolls a long form.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <maya/terminal/input.hpp>

#include "inputline.hpp"
#include "rich.hpp"

namespace nx::tui {

struct Field {
    enum class Kind { Text, Password, Toggle, Choice, List, Button, Heading };
    Kind        kind = Kind::Text;
    std::string label;
    std::string text;                 // Text / Password
    bool        on = false;           // Toggle
    std::vector<std::string> choices; // Choice
    int         choice = 0;
    std::vector<std::string> items;   // List
    int         item = 0;             // List: the selected entry
    int         id = 0;               // the owner's
    bool        enabled = true;
};

class Form {
public:
    enum class Outcome { None, Changed, Pressed };   // Pressed: a Button

    void set(std::vector<Field> fields);
    [[nodiscard]] std::vector<Field>& fields() { return fields_; }
    [[nodiscard]] const std::vector<Field>& fields() const { return fields_; }
    [[nodiscard]] Field* by_id(int id);

    void focus(int i);
    [[nodiscard]] int focused() const { return focus_; }
    /// The id of the button pressed (after Pressed).
    [[nodiscard]] int pressed() const { return pressed_; }

    /// Keys for the form; Esc / Enter-on-nothing are the owner's.
    Outcome key(const maya::KeyEvent& k, bool* used = nullptr);
    void paste(std::string_view s);
    /// A click at (x, y) relative to the form's top-left.
    Outcome click(int x, int y, bool dbl = false);
    void wheel(int d) { scroll_ = std::max(0, scroll_ + d); }
    [[nodiscard]] bool editing() const noexcept { return editing_; }
    /// The pointer is over row y (-1: off the form): that field's label is
    /// underlined, as a link would be.
    void hover(int y) {
        hover_ = -1;
        if (y >= 0 && static_cast<std::size_t>(y) < map_.size()) hover_ = map_[static_cast<std::size_t>(y)].field;
    }

    /// w×h rows, the focused field highlighted when `active`.
    [[nodiscard]] std::vector<Rich> rows(int w, int h, bool active = true) const;
    int label_width = 16;

private:
    void step(int d);
    void editor_open();
    void editor_commit();
    std::vector<Field> fields_;
    int focus_ = 0;
    int pressed_ = 0;
    // A Text / List entry being edited
    bool editing_ = false;
    InputLine editor_;
    mutable int scroll_ = 0;
    // which field each drawn row is, and where list entries start
    struct RowMap { int field = -1; int item = -1; int value_x = 0; };
    mutable std::vector<RowMap> map_;
    int hover_ = -1;
};

}  // namespace nx::tui
