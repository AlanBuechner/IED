// The plugin API (docs/26 section 2): the part of the server a plugin's C4 code includes - this folder is the script
// allow-list, and Bedrock is always allowed. The server compiles a plugin's code (runtime.sources) against its
// interface when the plugin activates and again when the code changes. Its entry (runtime.entry) is a free function
// `void <Name>(IED::PluginContext& context)` that registers what the plugin provides; its manifest's `function`
// entries name free functions of the code:
//     toolCalls               IED::ToolResult <Name>(const IED::ToolCall& call)        (tools.ch)
//     systemMessageSections   Core::String <Name>(const IED::Workspace& ws)            the section's text, "" for none
// A plugin's code runs on the plugin's own thread, one call at a time, never holding the server's lock; a hardware
// fault in it stops that call and marks the plugin failed until its code changes or it is reloaded.

include "workspace.ch"
include "bindings.ch"
include "tools.ch"
include "process.ch"

namespace IED
{
	class PluginContext
	{
		[operator("create")] public void Init(void* plugin) { host = plugin; }

		public Core::String Id() const;
		// The plugin's folder.
		public Core::String Folder() const;
		// The workspace a project plugin was loaded for; "" for a built-in or app plugin.
		public Core::String WorkspaceId() const;

		// A line in the server's log (the Output window's IED source), as "[<id>] <line>".
		public void Log(Core::StringView line) const;
		// A setting's effective value as JSON text ("" when it has none), in `wsId` ("": the app's).
		public Core::String Setting(Core::StringView key, Core::StringView wsId) const;

		// Answers calls of `name`, a server binding the manifest lists under bindings.provides.
		public void Provide(Core::StringView name, Binding handler);
		// Hears the protocol events of `kind` (docs/10: "tree.changed", "task.status", ...) on every topic.
		public void Listen(Core::StringView kind, EventHandler handler);
		// Runs `opened` for each workspace the plugin is loaded for: those open now, then each that opens.
		public void OnWorkspace(WorkspaceHandler opened);
		// Runs when the plugin stops: disabled, removed, or reloaded with changed code.
		public void OnDeactivate(func<void()> handler);
		// The plugin's computed entries of a contribution point (docs/26 section 3) in workspace `wsId` ("": every
		// workspace): a JSON array of entries shaped as in the manifest, replacing those it gave before ("[]" removes
		// them); false with `problem` when `entries` is not an array of objects.
		public bool Contribute(Core::StringView point, Core::StringView wsId, Core::StringView entries, Core::String& problem);

		private void* host;
	}
}
