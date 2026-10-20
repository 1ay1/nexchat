// nexchat TUI — Ignore List and Friends List (see windows.hpp): each
// entry on the left, its settings on the right, Add / Remove below.

#include <algorithm>
#include <ctime>

#include "bridge.h"
#include "windows.hpp"

namespace nx::tui {

namespace {

struct IgType { int flag; const char* label; };
const IgType kTypes[] = {
    {NX_IG_PRIV, "Private messages"}, {NX_IG_NOTI, "Notices"}, {NX_IG_CHAN, "Channel messages"},
    {NX_IG_CTCP, "CTCPs"}, {NX_IG_INVI, "Invites"}, {NX_IG_DCC, "DCC (file offers, chats)"},
    {NX_IG_UNIG, "Unignore (an exception to a wider mask)"},
};

std::string ago(long t) {
    if (!t) return "never";
    const long d = static_cast<long>(std::time(nullptr)) - t;
    if (d < 120) return "just now";
    if (d < 7200) return std::to_string(d / 60) + " minutes ago";
    if (d < 172800) return std::to_string(d / 3600) + " hours ago";
    return std::to_string(d / 86400) + " days ago";
}

}  // namespace

void PeopleList::open(Kind k) {
    kind_ = k;
    panel_.list_width = 30;
    panel_.set_buttons({{"Add", B_ADD}, {"Remove", B_REMOVE}, {"Close", B_CLOSE}});
    reload();
    panel_.open(k == Kind::Ignore ? "Ignore List" : "Friends List");
    panel_.set_part(Panel::Part::List);
}

void PeopleList::reload() {
    if (editing_) return;   // the core telling us about our own edit
    reload_rows();
    load(names_.empty() ? -1 : panel_.list_sel());
}

// The left list from the core, keeping the selection on the same entry.
void PeopleList::reload_rows() {
    const std::string keep = cur_ >= 0 && cur_ < static_cast<int>(names_.size()) ? names_[static_cast<std::size_t>(cur_)] : "";
    names_.clear();
    std::vector<std::string> rows;
    if (kind_ == Kind::Ignore) {
        const int n = nx_core_ignore_count();
        for (int i = 0; i < n; ++i) {
            int f;
            names_.push_back(nx_core_ignore_get(i, &f));
            rows.push_back(names_.back());
        }
        panel_.status = n ? std::to_string(n) + " ignored · Tab: what to ignore · Add / Remove"
                          : "Nobody is ignored. Add a mask like nick!*@* or *!*@host.";
    } else {
        const int n = nx_core_notify_count();
        for (int i = 0; i < n; ++i) {
            const char* nets; int on; long seen;
            names_.push_back(nx_core_notify_get(i, &nets, &on, &seen));
            rows.push_back(std::string(on ? "● " : "  ") + names_.back());
        }
        panel_.status = n ? "● online · Add a friend to be told when they come and go"
                          : "No friends yet. Add one to be told when they come and go.";
    }
    int sel = std::clamp(panel_.list_sel(), 0, std::max(0, static_cast<int>(names_.size()) - 1));
    for (std::size_t i = 0; i < names_.size(); ++i) if (!keep.empty() && names_[i] == keep) sel = static_cast<int>(i);
    panel_.set_list(std::move(rows), sel, kind_ == Kind::Ignore ? "Masks" : "Friends");
    if (cur_ >= 0) cur_ = names_.empty() ? -1 : sel;
}

void PeopleList::load(int i) {
    cur_ = i;
    using K = Field::Kind;
    std::vector<Field> f;
    if (i < 0) {
        f.push_back({K::Heading, kind_ == Kind::Ignore ? "Press Add to ignore someone" : "Press Add to add a friend"});
        panel_.form.set(std::move(f));
        return;
    }
    if (kind_ == Kind::Ignore) {
        int flags = 0;
        nx_core_ignore_get(i, &flags);
        f.push_back({K::Text, "Mask", names_[static_cast<std::size_t>(i)], false, {}, 0, {}, 0, F_MASK});
        f.push_back({K::Heading, "Ignore"});
        for (std::size_t t = 0; t < std::size(kTypes); ++t)
            f.push_back({K::Toggle, kTypes[t].label, "", (flags & kTypes[t].flag) != 0, {}, 0, {}, 0,
                         F_TYPE0 + static_cast<int>(t)});
    } else {
        const char* nets; int on; long seen;
        nx_core_notify_get(i, &nets, &on, &seen);
        f.push_back({K::Text, "Nick", names_[static_cast<std::size_t>(i)], false, {}, 0, {}, 0, F_MASK});
        f.push_back({K::Text, "On networks", nets, false, {}, 0, {}, 0, F_NETS});
        f.push_back({K::Heading, std::string("Status: ") + (on ? "online" : "offline") + " · last seen " + ago(seen)});
        f.push_back({K::Heading, "On networks: comma-separated, empty for every network"});
    }
    panel_.form.set(std::move(f));
    panel_.form.label_width = 20;
}

// The entry in the form back to the core (a renamed mask replaces the old).
void PeopleList::save() {
    if (cur_ < 0 || cur_ >= static_cast<int>(names_.size())) return;
    Form& f = panel_.form;
    Field* m = f.by_id(F_MASK);
    if (!m) return;
    const std::string old = names_[static_cast<std::size_t>(cur_)];
    editing_ = true;
    if (kind_ == Kind::Ignore) {
        int flags = 0;
        for (std::size_t t = 0; t < std::size(kTypes); ++t)
            if (Field* x = f.by_id(F_TYPE0 + static_cast<int>(t)); x && x->on) flags |= kTypes[t].flag;
        if (m->text != old) nx_core_ignore_remove(old.c_str());
        nx_core_ignore_set(m->text.c_str(), flags ? flags : NX_IG_PRIV);   // an ignore of nothing would vanish
    } else {
        Field* n = f.by_id(F_NETS);
        nx_core_notify_remove(old.c_str());
        nx_core_notify_add(m->text.c_str(), n ? n->text.c_str() : "");
    }
    names_[static_cast<std::size_t>(cur_)] = m->text;
    editing_ = false;
}

bool PeopleList::handle(Panel::Outcome o) {
    switch (o) {
    case Panel::Outcome::Closed: save(); return true;
    case Panel::Outcome::Picked: save(); reload(); return false;
    case Panel::Outcome::Activated: panel_.set_part(Panel::Part::Body); return false;
    case Panel::Outcome::Changed: {
        // a toggle as it flips, text as it's committed (Enter / Tab / ↑↓)
        save();
        const int f = panel_.form.focused();
        reload_rows();          // a renamed entry shows on the left
        panel_.form.focus(f);
        return false;
    }
    case Panel::Outcome::Button:
        switch (panel_.button()) {
        case B_ADD:
            save();
            editing_ = true;
            if (kind_ == Kind::Ignore) nx_core_ignore_set("new!*@*", NX_IG_PRIV | NX_IG_NOTI | NX_IG_CHAN | NX_IG_CTCP | NX_IG_INVI | NX_IG_DCC);
            else nx_core_notify_add("newfriend", "");
            editing_ = false;
            reload();
            // the new entry, selected, its name ready to type over
            for (std::size_t i = 0; i < names_.size(); ++i)
                if (names_[i] == (kind_ == Kind::Ignore ? "new!*@*" : "newfriend")) { panel_.set_list_sel(static_cast<int>(i)); load(static_cast<int>(i)); }
            panel_.set_part(Panel::Part::Body);
            panel_.form.focus(0);
            return false;
        case B_REMOVE:
            if (cur_ >= 0) {
                const std::string n = names_[static_cast<std::size_t>(cur_)];
                editing_ = true;
                if (kind_ == Kind::Ignore) nx_core_ignore_remove(n.c_str());
                else nx_core_notify_remove(n.c_str());
                editing_ = false;
                cur_ = -1;
                reload();
            }
            return false;
        case B_CLOSE: save(); panel_.close(); return true;
        default: return false;
        }
    default: return false;
    }
}

bool PeopleList::key(const maya::KeyEvent& k) { return handle(panel_.key(k)); }
bool PeopleList::mouse(const maya::MouseEvent& m) { return handle(panel_.mouse(m)); }

}  // namespace nx::tui
