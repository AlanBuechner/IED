// The inline parser: the emphasis, code spans, links and escapes of one block's text, as HTML.
include "bedrock.ch"

namespace MdHtml
{
	// The HTML of a paragraph's, a heading's or a list item's text (lines joined with '\n').
	Core::String RenderInline(Core::StringView text);
}
