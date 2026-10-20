// nexchat TUI — the Channel List window (see windows.hpp): /LIST in a
// table you filter by typing, sort by clicking a column, and join from
// with Enter or a double-click.

#include <algorithm>
#include <cctype>

#include "bridge.h"
#include "scrollbar.hpp"
#include "ircfmt.hpp"
#include "theme.hpp"
#include "windows.hpp"

namespace nx::tui {

namespace {

bool icontains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        std::size_t k = 0;
        while (k < needle.size() && lower(hay[i + k]) == lower(needle[k])) ++k;
        if (k == needle.size()) return true;
    }
    return false;
}

}  // namespace

void ChannelList::open(std::uintptr_t net, std::string network_name) {
    net_ = net;
    network_ = std::move(network_name);
    panel_.no_list();
    panel_.body_is_form = false;
    panel_.draw = [this](int w, int h, bool f) { return draw(w, h, f); };
    panel_.body_key = [this](const maya::KeyEvent& k) { return body_key(k); };
    panel_.body_click = [this](int x, int y, bool dbl, bool right) {
        if (y == 0) {                                   // the filter row
            return false;
        }
        if (y == 2) {                                   // the header: sort
            sort_ = x < 10 ? Sort::Users : Sort::Name;
            refilter();
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(top_ + y - 3);
        if (y >= 3 && i < shown_.size()) {
            sel_ = i;
            if (dbl) { join_selected(); return true; }
        }
        (void)right;
        return false;
    };
    panel_.set_buttons({{"Join", B_JOIN}, {"Refresh", B_REFRESH}, {"Close", B_CLOSE}});
    filter_.set_prompt(Rich{" ⌕ ", role::muted()});
    filter_.set_placeholder("Filter by name or topic");
    panel_.open("Channel List — " + network_);
    panel_.set_part(Panel::Part::Body);
    refilter();
}

void ChannelList::close() {
    nx_core_chanlist_closed(net_);
    panel_.close();
}

void ChannelList::begin() {
    rows_.clear();
    shown_.clear();
    sel_ = 0;
    loading_ = true;
}

void ChannelList::add(std::string chan, int users, std::string topic) {
    rows_.push_back({std::move(chan), users, strip_irc(topic)});
    // refilter every so often: a 50 000-channel network shouldn't redo the
    // sort per row, and the table should still fill as it arrives
    if (rows_.size() < 200 || rows_.size() % 500 == 0) refilter();
}

void ChannelList::refilter() {
    const std::string f = filter_.text();
    shown_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Row& r = rows_[i];
        if (r.users < min_users_) continue;
        if (!icontains(r.chan, f) && !icontains(r.topic, f)) continue;
        shown_.push_back(i);
    }
    if (sort_ == Sort::Users)
        std::stable_sort(shown_.begin(), shown_.end(), [&](std::size_t a, std::size_t b) { return rows_[a].users > rows_[b].users; });
    else
        std::stable_sort(shown_.begin(), shown_.end(), [&](std::size_t a, std::size_t b) {
            return strcasecmp(rows_[a].chan.c_str(), rows_[b].chan.c_str()) < 0;
        });
    sel_ = std::min(sel_, shown_.empty() ? 0 : shown_.size() - 1);
    panel_.status = std::to_string(shown_.size()) + " of " + std::to_string(rows_.size()) + " channels"
                  + (loading_ ? " · listing…" : "")
                  + " · Enter joins · click a heading to sort";
    panel_.set_buttons({{"Join", B_JOIN, !shown_.empty()}, {"Refresh", B_REFRESH}, {"Close", B_CLOSE}});
}

void ChannelList::join_selected() {
    if (sel_ < shown_.size()) nx_core_join(net_, rows_[shown_[sel_]].chan.c_str());
}

void ChannelList::paste(std::string_view s) {
    filter_.paste(s);
    refilter();
}

