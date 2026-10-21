// What a project offers to run (docs/06 "Task model", "Protocol and state"): the variables, tasks, toolbar and debug
// configurations of its presets with its own file's over them by key. Read from JSON
// (shared/host-api/schemas/project.v1.schema.json).

include "bedrock.ch"

namespace Ied
{
	enum class StepKind { Run, Task, Debug, Binding }
	enum class OnlyIf { PrevSuccess, PrevFailure, Always }
	enum class Concurrency { Queue, Single, Restart, Parallel }

	class TaskVariable
	{
		public Core::String key;
		public Core::String kind;            // dropdown, text, toggle
		public Core::String label;
		public Core::Array<Core::String> values;
		public Core::String defaultValue;    // a toggle's "true" / "false"
		public bool solutionScope;
	}

	class TaskStep
	{
		public StepKind kind;
		public Core::Array<Core::String> argv;   // run, as argv
		public Core::String shell;               // run, as a shell string
		public Core::String name;                // the task, debug configuration or binding it names
		public Core::String label;
		public Core::String cwd;
		public Core::Array<Core::String> env;    // "NAME=value"
		public Core::String output;
		public Core::Array<Core::String> matchers;
		public OnlyIf onlyIf;
		public Core::String when;
		public int64 timeoutSeconds;
	}

	class TaskDef
	{
		public Core::String key;
		public Core::String label;
		public Core::String icon;
		public Core::String tooltip;
		public Core::String slot;
		public Concurrency concurrency;
		public bool saveAll;                     // saveBefore all (or project: the server saves the workspace's)
		public Core::String when;
		public Core::Array<TaskStep*> steps;
	}

	// A debug configuration (docs/25 "Launch configurations"): {type, request, preLaunchTask?, ...adapter arguments}.
	class DebugConfig
	{
		public Core::String name;
		public Serialization::DomNode node;
	}

	// A configuration ready to launch: its strings expanded, `arguments` the whole object as JSON.
	class DebugLaunch
	{
		public Core::String project;
		public Core::String name;
		public Core::String type;
		public Core::String request;          // launch or attach
		public Core::String preLaunchTask;
		public Core::String arguments;
	}

	class OfferedTasks
	{
		public Core::Array<TaskVariable*> variables;
		public Core::Array<TaskDef*> tasks;
		public Core::Array<Core::String> toolbar;
		public Core::Array<DebugConfig*> debugConfigs;
		public Core::String debugDefault;
		public Core::Array<Core::String> problems;

		public TaskVariable* Variable(Core::StringView key) const;
		public TaskDef* Task(Core::StringView key) const;
		public TaskDef* InSlot(Core::StringView slot) const;
		// The configuration named `name`; empty: the default, else the first. Null when there is none.
		public DebugConfig* Debug(Core::StringView name) const;
	}

	// A project file's or a preset's `variables`, `tasks`, `toolbar` and `debug` into `into`, each key (and a debug
	// configuration's name) replacing an earlier one and a toolbar the earlier toolbar; what does not read is a problem.
	void ReadTaskEntries(const Serialization::DomNode& node, Core::StringView origin, OfferedTasks& into);
}
