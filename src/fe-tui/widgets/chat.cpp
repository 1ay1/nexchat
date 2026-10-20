// nx::tui::chat — see chat.hpp.

#include "chat.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <tuple>

#include <maya/dsl.hpp>
#include <maya/style/theme.hpp>

#include "ircfmt.hpp"
#include "theme.hpp"

namespace nx::tui {

// ── ChatBuffer ──────────────────────────────────────────────────────────

LineId ChatBuffer::add(std::string_view text, std::time_t stamp, bool hilight) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    const auto [l, r] = split_columns(text);
    ChatLine line;
    line.id = next_id_++;
    line.stamp = stamp;
    line.left = std::string{l};
    line.right = std::string{r};
    line.hilight = hilight;
    lines_.push_back(std::move(line));
    while (max_ && lines_.size() > max_) lines_.pop_front();
    ++version_;
    return lines_.back().id;
}

void ChatBuffer::clear() {
    lines_.clear();
    marker_.reset();
    ++version_;
}

namespace {
int day_of(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}
}  // namespace

// textgui.c day_separator: before the first line of a new day; a fresh
// buffer only when it starts in the past
void ChatBuffer::add_day_if_new(std::time_t stamp) {
    if (!stamp) return;
    const int day = day_of(stamp);
    int last = 0;
    for (auto it = lines_.rbegin(); it != lines_.rend(); ++it)
        if (it->stamp) { last = day_of(it->stamp); break; }
    if (day == last) return;
    if (!last && day == day_of(std::time(nullptr))) return;
    std::tm tm{};
    localtime_r(&stamp, &tm);
    std::tm now{};
    const std::time_t n = std::time(nullptr);
    localtime_r(&n, &now);
    char buf[64];
    std::strftime(buf, sizeof buf, tm.tm_year == now.tm_year ? "%A, %e %B" : "%A, %e %B %Y", &tm);
    std::string date = buf;
    for (std::size_t p; (p = date.find("  ")) != std::string::npos;) date.erase(p, 1);
    ChatLine line;
    line.id = next_id_++;
    line.stamp = stamp;
    line.right = date;
    line.day = true;
    lines_.push_back(std::move(line));
    while (max_ && lines_.size() > max_) lines_.pop_front();
    ++version_;
}

void ChatBuffer::trim(int n) {
    if (n == 0) { clear(); return; }
    for (; n > 0 && !lines_.empty(); --n) lines_.pop_front();
    for (; n < 0 && !lines_.empty(); ++n) lines_.pop_back();
    ++version_;
}

void ChatBuffer::set_max_lines(std::size_t n) {
    max_ = n;
    while (max_ && lines_.size() > max_) lines_.pop_front();
    ++version_;
}

std::optional<std::size_t> ChatBuffer::index_of(LineId id) const noexcept {
    if (lines_.empty() || id < lines_.front().id || id > lines_.back().id) return std::nullopt;
    // Ids are dense and increasing: the line is at a fixed offset.
    const std::size_t i = static_cast<std::size_t>(id - lines_.front().id);
    return i < lines_.size() && lines_[i].id == id ? std::optional{i} : std::nullopt;
}

std::optional<std::size_t> ChatBuffer::marker_index() const noexcept {
    if (!marker_ || lines_.empty() || *marker_ > lines_.back().id) return std::nullopt;
    if (*marker_ <= lines_.front().id) return std::nullopt;   // everything is new: no line to mark
    return index_of(*marker_);
}

// ── one line ────────────────────────────────────────────────────────────

