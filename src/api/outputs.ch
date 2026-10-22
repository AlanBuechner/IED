// Output sources for plugin code (docs/10 "out:<wsId>:<sourceId>"): what a source keeps of its text - the chat reads it
// for an `@output:` mention (docs/04 "Attachments and @-mentions"). Writing to one is PluginContext::Output (plugin.ch).

include "bedrock.ch"

namespace IED
{
	// The text the output source `sourceId` of workspace `wsId` keeps (its last 64 KB, cut at a line start); false with
	// `problem` for a workspace or source there is not.
	bool OutputTail(Core::StringView wsId, Core::StringView sourceId, Core::String& text, Core::String& problem);
}
