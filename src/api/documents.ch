// Documents and search through the host (docs/26 "What stays the host's"): a file's text as the people editing it see
// it (an open document's buffer, else the file), a whole-text change written through an open document (an edit, then
// saved) or straight to the file, and a search of a workspace that reads open documents as edited. Each checks the
// calling plugin's permissions (fs.read, fs.write) and the workspace's folder.

include "workspace.ch"
include "fs/search.ch"

namespace IED
{
	alias SearchQuery = Ied::SearchQuery;
	alias SearchMatch = Ied::SearchMatch;
	alias SearchResult = Ied::SearchResult;

	// `path`'s text as edited: an open document's buffer, else the file's; false with `problem` when neither is there.
	bool TextAsEdited(const Workspace& ws, Core::StringView path, Core::String& text, Core::String& problem);
	// Whether `path` is a file of `ws` or an open document.
	bool Exists(const Workspace& ws, Core::StringView path);
	// Replaces `path`'s text: through its open document (an edit by `actor`, then saved) or straight to the file.
	bool WriteThrough(const Workspace& ws, Core::StringView path, Core::StringView text, Core::StringView actor, Core::String& problem);
	// Searches `ws`'s files, its open documents as edited, until the query's limit or soon after `cancel` is set; false
	// with `problem` when the plugin may not read the workspace or the pattern is not a valid regex. Without the lock:
	// the search runs on the caller's thread.
	bool Search(const Workspace& ws, const SearchQuery& query, const Threading::Atomic<bool>* cancel, SearchResult& result, Core::String& problem);
	// Searches the files under `folder` (an absolute path that is no workspace's: a plugin's folder) as Search does a
	// workspace's; false with `problem` when the plugin may not read (fs.read), it is no folder or the pattern is not a
	// valid regex. Without the lock.
	bool SearchFolder(Core::StringView folder, const SearchQuery& query, const Threading::Atomic<bool>* cancel, SearchResult& result, Core::String& problem);
}
