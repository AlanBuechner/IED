// What a workspace's plugins give its AI (docs/26 section 8): their skills beside the workspace's own and the app's,
// the system-message sections whose `when` holds - a language's only where the workspace uses the language
// (workspaceLanguage.<id>) - and their command tool calls beside the core tools. Each reads the plugins loaded for the
// workspace now; call with the server lock held.

include "skills.ch"
include "tool.ch"

namespace Ai
{
	// .ied/skills/, then each loaded plugin's `skills` folders, then the app's skills/ (a name's first one wins).
	Core::Array<Skill*> SkillsOf(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);
	// The plugins' sections for the system message (docs/14 item 5): in load order, each within its maxTokens and all
	// within kSectionsMostTokens; empty when none holds.
	Core::String PluginSections(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);
	// The core tools and the plugins' tool calls that run a command and whose `when` holds; a tool whose name is taken,
	// or that is implemented some other way, is a problem.
	ToolRegistry* ToolsFor(IED::PluginContext* context, const IED::Workspace& ws, Core::Array<Core::String>& problems);

	const int64 kSectionDefaultTokens = 200;
	const int64 kSectionsMostTokens = 1500;
	const int64 kBytesPerToken = 4;
}
