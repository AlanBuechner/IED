// The toolbar's choices on this machine (docs/06 "Protocol and state"): the startup project and each variable's value,
// a solution-scoped variable once for the solution and the others per project; `.ied/local/selections.json`.

include "bedrock.ch"

namespace Ied
{
	class TaskSelections
	{
		public Core::String m_StartupProject;
		public Core::HashMap<Core::String, Core::String> m_Solution;
		public Core::HashMap<Core::String, Core::HashMap<Core::String, Core::String>*> m_Projects;

		// The chosen value of `key` for `project`; false when none was chosen.
		public bool Value(Core::StringView project, Core::StringView key, bool solutionScope, Core::String& value) const;
		public void Set(Core::StringView project, Core::StringView key, bool solutionScope, Core::StringView value);

		// {startupProject, solution: {key: value}, projects: {project: {key: value}}}
		public Core::String ToJson() const;
	}

	// The selections in `file`; empty when it does not exist or does not read.
	TaskSelections ReadTaskSelections(const FS::Path& file);
	bool WriteTaskSelections(const FS::Path& file, const TaskSelections& selections);
}
