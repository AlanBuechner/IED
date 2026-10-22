// Settings (docs/07 "Settings on the wire"): the app tier file and each workspace's solution tier file, read again when
// they change, the effective value of every setting per workspace, the "settings:app" and "settings:<wsId>" topics
// and the settings.* methods. Every member runs with the server lock held.

include "tierfile.ch"
include "merge.ch"
include "fs/watching.ch"

namespace Ied
{
	class Settings
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// Rebuilds the catalog from the loaded plugins and checks every tier file against it again.
		public void Load();
		// A workspace opened: its solution tier and topic.
		public void Opened(Workspace* ws);
		// A solution's projects were read (again): one project tier per project, and its local plugins' settings.
		public void SolutionChanged(Workspace* ws);
		// A workspace's trust changed: its local plugins' settings come or go.
		public void TrustChanged(Workspace* ws);
		// A workspace's own plugins changed (.ied/plugins/): their settings are read again.
		public void PluginsChanged(Workspace* ws);
		// A file of a workspace changed on disk (from its watcher): the tier file is read again if it is one.
		public void WorkspaceFileChanged(Workspace* ws, Core::StringView path);
		// The app tier file changed on disk.
		public void AppFileChanged();
		// The effective value of `key` for `ws` (null: the app tier alone) and a project of it (empty: none), JSON.
		public Core::String Effective(Workspace* ws, Core::StringView project, Core::StringView key);
		// Sets `key` to `valueJson` in the app tier, as settings.set does (a known app-tier key, a value that fits its
		// schema); false with `problem`. For a plugin's PluginContext::SetSetting (docs/26).
		public bool SetApp(Core::StringView key, Core::StringView valueJson, Core::String& problem);

	private:
		private SettingsCatalog* CatalogFor(Workspace* ws);
		private void Rebuild(Workspace* ws);
		private void Recheck(Workspace* ws);
		private TierFile* SolutionTier(Workspace* ws) const;
		private TierFile* ProjectTier(Workspace* ws, Core::StringView project) const;
		private TierFile* TierFor(Core::StringView tier, const Params& params);
		private Core::String Snapshot(Workspace* ws, Core::StringView project);
		private void Publish(Workspace* ws);
		private void PublishAll();
		private Core::String SchemaMethod(Call& call, const Params& params);
		private Core::String GetMethod(Call& call, const Params& params);
		private Core::String SetMethod(Call& call, const Params& params);
		private Core::String ResetMethod(Call& call, const Params& params);
		private Core::String Edit(TierFile* file, Core::StringView key, Core::StringView valueJson, const Params& params);

		private Server* server;
		private SettingsCatalog catalog;                                // the global plugins' settings: the app tier
		private Core::HashMap<Core::String, SettingsCatalog*> catalogs;   // wsId -> with its local plugins' settings too
		private TierFile* app;
		private Core::HashMap<Core::String, TierFile*> solutions;   // wsId -> its solution tier
		private Core::HashMap<Core::String, TierFile*> projects;    // "<wsId>/<project>" -> its project tier
	}

	// "<root>/.ied/solution.json": a workspace's solution tier (a folder workspace's too).
	const char* kSolutionSettings = ".ied/solution.json";
	// "<root>/.ied/projects/<Name>.json": a project's tier.
	const char* kProjectSettings = ".ied/projects/";
}
