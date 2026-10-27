// Ignore files (.gitignore's rules, for search; docs/10 "Files"): blank lines and '#' comments skipped, '!' re-includes,
// a trailing '/' matches folders only, a pattern with a '/' (other than a trailing one) is anchored to the file's folder,
// one without matches a name at any depth below it; globs as fs/Glob.ch reads them. The last rule that matches wins.

include "Glob.ch"

namespace Ied
{
	struct IgnoreRule
	{
		public Core::String m_Pattern;   // without its '!', its trailing '/' and a leading '/'
		public bool m_Negate;
		public bool m_DirectoryOnly;
		public bool m_Anchored;
	}

	// One ignore file's rules and the workspace-relative folder it sits in ("" for the root).
	class IgnoreFile
	{
		public Core::String m_Folder;
		public Core::Array<IgnoreRule> m_Rules;
	}

	Core::Array<IgnoreRule> ParseIgnoreRules(Core::StringView text);
	// Whether `path` (workspace-relative; a folder when `directory`) is ignored by `files`, the outermost first: each is
	// asked about the paths under its folder, and a later (deeper) file's verdict wins over an earlier one's.
	bool IsIgnored(const Core::Array<IgnoreFile*>& files, Core::StringView path, bool directory);
}
