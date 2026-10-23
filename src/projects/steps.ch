// The commands a project template or a project system runs (docs/03 "New projects", "Managing projects"): its steps
// read with their ${...} filled - the values the caller gives, ${config:KEY} (the workspace's settings, or the app's
// with none) and ${env:NAME} - each run to its end, two minutes at most, its output kept; a failure says why, with the
// output's end and, for a program a setting names, how to give it.

include "services/workspaces.ch"

namespace Ied
{
	const int64 kProjectStepMs = 120000;
	const int64 kProjectLogBytes = 4000;

	// A step as it runs: its command (argv or a command line), its folder, and the setting naming its program.
	class ProjectStep
	{
		public Core::Array<Core::String> argv;
		public Core::String shell;
		public Core::String cwd;
		public Core::String label;
		public Core::String setting;
	}

	// What making or changing a project left: the workspace that shows it, the project's folder or the solution file,
	// the project's name, what the steps printed (its end), and a deleted folder's place in the trash.
	class ProjectChange
	{
		public Workspace* ws;
		public Core::String path;
		public Core::String project;
		public Core::String log;
		public Core::String trashId;
		public Core::Array<Core::String> notes;   // what the caller should know: a choice made for it, a file to check
	}

	// `steps` (a step array of a plugin's manifest) with every ${...} filled from `values`, settings and the
	// environment; `owner` names their template or project system in errors. The default folder is ${workspaceDir}.
	// Throws bad_request for a step that runs nothing or names a variable nothing gives.
	Core::Array<ProjectStep*> PlanProjectSteps(Server* server, const Serialization::DomNode& steps, Workspace* ws, const Core::HashMap<Core::String, Core::String>* values, Core::StringView owner);
	// Runs `step` to its end with its output added to `log`; "" when it succeeded, else why not.
	Core::String RunProjectStep(const ProjectStep* step, Core::StringBuilder& log);
	// The steps in order until one fails: "" when all succeeded, else why the first that failed did not.
	Core::String RunProjectSteps(const Core::Array<ProjectStep*>& steps, Core::StringBuilder& log);
	// The end of a step log, at most kProjectLogBytes.
	Core::String StepLogTail(Core::StringView text);
	// `path` with '/' separators.
	Core::String ForwardSlashes(Core::StringView path);
}
