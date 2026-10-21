// nexchat TUI — Server > Bouncer... : the GUI's bouncer window
// (fe-gtk/bncwin.c) and its Stay online setup (fe-gtk/bncsetup.c), the same
// texts, buttons, rules and core calls, in a terminal.
//
//   ╭─ Bouncer ───────────────────────────────────────────────── ✕ ─╮
//   │ [Networks] Channels  Access  Storage                          │
//   │ Your bouncer is running, kept running by systemd              │
//   │ The networks switched on stay connected when nexchat is ...   │
//   │                                                [ Stop ] [ … ] │
//   │ Libera   in #nexchat                [ Nick and login... ] [on]│
//   │   as alice (its own nick), SASL as alice                      │
//   │ ...                                                           │
//   │ [ Use another bouncer... ]                          [ Close ] │
//   ╰───────────────────────────────────────────────────────────────╯
//
// Each sub-dialog of the GUI (a network's nick and login, a remote
// network's login, Add a network) is a page of the same window here, with
// Save / Cancel, as one modal stack.

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <glib.h>

#include <maya/dsl.hpp>

#include "bridge-bnc.h"
#include "chat.hpp"
#include "sheet.hpp"
#include "theme.hpp"
#include "windows.hpp"

namespace nx::tui {

namespace {

std::string take(char* s) {
    std::string r = s ? s : "";
    g_free(s);
    return r;
}

// "ok" / "no" / "" notes, the GUI's say()
struct Note { std::string kind, text; };

}  // namespace

// ── the window ──────────────────────────────────────────────────────────

class BouncerWin {
public:
    enum Tab { T_NETWORKS, T_CHANNELS, T_ACCESS, T_STORAGE };
    enum Page { P_MAIN, P_EDIT, P_REDIT, P_ADD, P_SETUP };

    bool open_ = false;
    int from_ = -1;
    Tab tab_ = T_NETWORKS;
    Page page_ = P_MAIN;
    Sheet sheet_;
    Note note_;
    std::string key_;              // nx_rbnc_key at the last fill
    bool busy_ = false;            // adopting: switches wait
    std::vector<int> sure_;        // ids pressed once (Remove..., Forget...)
    // what the controls mean (rebuilt with the page)
    struct Act { int kind; int net; std::string id, name; };
    std::vector<Act> acts_;
    // the Channels tab's network
    int ch_net_ = 0;
    // editors
    int edit_net_ = -1;
    nx_bnc_ident ident_{};
    bool ed_same_ = true, ed_sasl_ = false, ed_away_ = true, ed_enabled_ = true;
    std::string ed_cert_;
    int r_net_ = -1;
    std::string r_id_, r_name_;
    bool r_sasl_ = false;
    int add_net_ = -1;
    // setup wizard
    enum Step { S_CHOOSE, S_BOUNCER, S_NETWORKS, S_PERSIST, S_EXPLAIN } step_ = S_CHOOSE;
    struct Found { bool known = false, ours = false, soju = false, znc = false; std::string soju_version, soju_failed;
                   std::vector<std::string> listen, listen_addr; } found_;
    std::string probe_kind_, probe_text_, probe_kind_ok_;
    bool probe_checking_ = false, cert_bad_ = false, trust_ = false;
    int probe_gen_ = 0;
    std::string probed_for_;
    std::vector<int> nets_ticked_;            // networks page: popular indices ticked
    std::vector<std::pair<int, bool>> mine_ticked_;   // our soju: network index, ticked
    bool setup_mine_ = false;                 // networks page is for our own soju
    std::string mine_status_;
    int persist_net_ = 0;
    std::string persist_kind_, persist_text_;
    int persist_gen_ = 0;
    std::chrono::steady_clock::time_point last_tick_{};

    // geometry for the mouse
    mutable int x_ = 0, y_ = 0, w_ = 0, h_ = 0, body_y_ = 0, body_x_ = 0;
    mutable std::vector<std::pair<int, int>> tab_x_;

    // ── ids ────────────────────────────────────────────────────────────
    enum : int {
        I_TAB0 = 10, I_SETUP = 20, I_START, I_STOP, I_REMOVE, I_OTHER, I_CLOSE,
        I_ACT0 = 1000,                // + index into acts_
        I_F0 = 5000,                  // fields
    };
    enum Kind { K_SWITCH, K_EDIT, K_RCONNECT, K_REDITBNC, K_RFORGET, K_ROPEN, K_RLOGIN, K_RREMOVE, K_RADD,
                K_CHNET, K_CHDETACH, K_CHREMOVE, K_CHADD,
                K_AC_PASSWORD, K_AC_REMOTE, K_AC_SYSTEMD, K_AC_SHOWPASS,
                K_ST_DB, K_ST_FILES, K_ST_TITLE, K_ST_RELOAD,
                K_ED_SAME, K_ED_NONE, K_ED_SASL, K_ED_AWAY, K_ED_ENABLED, K_ED_CERT, K_SAVE, K_CANCEL,
                K_RED_SASL,
                K_ADD_POPULAR, K_ADD_ADD,
                K_S_CHOICE, K_S_LISTEN, K_S_USEMINE, K_S_TRUST, K_S_NEXT, K_S_BACK, K_S_POP, K_S_MINE, K_S_PNET };
    enum Field { F_ED_NICK = I_F0, F_ED_ACCOUNT, F_ED_PASS, F_ED_REAL, F_ED_USER, F_ED_SRVPASS, F_ED_COMMANDS,
                 F_R_NICK, F_R_USER, F_R_REAL, F_R_PASS, F_R_ACCOUNT, F_R_APASS,
                 F_ADD_NAME, F_ADD_ADDR, F_CH_NEW, F_AC_PASS, F_ST_TITLE,
                 F_S_ADDR, F_S_USER, F_S_PASS, F_S_OTHER_HOST, F_S_OTHER_NAME, F_S_PACCOUNT, F_S_PPASS };

    int act(int kind, int net = -1, std::string id = {}, std::string name = {}) {
        acts_.push_back({kind, net, std::move(id), std::move(name)});
        return I_ACT0 + static_cast<int>(acts_.size()) - 1;
    }
    bool is_sure(int id) const { return std::find(sure_.begin(), sure_.end(), id) != sure_.end(); }

    void say(std::string kind, std::string text) { note_ = {std::move(kind), std::move(text)}; }

    // ── opening ────────────────────────────────────────────────────────
    void open(int from) {
        from_ = from;
        open_ = true;
        page_ = P_MAIN;
        tab_ = T_NETWORKS;
        note_ = {};
        sure_.clear();
        // bncwin_open: opened from a network that isn't on it: it goes on now
        if (from_ >= 0 && nx_bnc_state() == NX_MYBNC_SET_UP) {
            nx_bnc_row* rows; const int n = nx_bnc_rows(from_, &rows);
            for (int i = 0; i < n; ++i) if (rows[i].net == from_ && !rows[i].mine) switch_on(from_, rows[i].name);
            nx_bnc_rows_free(rows, n);
        }
    }

    // ── the main page ──────────────────────────────────────────────────
    int remote_count() {
        nx_rbnc* l; const int n = nx_rbnc_list(&l);
        nx_rbnc_list_free(l, n);
        return n;
    }

    void head() {
        const int st = nx_bnc_state();
        const bool running = st == NX_MYBNC_SET_UP && nx_bnc_running();
        const int remote = remote_count();
        std::string h, sub;
        if (st != NX_MYBNC_SET_UP && remote) {
            h = remote > 1 ? "Your bouncers" : "Your bouncer";
            sub = "It stays on your networks while nexchat is closed, and you get what you missed when you "
                  "come back. Connect to it here, and add or remove the networks it keeps you on.";
        } else if (st == NX_MYBNC_NONE) {
            h = "No bouncer yet";
            sub = "Install soju from your system's packages (the package is called soju), then open this "
                  "again: nexchat sets it up and keeps it running. Using one on another computer: "
                  "\"Use another bouncer...\" below.";
        } else if (st == NX_MYBNC_AVAILABLE) {
            h = "soju is installed, not set up yet";
            sub = "Set it up and nexchat runs it for you, in its own folder, only reachable from this "
                  "computer. Then switch on the networks it should stay on.";
        } else {
            h = std::string("Your bouncer is ") + (running ? "running" : "stopped") + ", "
                + (std::strcmp(nx_bnc_kept_by(), "systemd") ? "started by nexchat" : "kept running by systemd");
            sub = running ? "The networks switched on stay connected when nexchat is closed, and you get what you "
                            "missed when you come back. Switching one off puts it back to connecting directly."
                          : "Start it and the networks switched on are online again.";
        }
        Sheet::Row& r = sheet_.row(Rich{h, role::title()});
        if (st == NX_MYBNC_AVAILABLE && !remote) r.controls.push_back({Sheet::Kind::Button, "Set it up", I_SETUP});
        if (st == NX_MYBNC_SET_UP && !running) r.controls.push_back({Sheet::Kind::Button, "Start", I_START});
        if (running) r.controls.push_back({Sheet::Kind::Button, "Stop", I_STOP});
        if (st == NX_MYBNC_SET_UP)
            r.controls.push_back({Sheet::Kind::Button, is_sure(I_REMOVE) ? "Remove, sure?" : "Remove...", I_REMOVE,
                                  false, true, is_sure(I_REMOVE)});
        sheet_.para(sub, role::muted());
        sheet_.blank();
    }

