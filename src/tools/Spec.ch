// Language tools (docs/26 "Language tools"): the programs a language needs - its compiler, its language server, its
// debug adapter - as a plugin's `tools` contribution describes each: where to look for it, how to read its version,
// where a workspace pins the version it wants, and how to install it into IED's tools folder. This is what is read of
// an entry; finding the tools is tools/detect, installing them tools/install, the service tools/service.

include "lsp/Profiles.ch"

namespace Ied
{
	// The `install` steps a server reads: its own system's.
	const char* c_ToolSystem = "windows";

	// One step of an install: a download (checked against its SHA-256, then unpacked into the install folder) or a
	// command.
	class InstallStep
	{
		public Core::String m_Label;
		public Core::String m_Download;                   // a URL; empty for a command
		public Core::String m_Sha256;                     // the download's digest for the tool's install version
		public Core::String m_Sha256Url;                  // a file holding the digest, for another version
		public Core::String m_Extract;                    // "zip", "tar" or "none"
		public int64 m_Strip;                             // leading folders an archive's paths lose
		public Core::String m_FileName;                   // with "none": the file's name in the install folder
		public Core::Array<Core::String> m_Run;           // a command's argv
		public Core::Array<Core::String> m_Environment;   // "NAME=value"
		public Core::String m_Cwd;                        // the command's folder; empty: the workspace's
		public bool m_UserScope;                          // writes outside IED's tools folder: the owner is asked first
	}

	// Where a workspace pins the version it wants: a line of a file in its root or a project's folder.
	class VersionSource
	{
		public Core::String m_File;
		public Core::String m_Regexp;   // group 1 is the version; ^ and $ match at each line
		public bool m_Minimum;          // that version or a newer one; otherwise that version
	}

	class ToolSpec
	{
		public Core::String m_Id;
		public Core::String m_Title;
		public Core::String m_Kind;                            // compiler, languageServer, debugAdapter, buildTool, other
		public Core::String m_PluginId;
		public Core::String m_PluginName;
		public FS::Path m_PluginDir;
		public Core::String m_When;
		public Core::String m_Program;                         // the file looked for: a name, ".exe" (PATHEXT) added
		public Core::String m_Setting;                         // a setting naming the program; set, nothing else is tried
		public Core::Array<Core::String> m_Bundled;            // files tried first (${pluginDir}, ${iedHome})
		public Core::Array<Core::String> m_SearchPaths;        // folders tried after IED's tools folder, before PATH
		public Core::Array<Core::String> m_VersionArgs;        // how its version is read; none: being there is enough
		public Core::String m_VersionPattern;                  // group 1 of its first match in the output is the version
		public Core::Array<Core::String> m_VersionEnvironment; // "NAME=value" while the version is read
		public Core::Array<VersionSource*> m_VersionFrom;
		public Core::Array<Core::String> m_Requires;           // tools an install needs first
		public Core::String m_InstallVersion;                  // what an install gives when no version is pinned
		public Core::String m_InstallBin;                      // the program's folder inside the install folder
		public Core::Array<InstallStep*> m_Steps;              // this system's install, in order; none: it cannot
		public Core::String m_Homepage;                        // how to install it by hand
		public bool m_OnPath;                                  // found outside PATH, its folder goes first on the PATH of the
		                                                     // workspace's processes (gopls finds the go IED installed)

		public bool CanInstall() const { return !m_Steps.IsEmpty(); }
		// Whether a step of its install writes outside IED's tools folder.
		public bool InstallsForUser() const;
	}

	// One `tools` entry of a plugin.
	ToolSpec* ReadToolSpec(const Serialization::DomNode& entry, Core::StringView pluginId, Core::StringView pluginName, const FS::Path& pluginDir);
	// Of the tools with one id, the last: a plugin loaded later replaces an earlier one's (as debug adapter types do).
	Core::Array<ToolSpec*> LastOfEachId(const Core::Array<ToolSpec*>& tools);
	// The tools of `tools` in install order: each after those it requires (that are in `tools`), each once.
	Core::Array<ToolSpec*> InstallOrder(const Core::Array<ToolSpec*>& tools);
}
