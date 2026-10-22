// The AI's view of the profiler (docs/13 T-PRF): each profile_* tool's answer as compact text a model reads well -
// times with units, tables with aligned columns, trees indented - made from the same queries the UI asks.

include "service.ch"

namespace Profiler
{
	// "850 ns", "12.4 us", "3.21 ms", "1.234 s".
	Core::String Duration(int64 ns);
	// A point in the capture as precisely as from_ms and to_ms take it: "1290.050 ms".
	Core::String Instant(int64 ns);
	Serialization::DomNode ParseJson(const Core::String& json);
	// A tool argument in milliseconds as nanoseconds; -1 when it is absent or null.
	int64 MsArg(const Serialization::DomNode& args, const char* name);

	// Rows of cells written as aligned columns: the first left-aligned, the rest right-aligned.
	class TextTable
	{
		public Core::Array<Core::Array<Core::String>*> rows;

		public Core::Array<Core::String>* Row();
		public void Write(Core::StringBuilder& out) const;
	}

	// profile_locks: each lock's waits and holds in a range, most waited for first.
	Core::String LocksText(const Capture& capture, const Serialization::DomNode& args);
	// The capture's busiest locks, for its summary ("" without locks).
	Core::String TopLocksText(const Serialization::DomNode& locks);
	// The capture's notes, for its summary ("" without any).
	Core::String NotesText(const Core::Array<Annotation>& notes);
	// profile_annotate: the AI's note on a range of capture `id`; the answer says what was added.
	Core::String AnnotateText(ProfilerPlugin* profiler, Core::StringView id, const Serialization::DomNode& args);

	// The capture a tool names - a capture id, a .iedtrace path in the workspace, a file name in the library, or
	// "latest"/empty for the newest (a live one first). Throws not_found. The lock is held.
	Core::String ResolveCapture(ProfilerPlugin* profiler, ProfWorkspace* ws, Core::StringView named);

	// The text of tool `tool` with `args` in workspace `ws`; throws an RpcError for a bad request.
	Core::String ToolAnswer(ProfilerPlugin* profiler, Core::StringView wsId, Core::StringView tool, const Serialization::DomNode& args);
}
