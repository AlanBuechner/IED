// The AI's view of the profiler (docs/13 T-PRF): each profile_* tool's answer as compact text a model reads well -
// times with units, tables with aligned columns, trees indented - made from the same queries the UI asks.

include "service.ch"

namespace Profiler
{
	// "850 ns", "12.4 us", "3.21 ms", "1.234 s".
	Core::String Duration(int64 ns);

	// The capture a tool names - a capture id, a .iedtrace path in the workspace, a file name in the library, or
	// "latest"/empty for the newest (a live one first). Throws not_found. The lock is held.
	Core::String ResolveCapture(ProfilerPlugin* profiler, ProfWorkspace* ws, Core::StringView named);

	// The text of tool `tool` with `args` in workspace `ws`; throws an RpcError for a bad request.
	Core::String ToolAnswer(ProfilerPlugin* profiler, Core::StringView wsId, Core::StringView tool, const Serialization::DomNode& args);
}
