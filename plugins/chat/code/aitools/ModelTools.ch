// The AI's tools for the models it runs on (docs/04 "Switching models", docs/13 group `models`): ai_usage reads the
// Claude plan's windows - how much of each is used, how far through it is, its pace and when it resets - the models
// ai.models offers and what pacing would pick now; chat_set_model pins a model for a chat's next turns (this chat's, or
// one it handed work to), so a lead chat can keep the strong model for itself and give routine work a cheaper one.

include "ChatService.ch"

namespace Ai
{
	void AddModelTools(ToolRegistry& registry, Chats* chats, Chat* chat);
	// ai_usage's text: the plan's windows, the CLI's sign-in, each profile, and each chain of `groupsJson` (ai.groups) with a
	// Claude model as pacing has it now.
	Core::String UsageText(const ClaudeAccount& account, const Core::Array<ModelProfile*>& models, Core::StringView groupsJson, int64 nowMs);
}
