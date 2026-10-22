// The inline parser: the emphasis, code spans, links and escapes of one block's text, as HTML.
#pragma once

#include <string>
#include <string_view>

namespace mdhtml {

// The HTML of a paragraph's, a heading's or a list item's text (lines joined with '\n').
std::string RenderInline(std::string_view text);

}  // namespace mdhtml
