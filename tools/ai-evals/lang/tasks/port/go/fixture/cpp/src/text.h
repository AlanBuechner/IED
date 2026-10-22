// Text helpers shared by the block and inline parsers: whitespace, lines and HTML escaping.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mdhtml {

// A space or a tab: the whitespace of the block structure.
bool IsSpace(char c);

// The text without its leading spaces and tabs.
std::string_view TrimStart(std::string_view text);

// The text without its leading and trailing spaces and tabs.
std::string_view Trim(std::string_view text);

// True when the line holds nothing but spaces and tabs.
bool IsBlank(std::string_view line);

// The number of leading spaces and tabs (a tab counts as one).
int Indentation(std::string_view line);

// The lines of a document: split at '\n', a '\r' ending a line dropped; a final '\n' starts no further line.
std::vector<std::string_view> SplitLines(std::string_view text);

// Appends the text with &, <, > and " written as entities.
void AppendEscaped(std::string& out, std::string_view text);

}  // namespace mdhtml
