// Tasks (docs/06 "Protocol and state"): what a workspace's projects offer to run, the toolbar's selections, and runs -
// task.list / select / setStartup / run / cancel / runs, the tasks:<wsId> topic and a run:<runId> topic per run. Members
// run with the server lock held.

include "runner.ch"
include "expand.ch"
include "selections.ch"
include "solutions/model.ch"
include "web/rpc.ch"

namespace Ied
{
	const int64 kTaskRunsKept = 50;
	const int64 kTaskNestingDepth = 16;

	// A project as tasks see it: a solution's project, or a folder workspace as one.
	class TaskProject
	{
		public Core::String name;
		public FS::Path root;
		public Core::String kind;
		public Serialization::DomNode data;    // its .ied.proj; an invalid node for a folder
		public Core::String origin;            // its file's name, for problems
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
		// Waits, the lock released, until `run` ends: whether it succeeded.
		public bool AwaitEnd(TaskRun* run);
		// The debug configuration `name` (empty: the default) of `projectName` (empty: the startup project), its strings
		// expanded as a task's are. Throws not_found when the project has none.
		public DebugLaunch ResolveDebug(Workspace* ws, Core::StringView projectName, Core::StringView name);

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

		private Server* server;
		private Core::HashMap<Core::String, TaskSelections*> selections;   // by wsId
		private Core::Array<TaskRun*> runs;
		private TaskRun* LastEnded(Workspace* ws);
		private int64 started;
		private Threading::ConditionVariable ended;
	}
}