namespace {

std::uint64_t style_key(const ChatStyle& st) {
    std::uint64_t k = std::hash<std::string>{}(st.stamp_format);
    k = k * 31 + static_cast<std::uint64_t>(st.nick_width);
    k = k * 31 + (st.show_stamp ? 1 : 0) + (st.separator ? 2 : 0) + (st.indent ? 4 : 0);
    // IRC colours resolve against the live theme when a line is parsed, so
    // a theme switch must reparse: the epoch is part of the key.
    k = k * 31 + maya::theme::live_epoch();
    return k;
}

std::string stamp_text(std::time_t t, const std::string& fmt) {
    if (!t) return {};
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    const std::size_t n = std::strftime(buf, sizeof buf, fmt.c_str(), &tm);
    return std::string(buf, n);
}

// The width a stamp takes, whatever the time: measured on a fixed time.
int stamp_width(const std::string& fmt) {
    std::tm tm{};
    tm.tm_hour = 12; tm.tm_min = 34; tm.tm_sec = 56; tm.tm_mday = 28; tm.tm_mon = 11; tm.tm_year = 99;
    char buf[64];
    const std::size_t n = std::strftime(buf, sizeof buf, fmt.c_str(), &tm);
    return display_width(std::string_view(buf, n));
}

}  // namespace

// Cells of `r[a, b)`, each tagged with its byte in the column's text.
static void tag_cells(std::vector<RowCell>& cells, const std::string& text, std::size_t a, std::size_t b,
                      RowCell::Src src) {
    for (std::size_t i = a; i < b;) {
        const int gw = glyph_width(text, i);
        for (int k = 0; k < std::max(gw, 0); ++k) cells.push_back({src, static_cast<std::uint32_t>(i)});
        i += utf8_len(static_cast<unsigned char>(text[i]));
    }
}

static void pad_cells(std::vector<RowCell>& cells, int n) {
    for (int i = 0; i < n; ++i) cells.push_back({});
}

