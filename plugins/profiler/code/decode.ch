// Turns ITP bytes into events (docs/16): a stream cut into chunks as its bytes arrive, and each chunk into events with
// absolute times. An unknown event type ends its chunk (the rest cannot be read without its layout) and says so.

include "wire.ch"

namespace Profiler
{
	const int32 kEventMostFields = 6;
	const int64 kChunkMostBytes = 67108864;

	// One decoded event. `fields` holds its varints in the order docs/16 lists them, without the time and strings.
	struct Event
	{
		public EventType type;
		public uint8 chunkKind;
		public uint64 thread;       // the chunk's thread id; Alloc, Free and Sample name their own
		public bool timed;
		public int64 ticks;         // absolute, when timed
		public uint64[6] fields;
		public double number;
		public Core::StringView text;
		public Core::StringView text2;
		public const Core::Array<uint64>* list;   // CallstackDef's addresses
	}

	alias EventVisitor = func<void(const Event& event)>;

	// Visits the events of one chunk (the bytes after its length). False with `problem` when the chunk is malformed or
	// holds an event type this version does not know; the events before it were visited.
	bool DecodeChunk(const uint8* bytes, int64 length, const EventVisitor& visit, Core::String& problem);

	// The bytes of a stream as they arrive, cut into whole chunks.
	class ChunkStream
	{
		public void Feed(const uint8* bytes, int64 length);
		// The next whole chunk's bytes (after its length), valid until the next Feed; false when none is whole yet.
		public bool Next(const uint8*& chunk, int64& length);
		// Why the stream cannot be read (not ITP, a newer version, an impossible chunk length); empty while fine.
		public Core::StringView Problem() const { return problem.View(); }
		public bool StartedOk() const { return started && problem.IsEmpty(); }
		// Stream bytes taken as whole chunks so far, the preamble included.
		public int64 Consumed() const { return consumed; }
		// Bytes held that are not a whole chunk yet.
		public int64 Pending() const { return buffer.Count() - at; }

		private bool ReadPreamble();
		private Core::Array<uint8> buffer;
		private int64 at;
		private int64 consumed;
		private bool started;
		private Core::String problem;
	}
}
