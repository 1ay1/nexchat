// tui-gallery — every nexchat TUI widget, live, with made-up data.
//
// A hand-written loop over maya::Screen (the same shape the real frontend
// will drive from GLib's main loop): poll the tty, route keys, present.
//
//   type + Enter   say something         Tab       complete a nick
//   PgUp/PgDn      scroll the chat       Ctrl-G    go to (switcher)
//   Alt-↑/↓        previous/next buffer  Alt-A     next active buffer
//   F2 / F3        focus tree / users    Esc       back to the input
//   Ctrl-L         toggle the user list  Ctrl-C    quit
//   Ctrl-T         next maya scheme      Ctrl-R    back to native
//
//   tui-gallery [--theme NAME]   NAME is one of maya::theme::schemes
//                                ("Dracula", "Gruvbox Dark", …) or native

#include <poll.h>
#include <signal.h>
#include <strings.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include <maya/maya.hpp>
#include <maya/screen.hpp>
#include <maya/style/schemes.hpp>

#include "layout.hpp"
#include "theme.hpp"

using namespace nx::tui;

namespace {

volatile sig_atomic_t g_winch = 0;
void on_winch(int) { g_winch = 1; }

struct Buffer {
    std::string name, network;
    ChanKind kind;
    ChatBuffer chat{2000};
    ChatView view;
    TopicBar topic;
    std::vector<UserEntry> users;
    Activity activity = Activity::None;
    int unread = 0;
};

std::string nick_text(std::string_view nick) {
    return "\003" + std::to_string(nick_color_index(nick)) + "<" + std::string(nick) + ">\017";
}

struct Gallery {
    std::vector<Buffer> bufs;
    std::size_t cur = 1;
    ChanTree tree;
    UserList users;
    StatusBar status;
    InputLine input;
    Switcher switcher;
    ChatStyle style;
    MainLayout layout;
    Pane focus = Pane::Input;
    WordCompleter completer{[this](std::string_view w, bool) {
        std::vector<std::string> out;
        for (auto& u : bufs[cur].users)
            if (u.nick.size() >= w.size() && strncasecmp(u.nick.c_str(), std::string(w).c_str(), w.size()) == 0)
                out.push_back(u.nick);
        for (const char* c : {"/join", "/part", "/msg", "/me", "/quit", "/topic"})
            if (std::string_view(c).starts_with(w)) out.push_back(c);
        return out;
    }};
    bool quit = false;
    int tick = 0;
    std::size_t scheme = std::size(maya::theme::schemes);   // native
    std::string theme_name = "native";

    void set_theme(const maya::Theme& t, std::string_view name) {
        maya::app_set_theme(t);
        theme_name = name;
        status.hints = {"theme: " + theme_name, "^T next", "^G go to", "^C quit"};
    }

