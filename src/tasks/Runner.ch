// A task run (docs/06 "Protocol and state"): steps planned when it starts, then run one after another on a thread of
// its own that holds the server lock only to change state - a step's process in a job (cancel kills its tree), its
// output streamed to run:<runId>, to the step's output source and through its problem matchers to the diagnostics.

include "Model.ch"
include "Matcher.ch"
include "services/Workspaces.ch"

namespace Ied
{
	enum class RunStatus { Queued, Running, Succeeded, Failed, Cancelled }

	const int64 c_RunTailBytes = 65536;

	// A step ready to run: its strings expanded, its matchers found.
	class PlannedStep
	{
		public StepKind m_Kind;
		public Core::String m_Label;
		public Core::Array<Core::String> m_Argv;    // what runs, as argv
		public Core::String m_Shell;                // or a command line the system's shell runs as written
		public Core::String m_ProgramSetting;       // the setting its program comes from (${config:KEY} first), or ""
		public Core::String m_Cwd;
		public Core::Array<Core::String> m_Env;
		public Core::String m_Output;               // the output source
		public Core::Array<ProblemMatcher*> m_Matchers;
		public Core::Array<Core::String> m_MatchRoots;   // the matchers' fileLocation.roots, expanded: searched after the cwd
		public OnlyIf m_OnlyIf;
		public int64 m_TimeoutSeconds;
		public Core::String m_Status;               // pending, running, ok, failed, skipped, cancelled
		public int64 m_ExitCode;
	}

	class TaskRun
	{
		public Core::String m_RunId;
		public Workspace* m_Ws;
		public Core::String m_Project;
		public Core::String m_Task;
		public Core::String m_Label;
		public Concurrency m_Concurrency;
		public bool m_SaveFirst;
		public Core::Array<Core::String> m_SearchRoots;   // where a matched relative file is looked for, after the step's cwd
		public Core::Array<PlannedStep*> m_Steps;
		public TaskRun* m_After;                          // a queued run starts once this one has ended
		public RunStatus m_Status;
		public int64 m_ExitCode;
		public int64 m_StartedMs;
		public int64 m_EndedMs;
		public Core::String m_Tail;
		public bool m_Cancelled;
		public Process::Child* m_Child;                  // the step's process while it runs
		public Core::HashMap<Core::String, Core::String> m_Problems;   // "<source>\n<path>" -> its items so far, comma-joined

		public Core::String Topic() const { return Core::String("run:") + m_RunId.View(); }
		public bool IsActive() const { return m_Status == RunStatus::Queued || m_Status == RunStatus::Running; }
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
	// Where a matched file is, as a path in the workspace: as written when absolute (or `absolute`), else the first of
	// `roots` that has it; empty when it is not in the workspace or nowhere to be found.
	Core::String ResolveProblemFile(Core::StringView file, bool absolute, const Core::Array<Core::String>& roots, Core::StringView workspaceRoot);
	// Where a run of one project looks for a matched file after its step's own roots: the project, the workspace, then
	// every other project of the workspace and its src - a build compiles its dependencies' sources too, and a compiler
	// names their files from their own folders.
	Core::Array<Core::String> RunSearchRoots(Core::StringView projectRoot, Core::StringView workspaceRoot, const Core::Array<Core::String>& otherProjects);

	// Runs `run` to its end; on a thread of its own, holding no lock. `ended` is notified, under the server lock, when a
	// run ends.
	void ExecuteRun(Server* server, TaskRun* run, Threading::ConditionVariable* ended);
}
