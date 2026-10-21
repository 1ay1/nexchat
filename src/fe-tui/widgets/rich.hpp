#pragma once
// nx::tui::Rich — a run of styled text, the unit every nexchat widget draws.
//
// maya's TextElement already carries `content` + `runs`; Rich is the same
// shape kept as a value we can build up, measure, cut at a display column
// and wrap ourselves. We wrap ourselves (rather than handing maya a long
// TextElement with TextWrap::Wrap) because a chat line needs a hanging
// indent under the nick column and the chat view scrolls in ROWS, so it has
// to know how many rows each line takes at the current width.
//
// Invariant: runs are in order, don't overlap and cover all of `text`
// (maya's renderer requires it). append() keeps it; merging adjacent runs
// of the same style keeps the run count down.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <maya/element/element.hpp>
#include <maya/element/text.hpp>
#include <maya/style/style.hpp>

namespace nx::tui {

using maya::Style;
using maya::StyledRun;

struct Rich {
    std::string            text;
    std::vector<StyledRun> runs;

    Rich() = default;
    Rich(std::string_view s, Style st = {}) { append(s, st); }

    void append(std::string_view s, Style st = {});
    void append(const Rich& r);
    void clear() { text.clear(); runs.clear(); }

    [[nodiscard]] bool empty() const noexcept { return text.empty(); }
    /// Display width in terminal columns (wide CJK / emoji count 2).
    [[nodiscard]] int width() const noexcept;

    /// The first `cols` columns (a wide glyph that would straddle the edge
    /// is dropped, never split).
    [[nodiscard]] Rich take(int cols) const;
    /// Everything after byte `from` (must be a codepoint boundary).
    [[nodiscard]] Rich slice(std::size_t from, std::size_t to = std::string::npos) const;

    /// Pad with spaces (styled `st`) on the right / left to `cols` columns.
    [[nodiscard]] Rich pad_right(int cols, Style st = {}) const;
    [[nodiscard]] Rich pad_left(int cols, Style st = {}) const;
    /// Fit exactly `cols` columns: cut with an ellipsis, or pad.
    [[nodiscard]] Rich fit(int cols, Style pad = {}) const;

    /// Paint over every run (patch fields of `over` that are set: fg, bg,
    /// and any attribute flag that is on).
    void overlay(const Style& over);
    /// The same, only over the bytes [a, b) of text (runs split as needed).
    void overlay_range(std::size_t a, std::size_t b, const Style& over);
    /// Every run takes `st`'s colours (fg and bg) whatever it had, keeping
    /// its attributes (bold, underline …): a selected row.
    void replace_colors(const Style& st);
    /// Fill in what runs leave unset (a band's bg + fg under coloured text).
    void underlay(const Style& under);

    /// One unwrapped row.
    [[nodiscard]] maya::Element element() const;
};

/// Word-wrap `r` into rows of at most `cols` columns. Breaks after spaces
/// when it can, mid-word when a word is longer than a row. Leading spaces
/// of continuation rows are dropped. Always returns at least one row.
[[nodiscard]] std::vector<Rich> wrap(const Rich& r, int cols);
/// The same wrap, as the byte range [begin, end) of `r.text` each row is.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> wrap_spans(const Rich& r, int cols);

/// Display width of a UTF-8 string.
[[nodiscard]] int display_width(std::string_view s) noexcept;

/// Byte length of the UTF-8 sequence starting with `lead`.
[[nodiscard]] constexpr std::size_t utf8_len(unsigned char lead) noexcept {
    return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
}

/// Columns the codepoint at `s[i]` takes (0 for combining marks).
[[nodiscard]] int glyph_width(std::string_view s, std::size_t i) noexcept;

}  // namespace nx::tui
