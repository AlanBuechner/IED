// JSONC: JSON with // line comments and /* */ block comments, as settings files and manifests are written. The comments
// become spaces (a block comment's line breaks stay), so an offset or a line number ReadJson reports points into the
// original text.
//     Serialization::DomDoc doc;
//     Serialization::ReadResult read = Serialization::ReadJsonc(text.View(), doc);

include "jsonreader.ch"

namespace Serialization
{
	// `text` with its comments blanked out, into `out`; false with `error` (static text) and the byte it is at for a
	// comment or a string that never ends.
	bool StripJsonComments(Core::StringView text, Core::StringBuilder& out, const char*& error, int64& at);
	// ReadJson after StripJsonComments; a comment that never ends is a read error at its start.
	ReadResult ReadJsonc(Core::StringView text, DomDoc& doc);
}
