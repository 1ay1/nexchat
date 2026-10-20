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
    int users = 0;         // people in it (0: not shown)

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
    /// what the pointer is over: underlined (it's clickable)
    enum class Hot { None, Network, Below, Hint0, Hint1, Hint2 } hot = Hot::None;
    /// the animation frame (connecting spinner, typing dots); 0 still
    int frame = 0;
    /// something in the row moves: the owner keeps frames coming
    [[nodiscard]] bool animating() const { return connecting || !typing_live.empty(); }
    /// "alice is typing" (without dots): drawn with animated dots
    std::string typing_live;

    [[nodiscard]] Rich row(int w) const;
    /// What's at column x of the last row drawn (for clicks and hover).
    [[nodiscard]] Hot at(int x) const;
    [[nodiscard]] maya::Element element() const;
    // [x0, x1) of each clickable thing in the last row drawn
    mutable std::vector<std::pair<std::pair<int, int>, Hot>> spans_{};
};

}  // namespace nx::tui
