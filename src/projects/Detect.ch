// What an existing folder holds, for a project IED makes of it (docs/03 "Managing projects"): its language - the first
// projectIcons entry whose marker file lies in it (Cargo.toml: rust), else the language most of its files are in among
// those a projectIcons entry draws (code, not data or docs) - and the task presets whose `detect` file lies in it, of
// plugins of that language or of none, less those another of them extends. Everything comes from the plugins.

include "services/Workspaces.ch"

namespace Ied
{
	const int64 c_DetectFilesMost = 20000;   // files counted for a folder's language

	class FolderFacts
	{
		public Core::String m_Language;               // "" when nothing tells
		public Core::Array<Core::String> m_Presets;
	}

	// The facts of `folder` with the plugins loaded for `ws` (null: the global ones). With the server lock held.
	FolderFacts DetectFolder(Server* server, Workspace* ws, const FS::Path& folder);
}
