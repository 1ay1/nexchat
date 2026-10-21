// Unit tests for nexchat's TUI widgets. No terminal: rows are checked as
// text (and a few styles), and the full layout through maya's
// render_to_string. Run: meson test -C build tui-widgets

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <type_traits>

#include <maya/dsl.hpp>
#include <maya/print.hpp>
#include <maya/style/schemes.hpp>

#include "bars.hpp"
#include "chantree.hpp"
#include "chat.hpp"
#include "inputline.hpp"
#include "ircfmt.hpp"
#include "layout.hpp"
#include "theme.hpp"
#include "rich.hpp"
#include "switcher.hpp"
#include "userlist.hpp"

using namespace nx::tui;

static int failures = 0;

template <class T> static std::string show(const T& v) {
    if constexpr (std::is_arithmetic_v<T>) return std::to_string(v);
    else return std::string(v);
}

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { \
    std::fprintf(stderr, "%s:%d: FAIL %s == %s\n   got: [%s]\n  want: [%s]\n", __FILE__, __LINE__, #a, #b, \
                 show(_a).c_str(), show(_b).c_str()); ++failures; } } while (0)

static maya::KeyEvent ch(char32_t c, bool ctrl = false, bool alt = false) {
    return maya::KeyEvent{maya::CharKey{c}, {.ctrl = ctrl, .alt = alt}, ""};
}
static maya::KeyEvent sk(maya::SpecialKey k, bool ctrl = false, bool alt = false, bool shift = false) {
    return maya::KeyEvent{k, {.ctrl = ctrl, .alt = alt, .shift = shift}, ""};
}
static void type(InputLine& in, std::string_view s) { for (char c : s) in.key(ch(static_cast<unsigned char>(c))); }

// ── Rich ────────────────────────────────────────────────────────────────

static void test_rich() {
    Rich r{"héllo 世界"};
    CHECK_EQ(r.width(), 10);
    CHECK_EQ(r.take(7).text, std::string("héllo "));     // 世 would straddle col 7
    CHECK_EQ(r.take(8).text, std::string("héllo 世"));
    CHECK_EQ(r.fit(5).text, std::string("héll…"));
    CHECK_EQ(r.fit(12).width(), 12);
    CHECK_EQ(Rich{"ab"}.pad_left(4).text, std::string("  ab"));

    Rich s;
    s.append("a", Style{}.with_bold());
    s.append("b", Style{}.with_bold());
    s.append("c");
    CHECK_EQ(s.runs.size(), std::size_t{2});              // same style merges

    auto rows = wrap(Rich{"the quick brown fox jumps"}, 10);
    CHECK_EQ(rows.size(), std::size_t{3});
    CHECK_EQ(rows[0].text, std::string("the quick"));
    CHECK_EQ(rows[1].text, std::string("brown fox"));
    CHECK_EQ(rows[2].text, std::string("jumps"));
    auto longw = wrap(Rich{"abcdefghijkl"}, 5);
    CHECK_EQ(longw.size(), std::size_t{3});
    CHECK_EQ(longw[2].text, std::string("kl"));
    CHECK_EQ(wrap(Rich{""}, 5).size(), std::size_t{1});
    CHECK_EQ(wrap(Rich{"世界世界"}, 3)[0].text, std::string("世"));
}

// ── IRC formatting ──────────────────────────────────────────────────────

static void test_ircfmt() {
    auto r = parse_irc("plain \002bold\002 \00304red\003 back \00312,01blue on black\017 reset");
    CHECK_EQ(r.text, std::string("plain bold red back blue on black reset"));
    CHECK(r.runs.size() >= 6);
    // "bold" is bold
    CHECK(r.runs[1].style.bold);
    CHECK_EQ(r.text.substr(r.runs[1].byte_offset, r.runs[1].byte_length), std::string("bold"));
    // "red" has mIRC 4's colour
    const auto& red = r.runs[3];
    CHECK_EQ(r.text.substr(red.byte_offset, red.byte_length), std::string("red"));
    CHECK(red.style.fg.has_value() && *red.style.fg == irc_color(4));
    // bg survives with the fg
    bool found_bg = false;
    for (auto& run : r.runs) if (run.style.bg && *run.style.bg == irc_color(1)) found_bg = true;
    CHECK(found_bg);
    // the last run ("reset") is plain
    CHECK(!r.runs.back().style.fg.has_value() && !r.runs.back().style.bold);

    CHECK_EQ(strip_irc("\0034,5x\003,y"), std::string("x,y"));       // bare \003 then a comma: literal
    CHECK_EQ(strip_irc("\00399default"), std::string("default"));
    CHECK_EQ(strip_irc("\004ff8800hex\004 \004zz"), std::string("hex zz"));
    auto hex = parse_irc("\004FF8800x");
    CHECK(hex.runs[0].style.fg && *hex.runs[0].style.fg == irc_color(7));   // orange: the theme's
    CHECK_EQ(strip_irc("a\010hidden\010b\007"), std::string("ahiddenb"));
    auto rev = parse_irc("\026x");
    CHECK(rev.runs[0].style.inverse);

    auto [l, rr] = split_columns("<alice>\thello");
    CHECK_EQ(std::string(l), std::string("<alice>"));
    CHECK_EQ(std::string(rr), std::string("hello"));
    CHECK(split_columns("no tab").left.empty());

    // The same nick colours as common/text.c text_color_of: "alice" sums
    // to 510, 510 % 9 = 6, rcolors[6] = 27.
    CHECK_EQ(nick_color_index("alice"), 27);
}

// ── theme ──────────────────────────────────────────────────────────────────────

