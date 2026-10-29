// An edit whose `old` text is in the file but indented differently - by the same number of tabs on every line, as when
// the file was formatted after it was read, or with its later lines indented otherwise, as a formatter re-indents
// continuation lines - still finds its place when that place is the only one; `new` is shifted by the tabs the first
// line moved.

include "bedrock.ch"

namespace Ai
{
	// The text with the one place `old` matches up to a whole-tab shift replaced by `replacement` shifted the same;
	// false when there is no such place, more than one, or the shift cannot apply to `replacement`.
	bool ReplaceShifted(Core::StringView text, Core::StringView old, Core::StringView replacement, Core::String& result);
	// As ReplaceShifted, but only old's first line must match up to a whole-tab shift (none included): its other lines
	// match whatever their leading tabs and spaces.
	bool ReplaceReindented(Core::StringView text, Core::StringView old, Core::StringView replacement, Core::String& result);
}