    void remote_cards() {
        nx_rbnc* l; const int n = nx_rbnc_list(&l);
        for (int i = 0; i < n; ++i) {
            const nx_rbnc& b = l[i];
            Rich title{b.name, role::title()};
            title.append("  " + std::string(b.where) + ", as " + b.user, role::muted());
            Sheet::Row& top = sheet_.row(title);
            top.controls.push_back({Sheet::Kind::Button, b.connecting || b.live ? "Disconnect" : "Connect", act(K_RCONNECT, b.net)});
            top.controls.push_back({Sheet::Kind::Button, "Edit...", act(K_REDITBNC, b.net)});
            const int fid = act(K_RFORGET, b.net);
            top.controls.push_back({Sheet::Kind::Button, is_sure(fid) ? "Forget it, sure?" : "Forget", fid, false, true, is_sure(fid)});
            Rich st;
            if (b.live) { st.append("● ", role::good()); st.append(b.soju ? "Connected (soju)" : "Connected (ZNC)", role::text()); }
            else if (b.connecting) { st.append("● ", role::warn()); st.append("Connecting...", role::text()); }
            else if (b.error) { st.append("● ", role::bad()); st.append(std::string("Couldn't log in: ") + b.error, role::text()); }
            else { st.append("● ", role::muted()); st.append("Not connected", role::text()); }
            sheet_.row(st, 2);
            if (!b.live) {
                const char* hint = b.connecting ? "Logging in. Its networks show here once it's in."
                    : b.error && std::strstr(b.error, "password") ? "Check the user name and password with Edit, then Connect again."
                    : b.error ? "Fix it with Edit, then Connect again."
                    : "Connect to see the networks it keeps you on, and to add more.";
                sheet_.para(hint, role::muted(), 2);
                sheet_.blank();
                continue;
            }
            if (!b.n) sheet_.para("It isn't on any network yet. Add one and it stays there for you, even while nexchat is closed.", role::muted(), 2);
            for (int j = 0; j < b.n; ++j) {
                const nx_rnet& nn = b.nets[j];
                const bool up = !std::strcmp(nn.state, "connected"), going = !std::strcmp(nn.state, "connecting");
                Rich t{nn.name, role::text().with_bold()};
                t.append("  " + std::string(nn.host), role::muted());
                Sheet::Row& r = sheet_.row(t, 2);
                r.controls.push_back({Sheet::Kind::Button, "Open", act(K_ROPEN, b.net, nn.id, nn.name)});
                r.controls.push_back({Sheet::Kind::Button, "Nick and login...", act(K_RLOGIN, b.net, nn.id, nn.name)});
                const int rid = act(K_RREMOVE, b.net, nn.id, nn.name);
                r.controls.push_back({Sheet::Kind::Button, is_sure(rid) ? "Remove, sure?" : "Remove", rid, false, true, is_sure(rid)});
                Rich s;
                s.append("● ", up ? role::good() : going ? role::warn() : role::bad());
                s.append(std::string(up ? "online" : going ? "connecting" : "offline") + " as ", role::muted());
                s.append(nn.nick, role::text().with_bold());
                if (nn.error) s.append(std::string(": ") + nn.error, role::muted());
                sheet_.row(s, 4);
            }
            Sheet::Row& add = sheet_.row({}, 2);
            add.controls_left = true;
            add.controls.push_back({Sheet::Kind::Button, "Add a network...", act(K_RADD, b.net)});
            sheet_.blank();
        }
        nx_rbnc_list_free(l, n);
    }

    void networks_tab() {
        remote_cards();
        if (nx_bnc_state() != NX_MYBNC_SET_UP) return;
        nx_bnc_row* rows; const int n = nx_bnc_rows(from_, &rows);
        for (int i = 0; i < n; ++i) {
            const nx_bnc_row& r = rows[i];
            Sheet::Row& row = sheet_.row(Rich{r.name, role::text().with_bold()});
            if (r.mine) row.controls.push_back({Sheet::Kind::Button, "Nick and login...", act(K_EDIT, r.net)});
            row.controls.push_back({Sheet::Kind::Switch, "", act(K_SWITCH, r.net, {}, r.name), r.mine != 0, !busy_});
            Rich s;
            if (r.mine) s.append("● ", r.up ? role::good() : role::bad());
            s.append(r.state, role::muted());
            sheet_.row(s, 2);
            if (r.mine && *r.who) sheet_.row(Rich{r.who, role::muted()}, 2);
        }
        if (!n && !remote_count())
            sheet_.para("No networks to put on it yet: mark the ones you use as a favorite in the Network List, "
                        "or open this from a network's tab.", role::muted());
        nx_bnc_rows_free(rows, n);
    }

    void channels_tab() {
        const int count = nx_bnc_owned_count();
        if (!count) { sheet_.para("No network is on the bouncer yet.", role::muted()); return; }
        ch_net_ = std::clamp(ch_net_, 0, count - 1);
        const int net = nx_bnc_owned(ch_net_);
        // the network picker, a row of choices (the GUI's drop-down)
        Sheet::Row& pick = sheet_.row(Rich{"Network ", role::muted()});
        pick.controls_left = true;
        for (int i = 0; i < count; ++i) {
            nx_net nn; nx_core_net_get(nx_bnc_owned(i), &nn);
            pick.controls.push_back({Sheet::Kind::Radio, nn.name, act(K_CHNET, i), i == ch_net_});
            nx_core_net_free(&nn);
        }
        sheet_.blank();
        const std::string all = take(nx_bnc_channels(net));
        int shown = 0;
        for (const auto& line : split(all, '\n')) {
            if (line.empty()) continue;
            const auto tab = line.find('\t');
            const std::string chan = line.substr(0, tab), st = tab == std::string::npos ? "" : line.substr(tab + 1);
            const bool detached = st.find("detached") != std::string::npos;
            Sheet::Row& r = sheet_.row(Rich{chan, role::title()});
            r.text.append("  " + st, role::muted());
            r.controls.push_back({Sheet::Kind::Check, "Detached", act(K_CHDETACH, net, chan), detached});
            r.controls.push_back({Sheet::Kind::Button, "Leave", act(K_CHREMOVE, net, chan)});
            ++shown;
        }
        if (!shown) sheet_.para("It isn't in any channel on this network.", role::muted());
        sheet_.blank();
        Sheet::Row& add = sheet_.row(Rich{"Join ", role::muted()});
        add.controls_left = true;
        add.controls.push_back({Sheet::Kind::Field, "", F_CH_NEW, false, true, false, 24, "", "#channel"});
        add.controls.push_back({Sheet::Kind::Button, "Join", act(K_CHADD, net)});
        sheet_.para("Detached: the bouncer stays in the channel and keeps its history, but it doesn't show "
                    "in nexchat until it's attached again (a message mentioning you attaches it).", role::muted());
    }

