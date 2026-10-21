#pragma once
// nx::tui::Dialog — a modal card over the window: a message, a yes/no
// question, or a one-line text prompt (fe_message, fe_confirm,
// fe_get_str / fe_get_int / fe_get_file).
//
//   ╭ Change nick ──────────────────────────╮
//   │ Enter new nickname:                    │
//   │ > alice_▏                              │
//   │                       [ Cancel ] [ OK ]│
//   ╰────────────────────────────────────────╯
//
// Enter = OK, Esc = Cancel, Tab moves between the field and the buttons,
// the buttons are clickable. One dialog at a time; a queue is the owner's.

#include <algorithm>
#include <functional>
#include <optional>
#include <string>

#include <maya/element/element.hpp>
#include <maya/terminal/input.hpp>

#include "inputline.hpp"
#include "rich.hpp"

namespace nx::tui {

class Dialog {
public:
    enum class Kind { Message, Confirm, Prompt };
    enum class Outcome { Open, Ok, Cancel };

    void message(std::string title, std::string text, bool error = false);
    void confirm(std::string title, std::string text);
    void prompt(std::string title, std::string text, std::string initial);

    [[nodiscard]] bool is_open() const { return open_; }
    [[nodiscard]] Kind kind() const { return kind_; }
    [[nodiscard]] const std::string& value() const { return field_.text(); }

    Outcome key(const maya::KeyEvent& k);
    Outcome mouse(const maya::MouseEvent& m);
    void paste(std::string_view s) { if (kind_ == Kind::Prompt) field_.paste(s); }

    /// The card centred on a w×h screen (zstack it over the window).
    [[nodiscard]] maya::Element element(int screen_w, int screen_h) const;

private:
    Outcome finish(bool ok) { open_ = false; return ok ? Outcome::Ok : Outcome::Cancel; }
    [[nodiscard]] std::vector<Rich> rows(int inner) const;

    bool        open_ = false;
    Kind        kind_ = Kind::Message;
    bool        error_ = false;
    std::string title_, text_;
    InputLine   field_;
    int         focus_ = 0;     // 0 field (prompt) / OK, 1 Cancel … see rows()
    // where the buttons were drawn, for clicks
    mutable int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    mutable int ok_x0_ = 0, ok_x1_ = 0, cancel_x0_ = 0, cancel_x1_ = 0, buttons_y_ = 0;
    int hover_ = 0;   // 1 OK, 2 Cancel under the pointer
    // text taller than the screen scrolls (↑↓ PgUp PgDn, the wheel)
    mutable int scroll_ = 0, text_room_ = 0, text_rows_ = 0;
    void scroll(int d) { scroll_ = std::clamp(scroll_ + d, 0, std::max(0, text_rows_ - text_room_)); }
};

}  // namespace nx::tui
