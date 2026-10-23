// Sub-agents (docs/04 "A group is a topology: an orchestrator and its sub-agents"): a group's `agents`, each a role with
// what it is for, the mode it runs in and the agents it may run itself. A chat that can run them gets agent_list and
// agent_run and a system-message section that says when delegating pays; an agent runs as a chat of its own (`agent`,
// `parentChatId`) and as a background task of the chat that ran it, which hears its answer when its turn ends. Each
// agent's record - runs, how they ended, time and tokens - is kept per workspace in .ied/local/agents.json.

include "background.ch"
include "chattools.ch"

namespace Ai
{
	const int64 kAgentsRunningMost = 4;          // a chat's agents running at once
	const int64 kAgentBriefShortBytes = 200;     // a shorter task brief gets a reminder
	const int64 kAgentRecordKept = 20;           // runs whose time and tokens the record keeps

	class AgentSpec
	{
		public Core::String name;
		public RoleModel role;
		public Core::String description;
		public ChatMode mode;
		public Core::Array<Core::String> calls;   // the agents it runs; "*" every one
	}

	// The agents of `group` in the ai.groups value, in order.
	Core::Array<AgentSpec*> GroupAgents(Core::StringView groupsJson, Core::StringView group);
	// Those a chat running as `self` may run: an orchestrator ("") every one, an agent those its `calls` names.
	Core::Array<AgentSpec*> CallableAgents(const Core::Array<AgentSpec*>& agents, Core::StringView self);
	// The agents a turn of `chat` may run; none for a chat on a single model (docs/04 "One choice per chat").
	Core::Array<AgentSpec*> AgentsOf(Chats* chats, const Chat* chat, const IED::Workspace& ws);
	// The system-message section for a chat that may run `agents`; "" for none.
	Core::String AgentsSection(const Core::Array<AgentSpec*>& agents, const Core::Array<ModelProfile*>& models);
	// agent_list and agent_run for a turn of `chat`, when it may run agents.
	void AddAgentTools(ToolRegistry& registry, Chats* chats, Chat* chat, const IED::Workspace& ws);
}
