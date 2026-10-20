// Blobs (docs/10): uploads (PUT /api/upload) kept by id for GET /api/blob/<id>, in memory. Thread-safe: HTTP handlers
// use them without the server lock.

include "bedrock.ch"

namespace Ied
{
	class Blob
	{
		public Core::String contentType;
		public Core::Array<uint8> bytes;
	}

	class Blobs
	{
		// The new blob's id: "b-" and 16 random hex digits.
		public Core::String Put(Core::StringView contentType, const Core::Array<uint8>& bytes);
		public Blob* Get(Core::StringView id);

		private Threading::Mutex mutex;
		private Core::HashMap<Core::String, Blob*> items;
	}
}