    void access_tab() {
        sheet_.row(Rich{"Logging in to it", role::title()});
        sheet_.row(Rich{std::string("Address: ") + "127.0.0.1:" + std::to_string(nx_bnc_port())
                        + "   User: " + nx_bnc_user(), role::text()}, 2);
        Sheet::Row& pw = sheet_.row(Rich{"Password ", role::muted()}, 2);
        pw.controls_left = true;
        pw.controls.push_back({Sheet::Kind::Password, "", F_AC_PASS, false, true, false, 22, "", "a new one"});
        pw.controls.push_back({Sheet::Kind::Button, "Change", act(K_AC_PASSWORD)});
        pw.controls.push_back({Sheet::Kind::Button, "Show it", act(K_AC_SHOWPASS)});
        sheet_.blank();
        sheet_.row(Rich{"From other devices", role::title()});
        Sheet::Row& rr = sheet_.row({}, 2);
        rr.controls_left = true;
        rr.controls.push_back({Sheet::Kind::Check, "Let phones and other computers connect to it", act(K_AC_REMOTE), nx_bnc_remote() != 0});
        if (nx_bnc_remote()) {
            const std::string fp = take(nx_bnc_remote_certfp());
            sheet_.para("On port " + std::to_string(nx_bnc_remote_port()) + ", over TLS. Its certificate's fingerprint: "
                        + fp, role::muted(), 2);
        }
        sheet_.blank();
        sheet_.row(Rich{"Keeping it running", role::title()});
        Sheet::Row& sd = sheet_.row({}, 2);
        sd.controls_left = true;
        sd.controls.push_back({Sheet::Kind::Check, "Start it with the computer (systemd keeps it running)", act(K_AC_SYSTEMD),
                               !std::strcmp(nx_bnc_kept_by(), "systemd"), nx_bnc_can_systemd() != 0});
        sheet_.para("Off: nexchat starts it when you open nexchat, and it keeps running after you close nexchat "
                    "until the computer restarts.", role::muted(), 2);
    }

    void storage_tab() {
        const bool files = nx_bnc_logs_as_files() != 0;
        sheet_.row(Rich{"History", role::title()});
        Sheet::Row& a = sheet_.row({}, 2);
        a.controls_left = true;
        a.controls.push_back({Sheet::Kind::Radio, "In the bouncer's database (search and missed messages work)", act(K_ST_DB), !files});
        Sheet::Row& b = sheet_.row({}, 2);
        b.controls_left = true;
        b.controls.push_back({Sheet::Kind::Radio, "As plain text files, one per channel and day", act(K_ST_FILES), files});
        sheet_.blank();
        sheet_.row(Rich{"Its name", role::title()});
        Sheet::Row& t = sheet_.row({}, 2);
        t.controls_left = true;
        t.controls.push_back({Sheet::Kind::Field, "", F_ST_TITLE, false, true, false, 30, "", "nexchat's bouncer"});
        t.controls.push_back({Sheet::Kind::Button, "Set", act(K_ST_TITLE)});
        sheet_.para("shown to apps that connect, e.g. in their network list", role::muted(), 2);
        sheet_.blank();
        Sheet::Row& lh = sheet_.row(Rich{"Its log", role::title()});
        lh.controls.push_back({Sheet::Kind::Button, "Reload", act(K_ST_RELOAD)});
        const std::string log = take(nx_bnc_log_tail(30));
        for (const auto& l : split(log, '\n')) if (!l.empty()) sheet_.row(Rich{l, role::muted()}, 2);
    }

    static std::vector<std::string> split(const std::string& s, char sep) {
        std::vector<std::string> out;
        std::size_t a = 0;
        for (std::size_t i = 0; i <= s.size(); ++i)
            if (i == s.size() || s[i] == sep) { out.push_back(s.substr(a, i - a)); a = i + 1; }
        return out;
    }

    // ── the editors (each a page) ──────────────────────────────────────
    void edit_page() {
        nx_net nn; nx_core_net_get(edit_net_, &nn);
        sheet_.row(Rich{std::string("Your nick on ") + nn.name, role::title()});
        Sheet::Row& same = sheet_.row({}, 2);
        same.controls_left = true;
        same.controls.push_back({Sheet::Kind::Check, "The same as everywhere (Network List > Nick name)", act(K_ED_SAME), ed_same_});
        Sheet::Row& nick = sheet_.row(Rich{"Nick      ", role::muted()}, 2);
        nick.controls_left = true;
        nick.controls.push_back({Sheet::Kind::Field, "", F_ED_NICK, false, !ed_same_, false, 24});
        sheet_.blank();
        sheet_.row(Rich{std::string("Logging in to ") + nn.name, role::title()});
        Sheet::Row& none = sheet_.row({}, 2);
        none.controls_left = true;
        none.controls.push_back({Sheet::Kind::Radio, "No login (as a guest)", act(K_ED_NONE), !ed_sasl_});
        Sheet::Row& sasl = sheet_.row({}, 2);
        sasl.controls_left = true;
        sasl.controls.push_back({Sheet::Kind::Radio, "With my account (SASL)", act(K_ED_SASL), ed_sasl_});
        Sheet::Row& acc = sheet_.row(Rich{"Account   ", role::muted()}, 4);
        acc.controls_left = true;
        acc.controls.push_back({Sheet::Kind::Field, "", F_ED_ACCOUNT, false, ed_sasl_, false, 24});
        Sheet::Row& pw = sheet_.row(Rich{"Password  ", role::muted()}, 4);
        pw.controls_left = true;
        pw.controls.push_back({Sheet::Kind::Password, "", F_ED_PASS, false, ed_sasl_, false, 24, "",
                               ident_.has_account ? "kept; type a new one to change it" : ""});
        sheet_.para("Your account on that network (the one you registered with NickServ), not the bouncer's. "
                    "The bouncer logs in with it for you, also while nexchat is closed.", role::muted(), 4);
        sheet_.blank();
        sheet_.row(Rich{"More", role::title()});
        Sheet::Row& rn = sheet_.row(Rich{"Real name ", role::muted()}, 2);
        rn.controls_left = true;
        rn.controls.push_back({Sheet::Kind::Field, "", F_ED_REAL, false, true, false, 24, "", "the bouncer's default"});
        Sheet::Row& un = sheet_.row(Rich{"User name ", role::muted()}, 2);
        un.controls_left = true;
        un.controls.push_back({Sheet::Kind::Field, "", F_ED_USER, false, true, false, 24, "", "the bouncer's default"});
        Sheet::Row& sp = sheet_.row(Rich{"Server password ", role::muted()}, 2);
        sp.controls_left = true;
        sp.controls.push_back({Sheet::Kind::Password, "", F_ED_SRVPASS, false, true, false, 18, "",
                               ident_.has_srvpass ? "kept; type a new one to change it" : "only if the server asks for one"});
        Sheet::Row& aw = sheet_.row({}, 2);
        aw.controls_left = true;
        aw.controls.push_back({Sheet::Kind::Check, "Show me as away when no client is connected", act(K_ED_AWAY), ed_away_});
        Sheet::Row& en = sheet_.row({}, 2);
        en.controls_left = true;
        en.controls.push_back({Sheet::Kind::Check, "Stay connected (off: kept on the bouncer, but offline)", act(K_ED_ENABLED), ed_enabled_});
        Sheet::Row& cm = sheet_.row(Rich{"On connect ", role::muted()}, 2);
        cm.controls_left = true;
        cm.controls.push_back({Sheet::Kind::Field, "", F_ED_COMMANDS, false, true, false, 40, "", "commands, separated by ;"});
        Sheet::Row& ct = sheet_.row({}, 2);
        ct.controls_left = true;
        ct.controls.push_back({Sheet::Kind::Button, ident_.has_cert ? "Show its certificate" : "Make a certificate", act(K_ED_CERT)});
        if (!ed_cert_.empty()) sheet_.para(ed_cert_, role::text(), 4);
        sheet_.blank();
        Sheet::Row& bb = sheet_.row();
        bb.controls.push_back({Sheet::Kind::Button, "Cancel", act(K_CANCEL)});
        bb.controls.push_back({Sheet::Kind::Button, "Save", act(K_SAVE)});
        nx_core_net_free(&nn);
    }

    void open_editor(int net) {
        edit_net_ = net;
        nx_bnc_ident_free(&ident_);
        nx_bnc_ident_get(net, &ident_);
        ed_same_ = ident_.same;
        ed_sasl_ = ident_.has_account;
        ed_away_ = ident_.away;
        ed_enabled_ = ident_.enabled;
        ed_cert_.clear();
        sheet_.set_field(F_ED_NICK, ident_.nick ? ident_.nick : "");
        sheet_.set_field(F_ED_ACCOUNT, ident_.account ? ident_.account : "");
        sheet_.set_field(F_ED_PASS, "");
        sheet_.set_field(F_ED_REAL, ident_.realname);
        sheet_.set_field(F_ED_USER, ident_.username);
        sheet_.set_field(F_ED_SRVPASS, "");
        sheet_.set_field(F_ED_COMMANDS, ident_.commands);
        page_ = P_EDIT;
        note_ = {};
    }

