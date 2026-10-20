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
    Rich out;
    out.append(" ");
    out.append(channel, role::title());
    if (!modes.empty()) { out.append(" "); out.append(modes, role::muted()); }
    if (!topic.empty()) {
        out.append("  │  ", role::border());
        // A topic is one line, whatever it holds.
        std::string flat = topic;
        for (char& c : flat) if (c == '\n' || c == '\r') c = ' ';
        out.append(parse_irc(flat));
    }
    out = out.fit(w);
    return out;
}

maya::Element TopicBar::element() const {
    return fill_row([this](int w) { return row(w); });
}

Rich StatusBar::row(int w) const {
    const Style dim = role::muted();
    const Rich sep{"  │  ", role::border()};
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
    left.append("● ", dot);
    left.append(network.empty() ? "not connected" : network, network.empty() ? dim : role::text().with_bold());
    if (!usermodes.empty()) left.append(" +" + (usermodes[0] == '+' ? usermodes.substr(1) : usermodes), dim);
    if (!lag_text.empty()) { left.append("  "); left.append(lag_text, connecting ? role::warn() : dim); }
    if (!nick.empty() && away) { left.append(sep); left.append("away", role::warn()); }
    if (lines_below > 0) {
        left.append(sep);
        left.append("↓ " + std::to_string(lines_below) + " more below", role::new_data().with_bold());
    }
    if (!typing.empty()) { left.append(sep); left.append(typing, dim.with_italic()); }

    // "F10 menu": the key bold, what it does dim
    Rich right;
    for (std::size_t i = 0; i < hints.size(); ++i) {
        if (i) right.append("   ");
        const std::string& hnt = hints[i];
        const std::size_t sp = hnt.find(' ');
        right.append(hnt.substr(0, sp), role::text().with_bold());
        if (sp != std::string::npos) right.append(hnt.substr(sp), dim);
    }
    right.append(" ");

    const int rw = right.width();
    Rich out;
    if (left.width() + rw + 1 <= w) {
        out = left.pad_right(w - rw);
        out.append(right);
    } else {
        out = left.fit(w);              // hints go first when it's tight
    }
    return out;
}

maya::Element StatusBar::element() const {
    return fill_row([this](int w) { return row(w); });
}

}  // namespace nx::tui