const std::vector<Rich>& line_rows(const ChatLine& line, const ChatStyle& st, int w) {
    const std::uint64_t key = style_key(st);
    if (line.cached_cols == w && line.cached_style == key) return line.cached_rows;

    const Style dim = role::muted();
    if (line.day) {
        // ────────── Tuesday, 14 October ──────────  across the whole view
        line.plain_left.clear();
        line.plain_right = line.right;
        const std::string label = " " + line.right + " ";
        const int lw = display_width(label);
        const int side = std::max(0, (w - lw) / 2);
        std::string a, b;
        for (int i = 0; i < side; ++i) a += "─";
        for (int i = 0; i < w - side - lw; ++i) b += "─";
        Rich row{a, role::rule()};
        row.append(label, dim.with_bold());
        row.append(b, role::rule());
        line.cached_rows = {row.fit(w)};
        line.cached_cells = {std::vector<RowCell>(static_cast<std::size_t>(std::max(w, 0)))};
        line.cached_cols = w;
        line.cached_style = key;
        return line.cached_rows;
    }
    Rich left = parse_irc(line.left);
    Rich right = parse_irc(line.right);
    if (line.hilight) right.overlay(Style{}.with_bold());
    line.plain_left = left.text;
    line.plain_right = right.text;

    Rich stamp;
    int stamp_w = 0;
    if (st.show_stamp) {
        stamp_w = stamp_width(st.stamp_format);
        stamp = Rich{stamp_text(line.stamp, st.stamp_format), dim}.pad_right(stamp_w).take(stamp_w);
        stamp.append(" ");
        stamp_w += 1;
    }

    std::vector<Rich> rows;
    std::vector<std::vector<RowCell>> cells;
    // the nick column gives way on a narrow chat (text under 40 columns):
    // down to 8, a long nick then cut with …; a small configured width stays
    const int sep_w = st.separator ? 3 : 1;           // " │ " or " "
    int nick_w = st.nick_width;
    if (w - stamp_w - sep_w - nick_w < 40 && nick_w > 8)
        nick_w = std::clamp(w - stamp_w - sep_w - 40, 8, nick_w);
    const int text_w = w - stamp_w - nick_w - sep_w;

    if (text_w < 12) {
        // Too narrow for columns: "stamp nick text", wrapped flush left.
        Rich all = stamp;
        const std::size_t left_at = all.text.size();
        if (!left.empty()) { all.append(left); all.append(" "); }
        const std::size_t right_at = all.text.size();
        all.append(right);
        for (auto [a, b] : wrap_spans(all, std::max(w, 1))) {
            rows.push_back(all.slice(a, b));
            std::vector<RowCell> c;
            for (std::size_t i = a; i < b;) {
                RowCell rc;
                if (i >= right_at) rc = {RowCell::Right, static_cast<std::uint32_t>(i - right_at)};
                else if (i >= left_at && i < left_at + left.text.size())
                    rc = {RowCell::Left, static_cast<std::uint32_t>(i - left_at)};
                for (int k = 0; k < std::max(glyph_width(all.text, i), 0); ++k) c.push_back(rc);
                i += utf8_len(static_cast<unsigned char>(all.text[i]));
            }
            cells.push_back(std::move(c));
        }
    } else {
        const bool cut = left.width() > nick_w;
        Rich nick = cut ? left.take(nick_w - 1) : left;
        if (cut) nick.append("…", left.runs.empty() ? Style{} : left.runs.back().style);
        const int nick_pad = nick_w - nick.width();
        // indent on: right-aligned against the │ (xtext's); off: flush left
        nick = st.indent ? nick.pad_left(nick_w) : nick.pad_right(nick_w);
        Rich sep{st.separator ? " │ " : " ", dim};
        Rich blank = Rich{}.pad_right(stamp_w + nick_w);

        const auto spans = wrap_spans(right, text_w);
        rows.reserve(spans.size());
        for (std::size_t i = 0; i < spans.size(); ++i) {
            Rich row = i == 0 ? stamp : Rich{};
            std::vector<RowCell> c;
            pad_cells(c, stamp_w);
            if (i == 0) {
                row.append(nick);
                if (st.indent) pad_cells(c, nick_pad);
                tag_cells(c, left.text, 0, cut ? left.take(nick_w - 1).text.size() : left.text.size(), RowCell::Left);
                if (cut) pad_cells(c, 1);
                if (!st.indent) pad_cells(c, nick_pad);
            } else {
                row.append(blank);
                pad_cells(c, nick_w);
            }
            row.append(sep);
            pad_cells(c, sep_w);
            row.append(right.slice(spans[i].first, spans[i].second));
            tag_cells(c, right.text, spans[i].first, spans[i].second, RowCell::Right);
            rows.push_back(std::move(row));
            cells.push_back(std::move(c));
        }
    }
    line.cached_rows = std::move(rows);
    line.cached_cells = std::move(cells);
    line.cached_cols = w;
    line.cached_style = key;
    return line.cached_rows;
}

maya::Element stack_rows(const std::vector<Rich>& rows) {
    std::vector<maya::Element> els;
    els.reserve(rows.size());
    for (const auto& r : rows) els.push_back(r.element());
    return maya::dsl::v(std::move(els)).build();
}

// ── ChatView ────────────────────────────────────────────────────────────

namespace {

Rich marker_row(int w) {
    // ─────────── new ──  (the read marker: what came while you were away)
    const char* tag = " new ";
    const int tw = 5;
    if (w < tw + 6) {
        std::string s;
        for (int i = 0; i < w; ++i) s += "─";
        return Rich{s, role::marker()};
    }
    std::string a, b = "──";
    for (int i = 0; i < w - tw - 2; ++i) a += "─";
    Rich r{a, role::marker()};
    r.append(tag, role::marker().with_bold());
    r.append(b, role::marker());
    return r;
}

// A position in the buffer by display row: line li, row ri within it
// (the read marker, when it sits above line li, is that line's row 0).
struct Cursor {
    const ChatBuffer& buf;
    const ChatStyle&  st;
    int               w;
    std::optional<std::size_t> marker;
    std::size_t li = 0;
    int         ri = 0;

    Cursor(const ChatBuffer& b, const ChatStyle& s, int w_)
        : buf(b), st(s), w(w_), marker(s.show_marker ? b.marker_index() : std::nullopt) {}