bool ChannelList::body_key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    if (const auto* sk = std::get_if<SpecialKey>(&k.key)) {
        switch (*sk) {
        case SpecialKey::Up: if (sel_ > 0) --sel_; return true;
        case SpecialKey::Down: if (sel_ + 1 < shown_.size()) ++sel_; return true;
        case SpecialKey::PageUp: sel_ = sel_ > 10 ? sel_ - 10 : 0; return true;
        case SpecialKey::PageDown: sel_ = std::min(shown_.empty() ? 0 : shown_.size() - 1, sel_ + 10); return true;
        case SpecialKey::Home: if (k.mods.ctrl) { sel_ = 0; return true; } break;
        case SpecialKey::End: if (k.mods.ctrl) { sel_ = shown_.empty() ? 0 : shown_.size() - 1; return true; } break;
        case SpecialKey::Enter: join_selected(); return true;
        case SpecialKey::Tab: case SpecialKey::BackTab: return false;
        default: break;
        }
    }
    // everything else edits the filter
    const auto r = filter_.key(k);
    if (r == InputLine::Result::Changed) { refilter(); return true; }
    return r != InputLine::Result::Unhandled;
}

std::vector<Rich> ChannelList::draw(int w, int h, bool focused) const {
    std::vector<Rich> out;
    out.push_back(filter_.row(w, focused));
    out.push_back(Rich{}.pad_right(w));
    // header: one ruled row (each title padded under the underline)
    Rich head;
    head.append(Rich{sort_ == Sort::Users ? "  Users ▾" : "  Users", sort_ == Sort::Users ? role::column_sorted() : role::column()}.fit(10, role::column()));
    head.append(Rich{sort_ == Sort::Name ? "Channel ▾" : "Channel", sort_ == Sort::Name ? role::column_sorted() : role::column()}.fit(24, role::column()));
    head.append(Rich{"Topic", role::column()}.fit(std::max(0, w - 34), role::column()));
    out.push_back(head.fit(w));

    const int body = h - 3;
    const int s = static_cast<int>(sel_);
    if (s < top_) top_ = s;
    if (s >= top_ + body) top_ = s - body + 1;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(shown_.size()) - body));
    for (int r = 0; r < body; ++r) {
        const std::size_t i = static_cast<std::size_t>(top_ + r);
        if (i >= shown_.size()) {
            // an empty list says why, a few rows down
            out.push_back(i == 1 && shown_.empty()
                ? Rich{loading_ ? "  Asking the server for its channels…"
                                : rows_.empty() ? "  No channels yet. Press Refresh to ask the server."
                                                : "  Nothing matches the filter.", role::muted()}.fit(w)
                : Rich{}.pad_right(w));
            continue;
        }
        const Row& row = rows_[shown_[i]];
        const bool cur = i == sel_;
        Rich line{cur ? "▌" : " ", role::title()};
        line.append(Rich{std::to_string(row.users), role::muted()}.pad_left(6).fit(9));
        line.append(Rich{row.chan, cur ? role::title().with_bold() : role::title()}.fit(24));
        line.append(Rich{row.topic, role::text()});
        line = line.fit(w);
        if (cur && focused) line.replace_colors(role::selected());
        out.push_back(std::move(line));
    }
    add_scrollbar(out, w, 3, body, static_cast<int>(shown_.size()), top_, focused);
    return out;
}

bool ChannelList::handle(Panel::Outcome o) {
    switch (o) {
    case Panel::Outcome::Closed: close(); return true;
    case Panel::Outcome::Button:
        switch (panel_.button()) {
        case B_JOIN: join_selected(); return false;
        case B_REFRESH: nx_core_chanlist_refresh(net_, ""); return false;
        case B_CLOSE: close(); return true;
        default: return false;
        }
    default: return false;
    }
}

bool ChannelList::key(const maya::KeyEvent& k) { return handle(panel_.key(k)); }
bool ChannelList::mouse(const maya::MouseEvent& m) { return handle(panel_.mouse(m)); }

}  // namespace nx::tui
