#pragma once
// nx::tui theme — the TUI's colours: maya's themes, and nothing else.
//
// The TUI has no colour settings of its own and reads none of the GUI's
// (fe-gtk's palette, colors.conf, its schemes): a maya Theme is the only
// provider. Nothing here states a colour. The widgets ask for a ROLE (muted
// text, the selection, a highlight), and each role is a maya ThemeSlot,
// resolved when the frame is written against whichever theme is live:
// `native` by default (the terminal's own palette: right on a light
// background, a dark one, a 16-colour tty), or any of maya's schemes
// (maya::theme::schemes), picked by name.
//
// IRC text colours are the one set whose MEANING isn't ours: \003NN in a
// message names "mIRC colour NN". What it looks like is still the theme's:
// mIRC 0-15 are a maya projection (IrcColors) of the live theme's role
// slots, re-derived on every theme switch, so on `native` they're the
// terminal's own ANSI colours and on a scheme they're the scheme's hues.
//
// Every other colour a message can name lands on those sixteen:
//
//   16-31  HexChat's duplicates of 0-15 (the core's text events use them:
//          %C18, %C24 …)
//   32-98  mIRC's extended wheel, by hue and shade (irc_index)
//   \004RRGGBB  a sender's literal RGB, by hue and shade (nearest_irc)
//
// so no colour on screen is one the theme didn't provide. The
// tui-colors test fails the build if a widget states one anyway.

#include <array>
#include <cstdint>
#include <string_view>

#include <maya/style/color.hpp>
#include <maya/style/style.hpp>
#include <maya/style/theme.hpp>

