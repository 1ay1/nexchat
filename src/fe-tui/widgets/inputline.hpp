#pragma once
// nx::tui::InputLine — the input box.
//
// One line of UTF-8 with the editing a terminal user expects (readline /
// emacs keys, which is also what fe-gtk's entry does on macOS):
//
//   ←/→ Ctrl-B/F       char       Alt-B/F, Ctrl-←/→    word
//   Home/End Ctrl-A/E  line       Backspace/Del Ctrl-H/D
//   Ctrl-W Alt-Backspace  kill word back    Alt-D  kill word forward
//   Ctrl-U  kill to start   Ctrl-K  kill to end   Ctrl-Y  yank
//   ↑/↓ Ctrl-P/N       history (the text being typed is kept as the newest)
//   Ctrl-T             transpose
//
// Enter is NOT handled here: the owner decides (send, or newline in a
// paste). Tab is handed to a Completer the owner sets. Keys the line doesn't
// use come back as Unhandled so the screen can have them (PageUp, Alt-1…).
//
// The cursor is a byte offset that always sits on a codepoint boundary.
// Drawing scrolls horizontally so the cursor stays visible.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <maya/element/element.hpp>
#include <maya/terminal/input.hpp>

#include "rich.hpp"

namespace nx::tui {

/// What tab completion needs: the line and cursor in, a replacement out.
/// The owner implements it over nicks, channels and /commands (the core's
/// own list); see complete_word() for the common case.
struct Completion {
    std::string text;      // the new line
    std::size_t cursor;    // the new cursor
};
using Completer = std::function<std::optional<Completion>(std::string_view line, std::size_t cursor,
                                                          int step /* +1 Tab, -1 Shift-Tab */)>;

class InputLine {
public:
    enum class Result { Handled, Unhandled, Changed };

    Result key(const maya::KeyEvent& k);
    void   paste(std::string_view s);        // newlines become spaces; the owner splits first if it wants

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::size_t cursor() const noexcept { return cur_; }
    void set(std::string s, std::size_t cursor = std::string::npos);
    void clear() { set({}); }

    /// Remember `line` (when it isn't the same as the last one) and reset
    /// the history position. Call on send.
    void commit_history(std::string line);
    void set_history_max(std::size_t n) { hist_max_ = n; }

    void set_completer(Completer c) { complete_ = std::move(c); }
    void set_prompt(Rich p) { prompt_ = std::move(p); }
    /// Shown dim after the caret while the line is empty.
    void set_placeholder(std::string p) { placeholder_ = std::move(p); }

    /// One row, w columns: prompt + visible slice, with the caret cell
    /// marked so the real terminal cursor lands there.
    [[nodiscard]] Rich row(int w, bool focused = true) const;
    [[nodiscard]] maya::Element element(bool focused = true) const;

private:
    void insert(std::string_view s);
    [[nodiscard]] std::size_t prev_cp(std::size_t i) const;
    [[nodiscard]] std::size_t next_cp(std::size_t i) const;
    [[nodiscard]] std::size_t prev_word(std::size_t i) const;
    [[nodiscard]] std::size_t next_word(std::size_t i) const;
    void kill(std::size_t a, std::size_t b, bool prepend);
    void history_step(int d);

    std::string              text_;
    std::size_t              cur_ = 0;
    std::string              yank_;
    bool                     last_was_kill_ = false;
    bool                     completing_ = false;

    std::vector<std::string> hist_;
    std::size_t              hist_max_ = 200;
    std::size_t              hist_pos_ = 0;   // == hist_.size(): editing the new line
    std::string              hist_draft_;

    Completer                complete_;
    std::string              placeholder_;
    Rich                     prompt_;
    mutable int              scroll_ = 0;     // first visible column of text
};

/// The usual completer body: complete the word before the cursor against
/// `candidates` (case-insensitive prefix), cycling on repeated calls. At
/// the start of the line a nick gets `suffix` (": " — hex_completion_suffix).
class WordCompleter {
public:
    explicit WordCompleter(std::function<std::vector<std::string>(std::string_view word, bool at_start)> source,
                           std::string suffix = ": ")
        : source_(std::move(source)), suffix_(std::move(suffix)) {}

    std::optional<Completion> operator()(std::string_view line, std::size_t cursor, int step);

private:
    std::function<std::vector<std::string>(std::string_view, bool)> source_;
    std::string suffix_;
    // The cycle in progress: what was typed, and where.
    std::string              seed_, before_, after_;
    std::vector<std::string> matches_;
    int                      idx_ = -1;
    std::string              last_line_;
};

}  // namespace nx::tui
