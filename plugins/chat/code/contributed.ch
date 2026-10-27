// What a workspace's plugins give its AI (docs/26 section 8): their skills beside the workspace's own and the app's,
// the system-message sections whose `when` holds - a language's only where the workspace uses the language
// (workspaceLanguage.<id>) - and their command tool calls beside the core tools. Each reads the plugins loaded for the
// workspace now; call with the server lock held.

include "skills.ch"
include "tool.ch"
include "docs.ch"
include "hints.ch"

namespace Ai
{
	// .ied/skills/, then each loaded plugin's `skills` folders, then the app's skills/ (a name's first one wins).
	Core::Array<Skill*> SkillsOf(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);
	// The plugins' reference docs (`aiDocs`: an id, a title and a folder inside the plugin), in load order.
	Core::Array<DocsFolder*> DocsOf(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);
	// The plugins' error hints (`aiHints`: a regex `match`, its `hint`, an optional `when`), in load order.
	Core::Array<ErrorHint*> HintsOf(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);
	// The same, for a tool call: what it shows the model gets them.
	Core::Array<ErrorHint*> CallHints(const ToolInvocation& call);
	// What the plugins add to the result of a call of `tool` that failed (`aiToolFailureNotes`: a `text` in which
	// {tool} is the tool's name, an optional `when`), in load order, a blank line apart; "" when none holds.
	Core::String FailureNotes(const IED::Workspace& ws, Core::StringView tool);
	// The plugins' sections for the system message (docs/14 item 5): in load order, each within its maxTokens and all
	// within kSectionsMostTokens; empty when none holds.
	Core::String PluginSections(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);
	// The core tools, docs_search and docs_read when the plugins have reference docs, and the plugins' tool calls that run
	// a command and whose `when` holds; a tool whose name is taken, or that is implemented some other way, is a problem.
	ToolRegistry* ToolsFor(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);

	const int64 kSectionDefaultTokens = 200;
	const int64 kSectionsMostTokens = 1500;
	const int64 kBytesPerToken = 4;
}
