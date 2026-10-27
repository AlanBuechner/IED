// Blobs from plugins (docs/10 "GET /api/blob/<id>"): bytes a plugin hands its UI by id - a decoded texture, a file to
// save - kept in the server's memory with the uploads, the oldest dropped past their limit.

include "bedrock.ch"

namespace IED
{
	// The new blob's id for `bytes` of `contentType`; "" with `problem` when it cannot be kept.
	Core::String PutBlob(Core::StringView contentType, const Core::Array<uint8>& bytes, Core::String& problem);
	// A blob's bytes and type - an upload a client made (`PUT /api/upload`), a plugin's blob; false with `problem` when
	// the server no longer keeps it (the oldest are dropped past their limit).
	bool ReadBlob(Core::StringView id, Core::String& contentType, Core::Array<uint8>& bytes, Core::String& problem);
}
