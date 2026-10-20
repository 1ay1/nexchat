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
    if (y < 1 || static_cast<std::size_t>(y - 1) >= drawn_.size()) return std::nullopt;
    const int i = drawn_[static_cast<std::size_t>(y - 1)];
    if (i < 0 || static_cast<std::size_t>(i) >= users_.size()) return std::nullopt;
    return users_[static_cast<std::size_t>(i)].nick;
}

namespace {
// a rank's group: the members sort by rank already (the core's order)
int group_of(char prefix) {
    switch (prefix) {
    case '~': case '&': return 0;
    case '@': return 1;
    case '%': return 2;
    case '+': return 3;
    default: return 4;
    }
}
const char* group_name(int g) {
    static const char* const n[] = {"Owners", "Operators", "Half-ops", "Voiced", "Members"};
    return n[g];
}
}  // namespace

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
    // the lines: a heading where the rank changes (only when there's more
    // than one group: a list of plain members needs none), then the nicks.
    // -1 - g marks group g's heading.
    std::vector<int> lines;
    std::vector<int> group_count(5, 0);
    for (const auto& u : users_) ++group_count[static_cast<std::size_t>(group_of(u.prefix))];
    // headings only when the list is long enough to want them (a small
    // channel reads fine flat) and there's more than one rank
    int groups = 0;
    for (int c : group_count) groups += c > 0;
    const bool grouped = groups > 1 && users_.size() > 12;
    int last = -1, sel_line = -1;
    for (std::size_t i = 0; i < users_.size(); ++i) {
        const int g = group_of(users_[i].prefix);
        if (grouped && g != last) lines.push_back(-1 - g);
        last = g;
        if (has_sel_ && i == sel_) sel_line = static_cast<int>(lines.size());
        lines.push_back(static_cast<int>(i));
    }
    if (sel_line >= 0) {
        if (sel_line < top_) top_ = std::max(0, sel_line - 1);     // its heading too, when it's just above
        if (sel_line >= top_ + body) top_ = sel_line - body + 1;
    }
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(lines.size()) - body));

    drawn_.clear();
    for (int r = 0; r < body; ++r) {
        const std::size_t li = static_cast<std::size_t>(top_ + r);
        if (li >= lines.size()) { out.push_back(Rich{}.pad_right(w)); drawn_.push_back(-1); continue; }
        if (lines[li] < 0) {
            // "OPERATORS — 3": the group, small and quiet
            const int g = -1 - lines[li];
            Rich hrow{" "};
            std::string name = group_name(g);
            for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            hrow.append(name, role::muted().with_bold());
            hrow.append(" " + std::to_string(group_count[static_cast<std::size_t>(g)]), role::muted());
            out.push_back(hrow.fit(w));
            drawn_.push_back(-1);
            continue;
        }
        const std::size_t i = static_cast<std::size_t>(lines[li]);
        drawn_.push_back(static_cast<int>(i));
        const UserEntry& u = users_[i];
        Rich row{" "};
        row.append(u.prefix ? std::string(1, u.prefix) : std::string(" "), role::rank(u.prefix));
        Style ns = u.away ? role::away() : Style{};
        if (u.me) ns = ns.merge(Style{}.with_bold());
        if (hover_ && *hover_ == u.nick) ns = ns.merge(Style{}.with_underline());
        row.append(u.nick, ns);
        row = row.fit(w);
        if (has_sel_ && i == sel_) {
            if (focused) row.replace_colors(role::selected());
            else row.overlay(role::current());
        }
        out.push_back(std::move(row));
    }
    add_scrollbar(out, w, 1, body, static_cast<int>(lines.size()), top_, focused);
    return out;
}

maya::Element UserList::element(int width, bool focused) const {
    return maya::dsl::fill([this, focused](int w, int h) {
        return stack_rows(rows(w, h, focused));
    }, width).width(width).grow(0).build();
}

}  // namespace nx::tui