    Gallery() {
        const std::time_t now = std::time(nullptr);
        auto add = [&](std::string name, std::string net, ChanKind k) -> Buffer& {
            bufs.push_back(Buffer{std::move(name), std::move(net), k});
            return bufs.back();
        };
        Buffer& srv = add("Libera", "Libera", ChanKind::Server);
        srv.chat.add("*\t\00323Connecting to irc.libera.chat (6697)", now - 600);
        srv.chat.add("*\tWelcome to the \002Libera.Chat\002 Internet Relay Chat Network alice", now - 590);

        Buffer& nx = add("#nexchat", "Libera", ChanKind::Channel);
        nx.topic = {"#nexchat", "+nt", "nexchat 0.1.0 is out \00303\002\\o/\017 | https://github.com/1ay1/nexchat | be nice"};
        nx.users = {{"ChanServ", '@'}, {"ayush", '@'}, {"alice", '+', false, true}, {"bob", '+'},
                    {"carol"}, {"dave", 0, true}, {"erin"}, {"frank"}, {"grace"}, {"heidi"}};
        const char* script[][2] = {
            {"ayush", "the TUI is drawn with maya now"},
            {"bob", "nice! does it do \00304c\00307o\00308l\00303o\00312u\00306r\003 codes?"},
            {"ayush", "all sixteen: \00300 00\00301 01\00302 02\00303 03\00304 04\00305 05\00306 06\00307 07"
                       "\00308 08\00309 09\00310 10\00311 11\00312 12\00313 13\00314 14\00315 15\003 "
                       "\00300,01 on black \003 \00301,00 on white \003 \00308,02 yellow on blue \003"},
            {"carol", "and \002bold\002, \035italic\035, \037underline\037 and \026reverse\026"},
            {"ayush", "all of them, plus the \004ff8800hex\004 ones xtext ignores"},
            {"dave", "how does it wrap a really long line, one that goes on and on well past the edge of "
                     "the window so that it has to come back under the text column instead of the nick?"},
            {"erin", "日本語もちゃんと幅2で並ぶ 👍"},
        };
        int t = 0;
        for (auto& l : script) nx.chat.add(nick_text(l[0]) + "\t" + l[1], now - 300 + 30 * t++);
        nx.chat.add("\00323-->\t\00323frank (~frank@example.org) has joined #nexchat", now - 60);
        nx.chat.mark_read();
        nx.chat.add(nick_text("frank") + "\talice: you there?", now - 30, true);

        Buffer& gtk = add("#gtk", "Libera", ChanKind::Channel);
        gtk.topic = {"#gtk", "+Cnt", "GTK: https://gtk.org | ask, don't ask to ask"};
        gtk.users = {{"ebassi", '@'}, {"mclasen", '@'}, {"alice", 0, false, true}};
        for (int i = 0; i < 120; ++i)
            gtk.chat.add(nick_text(i % 2 ? "ebassi" : "mclasen") + "\tbacklog line " + std::to_string(i), now - 4000 + i * 30);
        gtk.activity = Activity::Message;
        gtk.unread = 4;

        Buffer& q = add("bob", "Libera", ChanKind::Query);
        q.chat.add(nick_text("bob") + "\they, got a minute?", now - 20);
        q.activity = Activity::Hilight;
        q.unread = 1;

        Buffer& oftc = add("OFTC", "OFTC", ChanKind::Server);
        oftc.chat.add("*\t\00320Disconnected (Connection reset by peer)", now - 100);
        Buffer& deb = add("#debian", "OFTC", ChanKind::Channel);
        deb.topic = {"#debian", "", "Debian 13 \"trixie\""};
        deb.activity = Activity::Data;

        input.set_prompt(Rich{"alice ", role::muted()});
        input.set_completer([this](std::string_view l, std::size_t c, int s) { return completer(l, c, s); });
        status.nick = "alice";
        status.usermodes = "+Ziw";
        status.hints = {"^G go to", "PgUp scroll", "^C quit"};
        style.stamp_format = "%H:%M";
        sync();
    }

    std::vector<ChanEntry> entries() const {
        std::vector<ChanEntry> e;
        for (std::size_t i = 0; i < bufs.size(); ++i) {
            const Buffer& b = bufs[i];
            e.push_back({i, b.name, b.kind, b.activity, b.unread, b.network != "OFTC" || b.kind != ChanKind::Server});
        }
        return e;
    }

    void sync() {
        Buffer& b = bufs[cur];
        b.activity = Activity::None;
        b.unread = 0;
        tree.set(entries());
        tree.select_key(cur);
        users.set(b.users);
        status.network = b.network;
        status.lag = b.network == "OFTC" ? -1 : 0.08 + (tick % 7) * 0.01;
        status.connecting = b.network == "OFTC";
        status.lines_below = static_cast<int>(b.view.lines_below(b.chat));
    }

    void go(std::size_t i) {
        bufs[cur].chat.mark_read();
        cur = i;
        sync();
    }

