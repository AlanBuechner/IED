// Language tools (docs/26 "Language tools"): the programs a language needs - its compiler, its language server, its
// debug adapter - as a plugin's `tools` contribution describes each: where to look for it, how to read its version,
// where a workspace pins the version it wants, and how to install it into IED's tools folder. This is what is read of
// an entry; finding the tools is tools/detect, installing them tools/install, the service tools/service.

include "lsp/profiles.ch"

namespace Ied
{
	// The `install` steps a server reads: its own system's.
	const char* kToolSystem = "windows";

	// One step of an install: a download (checked against its SHA-256, then unpacked into the install folder) or a
	// command.
	class InstallStep
	{
		public Core::String label;
		public Core::String download;                   // a URL; empty for a command
		public Core::String sha256;                     // the download's digest for the tool's install version
		public Core::String sha256Url;                  // a file holding the digest, for another version
		public Core::String extract;                    // "zip", "tar" or "none"
		public int64 strip;                             // leading folders an archive's paths lose
		public Core::String fileName;                   // with "none": the file's name in the install folder
		public Core::Array<Core::String> run;           // a command's argv
		public Core::Array<Core::String> environment;   // "NAME=value"
		public Core::String cwd;                        // the command's folder; empty: the workspace's
		public bool userScope;                          // writes outside IED's tools folder: the owner is asked first
	}

	// Where a workspace pins the version it wants: a line of a file in its root or a project's folder.
	class VersionSource
	{
		public Core::String file;
		public Core::String regexp;   // group 1 is the version; ^ and $ match at each line
		public bool minimum;          // that version or a newer one; otherwise that version
	}

	class ToolSpec
	{
		public Core::String id;
		public Core::String title;
		public Core::String kind;                            // compiler, languageServer, debugAdapter, buildTool, other
		public Core::String pluginId;
		public Core::String pluginName;
		public FS::Path pluginDir;
		public Core::String when;
		public Core::String program;                         // the file looked for: a name, ".exe" (PATHEXT) added
		public Core::String setting;                         // a setting naming the program; set, nothing else is tried
		public Core::Array<Core::String> bundled;            // files tried first (${pluginDir}, ${iedHome})
		public Core::Array<Core::String> searchPaths;        // folders tried after IED's tools folder, before PATH
		public Core::Array<Core::String> versionArgs;        // how its version is read; none: being there is enough
		public Core::String versionPattern;                  // group 1 of its first match in the output is the version
		public Core::Array<Core::String> versionEnvironment; // "NAME=value" while the version is read
		public Core::Array<VersionSource*> versionFrom;
		public Core::Array<Core::String> requires;           // tools an install needs first
		public Core::String installVersion;                  // what an install gives when no version is pinned
		public Core::String installBin;                      // the program's folder inside the install folder
		public Core::Array<InstallStep*> steps;              // this system's install, in order; none: it cannot
		public Core::String homepage;                        // how to install it by hand
		public bool onPath;                                  // found outside PATH, its folder goes first on the PATH of the
		                                                     // workspace's processes (gopls finds the go IED installed)

		public bool CanInstall() const { return !steps.IsEmpty(); }
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
