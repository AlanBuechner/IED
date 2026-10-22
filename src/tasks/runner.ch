// A task run (docs/06 "Protocol and state"): steps planned when it starts, then run one after another on a thread of
// its own that holds the server lock only to change state - a step's process in a job (cancel kills its tree), its
// output streamed to run:<runId>, to the step's output source and through its problem matchers to the diagnostics.

include "model.ch"
include "matcher.ch"
include "services/workspaces.ch"

namespace Ied
{
	enum class RunStatus { Queued, Running, Succeeded, Failed, Cancelled }

	const int64 kRunTailBytes = 65536;

	// A step ready to run: its strings expanded, its matchers found.
	class PlannedStep
	{
		public StepKind kind;
		public Core::String label;
		public Core::Array<Core::String> argv;    // what runs, as argv
		public Core::String shell;                // or a command line the system's shell runs as written
		public Core::String programSetting;       // the setting its program comes from (${config:KEY} first), or ""
		public Core::String cwd;
		public Core::Array<Core::String> env;
		public Core::String output;               // the output source
		public Core::Array<ProblemMatcher*> matchers;
		public Core::Array<Core::String> matchRoots;   // the matchers' fileLocation.roots, expanded: searched after the cwd
		public OnlyIf onlyIf;
		public int64 timeoutSeconds;
		public Core::String status;               // pending, running, ok, failed, skipped, cancelled
		public int64 exitCode;
	}

	class TaskRun
	{
		public Core::String runId;
		public Workspace* ws;
		public Core::String project;
		public Core::String task;
		public Core::String label;
		public Concurrency concurrency;
		public bool saveFirst;
		public Core::Array<Core::String> searchRoots;   // where a matched relative file is looked for, after the step's cwd
		public Core::Array<PlannedStep*> steps;
		public TaskRun* after;                          // a queued run starts once this one has ended
		public RunStatus status;
		public int64 exitCode;
		public int64 startedMs;
		public int64 endedMs;
		public Core::String tail;
		public bool cancelled;
		public Process::Child* child;                  // the step's process while it runs
		public Core::HashMap<Core::String, Core::String> problems;   // "<source>\n<path>" -> its items so far, comma-joined

		public Core::String Topic() const { return Core::String("run:") + runId.View(); }
		public bool IsActive() const { return status == RunStatus::Queued || status == RunStatus::Running; }
		// {runId, project, task, label, status, exitCode, startedMs, endedMs}
		public Core::String SummaryJson() const;
		// The summary with {wsId, steps: [{step, label, status, exitCode}], tail}
		public Core::String SnapshotJson() const;
	}

	const char* RunStatusName(RunStatus status);
	// tasks:<wsId>: what the workspace offers, and its runs starting and ending (docs/06).
	Core::String TasksTopic(const Workspace* ws);
	// The diagnostics source of a matcher's problems in a project's run: "<source>/<project>".
	Core::String ProblemSource(const ProblemMatcher* matcher, Core::StringView project);
	// The problems a run found as the diagnostics snapshot holds them: {<path>: {<source>: [diagnostic]}}.
	Core::String RunProblemsJson(const TaskRun* run);

	// Runs `run` to its end; on a thread of its own, holding no lock. `ended` is notified, under the server lock, when a
	// run ends.
	void ExecuteRun(Server* server, TaskRun* run, Threading::ConditionVariable* ended);
}