    void submit() {
        std::string line = input.text();
        if (line.empty()) return;
        input.commit_history(line);
        input.clear();
        Buffer& b = bufs[cur];
        if (line == "/quit") { quit = true; return; }
        if (line.starts_with("/me "))
            b.chat.add("\00313*\t\00313alice " + line.substr(4), std::time(nullptr));
        else
            b.chat.add(nick_text("alice") + "\t" + line, std::time(nullptr));
        b.view.to_bottom();
    }

    void key(const maya::KeyEvent& k) {
        using maya::SpecialKey;
        const auto* sk = std::get_if<SpecialKey>(&k.key);
        const auto* ck = std::get_if<maya::CharKey>(&k.key);
        if (ck && k.mods.ctrl && ck->codepoint == 'c') { quit = true; return; }

        if (switcher.is_open()) {
            if (switcher.key(k) == Switcher::Outcome::Picked) go(*switcher.picked());
            return;
        }
        Buffer& b = bufs[cur];
        if (sk) switch (*sk) {
            case SpecialKey::PageUp:   b.view.page_up(b.chat); sync(); return;
            case SpecialKey::PageDown: b.view.page_down(b.chat); sync(); return;
            case SpecialKey::F2:       focus = Pane::Tree; return;
            case SpecialKey::F3:       focus = Pane::Users; return;
            case SpecialKey::Escape:   focus = Pane::Input; return;
            case SpecialKey::Up: case SpecialKey::Down:
                if (k.mods.alt) { tree.move(*sk == SpecialKey::Up ? -1 : 1); go(*tree.selected()); return; }
                break;
            default: break;
        }
        if (ck && k.mods.ctrl && ck->codepoint == 'g') {
            std::vector<std::string> nets;
            for (auto& x : bufs) nets.push_back(x.network);
            switcher.open(entries(), nets);
            return;
        }
        if (ck && k.mods.ctrl && ck->codepoint == 'l') { layout.show_users = !layout.show_users; return; }
        if (ck && k.mods.ctrl && (ck->codepoint == 't' || ck->codepoint == 'r')) {
            const std::size_t n = std::size(maya::theme::schemes);
            scheme = ck->codepoint == 'r' ? n : (scheme + 1) % (n + 1);   // n = native
            set_theme(scheme == n ? maya::theme::native : *maya::theme::schemes[scheme].theme,
                      scheme == n ? "native" : maya::theme::schemes[scheme].name);
            return;
        }
        if (ck && k.mods.alt && ck->codepoint == 'a') { if (tree.next_active()) go(*tree.selected()); return; }

        if (focus == Pane::Tree) {
            if (sk && (*sk == SpecialKey::Up || *sk == SpecialKey::Down)) { tree.move(*sk == SpecialKey::Up ? -1 : 1); return; }
            if (sk && *sk == SpecialKey::Enter) { go(*tree.selected()); focus = Pane::Input; return; }
            if (ck && ck->codepoint == ' ') { tree.toggle_collapse(); return; }
        }
        if (focus == Pane::Users) {
            if (sk && (*sk == SpecialKey::Up || *sk == SpecialKey::Down)) { users.move(*sk == SpecialKey::Up ? -1 : 1); return; }
            if (sk && *sk == SpecialKey::Enter && users.selected()) {
                input.set(input.text() + users.selected()->nick + ": ");
                focus = Pane::Input;
                return;
            }
            if (ck && !k.mods.ctrl && !k.mods.alt) { std::string s(1, static_cast<char>(ck->codepoint)); users.find(s); return; }
        }
        if (sk && *sk == SpecialKey::Enter) { submit(); sync(); return; }
        focus = Pane::Input;
        input.key(k);
    }

    // Someone talks now and then, somewhere, so activity shows.
    void on_tick() {
        ++tick;
        if (tick % 4 == 0) {
            const std::size_t i = 2;                                // #gtk
            bufs[i].chat.add(nick_text("ebassi") + "\ta live line, number " + std::to_string(tick / 4), std::time(nullptr));
            if (i != cur) { bufs[i].activity = std::max(bufs[i].activity, Activity::Message); ++bufs[i].unread; }
        }
        if (tick % 3 == 0) status.typing = status.typing.empty() ? "bob is typing…" : "";
        const std::size_t keep = cur;
        tree.set(entries());
        tree.select_key(keep);
        status.lines_below = static_cast<int>(bufs[cur].view.lines_below(bufs[cur].chat));
    }

