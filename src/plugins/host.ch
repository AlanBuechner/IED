// PluginHost (docs/02 "Lifecycle"): finds plugins - built-in ones in the install's plugins/ folder, app ones in
// <app data>/plugins/, and a solution's project-local ones - validates their manifests, lets a stronger source override
// a plugin of the same id, loads an app plugin only once the owner approved its exact content and a project-local one
// only in a trusted workspace (docs/11 "Trust layers"), checks dependencies, keeps which are disabled, and offers the
// declarative contributions of the loaded ones. The "plugins" topic and the plugins.* methods; every member runs with
// the server lock held.

include "manifest.ch"
include "web/rpc.ch"

namespace Ied
{
	enum class PluginState { Loaded, Disabled, Failed, NeedsApproval, Overridden, Untrusted }

	class Plugin
	{
		public Manifest* manifest;
		public PluginState state;
		public Core::String digest;        // an app plugin's content digest now
		public Core::String overriddenBy;  // the source of the plugin with this id that won

		// {"id", "name", "version", "kind", "source", "state", "problems", "permissions", "contributes": [point names]}
		public Core::String SummaryJson() const;
	}

	class PluginHost
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// Reads every plugin folder and the owner's choices again; a broken plugin is kept with its problems.
		public void Load();
		public Plugin* Get(Core::StringView id) const;
		// A loaded plugin's file for /plugins/<id>/<path>; empty when the plugin is not loaded or there is no such file.
		public FS::Path FileOf(Core::StringView id, Core::StringView path) const;
		public const Core::Array<Plugin*>& All() const { return plugins; }
		// A workspace's project-local plugins, read now: untrusted unless the workspace is trusted.
		public Core::Array<Plugin*> LocalFor(Workspace* ws);
		// The plugins loaded for a workspace (null: none): the global ones, less those a loaded local plugin overrides,
		// and the loaded local ones.
		public Core::Array<Plugin*> LoadedFor(Workspace* ws);

	private:
		private void Discover(const FS::Path& folder, PluginSource source, Core::Array<Manifest*>& found);
		private void Choose(const Core::Array<Manifest*>& found);
		private void CheckDependencies();
		private void ReadChoices();
		private bool SaveChoices() const;
		private void Changed();
		private Core::String Snapshot() const;
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String SetEnabledMethod(Call& call, const Params& params);
		private Core::String ApproveMethod(Call& call, const Params& params);
		private Core::String ContributionsMethod(Call& call, const Params& params);
		private Core::String ReloadMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<Plugin*> plugins;
		private Core::HashMap<Core::String, bool> disabled;
		private Core::HashMap<Core::String, Core::String> approved;   // app plugin id -> the digest the owner approved
	}

	const char* PluginStateName(PluginState state);
}
