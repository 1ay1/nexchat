// nx::tui::Rich — see rich.hpp.

#include "rich.hpp"

#include <algorithm>

#include <maya/text/unicode_width.hpp>

namespace nx::tui {

int glyph_width(std::string_view s, std::size_t i) noexcept {
    std::size_t p = i;
    const char32_t cp = maya::decode_utf8(s, p);
    return maya::unicode::char_width(cp, maya::unicode::WidthMode::Modern);
}

int display_width(std::string_view s) noexcept {
    int w = 0;
    for (std::size_t i = 0; i < s.size();) {
        w += glyph_width(s, i);
        i += utf8_len(static_cast<unsigned char>(s[i]));
    }
    return w;
}

void Rich::append(std::string_view s, Style st) {
    if (s.empty()) return;
    const std::size_t off = text.size();
    text.append(s);
    if (!runs.empty() && runs.back().style == st
        && runs.back().byte_offset + runs.back().byte_length == off)
        runs.back().byte_length += s.size();
    else
        runs.push_back({off, s.size(), st});
}

void Rich::append(const Rich& r) {
    for (const auto& run : r.runs)
        append(std::string_view{r.text}.substr(run.byte_offset, run.byte_length), run.style);
}

int Rich::width() const noexcept { return display_width(text); }

Rich Rich::slice(std::size_t from, std::size_t to) const {
    Rich out;
    to = std::min(to, text.size());
    if (from >= to) return out;
    for (const auto& run : runs) {
        const std::size_t a = std::max(from, run.byte_offset);
        const std::size_t b = std::min(to, run.byte_offset + run.byte_length);
        if (a < b) out.append(std::string_view{text}.substr(a, b - a), run.style);
    }
    return out;
}

// Byte offset where the first `cols` columns end (never splits a glyph).
static std::size_t cut_at(std::string_view s, int cols) noexcept {
    int w = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const int gw = glyph_width(s, i);
        if (w + gw > cols) break;
        w += gw;
        i += utf8_len(static_cast<unsigned char>(s[i]));
    }
    return std::min(i, s.size());
}

Rich Rich::take(int cols) const { return slice(0, cut_at(text, std::max(cols, 0))); }

Rich Rich::pad_right(int cols, Style st) const {
    Rich out = *this;
    if (const int gap = cols - width(); gap > 0) out.append(std::string(gap, ' '), st);
    return out;
}

Rich Rich::pad_left(int cols, Style st) const {
    Rich out;
    if (const int gap = cols - width(); gap > 0) out.append(std::string(gap, ' '), st);
    out.append(*this);
    return out;
}

Rich Rich::fit(int cols, Style pad) const {
    if (cols <= 0) return {};
    const int w = width();
    if (w <= cols) return pad_right(cols, pad);
    Rich out = take(cols - 1);
    const Style last = runs.empty() ? Style{} : runs.back().style;
    out.append("…", last);
    return out.pad_right(cols, pad);
}

void Rich::overlay(const Style& over) {
    for (auto& run : runs) run.style = run.style.merge(over);
}

void Rich::overlay_range(std::size_t a, std::size_t b, const Style& over) {
    if (a >= b) return;
    std::vector<StyledRun> out;
    out.reserve(runs.size() + 2);
    for (const auto& r : runs) {
        const std::size_t s = r.byte_offset, e = r.byte_offset + r.byte_length;
        const std::size_t ia = std::max(s, a), ib = std::min(e, b);
        if (ia >= ib) { out.push_back(r); continue; }
        if (s < ia) out.push_back({s, ia - s, r.style});
        out.push_back({ia, ib - ia, r.style.merge(over)});
        if (ib < e) out.push_back({ib, e - ib, r.style});
    }
    runs = std::move(out);
}

void Rich::underlay(const Style& under) {
    for (auto& run : runs) run.style = under.merge(run.style);
}

void Rich::replace_colors(const Style& st) {
    for (auto& run : runs) {
        run.style.fg = st.fg;
        run.style.bg = st.bg;
        run.style.inverse = false;
    }
}

maya::Element Rich::element() const {
    // Text with no colour of its own is the THEME's text colour. maya fills
    // a themed canvas's background but leaves an unset fg to the terminal,
    // so without this a light scheme in a dark terminal draws light text
    // on its light canvas.
    std::vector<StyledRun> rs = runs;
    for (auto& r : rs)
        if (!r.style.fg) r.style.fg = maya::Color::slot(maya::ThemeSlot::Text);
    return maya::Element{maya::TextElement{
        .content = text,
        .style   = {},
        .wrap    = maya::TextWrap::NoWrap,
        .runs    = std::move(rs),
    }};
}

std::vector<std::pair<std::size_t, std::size_t>> wrap_spans(const Rich& r, int cols) {
    std::vector<std::pair<std::size_t, std::size_t>> rows;
    cols = std::max(cols, 1);
    const std::string_view s = r.text;
    std::size_t start = 0;
    while (start < s.size()) {
        // Continuation rows don't start with the space we broke at.
        if (!rows.empty())
            while (start < s.size() && s[start] == ' ') ++start;
        if (start >= s.size()) break;

        int w = 0;
        std::size_t i = start, last_space = std::string::npos;
        while (i < s.size() && s[i] != '\n') {
            const int gw = glyph_width(s, i);
            if (w + gw > cols) break;
            if (s[i] == ' ') last_space = i;
            w += gw;
            i += utf8_len(static_cast<unsigned char>(s[i]));
        }
        std::size_t end = i, next = i;
        if (i < s.size() && s[i] == '\n') {
            next = i + 1;                               // hard break
        } else if (i < s.size() && last_space != std::string::npos && last_space > start) {
            end = last_space;                           // break after a word
            next = last_space + 1;
        } else if (i == start) {
            next = end = start + utf8_len(static_cast<unsigned char>(s[start]));  // wider than a row
        }
        rows.push_back({start, std::min(end, s.size())});
        start = next;
    }
    if (rows.empty()) rows.push_back({0, 0});
    return rows;
}

std::vector<Rich> wrap(const Rich& r, int cols) {
    std::vector<Rich> rows;
    for (auto [a, b] : wrap_spans(r, cols)) rows.push_back(r.slice(a, b));
    return rows;
}

}  // namespace nx::tui
