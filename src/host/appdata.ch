// AppDataWatch: the app data folder watched (docs/07 "Live", docs/02 "Lifecycle"): a changed settings.json is read
// again, and a change under plugins/ re-runs the plugin lifecycle, so app plugins load, change and go without a restart.

include "fs/watching.ch"

namespace Ied
{
	class AppDataWatch
	{
		[operator("create")] public void Init() {}

		// Watches `server`'s app data folder (made when missing); false with `error` when it cannot.
		public bool Start(Server* server, Core::String& error);

		private FolderWatch watch;
	}

	// What a batch of changes in the app data folder asks for.
	bool TouchesSettings(const ChangeBatch& batch);
	bool TouchesPlugins(const ChangeBatch& batch);
}