    int count(std::size_t i) const {
        return static_cast<int>(line_rows(buf[i], st, w).size()) + (marker == i ? 1 : 0);
    }
    Rich row() const {
        int r = ri;
        if (marker == li) {
            if (r == 0) return marker_row(w);
            --r;
        }
        return line_rows(buf[li], st, w)[static_cast<std::size_t>(r)];
    }
    bool up() {
        if (ri > 0) { --ri; return true; }
        if (li == 0) return false;
        --li; ri = count(li) - 1;
        return true;
    }
    bool down() {
        if (ri < count(li) - 1) { ++ri; return true; }
        if (li + 1 >= buf.size()) return false;
        ++li; ri = 0;
        return true;
    }
    bool at_end() const { return li == buf.size() - 1 && ri == count(li) - 1; }
};

}  // namespace

// Put the cursor on the view's bottom row.
static void seek_bottom(Cursor& c, const std::optional<std::pair<LineId, int>>& anchor) {
    c.li = c.buf.size() - 1;
    c.ri = c.count(c.li) - 1;
    if (!anchor) return;
    if (auto idx = c.buf.index_of(anchor->first)) {
        c.li = *idx;
        c.ri = std::clamp(anchor->second, 0, c.count(c.li) - 1);
    } else {
        c.li = 0;                                   // trimmed away: hold at the top
        c.ri = 0;
    }
}

std::vector<Rich> ChatView::rows(const ChatBuffer& buf, const ChatStyle& st, int w, int h) const {
    // A scrollbar column on the right once there's more than a screen: the
    // text is laid out a column narrower so it never sits under the bar.
    // (Every line's row count would cost a wrap of the whole buffer per
    // frame; the bar measures in lines instead, which is what it shows.)
    bool bar = false;
    if (w >= 20 && h > 0 && !buf.empty()) {
        std::size_t rows_seen = 0;
        for (std::size_t i = buf.size(); i-- > 0 && rows_seen <= static_cast<std::size_t>(h);)
            rows_seen += line_rows(buf[i], st, w - 1).size();
        bar = rows_seen > static_cast<std::size_t>(h) || anchor_.has_value();
    }
    std::vector<Rich> out = rows_at(buf, st, bar ? w - 1 : w, h);
    thumb_ = {0, 0};
    if (!bar) return out;
    for (auto& r : out) r = r.pad_right(w - 1);
    // in lines: the bottom line's place in the buffer
    std::size_t bottom = buf.size() - 1, top = bottom;
    for (const RowSrc& s : drawn_)
        if (s.line) { if (auto i = buf.index_of(s.line)) { top = std::min(top, *i); } }
    if (!drawn_.empty() && drawn_.back().line)
        if (auto i = buf.index_of(drawn_.back().line)) bottom = *i;
    const int total = static_cast<int>(buf.size());
    const int seen = std::max(1, static_cast<int>(bottom - top) + 1);
    // scale lines onto the h rows
    const int thumb_h = std::clamp(h * seen / std::max(1, total), 1, h);
    const int last = std::max(1, total - seen);
    const int thumb_y = (h - thumb_h) * std::clamp(static_cast<int>(top), 0, last) / last;
    thumb_ = {thumb_y, thumb_h};
    if (bar_outside) {
        // the owner draws it (on the pane divider): the text keeps the width
        last_w_ = w;
        return rows_at(buf, st, w, h);
    }
    const Style thumb = role::muted();
    for (int r = 0; r < h; ++r) {
        const bool on = r >= thumb_y && r < thumb_y + thumb_h;
        out[static_cast<std::size_t>(r)].append(on ? "▐" : " ", on ? thumb : Style{});
    }
    last_w_ = w - 1;   // hits and scrolling use the text's width
    return out;
}

