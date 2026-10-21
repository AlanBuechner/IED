// Builds an ITP stream from events an importer reads in another format (docs/16 "Trace files"): strings and source
// locations interned on first use, each thread's events in a chunk of their own, and every definition sent in a global
// chunk just before the chunk that first names it - the same rules a live client follows.

include "wire.ch"

namespace Profiler
{
	class StreamBuilder
	{
		[operator("create")] public void Init();
		public void Hello(Core::StringView app, uint64 pid, int64 clockFreqHz, int64 startTicks, int64 startUnixNs);

		public uint64 String(Core::StringView text);
		public uint64 Site(Core::StringView name, Core::StringView function, Core::StringView file, uint64 line);

		// Events go to `Body()` between these; `thread` 0 is a global chunk.
		public void BeginChunk(uint64 thread, int64 baseTicks);
		public void EndChunk();
		public WireWriter& Body() { return *body; }

		// The whole stream.
		public const Core::Array<uint8>& Bytes() const { return out.Bytes(); }

		private void Append(const WireWriter& from);
		private void OpenDefinitions();
		private WireWriter out;
		private WireWriter* defs;
		private WireWriter* body;
		private bool defsOpen;
		private Core::HashMap<Core::String, uint64> strings;
		private Core::HashMap<Core::String, uint64> sites;
	}
}
