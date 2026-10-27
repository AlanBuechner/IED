// Text helpers the server uses for its output and terminals (docs/26 section 2), for plugin code that keeps text the
// same way.

include "bedrock.ch"

namespace IED
{
	// `tail` with `text` after it, the oldest whole lines dropped past `limit` bytes.
	Core::String BoundedText(Core::StringView tail, Core::StringView text, int64 limit);
	// How many bytes of `bytes` end on a whole UTF-8 character (the rest waits for the next read).
	int64 WholeCharacters(Core::StringView bytes);
}
