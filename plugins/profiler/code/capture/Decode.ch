// Turns ITP bytes into events (docs/16): a stream cut into chunks as its bytes arrive, and each chunk into events with
// absolute times. An unknown event type ends its chunk (the rest cannot be read without its layout) and says so.

include "Wire.ch"

namespace Profiler
{
	const int32 c_EventMostFields = 6;
	const int64 c_ChunkMostBytes = 67108864;

	// One decoded event. `fields` holds its varints in the order docs/16 lists them, without the time and strings.
	struct Event
	{
		public EventType m_Type;
		public uint8 m_ChunkKind;
		public uint64 m_Thread;       // the chunk's thread id; Alloc, Free and Sample name their own
		public bool m_Timed;
		public int64 m_Ticks;         // absolute, when timed
		public uint64[6] m_Fields;
		public double m_Number;
		public Core::StringView m_Text;
		public Core::StringView m_Text2;
		public const Core::Array<uint64>* m_List;   // CallstackDef's addresses
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
		public Core::StringView Problem() const { return m_Problem.View(); }
		public bool StartedOk() const { return m_Started && m_Problem.IsEmpty(); }
		// Stream bytes taken as whole chunks so far, the preamble included.
		public int64 Consumed() const { return m_Consumed; }
		// Bytes held that are not a whole chunk yet.
		public int64 Pending() const { return m_Buffer.Count() - m_At; }

		private bool ReadPreamble();
		private Core::Array<uint8> m_Buffer;
		private int64 m_At;
		private int64 m_Consumed;
		private bool m_Started;
		private Core::String m_Problem;
	}
}