static void test_theme() {
    using maya::Color;
    // Roles are slots, never literals: they follow whatever theme is live.
    CHECK(role::muted().fg->kind() == Color::Kind::Slot);
    CHECK(role::selected().bg->kind() == Color::Kind::Slot);

    // native: mIRC colours are the terminal's ANSI ones.
    maya::theme::set_live(maya::theme::native);
    CHECK(irc_color(4) == Color{maya::theme::native.error});
    CHECK(irc_color(20) == irc_color(4));                          // HexChat's copy of 4
    CHECK(irc_color(52) == irc_color(4));                          // extended bright red: the theme's red
    CHECK(irc_color(28) == irc_color(12));
    CHECK(irc_index(40) == 5);                                     // extended dark red: brown
    CHECK(irc_index(98) == 0 && irc_index(88) == 1);               // the grey ramp
    for (int n = 0; n <= 98; ++n) CHECK(irc_index(n) >= 0 && irc_index(n) < 16);
    CHECK(nearest_irc(0x10, 0x10, 0x10) == 1);
    CHECK(nearest_irc(0xff, 0xff, 0xff) == 0);
    CHECK(nearest_irc(0x80, 0x80, 0x80) == 14);
    CHECK(nearest_irc(0xff, 0x20, 0x20) == 4);
    CHECK(nearest_irc(0x20, 0x20, 0xff) == 12);
    CHECK(nearest_irc(0x20, 0xc0, 0x20) == 9);
    {   // the bars are text on the terminal's background: no fill, no reverse
        StatusBar s;
        s.nick = "me";
        for (const auto& run : s.row(30).runs) CHECK(!run.style.bg && !run.style.inverse);
    }

    // a scheme: mIRC colours come from its roles, and follow a switch.
    const maya::Theme& scheme = *maya::theme::schemes[0].theme;   // any scheme that owns its canvas
    CHECK(maya::theme::owns_canvas(scheme));
    maya::theme::set_live(scheme);
    CHECK(irc_color(4) == Color{scheme.error});
    CHECK(irc_color(3) == Color{scheme.success});
    maya::theme::set_live(maya::theme::native);
    CHECK(irc_color(4) == Color{maya::theme::native.error});
}

// ── the theming rules (theme.hpp), on every run every widget draws ─────
//
// Checked on Styles, before maya resolves them: "this fg is the Selection
// slot" is a fact about the style; in the output it's just an RGB that
// two slots might share.

namespace {

using maya::ThemeSlot;

bool is_slot(const std::optional<maya::Color>& c, ThemeSlot s) {
    return c && c->kind() == maya::Color::Kind::Slot && c->theme_slot() == s;
}

// Slots that are ink: text may be drawn in them.
bool ink_slot(ThemeSlot s) {
    switch (s) {
    case ThemeSlot::Primary: case ThemeSlot::Secondary: case ThemeSlot::Accent:
    case ThemeSlot::Success: case ThemeSlot::Error: case ThemeSlot::Warning:
    case ThemeSlot::Info: case ThemeSlot::Text: case ThemeSlot::Muted:
    case ThemeSlot::Link:
        return true;
    case ThemeSlot::Border:          // only for rules / dividers: see below
    default:
        return false;
    }
}

bool is_line_glyph(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t n = utf8_len(static_cast<unsigned char>(s[i]));
        const std::string_view g = s.substr(i, n);
        if (g != "─" && g != "│" && g != " " && g != "╭" && g != "╮" && g != "╰" && g != "╯")
            return false;
        i += n;
    }
    return true;
}

struct RuleCheck {
    const char* where;
    int bad = 0;

    // The IRC projection's colours are resolved from ink slots (theme.cpp);
    // as a style they're literals, so they're recognised by value.
    static bool irc_ink(const maya::Color& c) {
        for (int n = 0; n < 16; ++n) if (c == irc_color(n)) return true;
        return false;
    }

    void fail(const Rich& row, const StyledRun& r, const char* why) {
        if (bad++ < 4)
            std::fprintf(stderr, "  %s: \"%s\" %s\n", where,
                         row.text.substr(r.byte_offset, std::min<std::size_t>(r.byte_length, 24)).c_str(), why);
    }

    void check(const Rich& row, const std::vector<std::pair<std::size_t, std::size_t>>& caret = {}) {
        for (const auto& r : row.runs) {
            const std::string_view txt = std::string_view{row.text}.substr(r.byte_offset, r.byte_length);
            const auto& s = r.style;
            const bool blank = txt.find_first_not_of(' ') == std::string_view::npos;
            bool is_caret = false;
            for (auto [a, n] : caret) is_caret |= (r.byte_offset == a && r.byte_length == n);

            // fg: a theme ink (or Border on a rule glyph), never chrome
            if (s.fg && s.fg->kind() == maya::Color::Kind::Slot) {
                const ThemeSlot fs = s.fg->theme_slot();
                const bool rule_ok = fs == ThemeSlot::Border && is_line_glyph(txt);
                const bool caret_ok = is_caret && fs == ThemeSlot::Background;
                // \026 reverse on a scheme: canvas-coloured text on an ink fill
                const bool reversed_ok = fs == ThemeSlot::Background && s.bg
                    && (s.bg->kind() != maya::Color::Kind::Slot ? irc_ink(*s.bg) : ink_slot(s.bg->theme_slot()));
                if (!ink_slot(fs) && !rule_ok && !caret_ok && !reversed_ok && !blank)
                    fail(row, r, "is drawn in a chrome slot");
            } else if (s.fg && !irc_ink(*s.fg)) {
                fail(row, r, "states a colour that isn't a theme slot");
            }

            // bg: only Selection under Text (or the theme's own pairs from
            // IRC: a message's \003fg,bg, reverse video), the caret's
            // Cursor, nothing else
            if (s.bg) {
                if (s.bg->kind() != maya::Color::Kind::Slot) {
                    if (!irc_ink(*s.bg)) fail(row, r, "bg isn't a theme colour");
                }
                else if (is_slot(s.bg, ThemeSlot::Selection) && !is_slot(s.fg, ThemeSlot::Text))
                    fail(row, r, "is on the selection without the text colour");
                else if (is_slot(s.bg, ThemeSlot::Cursor) && !is_caret)
                    fail(row, r, "uses the cursor colour off the caret");
                else if (!is_slot(s.bg, ThemeSlot::Selection) && !is_slot(s.bg, ThemeSlot::Cursor)
                         && !ink_slot(s.bg->theme_slot()))
                    fail(row, r, "has a background the theme doesn't pair with text");
                if (s.fg && *s.fg == *s.bg && !blank) fail(row, r, "is text in its own background colour");
            }

            // attributes: reverse only on native (its caret, and a
            // message's \026: the terminal's own pair, the only one it has)
            if (s.inverse && maya::theme::owns_canvas(maya::theme::live()))
                fail(row, r, "uses reverse video");
        }
    }
};

}  // namespace

