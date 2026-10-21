// Outputs (docs/10 "out:<wsId>:<sourceId>"): a workspace's output sources - text that only grows - each a coalescible
// topic whose snapshot is its label and its last 64 KB, with `append {text}` events that merge for a slow client and
// `clear`. The "ied" source of every workspace is the server's own log (docs/02: validation failures show there);
// tasks add theirs in M3. out.sources lists a workspace's sources, out.clear empties one. Members run with the server
// lock held.

include "../web/rpc.ch"

namespace Ied
{
	const int64 kOutputTailBytes = 65536;
	const char* kServerLogSource = "ied";

	class OutputSource
	{
		public Core::String id;
		public Core::String label;
		public Core::String tail;   // at most kOutputTailBytes (Bounded)
	}

	// One workspace's sources, with the workspace itself: the log reaches them without a lookup that could run before
	// the server has its workspaces.
	class WorkspaceOutputs
	{
		public Workspace* ws;
		public Core::Array<OutputSource*> sources;
	}

	class Outputs
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// A workspace opened: its sources, the server log's with what it holds so far.
		public void Opened(Workspace* ws);
		public void Append(Workspace* ws, Core::StringView sourceId, Core::StringView text);
		// A line of the server log, to every workspace's "ied" source.
		public void Log(Core::StringView line);

	private:
		private OutputSource* Find(Workspace* ws, Core::StringView sourceId) const;
		private Core::String Snapshot(const OutputSource* source) const;
		private Core::String SourcesMethod(Call& call, const Params& params);
		private Core::String ClearMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<WorkspaceOutputs*> opened;
		private Core::String logTail;                                               // the server log so far
	}

	// `tail` with `text` added, cut to its last `limit` bytes at a line start - or, when those bytes hold no line break
	// (one long line), at a character start.
	Core::String Bounded(Core::StringView tail, Core::StringView text, int64 limit);
	Core::String OutputTopic(const Workspace* ws, Core::StringView sourceId);
}
