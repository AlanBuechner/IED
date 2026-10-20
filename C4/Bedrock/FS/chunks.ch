// ChunkReader: a file read front to back in chunks, each next one read on a thread of its own while the caller works
// on the one it has (roadmap 3), so reading and decoding overlap. It owns the file and the thread, so it cannot be
// copied: move it with take(). A reader whose file could not be opened is closed and has no chunks.
//     FS::ChunkReader file = FS::ChunkReader("level.json");
//     for (Core::StringView chunk = file.Next(); !chunk.IsEmpty(); chunk = file.Next()) json.Feed(chunk);

include "file.ch"
include "../Container/array.ch"
include "../Threading/thread.ch"
include "../Threading/mutex.ch"
include "../Threading/condition.ch"

namespace FS
{
	const int64 kDefaultChunkBytes = 1048576;

	// What a reader and its thread share. It lives on the heap, so the reader can move while the thread runs.
	class ChunkState
	{
		public File file;
		public Core::Array<uint8> front;   // the caller's chunk
		public Core::Array<uint8> back;    // the next one
		public int64 backBytes;            // what reading the next one gave: 0 at the end of the file, -1 on an error
		public bool backReady;
		public bool wanted;                // the caller took the last one: read another
		public bool stopping;
		public Threading::Mutex mutex;
		public Threading::ConditionVariable changed;
	}

	class ChunkReader
	{
		[operator("create")] public void Init() {}
		// Opens `path` and starts reading its first chunk. `chunkBytes` below 1 reads the default.
		[operator("create")] public void Init(const char* path, int64 chunkBytes = kDefaultChunkBytes);
		[operator("delete")] public void Delete() { Close(); }

		public bool IsOpen() const { return state != nullptr; }
		// The next chunk, valid until the next call; empty at the end of the file, after a failed read and once closed.
		public Core::StringView Next();
		// A read failed: the chunks before it are all there is.
		public bool Failed() const { return failed; }
		// Stops the thread and closes the file; idempotent.
		public void Close();

		private ChunkState* state;
		private Threading::Thread worker;
		private bool ended;   // the last chunk has been handed out: the thread has stopped
		private bool failed;
	}
}
