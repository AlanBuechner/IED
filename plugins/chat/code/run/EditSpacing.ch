// An edit whose `old` text is in the file but spaced otherwise within its lines - a formatter put a space in
// (`[](){` became `[]() {`) or took one out after it was read - still finds its place when that place is the only one:
// old and the file are compared with their spaces and tabs left out, line breaks kept, and the place's whole lines'
// text is replaced by `new` as written.

include "bedrock.ch"

namespace Ai
{
	// The text with the one place `old` matches but for spaces and tabs replaced by `replacement`; false when there is no
	// such place, or more than one.
	bool ReplaceRespaced(Core::StringView text, Core::StringView old, Core::StringView replacement, Core::String& result);
}