static void test_theming_rules() {
    std::vector<std::pair<const char*, const maya::Theme*>> themes{{"native", &maya::theme::native}};
    for (const auto& s : maya::theme::schemes) themes.push_back({s.name, s.theme});

    int total = 0;
    for (auto [name, t] : themes) {
        maya::theme::set_live(*t);
        RuleCheck rc{name};

        // Every IRC colour, as fg and as bg, plus reverse and hex.
        for (int n = 0; n <= 98; ++n) {
            rc.check(parse_irc("\003" + std::to_string(n) + "x"));
            rc.check(parse_irc("\003" + std::to_string(n) + "," + std::to_string((n * 7) % 99) + "x"));
            rc.check(parse_irc("\003" + std::to_string(n) + "\026x"));
        }
        rc.check(parse_irc("\026plain reverse\026 \004ff8800\026hex reverse"));
        rc.check(parse_irc("\00304,05same"));                       // red on brown: one colour in most themes

        // Every widget, every state.
        ChanTree tree;
        tree.set({{1, "Libera", ChanKind::Server}, {2, "#hi", ChanKind::Channel, Activity::Hilight, 2},
                  {3, "#msg", ChanKind::Channel, Activity::Message, 1}, {4, "#data", ChanKind::Channel, Activity::Data},
                  {5, "OFTC", ChanKind::Server, Activity::None, 3, false, true}, {6, "#x", ChanKind::Channel}});
        for (std::uintptr_t k : {1, 2, 3, 4, 5})
            for (bool focus : {false, true}) {
                tree.select_key(k);
                for (auto& row : tree.rows(20, 6, focus)) rc.check(row);
            }

        UserList users;
        users.set({{"owner", '~'}, {"op", '@'}, {"half", '%'}, {"voice", '+'}, {"away", 0, true}, {"me", 0, false, true}});
        for (const char* n : {"owner", "op", "half", "voice", "away", "me"})
            for (bool focus : {false, true}) {
                users.select_nick(n);
                for (auto& row : users.rows(16, 8, focus)) rc.check(row);
            }

        ChatBuffer buf;
        buf.add("\00327<alice>\017\thello \00304red\003 \002bold\002 \026rev", 1700000000, true);
        buf.mark_read();
        buf.add("\00323-->\t\00323bob has joined", 1700000100);
        ChatView view;
        for (auto& row : view.rows(buf, ChatStyle{}, 60, 4)) rc.check(row);

        TopicBar topic{"#nexchat", "+nt", "\00304topic\003 \026rev"};
        rc.check(topic.row(60));
        StatusBar status{"me", "+i", "Libera", 9.0, true, false, "bob is typing", 3, {"^G go to"}};
        rc.check(status.row(80));
        status.connecting = true;
        rc.check(status.row(80));

        InputLine in;
        in.set_prompt(Rich{"me ", role::muted()});
        auto caret_runs = [](const Rich& row) {
            std::vector<std::pair<std::size_t, std::size_t>> c;
            const Style want = role::caret();
            for (auto& r : row.runs) if (r.style == want) c.push_back({r.byte_offset, r.byte_length});
            return c;
        };
        for (std::string text : {"hello", ""})
            for (bool focus : {true, false}) {
                in.set(text, 2);
                const Rich row = in.row(30, focus);
                const auto c = caret_runs(row);
                CHECK(c.size() == (focus ? 1u : 0u));
                rc.check(row, c);
            }

        Switcher sw;
        sw.open({{1, "#nexchat", ChanKind::Channel}, {2, "Libera", ChanKind::Server}}, {"Libera", "Libera"});
        for (auto& row : sw.rows(30)) rc.check(row, caret_runs(row));

        CHECK(rc.bad == 0);
        total += rc.bad;
        if (total > 20) break;                                      // enough to see the problem
    }
    maya::theme::set_live(maya::theme::native);
}

// What reaches the TERMINAL under a scheme: every cell has an explicit
// foreground (the theme's, never the terminal's own, which is whatever the
// user's terminal is and was never contrast-checked against the scheme's
// canvas), and every background is the scheme's.
static void test_scheme_owns_every_cell() {
    const maya::Theme& light = [] () -> const maya::Theme& {
        for (const auto& s : maya::theme::schemes)
            if (std::string_view(s.name) == "Solarized Light") return *s.theme;
        return *maya::theme::schemes[0].theme;
    }();
    maya::theme::set_live(light);
    TopicBar topic{"#nexchat", "", "plain topic text"};
    ChatBuffer buf;
    buf.add("<bob>\tplain message text");
    ChatView view;
    StatusBar status{"me"};
    InputLine in;
    in.set("typed");
    MainParts parts{&topic, nullptr, &buf, &view, nullptr, nullptr, &status, &in};
    using namespace maya::dsl;
    const std::string out = maya::render_to_string_ansi((v(MainLayout{}.element(parts)) | height(8)).build(), 60);
    // Walk the output tracking the SGR state: every printed glyph must have
    // an explicit foreground in force, never the terminal's own.
    int bare = 0;
    bool has_fg = false;
    for (std::size_t i = 0; i < out.size();) {
        if (out[i] == '\x1b' && i + 1 < out.size() && out[i + 1] == '[') {
            const std::size_t m = out.find_first_not_of("0123456789;", i + 2);
            if (m != std::string::npos && out[m] == 'm') {
                const std::string params = out.substr(i + 2, m - i - 2);
                // a reset (0 / empty / 39) clears it; 38;… sets it
                std::size_t a = 0;
                while (a <= params.size()) {
                    std::size_t b = params.find(';', a);
                    if (b == std::string::npos) b = params.size();
                    const std::string p = params.substr(a, b - a);
                    if (p.empty() || p == "0" || p == "39") has_fg = false;
                    else if (p == "38") { has_fg = true; break; }
                    else if ((p.size() == 2 && (p[0] == '3' || p[0] == '9') && p[1] >= '0' && p[1] <= '7')) has_fg = true;
                    a = b + 1;
                }
                i = m + 1;
                continue;
            }
        }
        const char c = out[i];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\x1b' && !has_fg && bare++ < 3) {
            const std::size_t e = out.find('\x1b', i);
            std::fprintf(stderr, "  text in the terminal's own colour: [%s]\n",
                         out.substr(i, std::min<std::size_t>(20, e - i)).c_str());
        }
        ++i;
    }
    CHECK(bare == 0);
    maya::theme::set_live(maya::theme::native);
}

