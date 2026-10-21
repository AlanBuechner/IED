// Settings (docs/07 "Settings on the wire"): the app tier file and each workspace's solution tier file, read again when
// they change, the effective value of every setting per workspace, the "settings:app" and "settings:<wsId>" topics
// and the settings.* methods. Every member runs with the server lock held.

include "tierfile.ch"
include "merge.ch"
include "../fs/watching.ch"

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
		// A file of a workspace changed on disk (from its watcher): the tier file is read again if it is one.
		public void WorkspaceFileChanged(Workspace* ws, Core::StringView path);
		// The app tier file changed on disk.
		public void AppFileChanged();
		// The effective value of `key` for `ws` (null: the app tier alone), JSON.
		public Core::String Effective(Workspace* ws, Core::StringView key);

	private:
		private TierFile* SolutionTier(Workspace* ws) const;
		private TierFile* TierFor(Core::StringView tier, const Params& params);
		private Core::String Snapshot(Workspace* ws);
		private void Publish(Workspace* ws);
		private void PublishAll();
		private Core::String SchemaMethod(Call& call, const Params& params);
		private Core::String GetMethod(Call& call, const Params& params);
		private Core::String SetMethod(Call& call, const Params& params);
		private Core::String ResetMethod(Call& call, const Params& params);
		private Core::String Edit(TierFile* file, Core::StringView key, Core::StringView valueJson, const Params& params);

		private Server* server;
		private SettingsCatalog catalog;
		private TierFile* app;
		private Core::HashMap<Core::String, TierFile*> solutions;   // wsId -> its solution tier
	}

	// "<root>/.ied/solution.json": a workspace's solution tier (a folder workspace's too).
	const char* kSolutionSettings = ".ied/solution.json";
}
