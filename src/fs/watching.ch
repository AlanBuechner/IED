// Watching (docs/10 tree.changed, doc.diskChanged): a thread per workspace follows the system's changes under its
// folder and gathers them into one batch until the folder has been quiet for 100 ms (or for at most a second of
// steady changes), then hands the batch over with the server lock held.

include "changes.ch"

namespace Ied
{
	const int32 kQuietMs = 100;
	const int64 kLongestBatchMs = 1000;

	alias BatchHandler = func<void(const ChangeBatch&)>;

	class FolderWatch
	{
		[operator("create")] public void Init() {}

		// Starts the thread; false with `error` when the folder cannot be watched. `handle` runs on that thread.
		public bool Start(const FS::Path& folder, BatchHandler handle, Core::String& error);

		private void Run(BatchHandler handle);

		private FS::Watcher* watcher;
		private Threading::Thread thread;
	}
}