// Every colour that reaches the terminal is one the live theme provides:
// draw the whole window full of colour codes and check each SGR colour
// against the theme's slot values. A widget that states its own colour,
// or a code that slips through unmapped, fails here.
static std::string theme_sgr(const maya::LitColor& c, bool bg);

static void test_only_theme_colours() {
    TopicBar topic{"#nexchat", "+nt", "\00304red \00352ext \004ff8800hex \00312,01pair"};
    ChanTree tree;
    tree.set({{1, "Libera", ChanKind::Server}, {2, "#nexchat", ChanKind::Channel, Activity::Hilight, 2},
              {3, "#a", ChanKind::Channel, Activity::Message, 1}, {4, "#b", ChanKind::Channel, Activity::Data},
              {5, "OFTC", ChanKind::Server, Activity::None, 0, false}});
    ChatBuffer buf;
    for (int n = 0; n <= 98; ++n)
        buf.add("\003" + std::to_string(n) + "<n" + std::to_string(n) + ">\t\003" + std::to_string(n) +
                "," + std::to_string(98 - n) + "colour " + std::to_string(n), 1700000000 + n);
    buf.add("*\t\004123456a \004fedcba,010203b \004ff00ffc");
    buf.mark_read();
    buf.add("*\tafter the marker");
    ChatView view;
    ChatStyle cs;
    UserList users;
    users.set({{"owner", '~'}, {"op", '@'}, {"half", '%'}, {"voice", '+'}, {"away", 0, true}});
    users.select_nick("op");
    StatusBar status{"me", "+i", "Libera", 9.0, true, false, "x is typing", 3, {"^G go to"}};
    InputLine input;
    input.set_prompt(Rich{"> "});
    input.set("hello");
    Switcher sw;
    sw.open({{1, "#nexchat", ChanKind::Channel}, {2, "#nex", ChanKind::Channel}}, {"Libera", "Libera"});
    MainParts parts{&topic, &tree, &buf, &view, &cs, &users, &status, &input, &sw, Pane::Tree};

    std::vector<const maya::Theme*> themes{&maya::theme::native};
    for (std::size_t i = 0; i < std::size(maya::theme::schemes); i += 97) themes.push_back(maya::theme::schemes[i].theme);

    for (const maya::Theme* t : themes) {
        maya::theme::set_live(*t);
        std::set<std::string> allowed;
#define X(f, E) allowed.insert(theme_sgr(t->f, false)); allowed.insert(theme_sgr(t->f, true));
        MAYA_THEME_SLOTS(X)
#undef X
        using namespace maya::dsl;
        const std::string out = maya::render_to_string_ansi((v(MainLayout{}.element(parts)) | height(40)).build(), 140);

        // Walk every SGR and pull out the colour parameters.
        int bad = 0;
        for (std::size_t i = out.find("\x1b["); i != std::string::npos; i = out.find("\x1b[", i + 2)) {
            const std::size_t m = out.find_first_not_of("0123456789;", i + 2);
            if (m == std::string::npos || out[m] != 'm') continue;
            std::vector<int> p;
            for (std::size_t a = i + 2; a < m;) {
                std::size_t b = out.find(';', a);
                if (b == std::string::npos || b > m) b = m;
                p.push_back(a == b ? 0 : std::stoi(out.substr(a, b - a)));
                a = b + 1;
            }
            for (std::size_t k = 0; k < p.size(); ++k) {
                std::string got;
                const int x = p[k];
                if ((x == 38 || x == 48) && k + 4 < p.size() && p[k + 1] == 2) {
                    got = (x == 48 ? "bg " : "fg ") + std::to_string(p[k + 2]) + "," + std::to_string(p[k + 3]) + "," + std::to_string(p[k + 4]);
                    k += 4;
                } else if ((x == 38 || x == 48) && k + 2 < p.size() && p[k + 1] == 5) {
                    got = (x == 48 ? "bg i" : "fg i") + std::to_string(p[k + 2]);
                    k += 2;
                } else if ((x >= 30 && x <= 37) || (x >= 90 && x <= 97)) {
                    got = "fg n" + std::to_string(x >= 90 ? x - 90 + 8 : x - 30);
                } else if ((x >= 40 && x <= 47) || (x >= 100 && x <= 107)) {
                    got = "bg n" + std::to_string(x >= 100 ? x - 100 + 8 : x - 40);
                } else {
                    continue;                                       // attributes, 39/49 defaults
                }
                if (!allowed.count(got) && bad++ < 3)
                    std::fprintf(stderr, "  colour %s isn't in the theme: [%s]\n", got.c_str(),
                                 out.substr(m + 1, std::min<std::size_t>(24, out.find('\x1b', m) - m - 1)).c_str());
            }
        }
        CHECK(bad == 0);
    }
    maya::theme::set_live(maya::theme::native);
}

static std::string theme_sgr(const maya::LitColor& c, bool bg) {
    const std::string side = bg ? "bg " : "fg ";
    switch (c.kind()) {
    case maya::LitColor::Kind::Rgb:
        return side + std::to_string(c.r()) + "," + std::to_string(c.g()) + "," + std::to_string(c.b());
    case maya::LitColor::Kind::Named:   return side + "n" + std::to_string(c.r());
    case maya::LitColor::Kind::Indexed: return side + "i" + std::to_string(c.r());
    default:                            return side + "default";
    }
}

// ── chat buffer + view ──────────────────────────────────────────────────

static std::string joined(const std::vector<Rich>& rows) {
    std::string s;
    for (auto& r : rows) { s += r.text; s += '\n'; }
    return s;
}

