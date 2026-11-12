// What a chat is for - its use (docs/04 "Defaults by use"; owner, 2026-11-11: "we should be able to set a default
// model/effort for things like the chat, planning board, inline chat, etc."). A plugin that starts chats contributes its
// uses (`aiUses`: {id, title, description?, model, effort}, `model` and `effort` naming settings of its own), and
// chat.create names one: a chat of that use with no model or effort of its own runs on the model and effort those
// settings give (empty: the default chain; the model's own effort). The chat plugin's own: `chat` (the chats the owner
// starts, ai.chats.model / ai.chats.effort) and `agent` (the chats the AI starts, ai.chats.agentModel / agentEffort).

include "bedrock.ch"
include "api/Plugin.ch"
include "Chat.ch"

namespace Ai
{
	const char* c_UseChat = "chat";       // a chat the owner starts: chat.create's default
	const char* c_UseAgent = "agent";     // a chat the AI starts (chat_create)

	struct UseDefaults
	{
		public bool m_Known;             // a plugin loaded for the workspace contributes the use
		public Core::String m_Title;     // "Planning board"
		public Core::String m_Model;     // a profile of ai.models, or a version Claude Code offers; empty: the default chain
		public Core::String m_Effort;    // low ... max; empty: the model's own
	}

	// The defaults of `use` in workspace `wsId`: its contribution's settings as the workspace has them.
	UseDefaults DefaultsOfUse(IED::PluginContext* context, Core::StringView wsId, Core::StringView use);
	// The ids of the uses the plugins loaded for `wsId` contribute, comma separated (for a refusal's message).
	Core::String UseIds(Core::StringView wsId);
	// The model a chat's turns try first: its own, else - when it is no sub-agent's - its use's default; empty: the
	// default chain.
	Core::String ChosenModelOf(IED::PluginContext* context, Core::StringView wsId, const Chat* chat);
	// The effort a turn gets when the chat names none: `roleEffort` (a sub-agent's), else its use's default; empty: the
	// model's own.
	Core::String DefaultEffortOf(IED::PluginContext* context, Core::StringView wsId, const Chat* chat, Core::StringView roleEffort);
}
