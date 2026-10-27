// A workspace's per-machine state (docs/07 "Workspace state"): a solution's is its folder's .ied/local/ (gitignored), a
// folder's or a file's is <app data>/workspaces/<key>/, so opening a folder writes nothing into it. A workspace-relative
// path under .ied/local/ names a file there wherever it is, so the protocol, plugins, the AI and the clients use one
// path for it.

include "bedrock.ch"

namespace Ied
{
	const char* c_LocalStatePath = ".ied/local";

	// Where a workspace of `kind` rooted at `root` keeps its per-machine state.
	FS::Path LocalStateFolder(const FS::Path& root, Core::StringView kind, Core::StringView appData);
	// Whether workspace-relative `path` is .ied/local or under it, in any ASCII case, with either separator.
	bool InLocalState(Core::StringView path);
	// The file workspace-relative `path` names: under `localState` for .ied/local and below, else under `root`.
	FS::Path LocalOrRootFile(const FS::Path& root, const FS::Path& localState, Core::StringView path);
	// Moves what an older server kept of IED's own in a folder's .ied/local (chats, breakpoints, task selections, the
	// browser's pages and shots, GPU and profiler captures) into `localState`, once; anything else there stays, a
	// build's lsp-options among it, and so does all of it in a folder holding a solution file (the solution's own
	// state). What it moved or left, one line each.
	Core::Array<Core::String> MoveFolderState(const FS::Path& root, const FS::Path& localState);
	// Moves what the folder at `root` kept while it was a plain folder workspace (its state in `appData`: chats,
	// layouts, breakpoints, ...) into a solution's `localState` (docs/03 "Managing projects"), once: an entry the
	// solution has already stays. What it moved or left, one line each.
	Core::Array<Core::String> AdoptFolderState(const FS::Path& root, const FS::Path& localState, Core::StringView appData);
}