static void test_chat() {
    {
        // a time only where it changes (stamp_repeat off); every line with it on
        ChatStyle ss;
        ss.show_stamp = true;
        ss.stamp_format = "%H:%M";
        ChatBuffer b;
        const std::time_t t0 = std::time(nullptr) / 3600 * 3600;   // on the hour
        b.add("<a>\tone", t0);
        b.add("<b>\ttwo", t0 + 10);       // same minute
        b.add("<a>\tthree", t0 + 120);    // two minutes on
        ChatView v;
        auto r = v.rows(b, ss, 70, 3);
        CHECK(r[0].text.find(':') == 2);                          // "HH:MM"
        CHECK(r[1].text.rfind("     ", 0) == 0);                  // blank: same minute
        CHECK(r[2].text.find(':') == 2);                          // a new minute shows
        ss.stamp_repeat = true;
        r = v.rows(b, ss, 70, 3);
        CHECK(r[1].text.find(':') == 2);                          // every line
        // the same person twice in a row: the second line's nick is blank
        ChatBuffer run;
        run.add("<a>\tone", t0);
        run.add("<a>\ttwo", t0 + 30);
        run.add("<b>\tthree", t0 + 40);
        ChatStyle rs;
        rs.show_stamp = false;
        rs.nick_width = 4;
        ChatView rv;
        auto rr = rv.rows(run, rs, 50, 3);
        CHECK(rr[0].text.rfind("   a │ one", 0) == 0);
        CHECK(rr[1].text.rfind("     │ two", 0) == 0);
        CHECK(rr[2].text.rfind("   b │ three", 0) == 0);
    }
    {
        // a day separator before the first line of a new day; none for a
        // fresh buffer that starts today, none twice for the same day
        ChatStyle ds;
        ds.show_stamp = false;
        ChatBuffer b;
        const std::time_t now = std::time(nullptr);
        b.add_day_if_new(now);
        CHECK_EQ(b.size(), std::size_t{0});
        b.add("<a>\ttoday", now);
        const std::time_t old = now - 3 * 86400;
        ChatBuffer h;
        h.add_day_if_new(old);
        CHECK_EQ(h.size(), std::size_t{1});                       // history from the past: says when
        h.add("<a>\tthen", old);
        h.add_day_if_new(old + 60);
        CHECK_EQ(h.size(), std::size_t{2});                       // same day: nothing
        h.add_day_if_new(now);
        h.add("<a>\tnow", now);
        CHECK_EQ(h.size(), std::size_t{4});
        ChatView hv;
        auto r = hv.rows(h, ds, 50, 4);
        CHECK(r[0].text.find("─ ") != std::string::npos && r[0].text.find(" ─") != std::string::npos);
        CHECK_EQ(r[0].width(), 50);
        CHECK(r[2].text.find("─") == 0);                      // centred: rules on both sides
    }
    ChatStyle st;
    st.show_stamp = false;
    st.nick_width = 6;
    st.brackets = true;   // the columns below are counted with <> (the GUI's)
    st.fit_nicks = false; // a fixed 6-column nick column

    ChatBuffer buf(5);
    for (int i = 1; i <= 3; ++i) buf.add("<n" + std::to_string(i) + ">\tline " + std::to_string(i) + "\n");
    ChatView view;
    auto rows = view.rows(buf, st, 40, 5);
    CHECK_EQ(rows.size(), std::size_t{5});
    CHECK(rows[0].text.empty() && rows[1].text.empty());            // short buffer sits at the bottom
    CHECK_EQ(rows[2].text, std::string("  <n1> │ line 1"));
    CHECK_EQ(rows[4].text, std::string("  <n3> │ line 3"));
    {
        // fitted (the default): the column is as wide as the widest nick
        ChatStyle fs = st;
        fs.fit_nicks = true;
        fs.nick_width = 14;
        auto fr = ChatView{}.rows(buf, fs, 40, 5);
        CHECK_EQ(fr[2].text, std::string("<n1> │ line 1"));
        ChatBuffer lb;
        lb.add("<a>\tx");
        lb.add("<averyveryverylongnickname>\ty");
        auto lr = ChatView{}.rows(lb, fs, 60, 2);
        CHECK(lr[0].text.rfind(std::string(11, ' ') + "<a> │ x", 0) == 0);   // capped at nick_width (14)
    }

    // Wrapping hangs under the text column (st.measure caps the text: 24).
    st.measure = 24;
    buf.add("<bob>\tone two three four five six seven eight nine ten eleven");
    rows = view.rows(buf, st, 60, 6);
    CHECK_EQ(rows[3].text, std::string(" <bob> │ one two three four five"));
    CHECK_EQ(rows[4].text, std::string("       │ six seven eight nine"));
    CHECK_EQ(rows[5].text, std::string("       │ ten eleven"));
    st.measure = 110;

    // Long nicks are cut with an ellipsis. (6 lines in 6 rows overflow
    // now: the scrollbar column takes the last cell.)
    buf.add("<averylongnick>\thi");
    rows = view.rows(buf, st, 40, 6);
    CHECK(rows.back().text.rfind("<aver… │ hi", 0) == 0);

    // The cap trims old lines.
    CHECK_EQ(buf.size(), std::size_t{5});
    buf.add("*\tsixth");
    CHECK_EQ(buf.size(), std::size_t{5});
    CHECK_EQ(buf[0].right, std::string("line 2"));
    buf.trim(-1);

    // Scroll up holds still while lines arrive.
    ChatBuffer big(1000);
    for (int i = 0; i < 50; ++i) big.add("*\tmsg " + std::to_string(i));
    ChatView v2;
    (void)v2.rows(big, st, 40, 10);
    v2.scroll(big, 15);
    CHECK(!v2.following());
    const std::string before = joined(v2.rows(big, st, 40, 10));
    CHECK(before.find("msg 34") != std::string::npos);
    // the scrollbar: a thumb (▐) in the last column, not at the bottom
    CHECK(before.find("▐") != std::string::npos);
    {
        auto r = v2.rows(big, st, 40, 10);
        CHECK(r.back().text.find("▐") == std::string::npos);
    }
    big.add("*\tnew one");
    // nothing on screen moves; the thumb may shrink a cell as the buffer grows
    auto strip = [](std::string s) {
        for (std::size_t p; (p = s.find("▐")) != std::string::npos;) s.replace(p, 3, " ");
        return s;
    };
    CHECK_EQ(strip(joined(v2.rows(big, st, 40, 10))), strip(before));
    CHECK_EQ(v2.lines_below(big), std::size_t{16});
    v2.scroll(big, -1000);
    CHECK(v2.following());
    CHECK(joined(v2.rows(big, st, 40, 10)).find("new one") != std::string::npos);

    // Scrolling past the top clamps to a full first screen.
    v2.scroll(big, 100000);
    auto top = v2.rows(big, st, 40, 10);
    CHECK(top[0].text.find("msg 0") != std::string::npos);
    CHECK(top[9].text.find("msg 9") != std::string::npos);

    // The read marker sits above the first unread line.
    ChatBuffer mb;
    mb.add("*\told");
    mb.mark_read();
    CHECK(!mb.marker_index());                                       // nothing new yet
    mb.add("*\tnew");
    CHECK(mb.marker_index() == std::optional<std::size_t>{1});
    ChatView v3;
    auto mrows = v3.rows(mb, st, 20, 3);
    CHECK(mrows[1].text.find("────") == 0);
    CHECK(mrows[2].text.find("new") != std::string::npos);

    // Stamps use the format's width.
    ChatStyle ts;
    ts.nick_width = 4;
    ChatBuffer sb;
    sb.add("*\tx", 1700000000);
    auto srows = ChatView{}.rows(sb, ts, 60, 1);
    CHECK(srows[0].text.find("• │ x") != std::string::npos);   // a system line's * is a dot
    CHECK(srows[0].text[0] == '[');

    // Responsive: the time goes before the nick gives way; then stacked.
    {
        ChatStyle rs;
        rs.stamp_format = "%H:%M";
        rs.nick_width = 10;
        ChatBuffer rb;
        rb.add("<alice>\thello there", 1700000000);
        auto wide = ChatView{}.rows(rb, rs, 70, 1);
        CHECK(wide[0].text.find(':') == 2);                          // room: the time shows
        auto mid = ChatView{}.rows(rb, rs, 52, 1);
        CHECK(mid[0].text.find(':') == std::string::npos);           // tight: the time goes first
        CHECK(mid[0].text.find("alice │ hello") != std::string::npos);
        auto tiny = ChatView{}.rows(rb, rs, 30, 3);
        // narrow: stacked, the nick (and its time) over the message
        CHECK(tiny[1].text.rfind("alice", 0) == 0);
        CHECK(tiny[2].text.rfind("  hello there", 0) == 0);
    }

    // Too narrow for columns: flush left.
    auto nrows = ChatView{}.rows(buf, st, 12, 2);
    CHECK(nrows[1].text.rfind("<aver", 0) == 0 || nrows[1].text.find("hi") != std::string::npos);

    // trim / clear
    buf.trim(-1);
    CHECK_EQ(buf.back().right, std::string("one two three four five six seven eight nine ten eleven"));
    buf.trim(0);
    CHECK(buf.empty());
}

