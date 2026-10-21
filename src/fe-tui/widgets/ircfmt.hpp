#pragma once
// nx::tui::ircfmt — IRC formatting codes to styled text.
//
// What the core hands a frontend (fe_print_text) is text with mIRC control
// codes in it, exactly as xtext.c parses them:
//
//   \002 bold   \035 italic   \037 underline   \036 strikethrough
//   \026 reverse   \017 reset   \010 hidden (event-format text: skipped)
//   \003[fg[,bg]]  colour, 0-98, 99 = default
//   \004RRGGBB[,RRGGBB]  hex colour (modern clients send it)
//   \007 beep (dropped), \t the nick/text separator (see split_columns)
//
// Colours are drawn in the live maya theme's colours (theme.hpp irc_color,
// nearest_irc), never as the raw RGB a sender asked for.
//
// A bare \003 with no digits ends colouring. Everything else passes through.

#include <string_view>

#include "rich.hpp"

namespace nx::tui {

/// Parse `s` into styled text. `base` is the style plain text starts in.
[[nodiscard]] Rich parse_irc(std::string_view s, Style base = {});

/// The text with every formatting code removed (for search, widths, URLs).
[[nodiscard]] std::string strip_irc(std::string_view s);

/// Emoji sequences as the terminal draws them, one 2-column glyph each:
/// ZWJ sequences cut to their first emoji, skin tones dropped, flags as
/// their two letters, a narrow symbol + U+FE0F padded to 2. Only for
/// the screen: the grid measures code points, the terminal clusters.
[[nodiscard]] std::string fit_emoji(std::string_view s);

/// A printed event line is "left\tright": the left column (nick, or an
/// event's "-->" / "*") and the message. Lines without a tab are all right.
struct Columns2 { std::string_view left, right; };
[[nodiscard]] Columns2 split_columns(std::string_view line) noexcept;

}  // namespace nx::tui
