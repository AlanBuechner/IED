// The block parser: a document's lines as headings, paragraphs, code blocks, quotes, rules and lists, in HTML.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mdhtml {

// The HTML of a whole Markdown document.
std::string RenderDocument(std::string_view text);

// Appends the HTML of the blocks these lines make: a document's lines, or the content of a quote's.
void RenderBlocks(const std::vector<std::string_view>& lines, std::string& out);

}  // namespace mdhtml
