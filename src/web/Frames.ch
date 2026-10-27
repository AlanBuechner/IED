// Protocol v1 frames (docs/10, shared/host-api/protocol/frames.v1.schema.json): what a client frame must look like,
// and the frames the server writes.

include "Json.ch"

namespace Ied
{
	const int32 c_ProtocolVersion = 1;

	// Why `frame` is not a valid client frame, or empty when it is one.
	Core::String FrameProblem(const Serialization::DomNode& frame);
	// A topic key: "<kind>[:<id>]", optionally under "<plugin>/" (lowercase names).
	bool IsTopicKey(Core::StringView key);

	Core::String PongFrame(const Serialization::DomNode& ts);
	Core::String ByeFrame(const char* reason);
	Core::String ResultFrame(int64 id, bool ok, Core::StringView json);
	Core::String GapFrame(Core::StringView topic, int64 first, int64 last);
}
