// What is at a path, copies, and absolute and real paths (D-69). Times are Unix time in nanoseconds, the clock of
// Platform::UnixTimeNs. Paths the system makes come back as it writes them ('\' on Windows).
//     FS::FileInfo info;
//     if (FS::GetInfo("game/assets/hero.png", info) && info.modifiedNs > lastBuildNs) Rebuild();
//     FS::CopyTree("templates/empty", "projects/new");

include "directory.ch"

namespace FS
{
	struct FileInfo
	{
		public EntryKind kind;
		public bool readOnly;
		public int64 size;          // bytes; 0 for a directory or a link
		public int64 modifiedNs;
		public int64 createdNs;
		public int64 accessedNs;
	}

	// What is at `path` into `info`; false when nothing is there or it cannot be read. A link (a symbolic link or a
	// junction) is followed to what it points to, unless `followLinks` is false: then it is described itself.
	bool GetInfo(const char* path, FileInfo& info, bool followLinks = true);
	// Sets a file's or directory's modification time.
	bool SetModifiedTime(const char* path, int64 unixNs);
	// Makes a file read-only (RemoveFile and writes then fail), or writable again.
	bool SetReadOnly(const char* path, bool readOnly);

	// Copies a file with its attributes and modification time; with `replace` false it fails when `to` exists.
	bool CopyFile(const char* from, const char* to, bool replace = true);
	// Copies the directory `from` with everything in it into `to` (made when missing), replacing files already there.
	// Links are neither followed nor copied. The files and directories copied, or -1 when one could not be (the rest
	// is still copied), `from` is not a directory, or `to` is inside it.
	int64 CopyTree(const char* from, const char* to);

	// `path` made absolute against the working directory, with "." and ".." folded away, without reading the file
	// system; an empty path when it cannot be.
	Path AbsolutePath(const char* path);
	// The path the file system resolves `path` to: absolute, links followed, each name in its stored case. An empty
	// path when nothing is there.
	Path CanonicalPath(const char* path);
}
