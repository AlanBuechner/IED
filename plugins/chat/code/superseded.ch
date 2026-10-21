// Tool results a later call made stale (docs/04 "Context management", the mechanical tier): a file read when the same
// range of the file is read again, or the file is written or edited after it. A compression turns each into a one-line
// note; the newest read of a file stays.

include "chat.ch"

namespace Ai
{
	// Replaces the stale results among `messages` (in the order they were sent) before `keepFrom`; how many it replaced.
	int64 DropSuperseded(const Core::Array<ModelMessage*>& messages, int64 keepFrom);
}
