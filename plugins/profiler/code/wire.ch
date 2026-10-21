// The IED Trace Protocol v1 at the byte level (docs/16 "ITP v1 wire format"): event types, little-endian varints and
// zig-zag time deltas read from a chunk, and a writer that builds a stream (the importers and the tests use it).

include "bedrock.ch"

namespace Profiler
{
	const int64 kPreambleBytes = 8;
	const int64 kChunkHeaderLength = 4;
	const uint16 kItpVersion = 1;
	const uint8 kThreadChunk = 0;
	const uint8 kGlobalChunk = 1;

	enum class EventType : uint8
	{
		Hello = 0x01, Calibration = 0x02, Bye = 0x03, Warning = 0x04, Stats = 0x05,
		StringDef = 0x10, SrcLocDef = 0x11, ThreadDef = 0x12, ThreadName = 0x13,
		ZoneBegin = 0x20, ZoneBeginNamed = 0x21, ZoneEnd = 0x22, ZoneText = 0x23, ZoneValue = 0x24, ZoneColor = 0x25, ZoneCallstack = 0x26,
		TaskBegin = 0x30, TaskEnd = 0x31, TaskSuspend = 0x32, TaskResume = 0x33,
		FrameMark = 0x40, FrameBegin = 0x41, FrameEnd = 0x42,
		PlotDef = 0x50, PlotValue = 0x51, Message = 0x52,
		PoolDef = 0x60, Alloc = 0x61, Free = 0x62, PoolReset = 0x63, GcBegin = 0x68, GcEnd = 0x69,
		LockDef = 0x70, LockWait = 0x71, LockObtain = 0x72, LockRelease = 0x73,
		ModuleDef = 0x80, CallstackDef = 0x81, Sample = 0x82, SymbolDef = 0x83,
		GpuContextDef = 0x90, GpuCalibration = 0x91, GpuZoneBegin = 0x92, GpuZoneEnd = 0x93, GpuTime = 0x94
	}

	// Reads one chunk's bytes; every read says whether the bytes were there.
	struct WireReader
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(const uint8* bytes, int64 length) { data = bytes; end = length; }

		public bool AtEnd() const { return at >= end; }
		public int64 Offset() const { return at; }
		public bool Byte(uint8& out);
		public bool Varint(uint64& out);
		public bool Zigzag(int64& out);
		public bool Double(double& out);
		// A varint length, then that many bytes, viewed in place.
		public bool Text(Core::StringView& out);

		private const uint8* data;
		private int64 end;
		private int64 at;
	}

	int64 Unzigzag(uint64 value);
	uint64 Zigzag(int64 value);

	// Builds an ITP stream: the preamble, then chunks whose events are appended one at a time. Times are absolute
	// ticks; each is written as the delta from the chunk's previous one.
	class WireWriter
	{
		[operator("create")] public void Init();

		public void BeginChunk(uint8 kind, uint64 threadId, int64 baseTicks);
		public void EndChunk();
		public void Type(EventType type);
		public void Time(int64 ticks);
		public void Varint(uint64 value);
		public void Double(double value);
		public void Text(Core::StringView value);
		// Bytes appended as they are (whole chunks another writer built).
		public void Append(const uint8* bytes, int64 count) { out.Append(bytes, count); }

		public const Core::Array<uint8>& Bytes() const { return out; }
		public int64 Length() const { return out.Count(); }

		private Core::Array<uint8> out;
		private int64 chunkStart = -1;
		private int64 cursor;
	}
}
