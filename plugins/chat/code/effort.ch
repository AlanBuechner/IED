// What a turn's effort sends to its model (docs/04 "Model groups and roles"): the chat's effort, else its role's, written
// as the profile maps it, else as its server reads one - llama.cpp's chat template, any other server OpenAI's field.

include "api/plugin.ch"
include "models.ch"

namespace Ai
{
	// The effort a request uses: the chat's pick, else its role's; empty for none.
	Core::StringView EffortOf(Core::StringView chatEffort, Core::StringView roleEffort);
	// The request fields `effort` adds for `profile`, into the open request object: its `effort` mapping's entry when it
	// has one (nothing for a level it leaves out), else `chat_template_kwargs.reasoning_effort` on a llama.cpp server
	// (`llamaCpp`) and `reasoning_effort` elsewhere. Nothing for an empty effort.
	void WriteEffort(IED::JsonWriter& json, const ModelProfile& profile, Core::StringView effort, bool llamaCpp);
}
