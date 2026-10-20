// Directory: an open listing of a directory's entries (std::filesystem::directory_iterator), without `.` and
// `..`, in the order the file system keeps them. It owns the OS handle and closes it on delete, so it cannot
// be copied. A listing is read once: range-for over it, or call Next.
//     for (FS::DirectoryEntry& entry : FS::Directory("assets"))
//         if (entry.kind == FS::EntryKind::File) printf("%s\n", entry.name.CStr());
// The free functions change the tree; paths are UTF-8, as everywhere in C4. A link (a symbolic link or a
// junction) is listed as a Link and never followed when the tree is changed.

include "path.ch"
include "file.ch"

namespace FS
{
	enum class EntryKind { File, Directory, Link }   // the runtime's kEntry* values

	struct DirectoryEntry {
		public Core::String name;   // the entry's own name, not its path
		public EntryKind kind;
	}

	class Directory;

	struct DirectoryIterator {
		public Directory* directory;
		public DirectoryEntry entry;
		public bool done;
		[operator("notequal")] public bool NotEqual(const DirectoryIterator& other) const { return done != other.done; }
		[operator("dereference")] public DirectoryEntry& Element() { return entry; }
		[operator("increment")] public DirectoryIterator Next();
	}

	class Directory
	{
		[operator("create")] public void Init() {}                    // closed
		[operator("create")] public void Init(const char* path);      // closed when `path` is not a readable directory
		[operator("delete")] public void Delete() { Close(); }

		public bool IsOpen() const { return handle != nullptr; }
		// The next entry into `entry`; false at the end, on an error or on a closed listing.
		public bool Next(DirectoryEntry& entry);
		public void Close();   // idempotent

		[operator("begin")] public DirectoryIterator Begin();
		[operator("end")] public DirectoryIterator End() { DirectoryIterator it; it.done = true; return it; }

		private void* handle;   // the runtime's listing, not GC memory
	}

	bool IsDirectory(const char* path);   // a link to a directory counts
	bool IsLink(const char* path);
	// One directory; false when it exists already or its parent does not.
	bool MakeDirectory(const char* path);
	// The directory and every missing parent; true when `path` is a directory at the end.
	bool MakeDirectories(const char* path);
	bool RemoveEmptyDirectory(const char* path);
	// A file, a link (not what it points to), or a directory with everything in it, read-only files too. The
	// number of entries removed, 0 when there is nothing at `path`; -1 when one could not be (what could be
	// removed is gone).
	int64 RemoveAll(const char* path);
	// Moves a file or directory; a file already at `to` is replaced.
	bool Rename(const char* from, const char* to);

	// The longest UTF-8 name of one entry: 255 UTF-16 units of 3 bytes on Windows, 255 bytes elsewhere.
	const int32 kMaxEntryNameBytes = 1024;
}
