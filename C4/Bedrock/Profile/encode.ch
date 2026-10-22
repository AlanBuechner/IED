// The IED Trace Protocol's bytes (IED docs/16 "ITP v1 wire format"): little-endian varints, zig-zag time deltas,
// length-prefixed text, raw doubles, and the event types a client sends.

include "Container/array.ch"
include "Text/stringview.ch"

namespace Profile
{
	enum class Event : uint8
	{
		Hello = 0x01, Calibration = 0x02, Bye = 0x03, Stats = 0x05,
		StringDef = 0x10, SrcLocDef = 0x11, ThreadDef = 0x12, ThreadName = 0x13,
		ZoneBegin = 0x20, ZoneBeginNamed = 0x21, ZoneEnd = 0x22, ZoneText = 0x23, ZoneValue = 0x24, ZoneAllocs = 0x27,
		TaskBegin = 0x30, TaskEnd = 0x31, TaskSuspend = 0x32, TaskResume = 0x33,
		FrameMark = 0x40, FrameBegin = 0x41, FrameEnd = 0x42,
		PlotDef = 0x50, PlotValue = 0x51, Message = 0x52,
		PoolDef = 0x60, Alloc = 0x61, Free = 0x62, PoolReset = 0x63, GcBegin = 0x68, GcEnd = 0x69, AllocCount = 0x6A,
		HeapCensus = 0x6B, HeapCensusType = 0x6C,
		LockDef = 0x70, LockWait = 0x71, LockObtain = 0x72, LockRelease = 0x73,
		CallstackDef = 0x81, Sample = 0x82, SymbolDef = 0x83
	}

	const uint8 kThreadChunk = 0;
	const uint8 kGlobalChunk = 1;
	const uint16 kProtocolVersion = 1;

	void PutByte(Core::Array<uint8>& out, uint8 value);
	void PutVarint(Core::Array<uint8>& out, uint64 value);
	void PutZigzag(Core::Array<uint8>& out, int64 value);
	void PutText(Core::Array<uint8>& out, Core::StringView text);
	void PutDouble(Core::Array<uint8>& out, double value);
	void PutEvent(Core::Array<uint8>& out, Event type);

	// The stream's first 8 bytes: "ITP1", the version, no flags.
	void PutPreamble(Core::Array<uint8>& out);
	// A whole chunk: its length, kind, thread and base time, then `body`.
	void PutChunk(Core::Array<uint8>& out, uint8 kind, uint64 thread, int64 baseTicks, const Core::Array<uint8>& body);
}