    void save_editor() {
        char* err = nullptr;
        // editor_toggled: the same nick shows the global one
        const std::string nick = sheet_.field_text(F_ED_NICK);
        const bool ok = nx_bnc_ident_save(edit_net_, ed_same_ ? nullptr : nick.c_str(),
                                          ed_sasl_ ? sheet_.field_text(F_ED_ACCOUNT).c_str() : nullptr,
                                          sheet_.field_text(F_ED_PASS).c_str(),
                                          sheet_.field_text(F_ED_REAL).c_str(), sheet_.field_text(F_ED_USER).c_str(),
                                          sheet_.field_text(F_ED_SRVPASS).c_str(), ed_away_, ed_enabled_,
                                          sheet_.field_text(F_ED_COMMANDS).c_str(), &err);
        if (!ok) { say("no", take(err)); return; }
        page_ = P_MAIN;
        say("ok", "");
    }

    void redit_page() {
        sheet_.row(Rich{"You on " + r_name_, role::title()});
        auto field = [&](const char* label, int id, Sheet::Kind k, const char* hint = "") {
            Sheet::Row& r = sheet_.row(Rich{label, role::muted()}, 2);
            r.controls_left = true;
            r.controls.push_back({k, "", id, false, true, false, 24, "", hint});
        };
        field("Nick            ", F_R_NICK, Sheet::Kind::Field);
        field("User name       ", F_R_USER, Sheet::Kind::Field);
        field("Real name       ", F_R_REAL, Sheet::Kind::Field);
        field("Server password ", F_R_PASS, Sheet::Kind::Password, "only if the server asks for one");
        sheet_.blank();
        Sheet::Row& s = sheet_.row({}, 2);
        s.controls_left = true;
        s.controls.push_back({Sheet::Kind::Check, "Log in to my account (SASL)", act(K_RED_SASL), r_sasl_});
        if (r_sasl_) {
            field("  Account       ", F_R_ACCOUNT, Sheet::Kind::Field);
            field("  Password      ", F_R_APASS, Sheet::Kind::Password);
        }
        sheet_.blank();
        Sheet::Row& bb = sheet_.row();
        bb.controls.push_back({Sheet::Kind::Button, "Cancel", act(K_CANCEL)});
        bb.controls.push_back({Sheet::Kind::Button, "Save", act(K_SAVE)});
    }

    void open_redit(int net, const std::string& id, const std::string& name) {
        r_net_ = net; r_id_ = id; r_name_ = name; r_sasl_ = false;
        nx_rbnc* l; const int n = nx_rbnc_list(&l);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < l[i].n; ++j)
                if (l[i].net == net && id == l[i].nets[j].id) sheet_.set_field(F_R_NICK, l[i].nets[j].nick);
        nx_rbnc_list_free(l, n);
        for (int f : {F_R_USER, F_R_REAL, F_R_PASS, F_R_ACCOUNT, F_R_APASS}) sheet_.set_field(f, "");
        page_ = P_REDIT;
        note_ = {};
    }

    void save_redit() {
        char* err = nullptr;
        auto t = [&](int f) { return sheet_.field_text(f); };
        const auto nick = t(F_R_NICK), user = t(F_R_USER), real = t(F_R_REAL), pass = t(F_R_PASS);
        if (!nx_rbnc_change(r_net_, r_id_.c_str(), nick.c_str(), user.c_str(), real.c_str(), pass.c_str(), &err) ||
            (r_sasl_ && !nx_rbnc_sasl(r_net_, r_name_.c_str(), t(F_R_ACCOUNT).c_str(), t(F_R_APASS).c_str(), &err))) {
            say("no", take(err));
            return;
        }
        page_ = P_MAIN;
    }

    void add_page() {
        sheet_.row(Rich{"Which network should it stay on?", role::title()});
        Sheet::Row& pop = sheet_.row({}, 2);
        pop.controls_left = true;
        const char *name, *host;
        for (int i = 0; nx_bnc_popular(i, &name, &host); ++i)
            pop.controls.push_back({Sheet::Kind::Button, name, act(K_ADD_POPULAR, i)});
        sheet_.blank();
        Sheet::Row& n = sheet_.row(Rich{"Name    ", role::muted()}, 2);
        n.controls_left = true;
        n.controls.push_back({Sheet::Kind::Field, "", F_ADD_NAME, false, true, false, 28});
        Sheet::Row& a = sheet_.row(Rich{"Address ", role::muted()}, 2);
        a.controls_left = true;
        a.controls.push_back({Sheet::Kind::Field, "", F_ADD_ADDR, false, true, false, 28, "", "irc.libera.chat"});
        sheet_.para("Encrypted (TLS, port 6697) unless you write irc:// in front or another port.", role::muted(), 2);
        sheet_.blank();
        Sheet::Row& bb = sheet_.row();
        bb.controls.push_back({Sheet::Kind::Button, "Cancel", act(K_CANCEL)});
        bb.controls.push_back({Sheet::Kind::Button, "Add", act(K_ADD_ADD)});
    }

    // ── Stay online setup (bncsetup.c), the same window's pages ─────────
    void open_setup() {
        page_ = P_SETUP;
        step_ = S_CHOOSE;
        note_ = {};
        found_ = {};
        probe_kind_.clear(); probe_text_.clear(); probed_for_.clear();
        cert_bad_ = trust_ = false;
        nets_ticked_.clear();
        // bncsetup_open: already on a bouncer elsewhere: straight to its page
        nx_net nn{};
        if (from_ >= 0) nx_core_net_get(from_, &nn);
        sheet_.set_field(F_S_ADDR, ""); sheet_.set_field(F_S_USER, ""); sheet_.set_field(F_S_PASS, "");
        for (int f : {F_S_OTHER_HOST, F_S_OTHER_NAME, F_S_PACCOUNT, F_S_PPASS}) sheet_.set_field(f, "");
        persist_net_ = std::max(0, from_);
        if (from_ >= 0) {
            nx_core_net_free(&nn);
        }
        nx_bnc_detect([](void* ud, const nx_found* f) {
            auto* self = static_cast<BouncerWin*>(ud);
            if (!self->open_ || self->page_ != P_SETUP) return;
            self->found_.known = true;
            self->found_.ours = f->ours; self->found_.soju = f->soju; self->found_.znc = f->znc;
            self->found_.soju_version = f->soju_version ? f->soju_version : "";
            self->found_.soju_failed = f->soju_failed ? f->soju_failed : "";
            self->found_.listen.clear(); self->found_.listen_addr.clear();
            for (int i = 0; i < f->n_listen; ++i) {
                self->found_.listen.push_back(f->listen[i]);
                self->found_.listen_addr.push_back(f->listen_addr[i]);
            }
            nx_ui_bouncer_changed();
        }, this);
    }