std::vector<Rich> ChatView::rows_at(const ChatBuffer& buf, const ChatStyle& st, int w, int h) const {
    last_w_ = w; last_h_ = h; last_style_ = st;
    std::vector<Rich> out;
    drawn_.assign(static_cast<std::size_t>(std::max(h, 0)), RowSrc{});
    if (h <= 0) return out;
    if (buf.empty()) { out.resize(static_cast<std::size_t>(h)); return out; }

    Cursor c{buf, st, w};
    seek_bottom(c, anchor_ ? std::optional{std::pair{anchor_->line, anchor_->row}} : std::nullopt);

    // Selection bounds, in buffer order: (line index, in_left ? 0 : 1, byte).
    auto key_of = [&](const Hit& x) -> std::optional<std::tuple<std::size_t, int, std::size_t>> {
        auto i = buf.index_of(x.line);
        if (!i) return std::nullopt;
        return std::tuple{*i, x.in_left ? 0 : 1, x.byte};
    };
    std::optional<std::tuple<std::size_t, int, std::size_t>> s0, s1;
    if (has_selection()) {
        s0 = key_of(*sel_a_);
        s1 = key_of(*sel_b_);
        if (s0 && s1 && *s1 < *s0) std::swap(s0, s1);
        if (!s0 || !s1) s0.reset();
    }

    std::vector<Rich> rev;
    std::vector<RowSrc> rsrc;
    rev.reserve(static_cast<std::size_t>(h));
    do {
        Rich row = c.row();
        RowSrc src;
        src.line = buf[c.li].id;
        const int r = c.ri - (c.marker == c.li ? 1 : 0);
        src.marker = r < 0;
        if (r >= 0) {
            const auto& cells = buf[c.li].cached_cells;
            if (static_cast<std::size_t>(r) < cells.size()) src.cells = cells[static_cast<std::size_t>(r)];
        }
        // Paint the selected cells, and the find matches.
        const bool finding = !find_.empty() && !src.marker;
        std::vector<Match> on_line;
        if (finding) {
            const ChatLine& L = buf[c.li];
            find_in(L.plain_right, L.id, on_line);
        }
        const bool hovering = hover_ && hover_->line == src.line && !src.marker;
        if ((s0 || !on_line.empty() || hovering) && !src.marker) {
            Rich painted;
            std::size_t cell = 0;
            for (std::size_t i = 0; i < row.text.size();) {
                const std::size_t n = utf8_len(static_cast<unsigned char>(row.text[i]));
                const int gw = std::max(glyph_width(row.text, i), 0);
                bool sel = false, hov = false;
                int found = 0;   // 1 a match, 2 the current one
                if (cell < src.cells.size() && src.cells[cell].src != RowCell::None) {
                    const bool left = src.cells[cell].src == RowCell::Left;
                    const auto byte = static_cast<std::size_t>(src.cells[cell].byte);
                    hov = hovering && hover_->in_left == left && byte >= hover_->begin && byte < hover_->end;
                    if (s0) {
                        const auto k = std::tuple{c.li, left ? 0 : 1, byte};
                        sel = k >= *s0 && k < *s1;
                    }
                    if (!left)
                        for (const Match& m : on_line)
                            if (byte >= m.byte && byte < m.byte + m.len) {
                                const bool cur = find_cur_ && find_cur_->line == m.line && find_cur_->byte == m.byte;
                                if (cur || find_opts_.all) found = std::max(found, cur ? 2 : 1);
                            }
                }
                Rich g = row.slice(i, i + n);
                if (sel) g.replace_colors(role::selected());
                else if (found == 2) { g.replace_colors(role::found_current()); g.overlay(Style{}.with_bold().with_underline()); }
                else if (found == 1) g.replace_colors(role::found());
                if (hov) g.overlay(Style{}.with_underline());
                painted.append(g);
                cell += static_cast<std::size_t>(gw);
                i += n;
            }
            row = std::move(painted);
        }
        rev.push_back(std::move(row));
        rsrc.push_back(std::move(src));
    } while (static_cast<int>(rev.size()) < h && c.up());

    // Short buffer: the text sits at the bottom, blank rows above it.
    const std::size_t blank = static_cast<std::size_t>(h) - rev.size();
    out.resize(blank);
    out.insert(out.end(), rev.rbegin(), rev.rend());
    for (std::size_t i = 0; i < rsrc.size(); ++i) drawn_[blank + i] = std::move(rsrc[rsrc.size() - 1 - i]);
    return out;
}