namespace nx::tui {

using maya::Color;
using maya::Style;
using maya::ThemeSlot;

// ── UI roles ────────────────────────────────────────────────────────────
//
// Rules every role follows (each is checked by tests.cpp):
//
//   • Text is INK: a role hue, Text or Muted. Never a chrome slot
//     (Selection, Highlight, Border, Surface, the diff tints, Placeholder):
//     those are blends toward the background, 1-2.3:1 against it.
//   • Plain text states Text, not "nothing". On a themed canvas maya fills
//     the background from the theme but an unset fg is the TERMINAL's
//     foreground: a white-on-black terminal under a light scheme would draw
//     white text on the scheme's cream canvas.
//   • A background is only ever Selection, and only under Text (the pair
//     the schemes are built for). It replaces the row's colours whole:
//     a red nick on the selection band is the pairing nobody checked.
//   • No attribute does a colour's job. maya never sends SGR 2 (dim), and
//     reverse video swaps in colours the theme didn't pick (the caret on
//     native, which has no colours to pick, is the one exception).
namespace role {

[[nodiscard]] inline Color slot(ThemeSlot s) noexcept { return Color::slot(s); }

/// Body text: the theme's text colour, stated.
[[nodiscard]] inline Style text()     { return Style{}.with_fg(slot(ThemeSlot::Text)); }
/// Secondary text: stamps, separators, counts, hints.
[[nodiscard]] inline Style muted()    { return Style{}.with_fg(slot(ThemeSlot::Muted)); }
/// The thing a bar is about: the channel name, a prompt.
[[nodiscard]] inline Style title()    { return Style{}.with_fg(slot(ThemeSlot::Primary)).with_bold(); }
/// The read marker: where unread text starts.
[[nodiscard]] inline Style marker()   { return Style{}.with_fg(slot(ThemeSlot::Warning)); }
/// A selected row in a list that has the focus. Applied with replace(),
/// not overlay(): the row becomes text-on-selection, every run of it.
[[nodiscard]] inline Style selected() {
    return Style{}.with_fg(slot(ThemeSlot::Text)).with_bg(slot(ThemeSlot::Selection));
}
/// The selected row of a list without the focus: where you are, quietly.
/// An attribute only, so the row keeps its own (activity) colours.
[[nodiscard]] inline Style current()  { return Style{}.with_bold().with_underline(); }
/// A find match in the chat (the selection band, as a selection is), and
/// the one you're on: the band, bold and underlined.
[[nodiscard]] inline Style found()         { return selected(); }
[[nodiscard]] inline Style found_current() { return selected().with_bold().with_underline(); }
/// A table's column titles: quiet and underlined, so the header reads as
/// one ruled row; the sorted column in the title colour.
[[nodiscard]] inline Style column()        { return muted().with_underline(); }
[[nodiscard]] inline Style column_sorted() { return title().with_underline(); }
/// Rules and pane dividers.
[[nodiscard]] inline Style rule()     { return Style{}.with_fg(slot(ThemeSlot::Border)); }
[[nodiscard]] inline Style border()   { return rule(); }
/// Away users.
[[nodiscard]] inline Style away()     { return muted(); }
/// The input caret, a block in the theme's Cursor slot (a scheme's own
/// cursorColor) under the theme's background-ink. maya only places the
/// terminal's hardware cursor in inline mode, so a fullscreen app paints
/// its caret. On native, Cursor and the inks are "the terminal's" and
/// can't be painted, so there the caret is reverse video: the one
/// attribute that is the terminal's own cursor look.
[[nodiscard]] Style caret();

// Buffer activity (the core's fe_set_tab_color levels): new data, new
// message, highlight.
[[nodiscard]] inline Style new_data() { return Style{}.with_fg(slot(ThemeSlot::Info)); }
[[nodiscard]] inline Style new_msg()  { return Style{}.with_fg(slot(ThemeSlot::Warning)); }
[[nodiscard]] inline Style hilight()  { return Style{}.with_fg(slot(ThemeSlot::Accent)).with_bold(); }

[[nodiscard]] inline Style good()     { return Style{}.with_fg(slot(ThemeSlot::Success)); }
[[nodiscard]] inline Style warn()     { return Style{}.with_fg(slot(ThemeSlot::Warning)); }
[[nodiscard]] inline Style bad()      { return Style{}.with_fg(slot(ThemeSlot::Error)); }

/// A channel member's rank prefix: ~ & owner/admin, @ op, % halfop, + voice.
[[nodiscard]] inline Style rank(char prefix) {
    switch (prefix) {
    case '~': case '&': return bad();
    case '@':           return good();
    case '%':           return Style{}.with_fg(slot(ThemeSlot::Accent));
    case '+':           return warn();
    default:            return {};
    }
}

}  // namespace role

// ── IRC colours ─────────────────────────────────────────────────────────

/// mIRC 0-15 derived from a theme (see the top of this file).
struct IrcColors {
    using type = std::array<maya::LitColor, 16>;
    static type project(const maya::Theme& t);
};

/// Which of the sixteen theme colours mIRC colour `n` (0-98) is drawn as.
[[nodiscard]] int irc_index(int n) noexcept;
/// Which of the sixteen a literal RGB (a \004 colour) is drawn as.
[[nodiscard]] int nearest_irc(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept;

/// The colour \003`n` means, under the live theme. `n` is 0-98; 99 is
/// "the default colour", which the caller handles by not setting one.
[[nodiscard]] Color irc_color(int n);

/// Nick colour, the same pick as common/text.c text_color_of(): the byte
/// sum of the nick, mod the nine colours that read on any background. The
/// result is an IRC colour number (19-29), drawn by irc_color like any other.
[[nodiscard]] constexpr int nick_color_index(std::string_view nick) noexcept {
    constexpr int rcolors[] = {19, 20, 22, 24, 25, 26, 27, 28, 29};
    unsigned sum = 0;
    for (char ch : nick) sum += static_cast<unsigned char>(ch);  // the core builds -funsigned-char
    return rcolors[sum % 9];
}

[[nodiscard]] inline Style nick_style(std::string_view nick) {
    return Style{}.with_fg(irc_color(nick_color_index(nick)));
}

}  // namespace nx::tui
