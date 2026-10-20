// Watcher: the changes made under a directory, as they happen (D-69). It owns the system's watch and closes it on
// delete, so it cannot be copied (move it with take()); a zero Watcher is closed. Every change made after the
// Watcher opened comes out of Next, or an Overflow in its place when too many came at once to keep (then look at the
// directory again). Changes come as the system reports them: saving a file is often several Modified changes, and an
// editor's safe save a Renamed or a Removed and an Added, so coalesce them before acting.
//     FS::Watcher assets = FS::Watcher("game/assets");
//     FS::Change change;
//     while (assets.Next(change, 500))
//         if (change.kind == FS::ChangeKind::Modified) Reload(change.path);

include "path.ch"

namespace FS
{
	// How long Next may wait: milliseconds, 0 to only check, or kWaitForever.
	const int32 kWaitForever = -1;

	// Overflow: changes were lost; the paths are empty.
	enum class ChangeKind { Added = 0, Removed = 1, Modified = 2, Renamed = 3, Overflow = 4 }

	struct Change
	{
		public ChangeKind kind;
		public Core::String path;      // relative to the watched directory, as the system writes it
		public Core::String oldPath;   // a rename's path before it; empty otherwise
	}

	class Watcher
	{
		[operator("create")] public void Init() {}   // closed
		// Watches the directory at `path` and everything under it; closed with Error() when it cannot.
		[operator("create")] public void Init(const char* path) { Open(path, true); }
		// With `recursive` false, only the directory's own entries.
		[operator("create")] public void Init(const char* path, bool recursive) { Open(path, recursive); }
		[operator("delete")] public void Delete() { Close(); }

		public bool IsOpen() const { return handle != nullptr; }
		// The next change into `change`; false when none comes within `timeoutMs` (TimedOut()), or watching failed
		// (Error(): the directory was removed, or the Watcher is closed).
		public bool Next(Change& change, int32 timeoutMs = kWaitForever);
		public void Close();   // idempotent

		public const char* Error() const { return error.CStr(); }
		public bool TimedOut() const { return timedOut; }

		private void Open(const char* path, bool recursive);

		private void* handle;   // the runtime's watch, not GC memory
		private Core::String error;
		private bool timedOut;
	}
}
