// Where an edit's `old` text went wrong: when it is not in the file, the place that agrees with the longest part of it
// (its start, or its end) and what the file has there instead, so a model corrects the edit without guessing again.

include "bedrock.ch"

namespace Ai
{
	const int64 c_EditMissShownBytes = 48;
	const int64 c_EditMissLeast = 12;   // a shorter agreement is a coincidence, not the place meant

	// "line N: ... the file has `x` where old has `y`"; empty when no part of `old` of kEditMissLeast bytes is in `text`.
	Core::String EditMiss(Core::StringView text, Core::StringView old);

	// `text` with its line breaks, tabs and other control bytes written as escapes, so the difference is visible.
	Core::String Visible(Core::StringView text);
}
