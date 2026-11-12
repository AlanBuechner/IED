// The sandbox_* AI tools (docs/34 "AI tools", docs/13 T-SBX-*) and the system-message section: the manifest's
// `function` entries. The long ones (run, validate, control, engine) run on threads of their own (ownThread) and wait
// for the service; a chat's stop reaches the command inside the sandbox.

include "SandboxService.ch"

namespace Sandbox
{
	// The service the tools use; null while the plugin is not active.
	void SetService(SandboxService* service);

	IED::ToolResult ListTool(const IED::ToolCall& call);
	IED::ToolResult CatalogTool(const IED::ToolCall& call);
	IED::ToolResult CreateTool(const IED::ToolCall& call);
	IED::ToolPlan CreatePlan(const IED::ToolCall& call);
	IED::ToolResult RunTool(const IED::ToolCall& call);
	IED::ToolResult ValidateTool(const IED::ToolCall& call);
	IED::ToolResult ControlTool(const IED::ToolCall& call);
	IED::ToolResult EngineTool(const IED::ToolCall& call);
	// Desktops (docs/34 "Desktops").
	IED::ToolResult LaunchTool(const IED::ToolCall& call);
	IED::ToolResult ScreenshotTool(const IED::ToolCall& call);
	IED::ToolResult InputTool(const IED::ToolCall& call);
	IED::ToolResult AppsTool(const IED::ToolCall& call);
	Core::String Section(const IED::Workspace& ws);

	// The definition sandbox_create's arguments describe (unknown fields ignored); false with `problem`.
	bool DefinitionOfArgs(const Serialization::DomNode& args, SandboxDefinition& definition, Core::String& problem);
	// A validation as the matrix the AI and the owner read.
	Core::String ValidationText(const ValidationRecord& record);
	// A whole-file change as a unified diff: `before` ("" for a new file) to `after`, the file `path`.
	Core::String FileDiff(Core::StringView path, Core::StringView before, Core::StringView after, int64& plus, int64& minus);

	const int64 c_RunDefaultSeconds = 900;
	const int64 c_RunMostSeconds = 1750;
}
