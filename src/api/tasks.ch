// Tasks and problems through the host (docs/26 "What stays the host's", docs/06): running a project's task as the
// toolbar does and waiting for its end, and a workspace's problems - every file's diagnostics by source, as the Error
// List shows them.

include "workspace.ch"

namespace IED
{
	class TaskOutcome
	{
		public bool succeeded;
		public bool cancelled;
		public Core::String label;      // the task as the toolbar names it
		public int64 ms;                // how long it ran
		public Core::String tail;       // the end of its output
		public Core::String problems;   // the problems the run's matchers found: {<path>: {<source>: [diagnostic]}}
		public Core::String problem;    // why it could not run
	}

	// Runs the task `key` (or the one in `slot`: "build", "run", "test") of `project` (empty: the startup project) of
	// workspace `ws` and waits for its end; process.spawn, a trusted workspace.
	TaskOutcome RunTask(const Workspace& ws, Core::StringView project, Core::StringView key, Core::StringView slot);
	// `ws`'s problems as the diag:<wsId> snapshot holds them: {files: {<path>: {<source>: [diagnostic]}}}.
	Core::String Problems(const Workspace& ws);
}
