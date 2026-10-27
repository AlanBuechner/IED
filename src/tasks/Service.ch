// Tasks (docs/06 "Protocol and state"): what a workspace's projects offer to run, the toolbar's selections, and runs -
// task.list / select / setStartup / run / cancel / runs, the tasks:<wsId> topic and a run:<runId> topic per run. Members
// run with the server lock held.

include "Runner.ch"
include "Expand.ch"
include "Selections.ch"
include "solutions/Model.ch"
include "web/Rpc.ch"

namespace Ied
{
	const int64 c_TaskRunsKept = 50;
	const int64 c_TaskNestingDepth = 16;

	// A project as tasks see it: a solution's project, or a folder workspace as one.
	class TaskProject
	{
		public Core::String m_Name;
		public FS::Path m_Root;
		public Core::String m_Kind;
		public Serialization::DomNode m_Data;    // its .iedproj; an invalid node for a folder
		public Core::String m_Origin;            // its file's name, for problems
	}

	class Tasks
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);
		// The project files changed (or the plugins): the offer is published again.
		public void ProjectsChanged(Workspace* ws);
		// Starts the task `key` (or the one in `slot`) of `projectName` (empty: the startup project), as task.run does.
		// Throws not_found and bad_request as task.run answers them.
		public TaskRun* RunTask(Workspace* ws, Core::StringView projectName, Core::StringView key, Core::StringView slot, const Serialization::DomNode& context);
		// Starts every project's task in `slot` (build, rebuild, clean or test) as one run, as task.run with `solution`
		// does (VS's Build Solution). Throws not_found when no project has one.
		public TaskRun* RunSolution(Workspace* ws, Core::StringView slot, const Serialization::DomNode& context);
		// Waits, the lock released, until `run` ends: whether it succeeded.
		public bool AwaitEnd(TaskRun* run);
		// The debug configuration `name` (empty: the default) of `projectName` (empty: the startup project), its strings
		// expanded as a task's are. Throws not_found when the project has none.
		public DebugLaunch ResolveDebug(Workspace* ws, Core::StringView projectName, Core::StringView name);
		// The run `runId` of the last kTaskRunsKept, or null.
		public TaskRun* Run(Core::StringView runId) const;
		// Cancels `run` as task.cancel does: its process trees are killed.
		public void Cancel(TaskRun* run);
		// A variable a plugin offers the tasks of workspace `wsId` (docs/06 "Variables": ${<name>}, such as the profiler's
		// ${profiler.connect}); an empty value takes it away.
		public void SetVariable(Core::StringView wsId, Core::StringView name, Core::StringView value);

	private:
		private Core::Array<TaskProject*> ProjectsOf(Workspace* ws) const;
		private TaskProject* ProjectNamed(Workspace* ws, Core::StringView name) const;
		private Core::String StartupProject(Workspace* ws);
		private OfferedTasks* Offered(Workspace* ws, const TaskProject* project);
		private void ReadPresets(Workspace* ws, const TaskProject* project, OfferedTasks& into);
		private TaskSelections* SelectionsOf(Workspace* ws);
		private void SaveSelections(Workspace* ws);
		private Core::String ValueOf(Workspace* ws, const TaskProject* project, const TaskVariable* variable);
		private Core::String ListJson(Workspace* ws, Core::StringView projectName);
		private Core::String Snapshot(Workspace* ws);

		// The run of `task` planned for `project`; throws bad_request naming what does not expand or exist.
		private TaskRun* Plan(Workspace* ws, const TaskProject* project, const OfferedTasks* offered, const TaskDef* task, const Serialization::DomNode& context);
		// A run with its id, nothing planned in it yet.
		private TaskRun* NewRun(Workspace* ws);
		// The run of every project's task in `slot`, one project after another in the solution's order: a project
		// without one is left out, and one that fails does not stop those after it (the run fails).
		private TaskRun* PlanSolution(Workspace* ws, Core::StringView slot, const Serialization::DomNode& context);
		// Starts `run` after another run of its project and task, as its concurrency says.
		private void Queue(TaskRun* run);
		// What ${...} stands for in `project`'s tasks and debug configurations (docs/06 "Variables").
		private TaskLookup LookupFor(Workspace* ws, const TaskProject* project, const OfferedTasks* offered, const Serialization::DomNode& context);
		// `entry` is the onlyIf of the step that runs `task` (a task step's), given to its first step when that one has the
		// default.
		private void PlanSteps(TaskRun* run, const TaskDef* task, const OfferedTasks* offered, const TaskLookup& lookup, Core::Array<Core::String>& nesting, OnlyIf entry);
		private ProblemMatcher* MatcherNamed(Workspace* ws, Core::StringView id);
		private void Start(TaskRun* run);

		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String SelectMethod(Call& call, const Params& params);
		private Core::String SetStartupMethod(Call& call, const Params& params);
		private Core::String RunMethod(Call& call, const Params& params);
		private Core::String CancelMethod(Call& call, const Params& params);
		private Core::String RunsMethod(Call& call, const Params& params);

		private Server* m_Server;
		private Core::HashMap<Core::String, TaskSelections*> m_Selections;   // by wsId
		private Core::HashMap<Core::String, Core::HashMap<Core::String, Core::String>*> m_OfferedVariables;   // by wsId
		private Core::Array<TaskRun*> m_Runs;
		private TaskRun* LastEnded(Workspace* ws);
		private int64 m_Started;
		private Threading::ConditionVariable m_Ended;
	}
}
