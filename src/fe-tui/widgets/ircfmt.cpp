// nx::tui::ircfmt — see ircfmt.hpp.

#include "ircfmt.hpp"

#include <cctype>
#include <optional>

#include <maya/style/theme.hpp>
#include <maya/text/unicode_width.hpp>

#include "theme.hpp"

namespace nx::tui {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Up to two digits at s[i]; advances i. nullopt when there are none.
std::optional<int> read_num(std::string_view s, std::size_t& i) {
    if (i >= s.size() || !is_digit(s[i])) return std::nullopt;
    int n = s[i++] - '0';
    if (i < s.size() && is_digit(s[i])) n = n * 10 + (s[i++] - '0');
    return n;
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// A \004 colour, as the theme colour nearest it.
std::optional<Color> read_hex(std::string_view s, std::size_t& i) {
    if (i + 6 > s.size()) return std::nullopt;
    int v[6];
    for (int k = 0; k < 6; ++k)
        if ((v[k] = hexval(s[i + k])) < 0) return std::nullopt;
    i += 6;
    return irc_color(nearest_irc(static_cast<std::uint8_t>(v[0] * 16 + v[1]),
                                 static_cast<std::uint8_t>(v[2] * 16 + v[3]),
                                 static_cast<std::uint8_t>(v[4] * 16 + v[5])));
}

// The formatting state as the codes toggle it.
struct State {
    std::optional<Color> fg, bg;
    bool bold = false, italic = false, under = false, strike = false, reverse = false;

    Style style(const Style& base) const {
        Style st = base;
        if (fg) st.fg = *fg;
        if (bg) st.bg = *bg;
        // Two mIRC numbers can land on one theme colour (\00304,05 is red
        // on brown, which most themes have as one red): text in its own
        // background colour is gone. The text wins; the fill goes.
        if (st.fg && st.bg && *st.fg == *st.bg) st.bg.reset();
        if (bold) st.bold = true;
        if (italic) st.italic = true;
        if (under) st.underline = true;
        if (strike) st.strikethrough = true;
        if (reverse) {
            // Swap with the theme's colours standing in for "none": the
            // text on its own canvas, so the swap is a pair the theme drew.
            const Color ink = st.fg.value_or(Color::slot(maya::ThemeSlot::Text));
            const Color paper = st.bg.value_or(Color::slot(maya::ThemeSlot::Background));
            if (maya::theme::owns_canvas(maya::theme::live())) {
                st.fg = paper;
                st.bg = ink;
            } else {
                st.inverse = !st.inverse;   // native: the terminal's own pair
            }
        }
        return st;
    }
};

}  // namespace

Rich parse_irc(std::string_view s, Style base) {
    Rich out;
    State st;
    std::size_t i = 0, plain = 0;   // plain: start of the pending run of text
    auto flush = [&](std::size_t end) {
        if (end > plain) out.append(fit_emoji(s.substr(plain, end - plain)), st.style(base));
    };

    while (i < s.size()) {
        const char c = s[i];
        switch (c) {
        case '\002': flush(i); st.bold = !st.bold; plain = ++i; break;
        case '\035': flush(i); st.italic = !st.italic; plain = ++i; break;
        case '\037': flush(i); st.under = !st.under; plain = ++i; break;
        case '\036': flush(i); st.strike = !st.strike; plain = ++i; break;
        case '\026': flush(i); st.reverse = !st.reverse; plain = ++i; break;
        case '\017': flush(i); st = State{}; plain = ++i; break;
        case '\007':
        case '\010': flush(i); plain = ++i; break;
        case '\003': {
            flush(i);
            ++i;
            auto fg = read_num(s, i);
            if (!fg) { st.fg.reset(); st.bg.reset(); plain = i; break; }
            st.fg = *fg == 99 ? std::nullopt : std::optional{irc_color(*fg)};
            if (i + 1 < s.size() && s[i] == ',' && is_digit(s[i + 1])) {
                ++i;
                auto bg = read_num(s, i);
                st.bg = *bg == 99 ? std::nullopt : std::optional{irc_color(*bg)};
            }
            plain = i;
            break;
        }
        case '\004': {
            flush(i);
            ++i;
            auto fg = read_hex(s, i);
            if (!fg) { st.fg.reset(); st.bg.reset(); plain = i; break; }
            st.fg = fg;
            if (i < s.size() && s[i] == ',') {
                std::size_t j = i + 1;
                if (auto bg = read_hex(s, j)) { st.bg = bg; i = j; }
            }
            plain = i;
            break;
        }
        default:
            ++i;
        }
    }
    flush(i);
    return out;
}

std::string strip_irc(std::string_view s) {
    return parse_irc(s).text;
}

// Emoji sequences, as one cell pair each. tmux and modern terminals draw a
// whole sequence (a flag, 👨‍💻, 👍🏽, ❤️) as ONE 2-column glyph; a grid that
// measures code point by code point gets 4 or 1 instead, and everything
// right of it on the row shifts. So, for the screen only:
//   ZWJ sequence → its first emoji      👨‍💻 → 👨
//   skin-tone modifier dropped          👍🏽 → 👍
//   flag (two regional indicators) → its two letters, 🇩🇪 → DE
//   emoji + U+FE0F → the emoji + a space (the selector makes it 2 wide)
std::string fit_emoji(std::string_view s) {
    auto dec = [&](std::size_t i, std::size_t& n) -> char32_t {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) { n = 1; return b; }
        if (b < 0xE0 && i + 1 < s.size()) { n = 2; return ((b & 0x1F) << 6) | (s[i + 1] & 0x3F); }
        if (b < 0xF0 && i + 2 < s.size()) {
            n = 3; return ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        }
        if (i + 3 < s.size()) {
            n = 4;
            return ((b & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
        }
        n = 1; return b;
    };
    auto ri = [](char32_t c) { return c >= 0x1F1E6 && c <= 0x1F1FF; };
    auto tone = [](char32_t c) { return c >= 0x1F3FB && c <= 0x1F3FF; };
    // fast path: no 4-byte sequences and no U+FE0F / U+200D: nothing to do
    bool any = false;
    for (std::size_t i = 0; i < s.size() && !any; ++i) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if (b >= 0xF0) any = true;
        else if (b == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xB8
                 && static_cast<unsigned char>(s[i + 2]) == 0x8F) any = true;
        else if (b == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80
                 && static_cast<unsigned char>(s[i + 2]) == 0x8D) any = true;
    }
    if (!any) return std::string(s);
    std::string out;
    out.reserve(s.size());
    char32_t prev = 0;   // the last code point written to out
    for (std::size_t i = 0; i < s.size();) {
        std::size_t n = 1;
        const char32_t c = dec(i, n);
        if (ri(c)) {
            std::size_t n2 = 0;
            if (i + n < s.size() && ri(dec(i + n, n2))) {
                // a flag: its letters
                out += static_cast<char>('A' + (c - 0x1F1E6));
                out += static_cast<char>('A' + (dec(i + n, n2) - 0x1F1E6));
                prev = 'A';
                i += n + n2;
                continue;
            }
            out += static_cast<char>('A' + (c - 0x1F1E6));   // a lone half
            prev = 'A';
            i += n;
            continue;
        }
        if (tone(c)) { i += n; continue; }                       // drop the modifier
        if (c == 0x200D) {                                       // ZWJ: drop it and what it joins
            i += n;
            if (i < s.size()) {
                std::size_t n2 = 1;
                (void)dec(i, n2);
                i += n2;
                // and any selector / modifier after that
                while (i < s.size()) {
                    std::size_t n3 = 1;
                    const char32_t d = dec(i, n3);
                    if (d == 0xFE0F || tone(d)) i += n3; else break;
                }
            }
            continue;
        }
        if (c == 0xFE0F) {
            // the selector makes a narrow symbol before it 2 wide (❤️): a
            // space keeps the columns where the terminal puts them
            if (prev >= 0x80 && maya::unicode::char_width(prev, maya::unicode::WidthMode::Modern) == 1) out += ' ';
            prev = ' ';
            i += n;
            continue;
        }
        out.append(s.substr(i, n));
        prev = c;
        i += n;
    }
    return out;
}

Columns2 split_columns(std::string_view line) noexcept {
    const auto tab = line.find('\t');
    if (tab == std::string_view::npos) return {{}, line};
    return {line.substr(0, tab), line.substr(tab + 1)};
}

}  // namespace nx::tui