// ── channel tree ────────────────────────────────────────────────────────

static void test_chantree() {
    ChanTree t;
    t.set({
        {1, "Libera", ChanKind::Server},
        {2, "#nexchat", ChanKind::Channel, Activity::Message, 3},
        {3, "#gtk", ChanKind::Channel, Activity::Hilight, 1},
        {4, "alice", ChanKind::Query},
        {5, "OFTC", ChanKind::Server, Activity::None, 0, false},
        {6, "#debian", ChanKind::Channel, Activity::Data},
    });
    auto rows = t.rows(16, 7);
    CHECK_EQ(rows[0].text, std::string("▌▾ Libera       "));   // the current row: ▌
    CHECK_EQ(rows[1].text, std::string("   #nexchat   3 "));
    CHECK_EQ(rows[2].text, std::string("   #gtk      ●1 "));
    CHECK_EQ(rows[6].text, std::string("                "));
    CHECK(*t.selected() == 1);

    t.move(1);
    CHECK(*t.selected() == 2);
    t.move(-2);
    CHECK(*t.selected() == 6);                                      // wraps
    CHECK(t.next_active());
    CHECK(*t.selected() == 3);                                      // the highlight first

    t.select_key(1);
    t.toggle_collapse();
    rows = t.rows(16, 7);
    CHECK_EQ(rows[0].text, std::string("▌▸ Libera    ●4 "));        // summed, the worst activity
    CHECK_EQ(rows[1].text, std::string(" ▾ OFTC         "));
    CHECK(t.key_at_row(2) == std::optional<std::uintptr_t>{6});

    // the selection follows its key across set()
    t.select_key(6);
    t.set({{9, "x", ChanKind::Server}, {6, "#debian", ChanKind::Channel}});
    CHECK(*t.selected() == 6);
}

// ── user list ───────────────────────────────────────────────────────────

static void test_userlist() {
    UserList u;
    u.set({{"ChanServ", '@'}, {"alice", '@'}, {"bob", '+'}, {"carol", 0, true}, {"me", 0, false, true}});
    auto rows = u.rows(12, 4);
    CHECK_EQ(rows[0].text, std::string(" 5 users · …"));
    CHECK_EQ(rows[1].text, std::string(" @ChanServ ▐"));          // a small channel: flat, a scrollbar
    CHECK(u.nick_at_row(1) == std::optional<std::string>{"ChanServ"});
    CHECK(!u.selected());
    CHECK(u.find("CA"));
    CHECK_EQ(u.selected()->nick, std::string("carol"));
    rows = u.rows(12, 4);                                           // scrolls to the selection
    CHECK(u.nick_at_row(3) == std::optional<std::string>{"carol"});
    {
        // a big one: grouped by rank, headings that aren't nicks
        UserList big;
        std::vector<UserEntry> all{{"op1", '@'}, {"op2", '@'}};
        for (int i = 0; i < 14; ++i) all.push_back({"m" + std::to_string(i)});
        big.set(all);
        auto br = big.rows(16, 8);
        CHECK(br[1].text.rfind(" OPERATORS", 0) == 0);
        CHECK(!big.nick_at_row(1));
        CHECK(big.nick_at_row(2) == std::optional<std::string>{"op1"});
        CHECK(br[4].text.rfind(" MEMBERS", 0) == 0);
    }
    {
        // one group only: no headings
        UserList plain;
        plain.set({{"a"}, {"b"}});
        auto pr = plain.rows(12, 4);
        CHECK_EQ(pr[1].text, std::string("  a         "));
    }
    u.set({{"carol"}, {"dave"}});
    CHECK_EQ(u.selected()->nick, std::string("carol"));             // kept by nick
}

