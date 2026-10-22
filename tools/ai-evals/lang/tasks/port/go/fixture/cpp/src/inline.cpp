#include "inline.h"

#include <optional>
#include <vector>

#include "text.h"

namespace mdhtml {
namespace {

// ASCII punctuation: what a backslash escapes.
bool IsPunctuation(char c) {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

// The whitespace that decides whether a run of '*' can open or close emphasis; '\n' joins a block's lines.
bool IsInlineSpace(char c) { return c == ' ' || c == '\t' || c == '\n'; }

size_t RunLength(std::string_view text, size_t at, char c) {
    size_t end = at;
    while (end < text.size() && text[end] == c) ++end;
    return end - at;
}

// Where the next run of exactly `length` backticks at or after `from` starts.
std::optional<size_t> FindBacktickRun(std::string_view text, size_t from, size_t length) {
    size_t at = from;
    while (at < text.size()) {
        if (text[at] != '`') {
            ++at;
            continue;
        }
        size_t run = RunLength(text, at, '`');
        if (run == length) return at;
        at += run;
    }
    return std::nullopt;
}

// A code span's content: line breaks become spaces, then one space comes off each end when both ends have one
// and the content is not all spaces.
std::string CodeSpanText(std::string_view raw) {
    std::string text(raw);
    for (char& c : text) {
        if (c == '\n') c = ' ';
    }
    bool padded = text.size() >= 2 && text.front() == ' ' && text.back() == ' ';
    if (padded && text.find_first_not_of(' ') != std::string::npos) text = text.substr(1, text.size() - 2);
    return text;
}

// The ']' that closes the '[' at `open`: brackets nest, and escaped characters and code spans are skipped.
std::optional<size_t> FindLabelEnd(std::string_view text, size_t open) {
    int depth = 0;
    size_t at = open;
    while (at < text.size()) {
        char c = text[at];
        if (c == '\\') {
            at += 2;
            continue;
        }
        if (c == '`') {
            size_t ticks = RunLength(text, at, '`');
            std::optional<size_t> close = FindBacktickRun(text, at + ticks, ticks);
            at = close ? *close + ticks : at + ticks;
            continue;
        }
        if (c == '[') ++depth;
        if (c == ']' && --depth == 0) return at;
        ++at;
    }
    return std::nullopt;
}

struct Destination {
    std::string url;  // escapes resolved
    size_t end = 0;   // just past the ')'
};

// "(url)" at `at`: the url runs to the first unescaped ')' and holds no whitespace.
std::optional<Destination> ReadDestination(std::string_view text, size_t at) {
    if (at >= text.size() || text[at] != '(') return std::nullopt;
    Destination destination;
    for (size_t i = at + 1; i < text.size(); ++i) {
        char c = text[i];
        if (c == ')') {
            destination.end = i + 1;
            return destination;
        }
        if (IsInlineSpace(c)) return std::nullopt;
        if (c == '\\' && i + 1 < text.size() && IsPunctuation(text[i + 1])) c = text[++i];
        destination.url += c;
    }
    return std::nullopt;
}

// A piece of text on its way to HTML: finished HTML, or a run of '*' that may open or close emphasis.
struct Token {
    bool delimiter = false;
    std::string html;      // the finished HTML of a piece that is not a delimiter
    int count = 0;         // the '*' of a delimiter not used by emphasis
    bool canOpen = false;  // followed by a character that is not whitespace
    bool canClose = false; // preceded by a character that is not whitespace
    bool active = true;    // false once inside a matched pair, which nothing outside can reach
    std::string closing;   // the tags it closes, written before its unused '*'
    std::string opening;   // the tags it opens, written after them
};

class InlineParser {
public:
    InlineParser(std::string_view text, bool linksAllowed) : text_(text), linksAllowed_(linksAllowed) {}

    std::string Render() {
        Tokenize();
        MatchEmphasis();
        std::string html;
        for (const Token& token : tokens_) {
            if (!token.delimiter) {
                html += token.html;
                continue;
            }
            html += token.closing;
            html.append(token.count, '*');
            html += token.opening;
        }
        return html;
    }

private:
    void Tokenize() {
        size_t at = 0;
        while (at < text_.size()) {
            char c = text_[at];
            if (c == '\\') {
                at = ReadEscape(at);
            } else if (c == '`') {
                at = ReadCodeSpan(at);
            } else if (c == '[' && linksAllowed_) {
                at = ReadLink(at);
            } else if (c == '*') {
                at = ReadDelimiter(at);
            } else {
                AppendText(text_.substr(at, 1));
                ++at;
            }
        }
    }

    void AppendHtml(std::string_view html) {
        if (tokens_.empty() || tokens_.back().delimiter) tokens_.emplace_back();
        tokens_.back().html += html;
    }

    void AppendText(std::string_view text) {
        std::string html;
        AppendEscaped(html, text);
        AppendHtml(html);
    }

    // "\*" is a literal '*'; a backslash before anything but ASCII punctuation is itself.
    size_t ReadEscape(size_t at) {
        if (at + 1 < text_.size() && IsPunctuation(text_[at + 1])) {
            AppendText(text_.substr(at + 1, 1));
            return at + 2;
        }
        AppendText("\\");
        return at + 1;
    }

    // A run of backticks opens a code span closed by the next run of the same length; without one it is text.
    size_t ReadCodeSpan(size_t at) {
        size_t ticks = RunLength(text_, at, '`');
        std::optional<size_t> close = FindBacktickRun(text_, at + ticks, ticks);
        if (!close) {
            AppendText(text_.substr(at, ticks));
            return at + ticks;
        }
        AppendHtml("<code>");
        AppendText(CodeSpanText(text_.substr(at + ticks, *close - at - ticks)));
        AppendHtml("</code>");
        return *close + ticks;
    }

    // "[text](url)"; the text is parsed on its own, without links. Anything else leaves the '[' as text.
    size_t ReadLink(size_t at) {
        std::optional<size_t> close = FindLabelEnd(text_, at);
        std::optional<Destination> destination;
        if (close) destination = ReadDestination(text_, *close + 1);
        if (!destination) {
            AppendText("[");
            return at + 1;
        }
        std::string html = "<a href=\"";
        AppendEscaped(html, destination->url);
        html += "\">";
        html += InlineParser(text_.substr(at + 1, *close - at - 1), false).Render();
        html += "</a>";
        AppendHtml(html);
        return destination->end;
    }

    size_t ReadDelimiter(size_t at) {
        size_t run = RunLength(text_, at, '*');
        Token token;
        token.delimiter = true;
        token.count = static_cast<int>(run);
        token.canOpen = at + run < text_.size() && !IsInlineSpace(text_[at + run]);
        token.canClose = at > 0 && !IsInlineSpace(text_[at - 1]);
        tokens_.push_back(token);
        return at + run;
    }

    // Each closer, left to right, pairs with the nearest opener before it while both have '*' left.
    void MatchEmphasis() {
        for (size_t closer = 0; closer < tokens_.size(); ++closer) {
            if (!tokens_[closer].delimiter || !tokens_[closer].canClose) continue;
            while (tokens_[closer].count > 0) {
                std::optional<size_t> opener = FindOpener(closer);
                if (!opener) break;
                Pair(*opener, closer);
            }
        }
    }

    std::optional<size_t> FindOpener(size_t closer) const {
        for (size_t i = closer; i-- > 0;) {
            const Token& token = tokens_[i];
            if (token.delimiter && token.active && token.canOpen && token.count > 0) return i;
        }
        return std::nullopt;
    }

    // Two '*' from each side make <strong> when both have two, else one makes <em>; what lies between is sealed.
    void Pair(size_t opener, size_t closer) {
        Token& open = tokens_[opener];
        Token& close = tokens_[closer];
        int used = open.count >= 2 && close.count >= 2 ? 2 : 1;
        std::string tag = used == 2 ? "strong" : "em";
        open.count -= used;
        close.count -= used;
        open.opening = "<" + tag + ">" + open.opening;
        close.closing += "</" + tag + ">";
        for (size_t i = opener + 1; i < closer; ++i) tokens_[i].active = false;
    }

    std::string_view text_;
    bool linksAllowed_;
    std::vector<Token> tokens_;
};

}  // namespace

std::string RenderInline(std::string_view text) { return InlineParser(text, true).Render(); }

}  // namespace mdhtml
