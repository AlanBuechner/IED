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
		public Core::String m_Key;
		public Core::String m_Kind;            // dropdown, text, toggle
		public Core::String m_Label;
		public Core::Array<Core::String> m_Values;
		public Core::String m_DefaultValue;    // a toggle's "true" / "false"
		public bool m_SolutionScope;
	}

	class TaskStep
	{
		public StepKind m_Kind;
		public Core::Array<Core::String> m_Argv;   // run, as argv
		public Core::String m_Shell;               // run, as a shell string
		public Core::String m_Name;                // the task, debug configuration or binding it names
		public Core::String m_Label;
		public Core::String m_Cwd;
		public Core::Array<Core::String> m_Env;    // "NAME=value"
		public Core::String m_Output;
		public Core::Array<Core::String> m_Matchers;
		public OnlyIf m_OnlyIf;
		public Core::String m_When;
		public int64 m_TimeoutSeconds;
	}

	// A step as a task file or a project template writes it.
	TaskStep* ReadTaskStep(const Serialization::DomNode& node);

	class TaskDef
	{
		public Core::String m_Key;
		public Core::String m_Label;
		public Core::String m_Icon;
		public Core::String m_Tooltip;
		public Core::String m_Slot;
		public Concurrency m_Concurrency;
		public bool m_SaveAll;                     // saveBefore all (or project: the server saves the workspace's)
		public Core::String m_When;
		public Core::Array<TaskStep*> m_Steps;
	}

	// A debug configuration (docs/25 "Launch configurations"): {type, request, preLaunchTask?, ...adapter arguments}.
	class DebugConfig
	{
		public Core::String m_Name;
		public Serialization::DomNode m_Node;
	}

	// A configuration ready to launch: its strings expanded, `arguments` the whole object as JSON.
	class DebugLaunch
	{
		public Core::String m_Project;
		public Core::String m_Name;
		public Core::String m_Type;
		public Core::String m_Request;          // launch or attach
		public Core::String m_PreLaunchTask;
		public Core::String m_Arguments;
	}

	class OfferedTasks
	{
		public Core::Array<TaskVariable*> m_Variables;
		public Core::Array<TaskDef*> m_Tasks;
		public Core::Array<Core::String> m_Toolbar;
		public Core::Array<DebugConfig*> m_DebugConfigs;
		public Core::String m_DebugDefault;
		public Core::Array<Core::String> m_DefaultMatchers;   // a preset's `problemMatcher`: for steps that name none
		public Core::Array<Core::String> m_Problems;

		public TaskVariable* Variable(Core::StringView key) const;
		public TaskDef* Task(Core::StringView key) const;
		public TaskDef* InSlot(Core::StringView slot) const;
		// The configuration named `name`; empty: the default, else the first. Null when there is none.
		public DebugConfig* Debug(Core::StringView name) const;
	}

	// A project file's or a preset's `variables`, `tasks`, `toolbar`, `debug` and `problemMatcher` into `into`, each key
	// (and a debug configuration's name) replacing an earlier one and a toolbar or default matchers the earlier ones;
	// what does not read is a problem.
	void ReadTaskEntries(const Serialization::DomNode& node, Core::StringView origin, OfferedTasks& into);
	// The default matchers given to every run step that names none (docs/26: a language's preset gives Nuke's steps its
	// compiler's matchers).
	void ApplyDefaultMatchers(OfferedTasks& offered);
}