    void setup_page() {
        switch (step_) {
        case S_CHOOSE: {
            sheet_.row(Rich{"Stay online", role::title()});
            sheet_.para("Stay in your channels while nexchat is closed, and get what you missed when you come back.",
                        role::muted());
            sheet_.blank();
            if (!found_.known) sheet_.para("Looking for bouncers on this computer...", role::muted());
            if (found_.soju) {
                std::string t = found_.ours
                    ? std::string("nexchat already runs a soju for you") + (nx_bnc_running() ? "" : " (stopped right now: it starts again)")
                      + ". Pick networks to put on it:"
                    : "soju" + (found_.soju_version.empty() ? "" : " " + found_.soju_version)
                      + " is installed on this computer. nexchat sets up its own copy (in its own folder, only reachable "
                        "from here), keeps it running and moves your networks onto it, logins and channels included. "
                        "Nothing to type.";
                if (!found_.soju_failed.empty())
                    t += "\n\nThe system's soju service is set to start but fails (" + found_.soju_failed
                         + "). That one needs root to fix; nexchat's own copy doesn't need it.";
                Sheet::Row& r = sheet_.row();
                r.controls_left = true;
                r.controls.push_back({Sheet::Kind::Button, found_.ours ? "Use my bouncer" : "Use soju on this computer", act(K_S_USEMINE)});
                sheet_.para(t, role::muted(), 2);
                sheet_.blank();
            }
            for (std::size_t i = 0; i < found_.listen.size(); ++i) {
                Sheet::Row& r = sheet_.row();
                r.controls_left = true;
                r.controls.push_back({Sheet::Kind::Button, "Use it", act(K_S_LISTEN, static_cast<int>(i))});
                r.text = Rich{found_.listen[i], role::text()};
            }
            auto choice = [&](const char* label, const char* text, int step) {
                Sheet::Row& r = sheet_.row();
                r.controls_left = true;
                r.controls.push_back({Sheet::Kind::Button, label, act(K_S_CHOICE, step)});
                sheet_.para(text, role::muted(), 2);
                sheet_.blank();
            };
            choice("I have a bouncer", "On another computer, or one a friend or service runs for you. You'll need its "
                   "address and your login.", S_BOUNCER);
            choice("Keep me online on my network", "Some networks can keep you connected themselves (ones running "
                   "Ergo). Needs an account there.", S_PERSIST);
            choice("What's a bouncer?", "How it works, and how to get one.", S_EXPLAIN);
            break;
        }
        case S_BOUNCER: {
            sheet_.row(Rich{"Your bouncer", role::title()});
            auto field = [&](const char* label, int id, Sheet::Kind k, const char* hint = "") {
                Sheet::Row& r = sheet_.row(Rich{label, role::muted()}, 2);
                r.controls_left = true;
                r.controls.push_back({k, "", id, false, true, false, 30, "", hint});
            };
            field("Address   ", F_S_ADDR, Sheet::Kind::Field, "bnc.example.org:6697");
            field("User name ", F_S_USER, Sheet::Kind::Field);
            field("Password  ", F_S_PASS, Sheet::Kind::Password);
            sheet_.para("The address is encrypted (TLS) unless you write irc:// in front or use port 6667. "
                        "It's checked as you type.", role::muted(), 2);
            if (!probe_text_.empty()) {
                Rich s;
                if (probe_checking_) s.append(probe_text_, role::text());
                else if (!probe_kind_.empty()) { s.append("✓ ", role::good()); s.append(probe_text_, role::good()); }
                else { s.append("✗ ", role::bad()); s.append(probe_text_, role::bad()); }
                for (auto& l : wrap(s, 70)) sheet_.row(l, 2);
            }
            if (cert_bad_) {
                Sheet::Row& r = sheet_.row({}, 2);
                r.controls_left = true;
                r.controls.push_back({Sheet::Kind::Check, "It's mine, trust this certificate", act(K_S_TRUST), trust_});
            }
            setup_buttons(!probe_kind_.empty() && !probe_checking_ && (!cert_bad_ || trust_), "Next");
            break;
        }
        case S_NETWORKS: {
            sheet_.row(Rich{"Which networks should it stay on?", role::title()});
            if (setup_mine_) {
                sheet_.para("Ticked ones move to the bouncer, logins and channels included.", role::muted());
                for (auto& [net, on] : mine_ticked_) {
                    nx_net nn; nx_core_net_get(net, &nn);
                    std::string host = nn.servers ? nn.servers : "";
                    host = host.substr(0, host.find('\n'));
                    Sheet::Row& r = sheet_.row({}, 2);
                    r.controls_left = true;
                    r.controls.push_back({Sheet::Kind::Check, std::string(nn.name) + "  " + host + (nn.autojoin && *nn.autojoin ? ", with its channels" : ""),
                                          act(K_S_MINE, net), on});
                    nx_core_net_free(&nn);
                }
                if (!mine_status_.empty()) sheet_.para(mine_status_, role::text(), 2);
            } else {
                sheet_.para(probe_kind_ == "znc" ? "ZNC keeps the networks it's set up for; nexchat adds these to it:"
                                                 : "The bouncer connects to these and stays there for you:", role::muted());
                const char *name, *host;
                for (int i = 0; nx_bnc_popular(i, &name, &host); ++i) {
                    const bool on = std::find(nets_ticked_.begin(), nets_ticked_.end(), i) != nets_ticked_.end();
                    Sheet::Row& r = sheet_.row({}, 2);
                    r.controls_left = true;
                    r.controls.push_back({Sheet::Kind::Check, std::string(name) + "  " + host, act(K_S_POP, i), on});
                }
                Sheet::Row& oh = sheet_.row(Rich{"Another one ", role::muted()}, 2);
                oh.controls_left = true;
                oh.controls.push_back({Sheet::Kind::Field, "", F_S_OTHER_HOST, false, true, false, 26, "", "irc.example.org"});
                Sheet::Row& on = sheet_.row(Rich{"Its name    ", role::muted()}, 2);
                on.controls_left = true;
                on.controls.push_back({Sheet::Kind::Field, "", F_S_OTHER_NAME, false, true, false, 26, "", "name, e.g. work"});
            }
            setup_buttons(true, "Connect");
            break;
        }
        case S_PERSIST: {
            sheet_.row(Rich{"Keep me online on my network", role::title()});
            Sheet::Row& pick = sheet_.row(Rich{"Network ", role::muted()}, 2);
            pick.controls_left = true;
            const int n = nx_core_net_count();
            // a scrolling choice: the one picked, ‹ › to change (the GUI's drop-down)
            nx_net nn; nx_core_net_get(std::clamp(persist_net_, 0, std::max(0, n - 1)), &nn);
            pick.controls.push_back({Sheet::Kind::Button, std::string("‹ ") + nn.name + " ›", act(K_S_PNET)});
            nx_core_net_free(&nn);
            Sheet::Row& a = sheet_.row(Rich{"Your account ", role::muted()}, 2);
            a.controls_left = true;
            a.controls.push_back({Sheet::Kind::Field, "", F_S_PACCOUNT, false, true, false, 24});
            Sheet::Row& p = sheet_.row(Rich{"Password     ", role::muted()}, 2);
            p.controls_left = true;
            p.controls.push_back({Sheet::Kind::Password, "", F_S_PPASS, false, true, false, 24});
            if (!persist_text_.empty()) {
                Rich s;
                if (persist_kind_ == "persist") s.append("✓ " + persist_text_, role::good());
                else if (persist_kind_.empty()) s.append(persist_text_, role::text());
                else s.append("✗ " + persist_text_, role::bad());
                for (auto& l : wrap(s, 70)) sheet_.row(l, 2);
            }
            setup_buttons(persist_kind_ == "persist" && !sheet_.field_text(F_S_PACCOUNT).empty()
                          && !sheet_.field_text(F_S_PPASS).empty(), "Connect");
            break;
        }
        case S_EXPLAIN:
            sheet_.row(Rich{"What's a bouncer?", role::title()});
            sheet_.para("A bouncer is a small program that stays connected to IRC for you, all the time. It keeps your "
                        "channels and collects what's said while nexchat is closed. nexchat connects to the bouncer "
                        "instead of the network, and you get it all when you come back, also on your phone.", role::text());
            sheet_.blank();
            sheet_.para("soju is the easiest to run, ZNC the most used. Some communities and services give out bouncer "
                        "accounts. And some networks (ones running Ergo) can keep you online themselves, with just an "
                        "account.", role::text());
            sheet_.blank();
            sheet_.para("soju.im  ·  wiki.znc.in", role::title());
            setup_buttons(false, "");
            break;
        }
    }

    void setup_buttons(bool next_ok, const char* next) {
        sheet_.blank();
        Sheet::Row& bb = sheet_.row();
        bb.controls.push_back({Sheet::Kind::Button, "Back", act(K_S_BACK)});
        bb.controls.push_back({Sheet::Kind::Button, "Cancel", act(K_CANCEL)});
        if (*next) bb.controls.push_back({Sheet::Kind::Button, next, act(K_S_NEXT), false, next_ok});
    }

    // bncsetup.c bouncer_check_now: as you type, after a pause
    void check_bouncer_soon() {
        const std::string key = sheet_.field_text(F_S_ADDR) + "\n" + sheet_.field_text(F_S_USER) + "\n" + sheet_.field_text(F_S_PASS);
        if (key == probed_for_) return;
        probed_for_ = key;
        probe_kind_.clear();
        cert_bad_ = false;
        if (sheet_.field_text(F_S_ADDR).empty()) { probe_text_.clear(); return; }
        const int gen = ++probe_gen_;
        probe_checking_ = true;
        probe_text_ = "Checking...";
        struct Ctx { BouncerWin* w; int gen; };
        if (!nx_bnc_probe(sheet_.field_text(F_S_ADDR).c_str(), sheet_.field_text(F_S_USER).c_str(),
                          sheet_.field_text(F_S_PASS).c_str(),
                          [](void* ud, const nx_probe* r) {
                              auto* c = static_cast<Ctx*>(ud);
                              BouncerWin* w = c->w;
                              if (c->gen == w->probe_gen_ && w->open_) w->probe_done(r);
                              delete c;
                              nx_ui_bouncer_changed();
                          }, new Ctx{this, gen})) {
            probe_checking_ = false;
            probe_text_ = "That address doesn't look right.";
        }
    }

