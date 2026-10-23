// Other plugins' contributions (docs/26 sections 3 and 8): what the plugins loaded for a workspace contribute to a
// point - from their manifests and their code - for a plugin that consumes a point (the chat reads `skills`,
// `systemMessageSections` and `toolCalls`), and calls into the code behind such an entry.

include "tools.ch"
include "bindings.ch"

namespace IED
{
	class Contribution
	{
		public Core::String pluginId;
		public Core::String pluginName;
		public Core::String folder;            // the plugin's folder
		public Serialization::DomNode entry;   // as the manifest (or the code) gives it
	}

	// The entries of `point` in workspace `wsId`, in the plugins' load order.
	Core::Array<Contribution> Contributions(Core::StringView wsId, Core::StringView point);
	// The text a plugin's `function` section computes for `ws` ("" for none); false with `problem` when it failed.
	bool PluginSection(const Workspace& ws, Core::StringView pluginId, Core::StringView function, Core::String& text, Core::String& problem);
	// Runs a plugin's tool call that its code implements (`function`) or a binding answers (`binding`), as `entry`
	// names it, and waits for it (at most `seconds`, or until the call is cancelled).
	ToolResult RunPluginTool(Core::StringView pluginId, const Serialization::DomNode& entry, const ToolCall& call, int64 seconds);
	// What a plugin's tool call will do, for its approval card: the function `entry`'s `plan` names, with `call`'s
	// arguments; waits at most kToolPlanMs or until the call is cancelled. False with `problem` when it could not be had.
	bool PlanPluginTool(Core::StringView pluginId, const Serialization::DomNode& entry, const ToolCall& call, ToolPlan& plan, Core::String& problem);
	// Whether a contribution's `when` clause holds in `ws`'s context (docs/26 section 8: workspaceTrusted,
	// workspaceKind, config.<key>, workspaceContains.<name>, workspaceLanguage.<id>); an empty one does. False with
	// `error` when it does not parse.
	bool WhenHolds(const Workspace& ws, Core::StringView clause, Core::String& error);
	// The app tier's folder (%APPDATA%\IED): its skills/, prompts/ and plugins/.
	Core::String AppFolder();
	// The install's folder (the server's home): its plugins/ and shared/.
	Core::String HomeFolder();

	const int64 kToolPlanMs = 5000;
}