// ── input line ──────────────────────────────────────────────────────────

static void test_input() {
    using maya::SpecialKey;
    InputLine in;
    type(in, "hello world");
    CHECK_EQ(in.text(), std::string("hello world"));
    in.key(ch('w', true));                                          // Ctrl-W
    CHECK_EQ(in.text(), std::string("hello "));
    in.key(ch('y', true));                                          // Ctrl-Y
    CHECK_EQ(in.text(), std::string("hello world"));
    in.key(sk(SpecialKey::Left, true));                             // Ctrl-←
    CHECK_EQ(in.cursor(), std::size_t{6});
    in.key(ch('k', true));
    CHECK_EQ(in.text(), std::string("hello "));
    in.key(ch('a', true));
    in.key(ch('d', true));
    CHECK_EQ(in.text(), std::string("ello "));
    in.key(ch('e', true));
    in.key(ch(U'é'));
    in.key(ch(U'世'));
    CHECK_EQ(in.text(), std::string("ello é世"));
    in.key(sk(SpecialKey::Backspace));
    CHECK_EQ(in.text(), std::string("ello é"));
    in.key(sk(SpecialKey::Left));
    CHECK_EQ(in.cursor(), std::size_t{5});                          // before é, never mid-codepoint
    in.key(ch('t', true));                                          // transpose " " and é
    CHECK_EQ(in.text(), std::string("elloé "));

    // Kills append: two Ctrl-W's yank back as one.
    in.set("a b c");
    in.key(ch('w', true));
    in.key(ch('w', true));
    in.key(ch('y', true));
    CHECK_EQ(in.text(), std::string("a b c"));

    // History keeps the draft.
    in.clear();
    in.commit_history("first");
    in.commit_history("second");
    in.commit_history("second");                                    // no duplicate
    type(in, "draft");
    in.key(sk(SpecialKey::Up));
    CHECK_EQ(in.text(), std::string("second"));
    in.key(sk(SpecialKey::Up));
    CHECK_EQ(in.text(), std::string("first"));
    in.key(sk(SpecialKey::Up));
    CHECK_EQ(in.text(), std::string("first"));
    in.key(sk(SpecialKey::Down));
    in.key(sk(SpecialKey::Down));
    CHECK_EQ(in.text(), std::string("draft"));

    // Unused keys go back to the owner.
    CHECK(in.key(sk(SpecialKey::PageUp)) == InputLine::Result::Unhandled);
    CHECK(in.key(sk(SpecialKey::Enter)) == InputLine::Result::Unhandled);
    CHECK(in.key(ch('1', false, true)) == InputLine::Result::Unhandled);

    // Paste flattens newlines.
    in.clear();
    in.paste("a\nb");
    CHECK_EQ(in.text(), std::string("a b"));

    // The row scrolls to keep the caret visible.
    in.set(std::string(50, 'x') + "END");
    in.set_prompt(Rich{"> "});
    Rich row = in.row(20);
    CHECK_EQ(row.width(), 20);
    CHECK(row.text.find("END") != std::string::npos);
    CHECK(row.text.find("…") != std::string::npos);

    // Tab completion through a WordCompleter.
    InputLine c;
    WordCompleter wc([](std::string_view w, bool) {
        std::vector<std::string> all = {"alice", "alfred", "bob", "/join"}, out;
        for (auto& n : all) if (n.rfind(std::string(w), 0) == 0) out.push_back(n);
        return out;
    });
    c.set_completer([&](std::string_view l, std::size_t cur, int step) { return wc(l, cur, step); });
    type(c, "al");
    c.key(sk(SpecialKey::Tab));
    CHECK_EQ(c.text(), std::string("alice: "));
    c.key(sk(SpecialKey::Tab));
    CHECK_EQ(c.text(), std::string("alfred: "));
    c.key(sk(SpecialKey::BackTab));
    CHECK_EQ(c.text(), std::string("alice: "));
    type(c, "hi b");
    c.key(sk(SpecialKey::Tab));
    CHECK_EQ(c.text(), std::string("alice: hi bob "));
    c.clear();
    type(c, "/jo");
    c.key(sk(SpecialKey::Tab));
    CHECK_EQ(c.text(), std::string("/join "));
}

// ── bars, switcher, layout ──────────────────────────────────────────────

static void test_bars() {
    TopicBar t{"#nexchat", "+nt", "Welcome \002home\002\nsecond line"};
    auto r = t.row(40);
    CHECK_EQ(r.width(), 40);
    CHECK_EQ(r.text, std::string(" #nexchat   +nt  │  Welcome home second…"));

    StatusBar s;
    s.nick = "alice";
    s.network = "Libera";
    s.lag = 0.25;
    s.hints = {"^P commands"};
    auto sr = s.row(60);
    CHECK_EQ(sr.width(), 60);
    CHECK(sr.text.find(" ● Libera  250 ms") == 0);
    CHECK(sr.text.find("^P commands ") != std::string::npos);
    CHECK(s.row(20).text.find("^P") == std::string::npos);          // hints give way first
}

static void test_switcher() {
    CHECK(Switcher::score("#nexchat", "nex") > Switcher::score("#annexchat", "nex"));
    CHECK(Switcher::score("#nexchat", "nc") > 0);
    CHECK(Switcher::score("#nexchat", "xz") == 0);
    CHECK(Switcher::score("#gtk", "gtk") > Switcher::score("#gtk-dev", "gtk"));

    Switcher sw;
    sw.open({{1, "#nexchat", ChanKind::Channel}, {2, "#gtk", ChanKind::Channel}, {3, "nexbot", ChanKind::Query}},
            {"Libera", "Libera", "OFTC"});
    CHECK(sw.is_open());
    sw.key(ch('g'));
    CHECK(sw.key(sk(maya::SpecialKey::Enter)) == Switcher::Outcome::Picked);
    CHECK(sw.picked() == std::optional<std::uintptr_t>{2});
    sw.open({{1, "#a", ChanKind::Channel}}, {});
    CHECK(sw.key(sk(maya::SpecialKey::Escape)) == Switcher::Outcome::Cancelled);
    CHECK(!sw.is_open());
}