    // bncsetup.c bouncer_checked: what to say
    void probe_done(const nx_probe* r) {
        probe_checking_ = false;
        cert_bad_ = r->cert_untrusted;
        if (r->kind) {
            const std::string k = r->kind;
            probe_kind_ = k;
            probe_text_ = k == "soju" ? "It's soju, and the login works."
                        : k == "znc" ? std::string("It's ZNC, and the login works.") + (r->networks && *r->networks ? std::string(" Its networks: ") + r->networks + "." : "")
                        : k == "persist" ? "That's a network that can keep you online itself (Ergo): pick \"Keep me online on my network\" instead."
                        : "That's an IRC server, not a bouncer. A bouncer's address, user name and password come from whoever runs it.";
            if (k == "persist" || k == "plain") probe_kind_.clear();
            if (cert_bad_ && r->cert_why) probe_text_ += std::string(" But ") + r->cert_why + ".";
        } else {
            probe_text_ = r->error ? r->error : "?";
        }
    }

    void check_persist() {
        nx_net nn; nx_core_net_get(persist_net_, &nn);
        std::string host = nn.servers ? nn.servers : "";
        host = host.substr(0, host.find('\n'));
        const bool tls = nn.ssl || host.find("/+") != std::string::npos;
        nx_core_net_free(&nn);
        if (const auto s = host.find('/'); s != std::string::npos) {
            std::string port = host.substr(s + 1);
            if (!port.empty() && port[0] == '+') port = port.substr(1);
            host = host.substr(0, s) + ":" + (tls ? "+" : "") + port;
        }
        if (!tls && host.rfind("irc://", 0) != 0) host = "irc://" + host;
        const int gen = ++persist_gen_;
        persist_kind_.clear();
        persist_text_ = "Checking whether this network can keep you online...";
        struct Ctx { BouncerWin* w; int gen; };
        nx_bnc_probe(host.c_str(), "", "", [](void* ud, const nx_probe* r) {
            auto* c = static_cast<Ctx*>(ud);
            BouncerWin* w = c->w;
            if (c->gen == w->persist_gen_ && w->open_) {
                if (r->kind && !std::strcmp(r->kind, "persist")) {
                    w->persist_kind_ = "persist";
                    w->persist_text_ = "It can: fill in your account on it and Connect.";
                } else {
                    w->persist_kind_ = r->kind ? r->kind : "error";
                    w->persist_text_ = "This network can't keep you online by itself; you'd need a bouncer for it. "
                                       "Go back and pick \"I have a bouncer\", or ask the network's people which "
                                       "bouncers they recommend.";
                }
            }
            delete c;
            nx_ui_bouncer_changed();
        }, new Ctx{this, gen});
    }

    // ── switching networks on and off (bncwin.c switched) ──────────────
    void switch_on(int net, const std::string& name) {
        busy_ = true;
        say("", "Moving " + name + " to your bouncer...");
        nx_bnc_adopt(net, [](void* ud, const char* text, int done, int ok) {
            auto* w = static_cast<BouncerWin*>(ud);
            if (!ok) w->say("no", text ? text : "?");
            else if (!done) w->say("", text ? text : "");
            if (done) { w->busy_ = false; if (ok) w->say("ok", text ? text : ""); }
            nx_ui_bouncer_changed();
        }, this);
    }

