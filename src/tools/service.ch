// Tools (docs/26 "Language tools"): each open workspace's language tools - what the `tools` contributions of the
// plugins loaded for it name, whose `when` holds there - found, their versions read against what the workspace pins,
// and installed on request into IED's tools folder (<data>/tools). Topic tools:<wsId>; tools.status, tools.detect,
// tools.install, tools.cancel and tools.dismiss; an install's output goes to the workspace's "tools" output source.
// Detection runs on a thread of its own when a workspace opens or is trusted, when its plugins, settings or pin files
// change, and after an install; nothing is run for an untrusted workspace. ProgramFor is ${tool:<id>} and EnvironmentFor
// the tools PATH, for language servers, debug adapters, tasks and terminals. Members run with the server lock held.

include "detect.ch"
include "install.ch"
include "fs/changes.ch"
include "web/rpc.ch"

namespace Ied
{
	const char* kToolsOutputSource = "tools";

	// One workspace's tools.
	class WorkspaceTools
	{
		public Workspace* ws;
		public Core::Array<ToolStatus*> statuses;                  // the last detection's
		public bool detected;                                      // a detection ended
		public bool detecting;
		public bool dirty;                                         // detect again when the running detection ends
		public Core::HashMap<Core::String, Core::String> failures; // tool id -> why its install failed, while it lacks
		public Core::String dismissed;                             // the lacking tools when the banner was dismissed
		public Core::String settings;                              // the tools' settings as they were last seen
		public Core::Array<Core::String> pinFiles;                 // the names of the files the tools' pins are read from
		public Core::Array<Core::String> queued;                   // an install's tools, in order
		public Core::String current;                               // the one installing now
	}

	class Tools
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);
		public void TrustChanged(Workspace* ws);
		public void PluginsChanged(Workspace* ws);
		public void SettingsChanged(Workspace* ws);
		public void FilesChanged(Workspace* ws, const ChangeBatch& batch);

		// ${tool:<id>}: the program found for tool `id` in `ws` (a wrong version's too); before a detection, the first
		// place it may be, else what its setting names, else its program's name; empty for a tool no plugin names.
		public Core::String ProgramFor(Workspace* ws, Core::StringView id);
		// Whether `command` is exactly ${tool:<id>} of a tool `ws` lacks - detected missing, or its install failed - with
		// what to say instead of starting it ("rust-analyzer is not installed: ...; Settings > Languages installs it").
		public bool LacksProgram(Workspace* ws, Core::StringView command, Core::String& problem);
		// `environment` with the folders of `ws`'s tools that are found outside PATH and ask for it (onPath) first on
		// PATH.
		public Core::Array<Core::String> EnvironmentFor(Workspace* ws, const Core::Array<Core::String>& environment);
		// The tools of the plugins loaded for `ws` whose `when` holds there; of one id, the last loaded plugin's.
		public Core::Array<ToolSpec*> ToolsOf(Workspace* ws);
		// tools:<wsId>'s snapshot; "" for a workspace that is not open.
		public Core::String SnapshotJson(Workspace* ws);

	private:
		private WorkspaceTools* EntryOf(Workspace* ws);
		private WorkspaceTools* EntryParam(const Params& params);
		private void Detect(WorkspaceTools* entry);
		private void DetectLoop(WorkspaceTools* entry);
		private DetectJob* JobFor(Workspace* ws, ToolSpec* spec);
		private ProfileVariables VariablesFor(Workspace* ws, const ToolSpec* spec);
		private Core::String SettingText(Workspace* ws, Core::StringView key);
		// The tools' settings and their values, one per line: what SettingsChanged compares.
		private Core::String ToolSettings(Workspace* ws);
		private Core::Array<Core::String> ProjectFolders(Workspace* ws);
		private ToolStatus* StatusOf(WorkspaceTools* entry, Core::StringView id);
		private void Store(WorkspaceTools* entry, ToolStatus* status);
		private Core::String Lacking(WorkspaceTools* entry);
		private Core::String Snapshot(WorkspaceTools* entry);
		private void Publish(WorkspaceTools* entry);
		private InstallPlan PlanFor(WorkspaceTools* entry, ToolSpec* spec);
		private void InstallLoop(WorkspaceTools* entry, Core::Array<ToolSpec*> order, InstallControl* control);
		private void Say(WorkspaceTools* entry, Core::StringView text);
		private Core::String StatusMethod(Call& call, const Params& params);
		private Core::String DetectMethod(Call& call, const Params& params);
		private Core::String InstallMethod(Call& call, const Params& params);
		private Core::String CancelMethod(Call& call, const Params& params);
		private Core::String DismissMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<WorkspaceTools*> entries;
		private WorkspaceTools* installing;    // the workspace an install runs for; one at a time
		private InstallControl* control;
	}

	Core::String ToolsTopic(const Workspace* ws);
	// The folder IED installs tools into: <data>/tools.
	FS::Path ToolsFolder(Server* server);
}
