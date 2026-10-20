// nexchat TUI — the Network List window (see windows.hpp).

#include <cstring>
#include <sstream>

#include <glib.h>

#include "bridge.h"
#include "windows.hpp"

namespace nx::tui {

namespace {

std::vector<std::string> split_lines(const char* s) {
    std::vector<std::string> out;
    std::istringstream in(s ? s : "");
    for (std::string l; std::getline(in, l);) if (!l.empty()) out.push_back(l);
    return out;
}

std::string join_lines(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& l : v) { if (!s.empty()) s += '\n'; s += l; }
    return s;
}

}  // namespace

void NetworkList::open() {
    panel_.list_width = 26;
    panel_.set_buttons({{"Connect", B_CONNECT}, {"Add", B_ADD}, {"Remove", B_REMOVE}, {"Bouncer…", B_BOUNCER}, {"Close", B_CLOSE}});
    reload_list(0);
    panel_.open("Network List");
    panel_.set_part(Panel::Part::List);
    panel_.status = "Enter connects · Tab edits";
}

void NetworkList::close() {
    save();
    panel_.close();
}

// The list: "User info" first (the global nicks), then every network.
void NetworkList::reload_list(int sel) {
    std::vector<std::string> items{"⚙ User info"};
    const int n = nx_core_net_count();
    for (int i = 0; i < n; ++i) {
        nx_net net;
        nx_core_net_get(i, &net);
        items.push_back(std::string(net.favorite ? "★ " : "  ") + net.name);
        nx_core_net_free(&net);
    }
    panel_.set_list(std::move(items), sel, "Networks");
    load(sel - 1);
}

void NetworkList::load(int i) {
    cur_ = i;
    using K = Field::Kind;
    if (i < 0) {
        char *n1, *n2, *n3, *u;
        nx_core_user_get(&n1, &n2, &n3, &u);
        panel_.form.set({
            {K::Heading, "User information (every network that uses the global)"},
            {K::Text, "Nick name", n1, false, {}, 0, {}, 0, F_NICK1},
            {K::Text, "Second choice", n2, false, {}, 0, {}, 0, F_NICK2},
            {K::Text, "Third choice", n3, false, {}, 0, {}, 0, F_NICK3},
            {K::Text, "User name", u, false, {}, 0, {}, 0, F_USER},
            {K::Heading, "Startup"},
            {K::Toggle, "Skip this list", "", nx_core_slist_skip() != 0, {}, 0, {}, 0, F_SKIP},
        });
        g_free(n1); g_free(n2); g_free(n3); g_free(u);
        return;
    }
    nx_net net;
    nx_core_net_get(i, &net);
    std::vector<std::string> logins;
    for (const char* const* p = nx_core_login_names(); *p; ++p) logins.push_back(*p);
    const bool global = net.use_global != 0;
    panel_.form.set({
        {K::Text, "Network", net.name, false, {}, 0, {}, 0, F_NAME},
        {K::List, "Servers", "", false, {}, 0, split_lines(net.servers), 0, F_SERVERS},
        {K::List, "Autojoin", "", false, {}, 0, split_lines(net.autojoin), 0, F_AUTOJOIN},
        {K::List, "Connect commands", "", false, {}, 0, split_lines(net.commands), 0, F_COMMANDS},
        {K::Heading, "Connecting"},
        {K::Toggle, "Use SSL for all", "", net.ssl != 0, {}, 0, {}, 0, F_SSL},
        {K::Toggle, "Accept invalid certs", "", net.allow_invalid != 0, {}, 0, {}, 0, F_ALLOW_INVALID},
        {K::Toggle, "Cycle until connected", "", net.cycle != 0, {}, 0, {}, 0, F_CYCLE},
        {K::Toggle, "Connect at startup", "", net.auto_connect != 0, {}, 0, {}, 0, F_AUTO},
        {K::Toggle, "Favorite", "", net.favorite != 0, {}, 0, {}, 0, F_FAVORITE},
        {K::Toggle, "Use a proxy", "", net.proxy != 0, {}, 0, {}, 0, F_PROXY},
        {K::Heading, "Logging in"},
        {K::Choice, "Login method", "", false, logins, net.login, {}, 0, F_LOGIN},
        {K::Password, "Password", net.pass, false, {}, 0, {}, 0, F_PASS},
        {K::Text, "Character set", net.encoding, false, {}, 0, {}, 0, F_ENCODING},
        {K::Heading, "Who you are here"},
        {K::Toggle, "Use the global info", "", global, {}, 0, {}, 0, F_USE_GLOBAL},
        {K::Text, "Nick name", net.nick, false, {}, 0, {}, 0, F_NETNICK, !global},
        {K::Text, "Second choice", net.nick2, false, {}, 0, {}, 0, F_NETNICK2, !global},
        {K::Text, "Real name", net.real, false, {}, 0, {}, 0, F_NETREAL, !global},
        {K::Text, "User name", net.user, false, {}, 0, {}, 0, F_NETUSER, !global},
    });
    panel_.form.label_width = 22;
    nx_core_net_free(&net);
}

