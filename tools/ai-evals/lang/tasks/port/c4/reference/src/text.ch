// Text helpers shared by the block and inline parsers: whitespace, lines and HTML escaping.
include "bedrock.ch"

namespace MdHtml
{
	// A space or a tab: the whitespace of the block structure.
	bool IsSpace(int32 c);
	// The text without its leading spaces and tabs.
	Core::StringView TrimStart(Core::StringView text);
	// The text without its leading and trailing spaces and tabs.
	Core::StringView Trim(Core::StringView text);
	// True when the line holds nothing but spaces and tabs.
	bool IsBlank(Core::StringView line);
	// The number of leading spaces and tabs (a tab counts as one).
	int32 Indentation(Core::StringView line);
	// The lines of a document: split at '\n', a '\r' ending a line dropped; a final '\n' starts no further line.
	Core::Array<Core::StringView> SplitLines(Core::StringView text);
	// Appends the text with &, <, > and " written as entities.
	void AppendEscaped(Core::StringBuilder& out, Core::StringView text);
}