std::optional<ChatView::Hit> ChatView::hit(int x, int y) const {
    if (y < 0 || static_cast<std::size_t>(y) >= drawn_.size()) return std::nullopt;
    const RowSrc& r = drawn_[static_cast<std::size_t>(y)];
    if (r.marker || r.cells.empty()) return std::nullopt;
    if (x < 0) x = 0;
    // Past the end of the row: the end of the text on it (a drag past the
    // edge selects to the end of the line, as xtext does).
    if (static_cast<std::size_t>(x) >= r.cells.size()) {
        for (std::size_t i = r.cells.size(); i-- > 0;)
            if (r.cells[i].src != RowCell::None) {
                const bool left = r.cells[i].src == RowCell::Left;
                return Hit{r.line, left, r.cells[i].byte + 1u};
            }
        return std::nullopt;
    }
    const RowCell& c = r.cells[static_cast<std::size_t>(x)];
    if (c.src == RowCell::None) {
        // Padding before the text column: the start of the row's text.
        for (std::size_t i = static_cast<std::size_t>(x); i < r.cells.size(); ++i)
            if (r.cells[i].src == RowCell::Right) return Hit{r.line, false, r.cells[i].byte};
        return std::nullopt;
    }
    return Hit{r.line, c.src == RowCell::Left, c.byte};
}

std::optional<ChatView::Word> ChatView::word_at(const ChatBuffer& buf, const Hit& h) {
    auto i = buf.index_of(h.line);
    if (!i) return std::nullopt;
    const ChatLine& line = buf[*i];
    const std::string& t = h.in_left ? line.plain_left : line.plain_right;
    if (h.byte >= t.size() || t[h.byte] == ' ') return std::nullopt;
    std::size_t a = h.byte, b = h.byte;
    while (a > 0 && t[a - 1] != ' ') --a;
    while (b < t.size() && t[b] != ' ') ++b;
    // Trim the punctuation around a word: <nick>, (url), "quoted", trailing ,.:;!?
    auto trim_l = [&](char ch) { return ch == '<' || ch == '(' || ch == '[' || ch == '"' || ch == '\''; };
    auto trim_r = [&](char ch) {
        return ch == '>' || ch == ')' || ch == ']' || ch == '"' || ch == '\'' || ch == ',' || ch == '.'
            || ch == ':' || ch == ';' || ch == '!' || ch == '?';
    };
    while (a < b && trim_l(t[a])) ++a;
    while (b > a && trim_r(t[b - 1])) --b;
    // The nick column carries the mode prefix too: <@alice> is alice.
    if (h.in_left) while (a < b && std::string_view("~&@%+").find(t[a]) != std::string_view::npos) ++a;
    if (a >= b) return std::nullopt;
    return Word{t.substr(a, b - a), a, b};
}

bool ChatView::set_hover(const ChatBuffer& buf, std::optional<Hit> h, bool linkish) {
    std::optional<HoverSpan> want;
    if (h && linkish)
        if (auto w = word_at(buf, *h)) want = HoverSpan{h->line, h->in_left, w->begin, w->end};
    const bool same = (!want && !hover_)
                   || (want && hover_ && want->line == hover_->line && want->in_left == hover_->in_left
                       && want->begin == hover_->begin && want->end == hover_->end);
    hover_ = want;
    return !same;
}

void ChatView::select_word(const ChatBuffer& buf, const Hit& h) {
    if (auto w = word_at(buf, h)) {
        sel_a_ = Hit{h.line, h.in_left, w->begin};
        sel_b_ = Hit{h.line, h.in_left, w->end};
    }
}

void ChatView::select_line(const ChatBuffer& buf, const Hit& h) {
    auto i = buf.index_of(h.line);
    if (!i) return;
    sel_a_ = Hit{h.line, true, 0};
    sel_b_ = Hit{h.line, false, buf[*i].plain_right.size()};
}