void NetworkList::save() {
    Form& f = panel_.form;
    auto text = [&](int id) { Field* x = f.by_id(id); return x ? x->text : std::string(); };
    auto on = [&](int id) { Field* x = f.by_id(id); return x && x->on; };
    if (cur_ < 0) {
        if (!f.by_id(F_NICK1)) return;
        nx_core_user_set(text(F_NICK1).c_str(), text(F_NICK2).c_str(), text(F_NICK3).c_str(), text(F_USER).c_str());
        nx_core_slist_skip_set(on(F_SKIP));
        return;
    }
    if (!f.by_id(F_NAME)) return;
    std::string name = text(F_NAME), nick = text(F_NETNICK), nick2 = text(F_NETNICK2), user = text(F_NETUSER),
                real = text(F_NETREAL), pass = text(F_PASS), enc = text(F_ENCODING);
    std::string servers = join_lines(f.by_id(F_SERVERS)->items), autojoin = join_lines(f.by_id(F_AUTOJOIN)->items),
                commands = join_lines(f.by_id(F_COMMANDS)->items);
    nx_net n{};
    n.name = name.data(); n.nick = nick.data(); n.nick2 = nick2.data(); n.user = user.data();
    n.real = real.data(); n.pass = pass.data(); n.encoding = enc.data();
    n.servers = servers.data(); n.autojoin = autojoin.data(); n.commands = commands.data();
    n.login = f.by_id(F_LOGIN)->choice;
    n.use_global = on(F_USE_GLOBAL);
    n.ssl = on(F_SSL); n.allow_invalid = on(F_ALLOW_INVALID); n.cycle = on(F_CYCLE);
    n.auto_connect = on(F_AUTO); n.favorite = on(F_FAVORITE); n.proxy = on(F_PROXY);
    nx_core_net_set(cur_, &n);
}

bool NetworkList::handle(Panel::Outcome o) {
    switch (o) {
    case Panel::Outcome::Closed:
        save();
        return true;
    case Panel::Outcome::Picked:
        save();
        // the name may have changed: refresh the list, keep the selection
        reload_list(panel_.list_sel());
        return false;
    case Panel::Outcome::Changed:
        // "Use the global info" greys the per-network nick fields
        if (cur_ >= 0) {
            Form& f = panel_.form;
            const bool global = f.by_id(F_USE_GLOBAL)->on;
            for (int id : {F_NETNICK, F_NETNICK2, F_NETREAL, F_NETUSER}) f.by_id(id)->enabled = !global;
        }
        return false;
    case Panel::Outcome::Activated:
        if (cur_ < 0) return false;
        [[fallthrough]];
    case Panel::Outcome::Button:
        switch (o == Panel::Outcome::Activated ? B_CONNECT : panel_.button()) {
        case B_CONNECT:
            if (cur_ < 0) { flash = "pick a network to connect to"; return false; }
            save();
            nx_core_net_connect(cur_);
            panel_.close();
            return true;
        case B_ADD:
            save();
            nx_core_net_add("New Network");
            reload_list(1);
            panel_.set_part(Panel::Part::Body);
            panel_.form.focus(0);
            return false;
        case B_REMOVE:
            if (cur_ < 0) return false;
            nx_core_net_remove(cur_);
            cur_ = -1;
            reload_list(std::max(0, panel_.list_sel() - 1));
            return false;
        case B_BOUNCER:
            // servlistgui.c servlist_stayonline_cb: the selected network's
            // address and login are the starting point
            save();
            bouncer_from = cur_;
            panel_.close();
            return true;
        case B_CLOSE:
            close();
            return true;
        default: return false;
        }
    default:
        return false;
    }
}

bool NetworkList::key(const maya::KeyEvent& k) { return handle(panel_.key(k)); }
bool NetworkList::mouse(const maya::MouseEvent& m) { return handle(panel_.mouse(m)); }

}  // namespace nx::tui
