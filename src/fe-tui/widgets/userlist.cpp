// nx::tui::UserList — see userlist.hpp.

#include "userlist.hpp"

#include <algorithm>
#include <cctype>

#include <maya/dsl.hpp>

#include "chat.hpp"
#include "theme.hpp"
#include "scrollbar.hpp"

namespace nx::tui {

namespace {

bool iequal_prefix(std::string_view s, std::string_view prefix) {
    if (prefix.size() > s.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

}  // namespace

void UserList::set(std::vector<UserEntry> users, int ops) {
    std::optional<std::string> keep;
    if (const auto* s = selected()) keep = s->nick;
    users_ = std::move(users);
    if (ops < 0) {
        ops = 0;
        for (const auto& u : users_) ops += (u.prefix == '@' || u.prefix == '~' || u.prefix == '&');
    }
    ops_ = ops;
    has_sel_ = false;
    if (keep) select_nick(*keep);
}

void UserList::move(int d) {
    if (users_.empty()) return;
    if (!has_sel_) { sel_ = d >= 0 ? 0 : users_.size() - 1; has_sel_ = true; return; }
    const long n = static_cast<long>(users_.size());
    sel_ = static_cast<std::size_t>(std::clamp<long>(static_cast<long>(sel_) + d, 0, n - 1));
}

void UserList::select_nick(std::string_view nick) {
    for (std::size_t i = 0; i < users_.size(); ++i)
        if (users_[i].nick == nick) { sel_ = i; has_sel_ = true; return; }
}

const UserEntry* UserList::selected() const {
    return has_sel_ && sel_ < users_.size() ? &users_[sel_] : nullptr;
}

std::optional<std::string> UserList::nick_at_row(int y) const {
    const int i = top_ + y - 1;                      // row 0 is the header
    if (y < 1 || i < 0 || static_cast<std::size_t>(i) >= users_.size()) return std::nullopt;
    return users_[static_cast<std::size_t>(i)].nick;
}

bool UserList::find(std::string_view prefix) {
    for (std::size_t i = 0; i < users_.size(); ++i)
        if (iequal_prefix(users_[i].nick, prefix)) { sel_ = i; has_sel_ = true; return true; }
    return false;
}

std::vector<Rich> UserList::rows(int w, int h, bool focused) const {
    std::vector<Rich> out;
    if (h <= 0 || w <= 0) return out;

    // " 19 users · 3 ops": the count stands out, the rest quiet
    Rich head{" "};
    head.append(std::to_string(users_.size()), role::text().with_bold());
    head.append(users_.size() == 1 ? " user" : " users", role::muted());
    if (ops_) {
        head.append(" · ", role::muted());
        head.append(std::to_string(ops_), role::text().with_bold());
        head.append(ops_ == 1 ? " op" : " ops", role::muted());
    }
    out.push_back(head.fit(w));

    const int body = h - 1;
    if (has_sel_) {
        const int s = static_cast<int>(sel_);
        if (s < top_) top_ = s;
        if (s >= top_ + body) top_ = s - body + 1;
    }
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(users_.size()) - body));

    for (int r = 0; r < body; ++r) {
        const std::size_t i = static_cast<std::size_t>(top_ + r);
        if (i >= users_.size()) { out.push_back(Rich{}.pad_right(w)); continue; }
        const UserEntry& u = users_[i];
        Rich row{" "};
        row.append(u.prefix ? std::string(1, u.prefix) : std::string(" "), role::rank(u.prefix));
        Style ns = u.away ? role::away() : Style{};
        if (u.me) ns = ns.merge(Style{}.with_bold());
        row.append(u.nick, ns);
        row = row.fit(w);
        if (has_sel_ && i == sel_) {
            if (focused) row.replace_colors(role::selected());
            else row.overlay(role::current());
        }
        out.push_back(std::move(row));
    }
    add_scrollbar(out, w, 1, body, static_cast<int>(users_.size()), top_, focused);
    return out;
}

maya::Element UserList::element(int width, bool focused) const {
    return maya::dsl::fill([this, focused](int w, int h) {
        return stack_rows(rows(w, h, focused));
    }, width).width(width).grow(0).build();
}

}  // namespace nx::tui
