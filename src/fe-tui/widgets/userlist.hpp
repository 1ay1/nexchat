#pragma once
// nx::tui::UserList — a channel's nicks, down the right side.
//
//   12 users, 2 ops        ← header (fe_userlist_numbers)
//   @ChanServ
//   @alice
//   +bob
//    carol                 ← away: the away colour
//
// Ordering is the core's (nick_cmp_az_ops: by rank, then by name) — the
// glue hands the users over already sorted, as userlist_double_list does.
// The prefix glyph is the user's highest mode prefix (struct User.prefix).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <maya/element/element.hpp>

#include "rich.hpp"

namespace nx::tui {

struct UserEntry {
    std::string nick;
    char        prefix = 0;      // '@', '%', '+', '~', '&' or 0
    bool        away = false;
    bool        me = false;
};

class UserList {
public:
    void set(std::vector<UserEntry> users, int ops = -1);
    [[nodiscard]] const std::vector<UserEntry>& users() const noexcept { return users_; }

    void move(int d);
    void select_nick(std::string_view nick);
    [[nodiscard]] const UserEntry* selected() const;
    [[nodiscard]] std::optional<std::string> nick_at_row(int y) const;
    /// The nick under the pointer: underlined. True when it changed.
    bool set_hover(std::optional<std::string> nick) {
        if (nick == hover_) return false;
        hover_ = std::move(nick);
        return true;
    }

    /// Type-to-find: select the first nick starting with `prefix`.
    bool find(std::string_view prefix);

    [[nodiscard]] std::vector<Rich> rows(int w, int h, bool focused = false) const;
    [[nodiscard]] maya::Element element(int width, bool focused = false) const;

private:
    std::vector<UserEntry> users_;
    int                    ops_ = 0;
    std::size_t            sel_ = 0;
    bool                   has_sel_ = false;
    mutable int            top_ = 0;
    std::optional<std::string> hover_;
};

}  // namespace nx::tui
