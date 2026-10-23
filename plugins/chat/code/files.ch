// The file tools of the core set (docs/13 T-FS-1..5): fs_read, fs_write, fs_edit, fs_list and fs_glob. They see a file
// as the editors do - an open document's text, unsaved changes and all - and write through the document store, so an
// open editor shows the AI's change at once and a write is saved like the owner's (docs/13 rule 6).

include "tool.ch"

namespace Ai
{
	const int64 kReadMostLines = 400;
	const int64 kReadMostBytes = 24 * 1024;
	const int64 kListMostEntries = 400;
	const int64 kGlobMostPaths = 200;

	void AddFileTools(ToolRegistry& registry);
	// A file's text as the editors have it. Throws not_found.
	Core::String TextAsEdited(const IED::Workspace& ws, Core::StringView path);
	// Writes `text` to `path`: into its open document, then saved, or to the disk. Throws unavailable.
	void WriteThrough(const IED::Workspace& ws, Core::StringView path, Core::StringView text);
	// The text of a file the model sees: lines numbered from `first` (1-based), at most `most` lines and
	// kReadMostBytes, with how to read on when it stops early.
	Core::String NumberedLines(Core::StringView text, int64 first, int64 most);
	// A path a tool call names, workspace-relative: its "." parts dropped ("." is the root, as models often write it),
	// then normalized as every workspace path is ("..", a drive letter: bad_request).
	Core::String ToolPath(Core::StringView path);
	// `text` with each edit's `old` replaced by its `new`, all or none; false with `error` naming the edit that failed.
	bool ApplyTextEdits(Core::StringView text, const Serialization::DomNode& edits, Core::String& result, Core::String& error);
}
