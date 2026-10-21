// ${name} in a task's strings (docs/06 "Task model"): each name looked up by the caller; one it does not know fails the
// expansion, naming it, so a typo never runs a command with a hole in it.

include "bedrock.ch"

namespace Ied
{
	alias TaskLookup = func<bool(Core::StringView name, Core::String& value)>;

	// `text` with every ${name} replaced; false, with `unknown` the first name `lookup` does not know.
	bool ExpandTaskText(Core::StringView text, const TaskLookup& lookup, Core::String& out, Core::String& unknown);
}