    maya::Element view() const {
        const Buffer& b = bufs[cur];
        MainParts p;
        p.topic = b.kind == ChanKind::Channel ? &b.topic : nullptr;
        p.tree = &tree;
        p.buffer = &b.chat;
        p.view = &b.view;
        p.chat_style = &style;
        p.users = b.kind == ChanKind::Channel ? &users : nullptr;
        p.status = &status;
        p.input = &input;
        p.switcher = &switcher;
        p.focus = focus;
        return layout.element(p);
    }
};

}  // namespace

int main(int argc, char** argv) {
    const maya::Theme* theme = &maya::theme::native;
    std::string theme_name = "native";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) != "--theme") continue;
        const std::string_view want = argv[i + 1];
        bool found = want == "native";
        for (const auto& s : maya::theme::schemes)
            if (want == s.name) { theme = s.theme; theme_name = s.name; found = true; }
        if (!found) { std::fprintf(stderr, "tui-gallery: no scheme \"%s\"\n", argv[i + 1]); return 2; }
    }
    auto screen = maya::Screen::open({.title = "nexchat — tui gallery", .mouse = false, .theme = *theme});
    if (!screen) {
        std::fprintf(stderr, "tui-gallery: needs a terminal\n");
        return 1;
    }
    Gallery g;
    g.set_theme(*theme, theme_name);
    // maya leaves SIGWINCH to whoever runs the loop (jaal, or us).
    struct sigaction sa{};
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, nullptr);
    auto last_tick = std::chrono::steady_clock::now();
    bool dirty = true;

    while (!g.quit) {
        if (dirty && screen->ready() && !screen->pending_output()) {
            const maya::Presented f = screen->present([&] { return g.view(); });
            dirty = f.redraw_now;
            if (std::getenv("NX_TUI_DEBUG"))
                std::fprintf(stderr, "frame now=%d at=%d bp=%d\n", f.redraw_now, f.redraw_at.has_value(), f.backpressured);
        }
        // The tty took part of a frame: push the rest when it can take more.
        pollfd pfd[2] = {
            {static_cast<int>(screen->input_handle()), POLLIN, 0},
            {static_cast<int>(screen->output_handle()), POLLOUT, 0},
        };
        const bool out = screen->pending_output();
        const int wait = screen->has_pending_input() || (dirty && !out) ? 30 : 1000;
        const int n = ::poll(pfd, out ? 2 : 1, wait);
        if (out && (pfd[1].revents & POLLOUT)) (void)screen->flush();
        if (g_winch) {
            g_winch = 0;
            screen->on_resize();
            dirty = true;
        }
        if (n > 0 && (pfd[0].revents & POLLIN)) {
            if (auto evs = screen->read()) {
                // Frame acks arrive as input but come back as no events:
                // redrawing for them would send another probe, forever.
                if (!evs->empty()) dirty = true;
                for (auto& ev : *evs) {
                    if (auto* k = std::get_if<maya::KeyEvent>(&ev)) g.key(*k);
                    else if (auto* p = std::get_if<maya::PasteEvent>(&ev)) g.input.paste(p->content);
                    else if (std::holds_alternative<maya::ResizeEvent>(ev)) screen->on_resize();
                }
            } else {
                break;                                              // the tty went away
            }
        } else if (screen->has_pending_input()) {
            for (auto& ev : screen->resolve_pending_input())
                if (auto* k = std::get_if<maya::KeyEvent>(&ev)) g.key(*k);
            dirty = true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_tick >= std::chrono::seconds(1)) {
            last_tick = now;
            g.on_tick();
            dirty = true;
        }
    }
    return 0;
}
