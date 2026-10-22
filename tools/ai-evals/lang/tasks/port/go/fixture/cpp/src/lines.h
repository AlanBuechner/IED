// What one line of Markdown is, told by its first characters: the vocabulary of the block parser.
#pragma once

#include <optional>
#include <string_view>

namespace mdhtml {

enum class LineKind { Blank, Fence, Heading, Rule, Quote, ListItem, Text };

struct Heading {
    int level;              // 1 to 6
    std::string_view text;  // without the marker and a closing run of '#'
};

struct Fence {
    int ticks;                  // the length of the opening backtick run, 3 or more
    std::string_view language;  // the first word of the info string; empty when there is none
};

struct ListMarker {
    int indent;             // leading spaces and tabs
    bool ordered;           // "12." rather than "-" or "*"
    int number;             // an ordered item's number
    std::string_view text;  // what follows the marker, trimmed
};

// The kind of a line, tested in this order: blank, fence, heading, rule, quote, list item; anything else is text.
LineKind Classify(std::string_view line);

// "```lang": three or more backticks; an info string with a backtick in it makes the line no fence.
std::optional<Fence> MatchFence(std::string_view line);

// A run of at least `ticks` backticks with nothing but whitespace after it.
bool ClosesFence(std::string_view line, int ticks);

// "## Title": one to six '#' followed by whitespace or the end of the line.
std::optional<Heading> MatchHeading(std::string_view line);

// Three or more '-', or three or more '*', and nothing else but whitespace.
bool IsRule(std::string_view line);

// "> text": what follows the '>' and the one space or tab after it.
std::optional<std::string_view> MatchQuote(std::string_view line);

// "- text", "* text" or "7. text": the marker followed by whitespace or the end of the line.
std::optional<ListMarker> MatchListItem(std::string_view line);

}  // namespace mdhtml