    void press(int id) {
        // the tabs and fixed buttons
        if (id >= I_TAB0 && id < I_TAB0 + 4) { tab_ = static_cast<Tab>(id - I_TAB0); note_ = {}; return; }
        char* err = nullptr;
        switch (id) {
        case I_SETUP:
            if (!nx_bnc_setup(&err)) { say("no", take(err)); return; }
            say("ok", "");
            if (from_ >= 0) { nx_net nn; nx_core_net_get(from_, &nn); switch_on(from_, nn.name); nx_core_net_free(&nn); }
            return;
        case I_START: if (!nx_bnc_start(&err)) say("no", take(err)); return;
        case I_STOP: nx_bnc_stop(); say("ok", "Stopped. The networks on it are offline until you start it again."); return;
        case I_REMOVE:
            if (!is_sure(id)) { sure_.push_back(id); return; }
            sure_.clear();
            if (!nx_bnc_remove(&err)) say("no", take(err));
            else say("ok", "Removed. Every network connects directly again, as it did before.");
            return;
        case I_OTHER: open_setup(); return;
        case I_CLOSE: open_ = false; return;
        default: break;
        }
        if (id < I_ACT0 || id >= I_ACT0 + static_cast<int>(acts_.size())) return;
        const Act a = acts_[static_cast<std::size_t>(id - I_ACT0)];
        switch (a.kind) {
        case K_SWITCH: {
            nx_bnc_row* rows; const int n = nx_bnc_rows(from_, &rows);
            bool mine = false;
            for (int i = 0; i < n; ++i) if (rows[i].net == a.net) mine = rows[i].mine;
            nx_bnc_rows_free(rows, n);
            if (busy_) return;
            if (!mine) switch_on(a.net, a.name);
            else if (!nx_bnc_release(a.net, &err)) say("no", take(err));
            else say("", a.name + " connects directly again (when you next connect to it).");
            return;
        }
        case K_EDIT: open_editor(a.net); return;
        case K_RCONNECT: nx_rbnc_connect(a.net); return;
        case K_REDITBNC: from_ = a.net; open_setup(); step_ = S_BOUNCER; prefill_bouncer(a.net); return;
        case K_RFORGET:
            if (!is_sure(id)) { sure_.push_back(id); return; }
            sure_.clear();
            nx_rbnc_forget(a.net);
            say("ok", "Forgotten here. The bouncer itself and what's on it are unchanged.");
            return;
        case K_ROPEN: nx_rbnc_open(a.net, a.name.c_str()); open_ = false; return;
        case K_RLOGIN: open_redit(a.net, a.id, a.name); return;
        case K_RREMOVE:
            if (!is_sure(id)) { sure_.push_back(id); return; }
            sure_.clear();
            if (!nx_rbnc_del(a.net, a.id.c_str(), &err)) say("no", take(err));
            else say("ok", "Removed from the bouncer.");
            return;
        case K_RADD: add_net_ = a.net; sheet_.set_field(F_ADD_NAME, ""); sheet_.set_field(F_ADD_ADDR, ""); page_ = P_ADD; note_ = {}; return;
        case K_ADD_POPULAR: {
            const char *name, *host;
            if (nx_bnc_popular(a.net, &name, &host)) { sheet_.set_field(F_ADD_NAME, name); sheet_.set_field(F_ADD_ADDR, host); }
            return;
        }
        case K_ADD_ADD:
            if (!nx_rbnc_add(add_net_, sheet_.field_text(F_ADD_NAME).c_str(), sheet_.field_text(F_ADD_ADDR).c_str(), &err)) {
                say("no", take(err));
                return;
            }
            page_ = P_MAIN;
            say("ok", "Added: the bouncer is connecting to it, and its tab opens here when it's in.");
            return;
        case K_CHNET: ch_net_ = a.net; return;
        case K_CHDETACH: {
            const std::string all = take(nx_bnc_channels(a.net));
            const bool now = all.find(a.id + "\t") != std::string::npos
                && all.substr(all.find(a.id + "\t")).substr(0, all.substr(all.find(a.id + "\t")).find('\n')).find("detached") != std::string::npos;
            if (!nx_bnc_channel_detach(a.net, a.id.c_str(), !now, &err)) say("no", take(err));
            else note_ = {};
            return;
        }
        case K_CHREMOVE: if (!nx_bnc_channel_del(a.net, a.id.c_str(), &err)) say("no", take(err)); return;
        case K_CHADD: {
            std::string c = sheet_.field_text(F_CH_NEW);
            if (c.empty()) return;
            if (c[0] != '#' && c[0] != '&') c = "#" + c;
            if (!nx_bnc_channel_add(a.net, c.c_str(), &err)) say("no", take(err));
            else { sheet_.set_field(F_CH_NEW, ""); note_ = {}; }
            return;
        }
        case K_AC_PASSWORD:
            if (!nx_bnc_set_password(sheet_.field_text(F_AC_PASS).c_str(), &err)) say("no", take(err));
            else { sheet_.set_field(F_AC_PASS, ""); say("ok", "Password changed."); }
            return;
        case K_AC_SHOWPASS: say("", "The password: " + take(nx_bnc_password())); return;
        case K_AC_REMOTE:
            if (!nx_bnc_set_remote(!nx_bnc_remote(), &err)) say("no", take(err));
            else say("ok", "Changed; the bouncer restarted to use it.");
            return;
        case K_AC_SYSTEMD:
            if (!nx_bnc_set_systemd(std::strcmp(nx_bnc_kept_by(), "systemd") != 0, &err)) say("no", take(err));
            else say("ok", "Changed.");
            return;
        case K_ST_DB: case K_ST_FILES:
            if ((a.kind == K_ST_FILES) == (nx_bnc_logs_as_files() != 0)) return;
            if (!nx_bnc_set_logs_as_files(a.kind == K_ST_FILES, &err)) say("no", take(err));
            else say("ok", "Changed; the bouncer restarted to use it.");
            return;
        case K_ST_TITLE:
            if (!nx_bnc_set_title(sheet_.field_text(F_ST_TITLE).c_str(), &err)) say("no", take(err));
            else say("ok", "Name saved.");
            return;
        case K_ST_RELOAD: return;
        case K_ED_SAME: ed_same_ = !ed_same_; if (ed_same_) sheet_.set_field(F_ED_NICK, nx_core_pref_str("irc_nick1")); return;
        case K_ED_NONE: ed_sasl_ = false; return;
        case K_ED_SASL: ed_sasl_ = true; return;
        case K_ED_AWAY: ed_away_ = !ed_away_; return;
        case K_ED_ENABLED: ed_enabled_ = !ed_enabled_; return;
        case K_ED_CERT: {
            char* fp = nx_bnc_certfp(edit_net_, TRUE, &err);
            if (!fp) { say("no", take(err)); return; }
            ed_cert_ = std::string("Its fingerprint, to add to your account (NickServ CERT ADD): ") + take(fp);
            ident_.has_cert = 1;
            return;
        }
        case K_RED_SASL: r_sasl_ = !r_sasl_; return;
        case K_SAVE:
            if (page_ == P_EDIT) save_editor();
            else if (page_ == P_REDIT) save_redit();
            return;
        case K_CANCEL:
            if (page_ == P_SETUP && from_open_setup_only_) { open_ = false; return; }
            page_ = P_MAIN; note_ = {};
            return;
        // the setup's pages
        case K_S_CHOICE:
            step_ = static_cast<Step>(a.net);
            if (step_ == S_PERSIST) check_persist();
            return;
        case K_S_USEMINE: {
            if (nx_bnc_state() != NX_MYBNC_SET_UP && !nx_bnc_setup(&err)) { mine_status_ = take(err); return; }
            setup_mine_ = true;
            mine_ticked_.clear();
            // bncsetup.c networks_fill_mine: not on it, not a bouncer's own entry
            const int n = nx_core_net_count();
            for (int i = 0; i < n; ++i) {
                nx_net nn; nx_core_net_get(i, &nn);
                const bool bouncer_entry = nn.login == bnc_login_pass_index() || std::strchr(nn.name, '/');
                bool mine = false;
                nx_bnc_row* rows; const int rn = nx_bnc_rows(i, &rows);
                for (int k = 0; k < rn; ++k) if (rows[k].net == i) mine = rows[k].mine;
                nx_bnc_rows_free(rows, rn);
                if (nn.servers && *nn.servers && !bouncer_entry && !mine)
                    mine_ticked_.push_back({i, i == from_ || nn.favorite || nn.auto_connect || (nn.autojoin && *nn.autojoin)});
                nx_core_net_free(&nn);
            }
            step_ = S_NETWORKS;
            return;
        }
        case K_S_LISTEN:
            sheet_.set_field(F_S_ADDR, found_.listen_addr[static_cast<std::size_t>(a.net)]);
            step_ = S_BOUNCER;
            sheet_.focus_id(F_S_USER);
            return;
        case K_S_TRUST: trust_ = !trust_; return;
        case K_S_POP: {
            auto it = std::find(nets_ticked_.begin(), nets_ticked_.end(), a.net);
            if (it == nets_ticked_.end()) nets_ticked_.push_back(a.net); else nets_ticked_.erase(it);
            return;
        }
        case K_S_MINE: for (auto& [n, on] : mine_ticked_) if (n == a.net) on = !on; return;
        case K_S_PNET: {
            const int n = nx_core_net_count();
            if (n) persist_net_ = (persist_net_ + 1) % n;
            check_persist();
            return;
        }
        case K_S_BACK:
            if (step_ == S_CHOOSE) { page_ = P_MAIN; return; }
            step_ = step_ == S_NETWORKS && !setup_mine_ ? S_BOUNCER : S_CHOOSE;
            setup_mine_ = setup_mine_ && step_ == S_NETWORKS;
            return;
        case K_S_NEXT:
            if (step_ == S_BOUNCER) { setup_mine_ = false; step_ = S_NETWORKS; return; }
            if (step_ == S_NETWORKS && setup_mine_) {
                // bncsetup.c finish_mine: the ticked ones, one after another
                std::vector<int> nets;
                for (auto& [n, on] : mine_ticked_) if (on) nets.push_back(n);
                if (nets.empty()) { mine_status_ = "✗ Tick at least one network."; return; }
                for (int n : nets) { nx_net nn; nx_core_net_get(n, &nn); switch_on(n, nn.name); nx_core_net_free(&nn); }
                page_ = P_MAIN;
                return;
            }
            if (step_ == S_NETWORKS) {
                std::string todo;
                const char *name, *host;
                for (int i : nets_ticked_) if (nx_bnc_popular(i, &name, &host)) todo += std::string(name) + " " + host + "\n";
                if (nx_bnc_setup_bouncer(from_, sheet_.field_text(F_S_ADDR).c_str(), sheet_.field_text(F_S_USER).c_str(),
                                         sheet_.field_text(F_S_PASS).c_str(), trust_, todo.c_str(),
                                         sheet_.field_text(F_S_OTHER_HOST).c_str(),
                                         sheet_.field_text(F_S_OTHER_NAME).c_str(), &err) < 0) {
                    say("no", take(err));
                    return;
                }
                page_ = P_MAIN;
                say("ok", "Saved; connecting to your bouncer.");
                return;
            }
            if (step_ == S_PERSIST) {
                if (!nx_bnc_setup_persist(persist_net_, sheet_.field_text(F_S_PACCOUNT).c_str(),
                                          sheet_.field_text(F_S_PPASS).c_str(), &err)) {
                    say("no", take(err));
                    return;
                }
                page_ = P_MAIN;
                say("ok", "Saved; connecting, and the network keeps you on from now on.");
            }
            return;
        default: return;
        }
    }

    bool from_open_setup_only_ = false;

    static int bnc_login_pass_index() {
        int i = 0;
        for (const char* const* p = nx_core_login_names(); *p; ++p, ++i)
            if (std::strstr(*p, "/PASS")) return i;
        return -1;
    }

    void prefill_bouncer(int net) {
        // bncsetup.c fill_from: its address and login
        nx_net nn; nx_core_net_get(net, &nn);
        std::string s = nn.servers ? nn.servers : "";
        s = s.substr(0, s.find('\n'));
        const auto slash = s.find('/');
        const bool tls = nn.ssl || (slash != std::string::npos && s.size() > slash + 1 && s[slash + 1] == '+');
        std::string addr = slash == std::string::npos ? s
            : s.substr(0, slash) + ":" + s.substr(slash + 1 + (s.size() > slash + 1 && s[slash + 1] == '+' ? 1 : 0));
        if (!tls) addr = "irc://" + addr;
        sheet_.set_field(F_S_ADDR, addr);
        sheet_.set_field(F_S_USER, nn.user ? nn.user : "");
        sheet_.set_field(F_S_PASS, nn.pass ? nn.pass : "");
        nx_core_net_free(&nn);
        probed_for_.clear();
    }

    // ── a frame ────────────────────────────────────────────────────────
    void build() {
        acts_.clear();
        sheet_.begin();
        switch (page_) {
        case P_EDIT: edit_page(); break;
        case P_REDIT: redit_page(); break;
        case P_ADD: add_page(); break;
        case P_SETUP: setup_page(); break;
        case P_MAIN:
            head();
            switch (nx_bnc_state() == NX_MYBNC_SET_UP ? tab_ : T_NETWORKS) {
            case T_NETWORKS: networks_tab(); break;
            case T_CHANNELS: channels_tab(); break;
            case T_ACCESS: access_tab(); break;
            case T_STORAGE: storage_tab(); break;
            }
            break;
        }
    }

    std::string title() const {
        switch (page_) {
        case P_EDIT: { nx_net nn; nx_core_net_get(edit_net_, &nn); std::string t = std::string(nn.name) + " on your bouncer"; nx_core_net_free(&nn); return t; }
        case P_REDIT: return r_name_ + " on the bouncer";
        case P_ADD: return "Add a network to the bouncer";
        case P_SETUP: return "Stay online";
        default: return "Bouncer";
        }
    }

    // the 3 s tick: a key over everything shown; redraw only on change
    bool tick() {
        if (page_ == P_SETUP && step_ == S_BOUNCER) check_bouncer_soon();
        const auto now = std::chrono::steady_clock::now();
        if (now - last_tick_ < std::chrono::seconds(3)) return false;
        last_tick_ = now;
        std::string k = take(nx_rbnc_key());
        if (k == key_) return false;
        key_ = std::move(k);
        return true;
    }