static void test_layout() {
    TopicBar topic{"#nexchat", "", "the topic"};
    ChanTree tree;
    tree.set({{1, "Libera", ChanKind::Server}, {2, "#nexchat", ChanKind::Channel}});
    ChatBuffer buf;
    buf.add("<alice>\thello");
    ChatView view;
    ChatStyle cs;
    cs.show_stamp = false;
    cs.brackets = true;
    UserList users;
    users.set({{"alice", '@'}, {"bob"}});
    StatusBar status;
    status.nick = "bob";
    InputLine input;
    input.set_prompt(Rich{"> "});

    MainParts parts{&topic, &tree, &buf, &view, &cs, &users, &status, &input};
    MainLayout layout;
    // render_to_string sizes a fill() to its content; pin the height.
    auto frame = [&](int w) {
        using namespace maya::dsl;
        return maya::render_to_string((v(layout.element(parts)) | height(8)).build(), w);
    };

    std::string wide = frame(120);
    CHECK(wide.find("#nexchat  │  the topic") != std::string::npos);
    CHECK(wide.find("▾ Libera") != std::string::npos);
    CHECK(wide.find("<alice> │ hello") != std::string::npos);
    CHECK(wide.find("2 users") != std::string::npos);
    CHECK(wide.find("> ") != std::string::npos);

    std::string mid = frame(80);                                    // no user list
    CHECK(mid.find("2 users") == std::string::npos);
    CHECK(mid.find("▾ Libera") != std::string::npos);

    std::string narrow = frame(50);                                 // chat only
    CHECK(narrow.find("▾ Libera") == std::string::npos);
    CHECK(narrow.find("<alice> │ hello") != std::string::npos);
}

// ── what a line is, and the marks people type ───────────────────────────

static void test_readability() {
    ChatStyle st;
    st.show_stamp = false;
    auto style_of = [&](const ChatLine& L, std::string_view word) {
        const auto& rows = line_rows(L, st, 100);
        for (const auto& r : rows) {
            const auto at = r.text.find(word);
            if (at == std::string::npos) continue;
            for (const auto& run : r.runs)
                if (at >= run.byte_offset && at < run.byte_offset + run.byte_length) return run.style;
        }
        return Style{};
    };
    ChatBuffer buf(100);
    buf.add("alice\tthis is *really* it, `make test`, _so_ 2*3*4 snake_case a * b", 0, false, ChatLine::Kind::Message);
    const ChatLine& m = buf[0];
    CHECK(style_of(m, "really").bold);
    CHECK(style_of(m, "so_").italic);
    CHECK(style_of(m, "make test").fg.has_value());
    CHECK(!style_of(m, "3*4").bold);                    // not inside a word
    CHECK(!style_of(m, "case").italic);                 // snake_case
    CHECK(line_rows(m, st, 100)[0].text.find("*really*") != std::string::npos);   // the marks stay
    st.markup = false;
    CHECK(!style_of(m, "really").bold);
    st.markup = true;

    // what happened goes muted, what people said doesn't
    buf.add("•\tbob gives voice to carol", 0, false, ChatLine::Kind::Status);
    buf.add("bob\thello there", 0, false, ChatLine::Kind::Message);
    const Style status = style_of(buf[1], "gives voice");
    CHECK(status.fg.has_value() && *status.fg == *role::muted().fg);
    CHECK(!style_of(buf[2], "hello there").fg.has_value());
    st.quiet_status = false;
    CHECK(!style_of(buf[1], "gives voice").fg.has_value());
    st.quiet_status = true;
    // a /me reads as narration
    buf.add("•\talice waves", 0, false, ChatLine::Kind::Action);
    CHECK(style_of(buf[3], "waves").italic);

    // your nick where someone says it, whole words only, any case
    st.nick = "Poke";
    buf.add("bob\they poke, pokemon and poke_ aren't you", 0, false, ChatLine::Kind::Message);
    CHECK(style_of(buf[4], "poke,").fg.has_value());
    CHECK(!style_of(buf[4], "pokemon").fg.has_value());
    buf.add("Poke\tI said poke", 0, false, ChatLine::Kind::Own);
    CHECK(!style_of(buf[5], "poke").fg.has_value());     // not in your own lines

    // a blank row where nobody spoke for a while
    ChatBuffer talk(100);
    talk.add("a\tone", 1000);
    talk.add("b\ttwo", 1060);
    talk.add("a\tthree", 1060 + 30 * 60);
    ChatView view;
    ChatStyle ps;
    ps.show_stamp = false;
    ps.pause_gap = 20 * 60;
    auto rows = view.rows(talk, ps, 60, 6);
    std::vector<std::string> texts;
    for (auto& r : rows) texts.push_back(r.text);
    int one = -1, two = -1, three = -1;
    for (int i = 0; i < static_cast<int>(texts.size()); ++i) {
        if (texts[i].find("one") != std::string::npos) one = i;
        if (texts[i].find("two") != std::string::npos) two = i;
        if (texts[i].find("three") != std::string::npos) three = i;
    }
    CHECK(two == one + 1);                                 // a minute: no gap
    CHECK(three == two + 2);                               // half an hour: one blank row
    ps.pause_gap = 0;
    rows = view.rows(talk, ps, 60, 6);
    texts.clear();
    for (auto& r : rows) texts.push_back(r.text);
    int t2 = -1, t3 = -1;
    for (int i = 0; i < static_cast<int>(texts.size()); ++i) {
        if (texts[i].find("two") != std::string::npos) t2 = i;
        if (texts[i].find("three") != std::string::npos) t3 = i;
    }
    CHECK(t3 == t2 + 1);
}

int main() {
    test_rich();
    test_ircfmt();
    test_readability();
    test_theme();
    test_only_theme_colours();
    test_theming_rules();
    test_scheme_owns_every_cell();
    test_chat();
    test_chantree();
    test_userlist();
    test_input();
    test_bars();
    test_switcher();
    test_layout();
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("tui widgets: all good");
    return 0;
}
