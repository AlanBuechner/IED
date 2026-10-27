// Tasks and problems through the host (docs/26 "What stays the host's", docs/06): running a project's task as the
// toolbar does and waiting for its end, and a workspace's problems - every file's diagnostics by source, as the Error
// List shows them.

include "Workspace.ch"

namespace IED
{
	class TaskOutcome
	{
		public bool m_Succeeded;
		public bool m_Cancelled;
		public Core::String m_Label;      // the task as the toolbar names it
		public int64 m_Ms;                // how long it ran
		public Core::String m_Tail;       // the end of its output
		public Core::String m_Problems;   // the problems the run's matchers found: {<path>: {<source>: [diagnostic]}}
		public Core::String m_Problem;    // why it could not run
	}

	// Runs the task `key` (or the one in `slot`: "build", "run", "test") of `project` (empty: the startup project) of
	// workspace `ws` and waits for its end; process.spawn, a trusted workspace.
	TaskOutcome RunTask(const Workspace& ws, Core::StringView project, Core::StringView key, Core::StringView slot);
	// Starts the task as RunTask does without waiting for it: the run's id, or empty with `problem`.
	Core::String StartTask(const Workspace& ws, Core::StringView project, Core::StringView key, Core::StringView slot, Core::String& problem);
	// Whether the run StartTask started has ended, with its outcome then; false while it runs (or for no such run, with
	// outcome.problem saying so).
	bool TaskEnded(Core::StringView runId, TaskOutcome& outcome);
	// Cancels a run StartTask started (its process trees are killed); false when no such run is going.
	bool CancelTask(Core::StringView runId);
	// `ws`'s problems as the diag:<wsId> snapshot holds them: {files: {<path>: {<source>: [diagnostic]}}}.
	Core::String Problems(const Workspace& ws);
	// Offers the variable ${<name>} to the tasks of workspace `wsId` (docs/06 "Variables"), as the profiler offers
	// ${profiler.connect}; an empty value takes it away. False when the caller is not a plugin's code.
	bool SetTaskVariable(Core::StringView wsId, Core::StringView name, Core::StringView value);
}