    maya::Element element(int sw, int shh) {
        using namespace maya::dsl;
        build();
        // like Panel: a small screen gets the whole of it, a big one a comfortable card
        const int w = sw < 60 ? std::max(20, sw) : std::clamp(sw - 4, 50, 110);
        const int h = shh < 18 ? std::max(8, shh) : std::clamp(shh - 2, 14, 44);
        x_ = std::max(0, (sw - w) / 2);
        y_ = std::max(0, (shh - h) / 2);
        w_ = w; h_ = h;
        const int inner = w - 4;
        const bool tabs = page_ == P_MAIN && nx_bnc_state() == NX_MYBNC_SET_UP;
        const int body_h = std::max(1, h - 2 - 1 - (tabs ? 2 : 0) - 2 - (note_.text.empty() ? 0 : 1));
        std::vector<Rich> card;
        // top border with the title and ✕
        {
            Rich top{"╭─", role::border()};
            Rich t{" " + title() + " "};
            if (t.width() > w - 7) { t = t.take(std::max(0, w - 9)); t.append("… "); }
            top.append(t.text, role::selected().with_bold());
            std::string fill;
            for (int i = top.width(); i < w - 5; ++i) fill += "─";
            top.append(fill, role::border());
            top.append(" ✕ ", role::muted());
            top.append("─╮", role::border());
            card.push_back(top);
        }
        auto line = [&](Rich r) {
            Rich row{"│ ", role::border()};
            row.append(r.fit(inner));
            row.append(" │", role::border());
            card.push_back(row);
        };
        tab_x_.clear();
        line(Rich{});                     // room under the title
        if (tabs) {
            Rich t;
            // narrow: the short names ("Net" "Chan" …) so all four stay clickable
            const bool shrt = inner < 44;
            const char* names[] = {shrt ? "Net" : "Networks", shrt ? "Chan" : "Channels", shrt ? "Acc" : "Access", shrt ? "Store" : "Storage"};
            int x = x_ + 2;
            for (int i = 0; i < 4; ++i) {
                Rich one{std::string(" ") + names[i] + " ", i == tab_ ? role::title().with_bold() : role::muted()};
                if (i == tab_) one.replace_colors(role::selected());
                tab_x_.push_back({x, x + one.width()});
                x += one.width() + 1;
                t.append(one);
                t.append(" ");
            }
            line(t);
            std::string rule;
            for (int i = 0; i < inner; ++i) rule += "─";
            line(Rich{rule, role::border()});
        }
        body_x_ = x_ + 2;
        body_y_ = y_ + 2 + (tabs ? 2 : 0);
        for (auto& r : sheet_.rows(inner, body_h)) line(r);
        if (!note_.text.empty()) {
            Rich n;
            if (note_.kind == "ok") n.append("✓ " + note_.text, role::good());
            else if (note_.kind == "no") n.append("✗ " + note_.text, role::bad());
            else n.append(note_.text, role::text());
            line(n);
        }
        // the bottom row, under a rule as the panels have it: "Use another
        // bouncer..." left, Close right (main page only)
        {
            std::string s;
            for (int i = 0; i < w - 2; ++i) s += "─";
            card.push_back(Rich{"├" + s + "┤", role::border()});
        }
        {
            Rich b;
            other_x1_ = close_x0_ = -1;
            if (page_ == P_MAIN) {
                const bool more = remote_count() > 0;
                // narrow: shorter labels, Close stays on the right
                std::string other = more ? "[ Add another bouncer... ]" : "[ Use another bouncer... ]";
                if (static_cast<int>(other.size()) + 10 > inner) other = more ? "[ Add... ]" : "[ Another... ]";
                b.append(other, role::text());
                other_x1_ = x_ + 2 + b.width();
                b = b.pad_right(std::max(b.width() + 1, inner - 9));
                close_x0_ = x_ + 2 + b.width();
                b.append("[ Close ]", role::text());
            }
            line(b);
            bottom_y_ = y_ + static_cast<int>(card.size()) - 1;
        }
        std::string bottom = "╰";
        for (int i = 0; i < w - 2; ++i) bottom += "─";
        bottom += "╯";
        card.push_back(Rich{bottom, role::border()});
        return (v(stack_rows(card)) | padding(y_, 0, 0, x_)).build();
    }
    mutable int bottom_y_ = 0, other_x1_ = -1, close_x0_ = -1;

    // ── input ──────────────────────────────────────────────────────────
    void key(const maya::KeyEvent& k) {
        using maya::SpecialKey;
        const auto* sk = std::get_if<SpecialKey>(&k.key);
        const auto* ck = std::get_if<maya::CharKey>(&k.key);
        if (sk && *sk == SpecialKey::Escape && !sheet_.editing()) {
            if (page_ != P_MAIN) { page_ = P_MAIN; note_ = {}; }
            else open_ = false;
            return;
        }
        // Ctrl-←/→ or Alt-1..4: the tabs
        if (page_ == P_MAIN && ck && k.mods.alt && ck->codepoint >= '1' && ck->codepoint <= '4') {
            tab_ = static_cast<Tab>(ck->codepoint - '1');
            return;
        }
        if (sk && page_ == P_MAIN && k.mods.ctrl && (*sk == SpecialKey::Left || *sk == SpecialKey::Right)) {
            tab_ = static_cast<Tab>((tab_ + (*sk == SpecialKey::Left ? 3 : 1)) % 4);
            return;
        }
        build();                          // the controls as they are now
        const Sheet::Outcome o = sheet_.key(k);
        if (o == Sheet::Outcome::Pressed) press(sheet_.pressed());
        if (o == Sheet::Outcome::Edited && page_ == P_SETUP && step_ == S_BOUNCER) check_bouncer_soon();
        if (o == Sheet::Outcome::Edited && sheet_.pressed() == F_ST_TITLE) press_title();
        // pressing anything else disarms a "sure?"
        if (o == Sheet::Outcome::Pressed && !sure_.empty() && sure_.back() != sheet_.pressed()) sure_.clear();
    }
    void press_title() {}

    void mouse(const maya::MouseEvent& m) {
        const int x = m.x.value, y = m.y.value;
        const bool press_ = m.kind == maya::MouseEventKind::Press && m.button == maya::MouseButton::Left;
        if (m.button == maya::MouseButton::ScrollUp || m.button == maya::MouseButton::ScrollDown) {
            sheet_.wheel(m.button == maya::MouseButton::ScrollUp ? -3 : 3);
            return;
        }
        if (m.kind == maya::MouseEventKind::Move && m.button == maya::MouseButton::None) {
            sheet_.hover(x - body_x_, y - body_y_);
            return;
        }
        if (!press_) return;
        if (y == y_ && x >= x_ + w_ - 5 && x < x_ + w_ - 2) { open_ = false; return; }
        for (std::size_t i = 0; i < tab_x_.size(); ++i)
            if (y == y_ + 2 && x >= tab_x_[i].first && x < tab_x_[i].second) { tab_ = static_cast<Tab>(i); note_ = {}; return; }
        if (y == bottom_y_ && page_ == P_MAIN) {
            if (x >= x_ + 2 && x < other_x1_) { press(I_OTHER); return; }
            if (close_x0_ >= 0 && x >= close_x0_ && x < close_x0_ + 9) { open_ = false; return; }
        }
        build();
        if (sheet_.click(x - body_x_, y - body_y_) == Sheet::Outcome::Pressed) {
            const int id = sheet_.pressed();
            if (!sure_.empty() && sure_.back() != id) sure_.clear();
            press(id);
        }
    }

    void paste(std::string_view s) { sheet_.paste(s); }
};

// ── the windows.hpp face ────────────────────────────────────────────────

Bouncer::Bouncer() : w_(std::make_unique<BouncerWin>()) {}
Bouncer::~Bouncer() = default;
void Bouncer::open(int from) { w_->open(from); }
void Bouncer::open_setup(int from) { w_->open(from); w_->open_setup(); w_->from_open_setup_only_ = true; }
bool Bouncer::is_open() const { return w_->open_; }
void Bouncer::close() { w_->open_ = false; }
void Bouncer::key(const maya::KeyEvent& k) { w_->key(k); }
void Bouncer::mouse(const maya::MouseEvent& m) { w_->mouse(m); }
void Bouncer::paste(std::string_view s) { w_->paste(s); }
bool Bouncer::tick() { return w_->open_ && w_->tick(); }
maya::Element Bouncer::element(int w, int h) const { return w_->element(w, h); }

}  // namespace nx::tui
