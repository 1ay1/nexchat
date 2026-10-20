// nx::tui theme — see theme.hpp.

#include "theme.hpp"

#include <algorithm>

#include <maya/style/theme.hpp>

namespace nx::tui {

// mIRC 0-15 from the theme, for every theme alike (native included, whose
// slots are the terminal's own ANSI colours).
//
// Only slots that are INK may carry text: the role hues (primary,
// secondary, accent, success, error, warning, info, link — the eight
// maya::theme::series() uses because schemes keep them distinct), text and
// muted. The rest of a theme is chrome — selection, highlight, the diff
// tints, border, placeholder, surface — derived by blending toward the
// background, 1.0-2.3:1 against it across maya's schemes: text in them
// is close to invisible. inverse_text IS the background.
//
// mIRC black and white aren't hues, they're "dark ink" and "light ink",
// and which of those is the text colour depends on the theme's polarity:
// on a dark theme white is text and black would be the background, so
// black becomes muted (the darkest thing still readable), and the other
// way round on a light one. Native doesn't know its polarity (its text
// and background are "the terminal's"), so there white is the text colour
// and black is muted: both readable on whatever the terminal is.
IrcColors::type IrcColors::project(const maya::Theme& t) {
    using C = maya::LitColor;
    const auto lum = [](const C& c) {
        const C x = c.to_rgb();
        return 0.2126 * x.r() + 0.7152 * x.g() + 0.0722 * x.b();
    };
    const bool known = maya::theme::owns_canvas(t) && t.text.has_channels();
    const bool dark = !known || lum(t.background) < lum(t.text);
    const C light_ink = dark ? t.text : t.muted;
    const C dark_ink  = dark ? t.muted : t.text;
    return {
        light_ink,       //  0 white
        dark_ink,        //  1 black
        t.primary,       //  2 blue (navy)
        t.success,       //  3 green
        t.error,         //  4 red
        t.error,         //  5 brown (maroon)
        t.accent,        //  6 purple
        t.warning,       //  7 orange
        t.warning,       //  8 yellow
        t.secondary,     //  9 light green
        t.info,          // 10 cyan (teal)
        t.link,          // 11 light cyan
        t.primary,       // 12 light blue
        t.accent,        // 13 pink
        t.muted,         // 14 grey
        t.text,          // 15 light grey
    };
}

namespace {

// mIRC's extended colours (16-98) are a 12-hue wheel in 30° steps (red,
// orange, yellow, chartreuse, green, spring, cyan, azure, blue, violet,
// magenta, rose) at seven shades, then a grey ramp. They map onto the 16
// theme-derived colours by hue, dark or light: a colour a theme doesn't
// have is never invented.
constexpr int kDark[12]  = {5, 7, 7, 3, 3, 10, 10, 2, 2, 6, 6, 6};
constexpr int kLight[12] = {4, 7, 8, 9, 9, 10, 11, 12, 12, 13, 13, 13};

}  // namespace

int irc_index(int n) noexcept {
    if (n < 0) return 0;
    if (n < 32) return n % 16;                    // 16-31: HexChat's copies of 0-15
    if (n <= 87) {
        const int k = n - 16;                     // rows 0-2 dark, 3-6 light
        return (k / 12 < 3 ? kDark : kLight)[k % 12];
    }
    if (n <= 90) return 1;                        // the grey ramp, 88-98
    if (n <= 93) return 14;
    if (n <= 96) return 15;
    return 0;
}

int nearest_irc(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    const int mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const int chroma = mx - mn;
    if (chroma * 100 < 15 * std::max(mx, 1) || chroma < 24) {   // a grey
        if (mx < 0x40) return 1;
        if (mx < 0x90) return 14;
        if (mx < 0xd8) return 15;
        return 0;
    }
    int h;                                                      // hue, degrees
    if (mx == r)      h = 60 * (g - b) / chroma;
    else if (mx == g) h = 120 + 60 * (b - r) / chroma;
    else              h = 240 + 60 * (r - g) / chroma;
    if (h < 0) h += 360;
    const int sector = ((h + 15) / 30) % 12;
    return (mx < 0x8c ? kDark : kLight)[sector];
}

Color irc_color(int n) {
    return maya::theme::projected<IrcColors>()[static_cast<std::size_t>(irc_index(n))];
}

Style role::caret() {
    const maya::Theme& t = maya::theme::live();
    if (!maya::theme::owns_canvas(t) || t.cursor.kind() == maya::LitColor::Kind::Default)
        return Style{}.with_inverse();
    return Style{}.with_fg(slot(ThemeSlot::Background)).with_bg(slot(ThemeSlot::Cursor));
}

}  // namespace nx::tui
