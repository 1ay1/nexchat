#pragma once
// nx::tui::Sheet — a scrolling page of rows with buttons in them: the
// body of the Bouncer window (cards, switches, "Nick and login...", sure?
// buttons), drawn and hit-tested in one place.
//
//   ▾ Libera  in #nexchat, #help             [ Nick and login... ] [on ]
//     as alice (its own nick), SASL as alice
//
// The owner builds the page each frame from its state: rows of text, and
// on any row "controls" (a button, a switch, a check, a text field) with
// an id it chooses. Tab / ↑↓ move between controls, Enter / Space press
// the focused one, a click presses the one under the pointer, the wheel
// scrolls. What a press means is the owner's.

#include <functional>
#include <string>
#include <vector>

#include <maya/terminal/input.hpp>

#include "inputline.hpp"
#include "rich.hpp"

namespace nx::tui {

class Sheet {
public:
    enum class Kind { Button, Switch, Check, Radio, Field, Password };
    struct Control {
        Kind kind = Kind::Button;
        std::string label;    // Button / Check / Radio text
        int id = 0;
        bool on = false;      // Switch / Check / Radio
        bool enabled = true;
        bool danger = false;  // a "sure?" button
        int width = 24;       // Field / Password
        std::string text;     // Field / Password: what's in it
        std::string hint;     // Field / Password: shown when empty
    };
    struct Row {
        Rich text;                    // the row's own text (left)
        std::vector<Control> controls;   // right-aligned after the text
        bool controls_left = false;   // controls right after the text instead
        int indent = 0;
    };

    // ── building a page (each frame) ───────────────────────────────────
    void begin() { rows_.clear(); }
    Row& row(Rich text = {}, int indent = 0) { rows_.push_back({std::move(text), {}, false, indent}); return rows_.back(); }
    void blank() { rows_.push_back({}); }
    /// Text that wraps to the width (a sentence, not a row).
    void para(std::string_view text, Style st, int indent = 0);

    // ── input ─────────────────────────────────────────────────────────
    enum class Outcome { None, Pressed, Edited };
    Outcome key(const maya::KeyEvent& k);
    Outcome click(int x, int y);
    void wheel(int d) { scroll_ = std::max(0, scroll_ + d); }
    void paste(std::string_view s);
    /// The control pressed / edited (after the Outcome).
    [[nodiscard]] int pressed() const { return pressed_; }
    [[nodiscard]] const std::string& field_text(int id) const;
    void set_field(int id, std::string text);
    void focus_id(int id) { want_focus_ = id; }
    [[nodiscard]] int focused_id() const;
    [[nodiscard]] bool editing() const { return editing_ != 0; }

    [[nodiscard]] std::vector<Rich> rows(int w, int h, bool active = true) const;

private:
    struct Hot { int row; int x0, x1; int id; };
    void step(int d);
    void flatten(int w) const;
    std::vector<Row> rows_;
    // fields keep their text across rebuilds, by id
    std::vector<std::pair<int, std::string>> fields_;
    std::string& field_ref(int id);
    int editing_ = 0;            // the field id being typed into
    InputLine editor_;
    int focus_ = 0;              // index into ids_ (the controls in order)
    int want_focus_ = 0;
    int pressed_ = 0;
    mutable int scroll_ = 0;
    mutable int width_ = 80;
    mutable std::vector<int> ids_;            // every enabled control, in order
    mutable std::vector<Hot> hots_;           // where they were drawn (screen rows)
    mutable std::vector<Rich> flat_;          // the page, wrapped, before scrolling
    mutable std::vector<std::vector<Hot>> flat_hots_;
};

}  // namespace nx::tui
