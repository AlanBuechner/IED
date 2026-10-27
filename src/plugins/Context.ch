// The when-clause context of a workspace, for the `when` of the contributions the server acts on (language servers,
// debug adapters, system-message sections, tool calls): workspaceTrusted, workspaceKind, config.<key> (a setting's
// effective value), workspaceContains.<name> (a file or folder of that name in the workspace's root) and
// workspaceLanguage.<id> (docs/26): the workspace uses the language - a project of it names it, or a plugin's
// projectIcons marker for it lies in the workspace's root or a project's root.

include "When.ch"

namespace Ied
{
	// Whether `marker` names one of `names`: a whole name, or "*.ext" any name with that ending (ASCII case folded).
	bool MarkerNamed(const Core::Array<Core::String>& names, Core::StringView marker);
	// The names of what lies directly in `folder`; none when it is no folder.
	Core::Array<Core::String> FolderNames(const FS::Path& folder);
	// The languages `ws` uses, each once (see above); read now, with the server lock held.
	Core::Array<Core::String> WorkspaceLanguages(Server* server, Workspace* ws);
	// The lookup of `ws`'s context; workspaceLanguage.<id> reads the languages on its first use. With the lock held.
	WhenLookup WorkspaceWhen(Server* server, Workspace* ws);
}
