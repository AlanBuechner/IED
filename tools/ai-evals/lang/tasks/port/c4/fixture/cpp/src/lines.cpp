#include "lines.h"

#include "text.h"

namespace mdhtml {
namespace {

constexpr int kMinFenceTicks = 3;
constexpr int kMaxHeadingLevel = 6;
constexpr int kMinRuleMarks = 3;
constexpr size_t kMaxNumberDigits = 9;

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

int CountLeading(std::string_view text, char c) {
    size_t count = 0;
    while (count < text.size() && text[count] == c) ++count;
    return static_cast<int>(count);
}

// A closing run of '#' goes when it is all there is or follows whitespace: "Title ##" is "Title", "C#" stays.
std::string_view StripClosingHashes(std::string_view text) {
    size_t end = text.size();
    while (end > 0 && text[end - 1] == '#') --end;
    if (end == text.size() || (end > 0 && !IsSpace(text[end - 1]))) return text;
    return Trim(text.substr(0, end));
}

}  // namespace

LineKind Classify(std::string_view line) {
    if (IsBlank(line)) return LineKind::Blank;
    if (MatchFence(line)) return LineKind::Fence;
    if (MatchHeading(line)) return LineKind::Heading;
    if (IsRule(line)) return LineKind::Rule;
    if (MatchQuote(line)) return LineKind::Quote;
    if (MatchListItem(line)) return LineKind::ListItem;
    return LineKind::Text;
}

std::optional<Fence> MatchFence(std::string_view line) {
    std::string_view rest = TrimStart(line);
    int ticks = CountLeading(rest, '`');
    if (ticks < kMinFenceTicks) return std::nullopt;
    std::string_view info = Trim(rest.substr(ticks));
    if (info.find('`') != std::string_view::npos) return std::nullopt;
    size_t word = 0;
    while (word < info.size() && !IsSpace(info[word])) ++word;
    return Fence{ticks, info.substr(0, word)};
}

bool ClosesFence(std::string_view line, int ticks) {
    std::string_view rest = TrimStart(line);
    int run = CountLeading(rest, '`');
    return run >= ticks && IsBlank(rest.substr(run));
}

std::optional<Heading> MatchHeading(std::string_view line) {
    std::string_view rest = TrimStart(line);
    int level = CountLeading(rest, '#');
    if (level == 0 || level > kMaxHeadingLevel) return std::nullopt;
    if (static_cast<size_t>(level) < rest.size() && !IsSpace(rest[level])) return std::nullopt;
    return Heading{level, StripClosingHashes(Trim(rest.substr(level)))};
}

bool IsRule(std::string_view line) {
    char mark = 0;
    int count = 0;
    for (char c : line) {
        if (IsSpace(c)) continue;
        if ((c != '-' && c != '*') || (mark != 0 && c != mark)) return false;
        mark = c;
        ++count;
    }
    return count >= kMinRuleMarks;
}

std::optional<std::string_view> MatchQuote(std::string_view line) {
    std::string_view rest = TrimStart(line);
    if (rest.empty() || rest[0] != '>') return std::nullopt;
    rest.remove_prefix(1);
    if (!rest.empty() && IsSpace(rest[0])) rest.remove_prefix(1);
    return rest;
}

std::optional<ListMarker> MatchListItem(std::string_view line) {
    std::string_view rest = TrimStart(line);
    ListMarker item{Indentation(line), false, 0, {}};
    size_t length = 1;
    if (rest.empty() || (rest[0] != '-' && rest[0] != '*')) {
        size_t digits = 0;
        while (digits < rest.size() && IsDigit(rest[digits])) ++digits;
        if (digits == 0 || digits > kMaxNumberDigits || digits == rest.size() || rest[digits] != '.') return std::nullopt;
        item.ordered = true;
        for (size_t i = 0; i < digits; ++i) item.number = item.number * 10 + (rest[i] - '0');
        length = digits + 1;
    }
    if (length < rest.size() && !IsSpace(rest[length])) return std::nullopt;
    item.text = Trim(rest.substr(length));
    return item;
}

}  // namespace mdhtml