std::string ChatView::selection_text(const ChatBuffer& buf) const {
    if (!has_selection()) return {};
    auto ia = buf.index_of(sel_a_->line), ib = buf.index_of(sel_b_->line);
    if (!ia || !ib) return {};
    auto key = [](std::size_t li, const Hit& x) { return std::tuple{li, x.in_left ? 0 : 1, x.byte}; };
    Hit a = *sel_a_, b = *sel_b_;
    std::size_t la = *ia, lb = *ib;
    if (key(lb, b) < key(la, a)) { std::swap(a, b); std::swap(la, lb); }

    std::string out;
    for (std::size_t li = la; li <= lb; ++li) {
        const ChatLine& line = buf[li];
        if (line.cached_cols < 0) (void)line_rows(line, last_style_, last_w_);
        // The line's text as copied: "nick\ttext" like xtext, minus the tab.
        auto part = [&](bool left, std::size_t from, std::size_t to) {
            const std::string& t = left ? line.plain_left : line.plain_right;
            to = std::min(to, t.size());
            if (from < to) out.append(t, from, to - from);
        };
        const bool first = li == la, last = li == lb;
        const int from_col = first ? (a.in_left ? 0 : 1) : 0;
        const int to_col = last ? (b.in_left ? 0 : 1) : 1;
        for (int col = from_col; col <= to_col; ++col) {
            const bool left = col == 0;
            const std::size_t f = (first && col == from_col) ? a.byte : 0;
            const std::size_t t = (last && col == to_col) ? b.byte : std::string::npos;
            part(left, f, t);
            if (left && col < to_col && !line.plain_left.empty()) out += ' ';
        }
        if (li < lb) out += '\n';
    }
    return out;
}

// ── find ─────────────────────────────────────────────────────────────────────────

bool ChatView::set_find(std::string needle, FindOpts o) {
    if (needle == find_ && o.match_case == find_opts_.match_case && o.regex == find_opts_.regex && o.all == find_opts_.all)
        return !find_opts_.regex || find_.empty() || find_re_;
    const bool same = needle == find_ && o.match_case == find_opts_.match_case && o.regex == find_opts_.regex;
    find_ = std::move(needle);
    find_opts_ = o;
    find_re_.reset();
    if (!same) find_cur_.reset();
    if (o.regex && !find_.empty()) {
        try {
            auto f = std::regex::ECMAScript | (o.match_case ? std::regex::flag_type{} : std::regex::icase);
            find_re_ = std::make_shared<const std::regex>(find_, f);
        } catch (const std::regex_error&) {
            return false;
        }
    }
    return true;
}

void ChatView::find_in(std::string_view t, LineId id, std::vector<Match>& out) const {
    if (find_.empty()) return;
    if (find_opts_.regex) {
        if (!find_re_) return;
        for (auto it = std::cregex_iterator(t.data(), t.data() + t.size(), *find_re_); it != std::cregex_iterator(); ++it) {
            if (it->length(0) == 0) continue;           // "a*" and the like: nothing to show
            out.push_back({id, static_cast<std::size_t>(it->position(0)), static_cast<std::size_t>(it->length(0))});
        }
        return;
    }
    if (t.size() < find_.size()) return;
    auto low = [](unsigned char ch) { return static_cast<unsigned char>(std::tolower(ch)); };
    const bool mc = find_opts_.match_case;
    for (std::size_t i = 0; i + find_.size() <= t.size();) {
        std::size_t k = 0;
        while (k < find_.size()
               && (mc ? t[i + k] == find_[k]
                      : low(static_cast<unsigned char>(t[i + k])) == low(static_cast<unsigned char>(find_[k]))))
            ++k;
        if (k == find_.size()) { out.push_back({id, i, find_.size()}); i += find_.size(); }
        else ++i;
    }
}

