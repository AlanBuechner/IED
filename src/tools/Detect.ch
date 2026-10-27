// Finding a language tool (docs/26 "Language tools"): the places its program may be, in order, and asking each its
// version until one gives what the workspace pins. The places are gathered with the server lock held; the programs
// run without it.

include "Versions.ch"

namespace Ied
{
	// A place a tool's program was found.
	struct ToolCandidate
	{
		public Core::String m_Path;
		public Core::String m_Source;    // setting, bundled, installed, searchPath, path
		public Core::String m_Version;   // known without asking (an IED install's folder); else empty
	}

	// What a tool is in a workspace now.
	class ToolStatus
	{
		public ToolSpec* m_Spec;
		public Core::String m_State;      // unchecked, found, wrongVersion, missing, installing, failed
		public Core::String m_Path;       // the program found (a wrong version's too)
		public Core::String m_Version;
		public Core::String m_Source;
		public PinnedVersion m_Pin;
		public Core::String m_Problem;    // why it is missing, or why its install failed

		// {id, title, kind, plugin, pluginName, state, path, version, source, pin: {version, match, file} | null, problem,
		// canInstall, installs (the version an install gives), installsForUser, requires, homepage}
		public Core::String Json() const;
		public ToolStatus* Copy() const;
		// Whether the workspace lacks it: missing, a wrong version, or an install that failed.
		public bool Lacking() const { return m_State == "missing" || m_State == "wrongVersion" || m_State == "failed"; }
	}

	// What one detection needs, read with the lock held.
	class DetectJob
	{
		public ToolSpec* m_Spec;
		public Core::Array<ToolCandidate> m_Candidates;
		public Core::String m_Cwd;                      // the workspace's folder: a rustup proxy picks its toolchain there
		public PinnedVersion m_Pin;
		public Core::Array<Core::String> m_Environment; // "NAME=value" for the programs asked
		public Core::String m_Looked;                   // where it was looked for, said when it is missing
	}

	// The places a tool may be, in the order they are tried: the program its setting names when `setting` (the
	// setting's value) is not empty - then nothing else -, else its bundled files, IED's installs of it (newest
	// first), its search folders and PATH. Only files that exist, each once; `looked` says where it looked.
	Core::Array<ToolCandidate> ToolCandidates(const ToolSpec* spec, Core::StringView setting, const ProfileVariables& variables, const FS::Path& toolsRoot, Core::String& looked);
	// The versions IED installed of tool `id`: the folders under <toolsRoot>/<id>/, newest first.
	Core::Array<Core::String> InstalledVersions(const FS::Path& toolsRoot, Core::StringView id);
	// The first candidate that runs and gives the pinned version is found; else the first that runs is a wrong
	// version; with none that runs the tool is missing, with why. Runs programs: no lock held.
	ToolStatus* DetectTool(const DetectJob& job);
	// Group 1 of `pattern`'s first match in `output` (lines matched by ^ and $); empty without one.
	Core::String VersionIn(Core::StringView output, Core::StringView pattern);

	const int32 c_ToolVersionMs = 15000;
}
