// The block parser: a document's lines as headings, paragraphs, code blocks, quotes, rules and lists, in HTML.
include "bedrock.ch"

namespace MdHtml
{
	// The HTML of a whole Markdown document.
	Core::String RenderDocument(Core::StringView text);
	// Appends the HTML of the blocks these lines make: a document's lines, or the content of a quote's.
	void RenderBlocks(const Core::Array<Core::StringView>& lines, Core::StringBuilder& out);
}