std::vector<ChatView::Match> ChatView::find_all(const ChatBuffer& buf) const {
    std::vector<Match> out;
    if (find_.empty()) return out;
    for (std::size_t li = 0; li < buf.size(); ++li) {
        const ChatLine& line = buf[li];
        if (line.cached_cols < 0) (void)line_rows(line, last_style_, std::max(last_w_, 20));
        find_in(line.plain_right, line.id, out);
    }
    return out;
}

bool ChatView::find_step(const ChatBuffer& buf, int dir) {
    const auto all = find_all(buf);
    if (all.empty()) { find_cur_.reset(); return false; }
    std::size_t at = all.size() - 1;                       // dir 0: the newest
    if (find_cur_ && dir != 0) {
        auto it = std::find_if(all.begin(), all.end(), [&](const Match& m) {
            return m.line == find_cur_->line && m.byte == find_cur_->byte;
        });
        if (it != all.end()) {
            const auto i = static_cast<std::size_t>(it - all.begin());
            at = dir < 0 ? (i == 0 ? all.size() - 1 : i - 1) : (i + 1 == all.size() ? 0 : i + 1);
        } else {
            // its line was trimmed: the newest older (or the oldest newer) one
            at = dir < 0 ? all.size() - 1 : 0;
        }
    }
    find_cur_ = all[at];
    reveal(buf, all[at].line);
    return true;
}

std::pair<int, int> ChatView::find_count(const ChatBuffer& buf) const {
    const auto all = find_all(buf);
    int cur = 0;
    if (find_cur_)
        for (std::size_t i = 0; i < all.size(); ++i)
            if (all[i].line == find_cur_->line && all[i].byte == find_cur_->byte) cur = static_cast<int>(i) + 1;
    return {cur, static_cast<int>(all.size())};
}

void ChatView::reveal(const ChatBuffer& buf, LineId id) {
    auto idx = buf.index_of(id);
    if (!idx || last_h_ <= 0) return;
    // already on screen: leave the view where it is
    for (const RowSrc& r : drawn_) if (r.line == id && !r.cells.empty()) return;
    // its last row half a screen up from the bottom
    Cursor c{buf, last_style_, last_w_};
    c.li = *idx;
    c.ri = c.count(c.li) - 1;
    for (int i = 0; i < last_h_ / 2 && c.down(); ++i) {}
    if (c.at_end()) anchor_.reset();
    else anchor_ = Anchor{buf[c.li].id, c.ri};
}

void ChatView::scroll(const ChatBuffer& buf, int rows) {
    if (buf.empty() || rows == 0) return;
    Cursor c{buf, last_style_, last_w_};
    seek_bottom(c, anchor_ ? std::optional{std::pair{anchor_->line, anchor_->row}} : std::nullopt);

    if (rows > 0) {
        while (rows-- > 0 && c.up()) {}
        // Never past the top: a full screen must fit above the bottom row.
        Cursor top = c;
        int above = 0;
        while (above < last_h_ - 1 && top.up()) ++above;
        for (int need = last_h_ - 1 - above; need > 0 && c.down(); --need) {}
    } else {
        while (rows++ < 0 && c.down()) {}
    }
    if (c.at_end()) anchor_.reset();
    else anchor_ = Anchor{buf[c.li].id, c.ri};
}

void ChatView::to_top(const ChatBuffer& buf) {
    if (buf.empty()) return;
    anchor_ = Anchor{buf[0].id, 0};
    scroll(buf, 1);                      // clamps so the top fills the screen
}

std::size_t ChatView::lines_below(const ChatBuffer& buf) const noexcept {
    if (!anchor_) return 0;
    if (auto idx = buf.index_of(anchor_->line)) return buf.size() - 1 - *idx;
    return buf.size();
}

maya::Element ChatView::element(const ChatBuffer& buf, const ChatStyle& st) const {
    return maya::dsl::fill([this, &buf, st](int w, int h) {
        return stack_rows(rows(buf, st, w, h));
    }).build();
}

}  // namespace nx::tui
