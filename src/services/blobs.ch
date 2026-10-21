// Blobs (docs/10): uploads (PUT /api/upload) kept by id for GET /api/blob/<id>, in memory, at most a limit in all: past
// it the oldest are dropped (their ids answer 404). Thread-safe: HTTP handlers use them without the server lock.

include "bedrock.ch"

namespace Ied
{
	const int64 kBlobsMaxBytes = 256 * 1024 * 1024;

	class Blob
	{
		public Core::String contentType;
		public Core::Array<uint8> bytes;
	}

	class Blobs
	{
		[operator("create")] public void Init() { limitBytes = kBlobsMaxBytes; }
		[operator("create")] public void Init(int64 limit) { limitBytes = limit; }

		// The new blob's id: "b-" and 16 random hex digits.
		public Core::String Put(Core::StringView contentType, const Core::Array<uint8>& bytes);
		public Blob* Get(Core::StringView id);
		public int64 KeptBytes();

		private void DropOldest();

		private Threading::Mutex mutex;
		private Core::HashMap<Core::String, Blob*> items;
		private Core::Array<Core::String> order;   // ids, oldest first
		private int64 keptBytes;
		private int64 limitBytes;
	}
}
