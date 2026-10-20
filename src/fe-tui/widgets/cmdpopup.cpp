// nx::tui CmdPopup — see cmdpopup.hpp.

#include "cmdpopup.hpp"

#include <algorithm>

#include "scrollbar.hpp"
#include "theme.hpp"

namespace nx::tui {

void CmdPopup::set(Data d, std::string for_text) {
    std::string keep;
    if (const Item* s = selected()) keep = s->insert + "\x01" + s->name;
    const bool same_list = data_.args == d.args && data_.items.size() == d.items.size();
    data_ = std::move(d);
    for_ = std::move(for_text);
    open_ = !data_.items.empty() || !data_.usage.empty();
    sel_ = 0;
    if (!keep.empty() && same_list)
        for (std::size_t i = 0; i < data_.items.size(); ++i)
            if (data_.items[i].insert + "\x01" + data_.items[i].name == keep) { sel_ = i; break; }
    if (!same_list) top_ = 0;
}

void CmdPopup::move(int d) {
    const int n = static_cast<int>(data_.items.size());
    if (!n) return;
    const int i = static_cast<int>(sel_);
    // single steps wrap; page steps stop at the ends
    sel_ = static_cast<std::size_t>(d == 1 || d == -1 ? ((i + d) % n + n) % n : std::clamp(i + d, 0, n - 1));
}

const CmdPopup::Item* CmdPopup::selected() const {
    if (!open_ || sel_ >= data_.items.size()) return nullptr;
    return &data_.items[sel_];
}

std::vector<Rich> CmdPopup::rows(int w, int max_h) const {
    std::vector<Rich> out;
    row_item_.clear();
    if (!is_open() || w < 20 || max_h < 3) return out;
    const Style dim = role::muted();
    const int inner = w - 4;                 // "│ " … " │"
    auto boxed = [&](Rich r, int item = -1) {
        Rich row{"\u2502 ", role::border()};
        row.append(r.fit(inner));
        row.append(" \u2502", role::border());
        out.push_back(std::move(row));
        row_item_.push_back(item);
    };

    // top edge, titled: "╭─ Commands ──…─╮" / "╭─ /kick <nick> [reason] ─╮"
    {
        Rich top{"\u256d\u2500", role::border()};
        std::string title = data_.args ? (data_.usage.empty() ? std::string("Values") : std::string())
                                       : (for_.size() <= 1 ? "Commands" : "Commands matching \u201c" + for_.substr(1) + "\u201d");
        if (data_.args && !data_.usage.empty()) {
            // the usage, the argument under the caret bold + underlined
            Rich u;
            u.append(" ");
            const std::string& s = data_.usage;
            if (data_.arg_start >= 0 && data_.arg_end <= static_cast<int>(s.size()) && data_.arg_start < data_.arg_end) {
                u.append(s.substr(0, static_cast<std::size_t>(data_.arg_start)), role::title());
                u.append(s.substr(static_cast<std::size_t>(data_.arg_start),
                                  static_cast<std::size_t>(data_.arg_end - data_.arg_start)),
                         role::text().with_bold().with_underline());
                u.append(s.substr(static_cast<std::size_t>(data_.arg_end)), role::title());
            } else {
                u.append(s, role::title());
            }
            u.append(" ");
            top.append(u.take(std::max(0, w - 6)));
        } else {
            top.append(" " + title + " ", role::title());
        }
        std::string fill;
        for (int i = top.width(); i < w - 1; ++i) fill += "\u2500";
        top.append(fill, role::border());
        top.append("\u256e", role::border());
        out.push_back(top.fit(w));
        row_item_.push_back(-1);
    }
    if (data_.args && !data_.desc.empty()) boxed(Rich{data_.desc, dim});

    // the items: name, middle, description in columns
    const auto& it = data_.items;
    int name_w = 0, mid_w = 0;
    for (const auto& i : it) {
        name_w = std::max(name_w, display_width(i.name));
        mid_w = std::max(mid_w, display_width(i.middle));
    }
    name_w = std::min(name_w, inner / 3);
    mid_w = std::min(mid_w, inner / 3);

    // rows available for items (top, footer, bottom edge, the desc line)
    const bool foot = true;
    const int fixed = static_cast<int>(out.size()) + (foot ? 2 : 0) + 1;
    const bool details_line = !data_.args && !details.empty();
    const int room = std::max(1, max_h - fixed - (details_line ? 3 : 0));
    // headings take rows too: lay out (item, heading?) lines
    struct Line { int item; bool heading; };
    std::vector<Line> lines;
    for (std::size_t i = 0; i < it.size(); ++i) {
        if (!it[i].heading.empty()) lines.push_back({static_cast<int>(i), true});
        lines.push_back({static_cast<int>(i), false});
    }
    int sel_line = 0;
    for (std::size_t k = 0; k < lines.size(); ++k)
        if (!lines[k].heading && lines[k].item == static_cast<int>(sel_)) sel_line = static_cast<int>(k);
    const int view = std::min(room, static_cast<int>(lines.size()));
    if (sel_line < top_) top_ = sel_line;
    if (sel_line >= top_ + view) top_ = sel_line - view + 1;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(lines.size()) - view));
    const std::size_t first = out.size();
    for (int r = 0; r < view; ++r) {
        const Line& ln = lines[static_cast<std::size_t>(top_ + r)];
        const Item& i = it[static_cast<std::size_t>(ln.item)];
        if (ln.heading) { boxed(Rich{i.heading, role::title()}); continue; }
        const bool cur = static_cast<std::size_t>(ln.item) == sel_;
        const bool hov = ln.item == hover_ && !cur;
        Rich row{cur ? "\u258c" : " ", role::title()};
        row.append(Rich{i.name, hov ? role::text().with_bold().with_underline() : role::text().with_bold()}.fit(name_w));
        row.append("  ");
        if (mid_w) {
            row.append(Rich{i.middle, role::title()}.fit(mid_w));
            row.append("  ");
        }
        row.append(i.desc, dim);
        row = row.fit(inner);
        if (cur) row.replace_colors(role::selected());
        boxed(row, ln.item);
    }
    // a scrollbar on the right edge when the list is longer than the box
    if (static_cast<int>(lines.size()) > view) {
        const Thumb t = thumb_for(view, static_cast<int>(lines.size()), top_);
        for (int r = 0; r < view; ++r) {
            Rich& row = out[first + static_cast<std::size_t>(r)];
            const bool on = r >= t.y && r < t.y + t.h;
            Rich fixed_row = row.take(w - 1);
            fixed_row.append(on ? "\u2503" : "\u2502", on ? role::muted() : role::border());
            row = fixed_row;
        }
    }
    if (it.empty() && data_.args) boxed(Rich{"Type the value; Enter sends.", dim});

    // details of the selected command: what more it does, an example
    // (wrapped, two lines at most)
    if (details_line) {
        boxed(Rich{});
        auto wrapped = wrap(Rich{details, dim.with_italic()}, inner);
        for (std::size_t i = 0; i < wrapped.size() && i < 2; ++i) boxed(wrapped[i]);
    }
    if (foot) {
        std::string s;
        for (int i = 0; i < w - 2; ++i) s += "\u254c";
        out.push_back(Rich{"\u251c" + s + "\u2524", role::border()});
        row_item_.push_back(-1);
        Rich keys;
        auto key = [&](const char* k, const char* what) {
            if (!keys.text.empty()) keys.append("   ");
            keys.append(k, role::text().with_bold());
            keys.append(std::string(" ") + what, dim);
        };
        if (!it.empty()) { key("\u2191\u2193", "pick"); key("Tab", "fill in"); }
        key("Enter", it.empty() ? "send" : "fill in");
        key("Esc", "close");
        boxed(keys);
    }
    {
        std::string s;
        for (int i = 0; i < w - 2; ++i) s += "\u2500";
        out.push_back(Rich{"\u2570" + s + "\u256f", role::border()});
        row_item_.push_back(-1);
    }
    return out;
}

std::optional<std::size_t> CmdPopup::item_at_row(int y) {
    if (y < 0 || static_cast<std::size_t>(y) >= row_item_.size() || row_item_[static_cast<std::size_t>(y)] < 0)
        return std::nullopt;
    sel_ = static_cast<std::size_t>(row_item_[static_cast<std::size_t>(y)]);
    return sel_;
}

void CmdPopup::hover_row(int y) {
    hover_ = (y >= 0 && static_cast<std::size_t>(y) < row_item_.size()) ? row_item_[static_cast<std::size_t>(y)] : -1;
}

}  // namespace nx::tui
