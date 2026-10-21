// File: an open file (std::fstream). It owns the stream and closes it on delete, so a value local is RAII;
// it cannot be copied (pass it by reference or move it with take()). Positions are 64-bit.
//     FS::File out = FS::File("out.txt", "w");
//     out.Write("hello", 5);
// Modes are C mode strings: "r" read, "w" write (truncate), "a" append, and the binary "rb", "wb", "ab".

include "Text/string.ch"

namespace FS
{
	enum class SeekOrigin { Start, Current, End }

	class File
	{
		[operator("create")] public void Init() {}                                  // closed
		[operator("create")] public void Init(const char* path, const char* mode);   // closed when it cannot open
		[operator("delete")] public void Delete() { Close(); }

		public bool IsOpen() const { return handle != nullptr; }
		// Bytes read or written, 0 at the end of the file, -1 on an error or a closed file.
		public int64 Read(void* buffer, int64 count);
		public int64 Write(const void* bytes, int64 count);
		public bool Flush();
		// Flush, then have the system write the data to the disk: it outlives a power loss.
		public bool Sync();
		public void Close();   // idempotent

		// The new position from the start, or -1 on an error.
		public int64 Seek(int64 offset) { return Seek(offset, SeekOrigin::Start); }
		public int64 Seek(int64 offset, SeekOrigin origin);
		public int64 Tell() const;   // -1 on a closed file
		// Reads until the end of the file or until `capacity` bytes; the count read, -1 on a closed file.
		public int64 ReadAll(void* buffer, int64 capacity);
		// The rest of the file as text.
		public Core::String ReadText();

		private void* handle;   // the C runtime's FILE*, not GC memory
	}

	bool FileExists(const char* path);   // an existing regular file
	int64 FileSize(const char* path);    // bytes, -1 when there is no such file
	bool RemoveFile(const char* path);
	// The file's whole content: written under a temporary name beside it, synced, then renamed over `path`, so a
	// crash or a power loss leaves the old content or the new one, never part of either. False, with the old file as it
	// was, when a step fails.
	bool WriteFileAtomically(const char* path, const void* bytes, int64 count);

	const int64 kReadTextChunkBytes = 4096;   // how much ReadText reads at a time
}
