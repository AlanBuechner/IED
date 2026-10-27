// Locations as people type them, "C:\src\a.cf:42:5", and the control channel's `open` request for one.

include "bedrock.ch"

namespace Launch
{
	// "C:\src\a.cf:42:3" to the path, line and column (0: none); a Windows drive's colon stays in the path.
	void SplitLocation(Core::StringView text, Core::String& path, int64& line, int64& col);
	// The params of `open` (docs/10): the path made absolute, its line and column.
	Core::String OpenParams(Core::StringView path, int64 line, int64 col);
}
