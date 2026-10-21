// .iedtrace files (docs/16 "Trace files"): the ITP stream exactly as it arrived, then an index block holding the
// capture's metadata as JSON, then a 16-byte footer { indexOffset u64, version u16, reserved u16, "ITPX" }. A file
// without a footer - a crash, or a client writing in file mode - is the stream alone and still opens.

include "ingest.ch"

namespace Profiler
{
	const int64 kFooterBytes = 16;
	const uint16 kTraceFileVersion = 1;

	// Writes a capture's stream as it arrives and its index block at the end.
	class TraceWriter
	{
		public bool Open(const char* path, Core::String& problem);
		public bool IsOpen() const { return file.IsOpen(); }
		public void Append(const uint8* bytes, int64 length);
		public void Flush() { file.Flush(); }
		// The index block and footer after the stream; the file is closed.
		public void Finish(Core::StringView metadata);
		public int64 Written() const { return written; }

		private FS::File file;
		private int64 written;
	}

	// Where the stream of a trace file ends and the metadata its index block holds ("" without one).
	struct TraceLayout
	{
		public int64 streamBytes;
		public int64 fileBytes;
		public Core::String metadata;
		public bool finished;
	}

	bool ReadLayout(const char* path, TraceLayout& layout, Core::String& problem);

	// Reads a whole trace file into `capture` and finishes it.
	bool LoadTraceFile(const char* path, Capture& capture, TraceLayout& layout, Core::String& problem);

	// Reads a stream held in memory (an importer's output) into `capture` and finishes it.
	void LoadStream(Capture& capture, const uint8* bytes, int64 length);

	// The Hello at the start of a trace file without metadata: its app name and start time.
	bool PeekHello(const char* path, Core::String& app, int64& startUnixNs);

	// Replaces a finished file's metadata (annotations, a rename), keeping its stream.
	bool RewriteMetadata(const char* path, Core::StringView metadata, Core::String& problem);

	// Writes a whole stream and its metadata as a trace file (an import).
	bool WriteTraceFile(const char* path, const Core::Array<uint8>& stream, Core::StringView metadata, Core::String& problem);
}
