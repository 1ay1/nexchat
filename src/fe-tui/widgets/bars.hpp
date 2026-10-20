#pragma once
// nx::tui bars — the one-row strips around the chat.
//
//   TopicBar   #nexchat [+nt] │ the topic, IRC formatting kept, cut to fit
//   StatusBar  [alice(+i)] Libera · lag 0.2s · away       alice is typing…
//
// Both are plain data in, one Rich row out; the glue fills the fields from
// the session (fe_set_topic, fe_set_lag, fe_set_away, fe_typing_changed).

#include <string>
#include <vector>

#include <maya/element/element.hpp>

#include "rich.hpp"

namespace nx::tui {

struct TopicBar {
    std::string channel;
    std::string modes;     // "+nt", "" = none
    std::string topic;     // raw IRC text

    [[nodiscard]] Rich row(int w) const;
    [[nodiscard]] maya::Element element() const;
};

struct StatusBar {
    std::string nick;
    std::string usermodes;   // our modes on the server, "+i"
    std::string network;
    double      lag = -1;    // seconds; < 0 = unknown
    bool        away = false;
    bool        connecting = false;
    std::string typing;      // "alice is typing…", from typing_text()
    int         lines_below = 0;   // the chat is scrolled up: "↓ 12 new"
    std::vector<std::string> hints;  // right-aligned key hints, dim

    [[nodiscard]] Rich row(int w) const;
    [[nodiscard]] maya::Element element() const;
};

}  // namespace nx::tui
