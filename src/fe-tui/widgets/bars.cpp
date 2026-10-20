// nx::tui bars — see bars.hpp.

#include "bars.hpp"

#include <cstdio>

#include <maya/dsl.hpp>

#include "ircfmt.hpp"
#include "theme.hpp"

namespace nx::tui {

namespace {

maya::Element fill_row(std::function<Rich(int)> make) {
    return maya::dsl::fill([make = std::move(make)](int w, int) { return make(w).element(); }).build();
}

}  // namespace

Rich TopicBar::row(int w) const {
    // " # nexchat   19 · +nt   HexChat, continued — now on GTK4"
    // the channel big (the # quiet), its people and modes small, the topic
    Rich out;
    out.append(" ");
    std::size_t sig = 0;
    while (sig < channel.size() && (channel[sig] == '#' || channel[sig] == '&' || channel[sig] == '!' || channel[sig] == '+'))
        ++sig;
    if (sig) out.append(channel.substr(0, sig), role::muted().with_bold());
    out.append(channel.substr(sig), role::title());
    Rich meta;
    if (users > 0) {
        meta.append(std::to_string(users), role::text());
        meta.append(users == 1 ? " person" : " people", role::muted());
    }
    if (!modes.empty()) {
        if (!meta.text.empty()) meta.append(" · ", role::muted());
        meta.append(modes, role::muted());
    }
    if (!meta.text.empty()) { out.append("   "); out.append(meta); }
    if (!topic.empty()) {
        out.append("  │  ", role::border());
        // A topic is one line, whatever it holds.
        std::string flat = topic;
        for (char& c : flat) if (c == '\n' || c == '\r') c = ' ';
        out.append(parse_irc(flat));
    } else {
        out.append("  │  ", role::border());
        out.append("no topic", role::muted().with_italic());
    }
    out = out.fit(w);
    return out;
}

maya::Element TopicBar::element() const {
    return fill_row([this](int w) { return row(w); });
}

Rich StatusBar::row(int w) const {
    static constexpr const char* kSpin[10] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const Style dim = role::muted();
    const Rich sep{"  │  ", role::border()};
    spans_.clear();
    auto u = [&](Hot h, Style s) { return hot == h ? s.with_underline() : s; };
    Rich left;
    left.append(" ");
    // the connection: a dot (green, amber when slow or connecting, red when
    // lagging badly or gone), the network, our modes
    Style dot = role::good();
    std::string lag_text;
    if (connecting) { dot = role::warn(); lag_text = "connecting…"; }
    else if (lag < 0) dot = dim;
    else {
        char buf[32];
        if (lag < 1) std::snprintf(buf, sizeof buf, "%d ms", static_cast<int>(lag * 1000 + 0.5));
        else std::snprintf(buf, sizeof buf, "%.1f s", lag);
        lag_text = buf;
        if (lag > 5) dot = role::bad();
        else if (lag > 1) dot = role::warn();
    }
    left.append(connecting ? std::string(kSpin[frame % 10]) + " " : std::string("● "), dot);
    {
        const int x0 = left.width();
        left.append(network.empty() ? "not connected" : network,
                    u(Hot::Network, network.empty() ? dim : role::text().with_bold()));
        spans_.push_back({{x0, left.width()}, Hot::Network});
    }
    if (!usermodes.empty()) left.append(" +" + (usermodes[0] == '+' ? usermodes.substr(1) : usermodes), dim);
    if (!lag_text.empty()) { left.append("  "); left.append(lag_text, connecting ? role::warn() : dim); }
    if (!nick.empty() && away) { left.append(sep); left.append("away", role::warn()); }
    if (lines_below > 0) {
        left.append(sep);
        const int x0 = left.width();
        left.append("↓ " + std::to_string(lines_below) + " more below", u(Hot::Below, role::new_data().with_bold()));
        spans_.push_back({{x0, left.width()}, Hot::Below});
    }
    if (!typing.empty()) { left.append(sep); left.append(typing, dim.with_italic()); }
    if (!typing_live.empty()) {
        left.append(sep);
        left.append(typing_live, dim.with_italic());
        // three dots, one lit at a time, walking
        left.append(" ");
        for (int i = 0; i < 3; ++i) left.append("•", i == frame % 3 ? role::text() : dim);
    }

    // "F10 menu": the key bold, what it does dim; each one clickable
    Rich right;
    std::vector<std::pair<int, int>> hint_x;
    for (std::size_t i = 0; i < hints.size(); ++i) {
        if (i) right.append("   ");
        const Hot h = static_cast<Hot>(static_cast<int>(Hot::Hint0) + static_cast<int>(std::min<std::size_t>(i, 2)));
        const int x0 = right.width();
        const std::string& hnt = hints[i];
        const std::size_t sp = hnt.find(' ');
        right.append(hnt.substr(0, sp), u(h, role::text().with_bold()));
        if (sp != std::string::npos) right.append(hnt.substr(sp), u(h, dim));
        hint_x.push_back({x0, right.width()});
    }
    right.append(" ");

    const int rw = right.width();
    Rich out;
    if (left.width() + rw + 1 <= w) {
        out = left.pad_right(w - rw);
        for (std::size_t i = 0; i < hint_x.size(); ++i)
            spans_.push_back({{w - rw + hint_x[i].first, w - rw + hint_x[i].second},
                              static_cast<Hot>(static_cast<int>(Hot::Hint0) + static_cast<int>(std::min<std::size_t>(i, 2)))});
        out.append(right);
    } else {
        out = left.fit(w);              // hints go first when it's tight
    }
    return out;
}

StatusBar::Hot StatusBar::at(int x) const {
    for (const auto& [span, h] : spans_)
        if (x >= span.first && x < span.second) return h;
    return Hot::None;
}

maya::Element StatusBar::element() const {
    return fill_row([this](int w) { return row(w); });
}

}  // namespace nx::tui
