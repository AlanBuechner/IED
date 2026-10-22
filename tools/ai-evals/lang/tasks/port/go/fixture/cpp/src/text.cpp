#include "text.h"

namespace mdhtml {

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

std::string_view TrimStart(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && IsSpace(text[start])) ++start;
    return text.substr(start);
}

std::string_view Trim(std::string_view text) {
    text = TrimStart(text);
    size_t end = text.size();
    while (end > 0 && IsSpace(text[end - 1])) --end;
    return text.substr(0, end);
}

bool IsBlank(std::string_view line) { return TrimStart(line).empty(); }

int Indentation(std::string_view line) { return static_cast<int>(line.size() - TrimStart(line).size()); }

std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

void AppendEscaped(std::string& out, std::string_view text) {
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
}

}  // namespace mdhtml
