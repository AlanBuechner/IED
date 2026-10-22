// A web page as text for the model (docs/29 "Conversion"): headings, paragraphs, lists, links made absolute, code,
// quotes and tables kept as Markdown (or as plain text), menus and page chrome left out, and only the page's <main> -
// or its one <article> - when it has one.

include "bedrock.ch"

namespace WebFetch
{
	// `html`'s text: Markdown, or plain text when `plain`; `title` gets its <title>. Links resolve against `pageUrl`, or
	// the page's <base href>.
	Core::String HtmlToText(Core::StringView html, Core::StringView pageUrl, bool plain, Core::String& title);
}
