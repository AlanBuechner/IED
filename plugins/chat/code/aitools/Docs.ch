// Reference docs (docs/13 "Reference docs"): a language plugin's Markdown documentation of its language and library,
// which a model knows less well than the code it reads - C4 and its Bedrock, a game engine's API. Plugins name the
// folders (`aiDocs`); docs_search finds sections by words and docs_read reads one, so the model looks a name up
// instead of guessing it from another language or grepping the files through the shell.

include "Tool.ch"

namespace Ai
{
	const int64 c_DocsSearchMost = 6;          // sections one search gives
	const int64 c_DocsMatchLines = 2;          // lines shown under each
	const int64 c_DocsReadMostLines = 250;
	const int64 c_DocsLineBytes = 220;

	// One plugin's docs: what the model calls them (`id`, the first part of their paths) and where they are.
	class DocsFolder
	{
		public Core::String m_Id;
		public Core::String m_Title;
		public FS::Path m_Folder;
		public Core::String m_Shown;     // the folder as the file tools name it: "plugins:/c4/docs"
		public bool m_ReadOnly;          // a plugin's that is not the workspace's own (docs/13 "Plugin folders")
	}

	// A part of a Markdown file under one heading, up to the next heading of any level.
	class DocSection
	{
		public Core::String m_Path;      // "<id>/<path in the folder>"
		public Core::String m_Heading;   // without its #s; the file's name for text before the first heading
		public int64 m_First;            // its lines, 1-based
		public int64 m_Last;
	}

	// The sections of one Markdown text; `path` names them. Headings inside fenced code are not headings.
	Core::Array<DocSection> SplitSections(Core::StringView text, Core::StringView path);
	// How well a section matches the query's words (`words` lowercased): 0 for no word at all.
	int64 SectionScore(Core::StringView heading, Core::StringView loweredBody, const Core::Array<Core::String>& words, Core::StringView loweredQuery);
	// The query's words, lowercased, without the small words that match everything.
	Core::Array<Core::String> QueryWords(Core::StringView query);
	// The one heading nearest to `wanted` by edit distance (ignoring case), for a heading no section holds - a typo or a
	// guessed name ("Math::TMat4" for "Math::Mat4"); -1 when none is close or two are as close.
	int64 ClosestHeading(const Core::Array<Core::String>& headings, Core::StringView wanted);
	// docs_read's answer for a path no docs folder holds (`index`: the docs names there are); a Markdown or text path
	// is told that fs_read reads the workspace's own files.
	Core::String UnknownDocProblem(Core::StringView path, Core::StringView index);
	// The system message's list of them: "- c4: C4 reference".
	Core::String DocsIndex(const Core::Array<DocsFolder*>& folders);
	// docs_search and docs_read, reading the folders `folders` gives for the call's workspace.
	void AddDocsTools(ToolRegistry& registry, func<Core::Array<DocsFolder*>(const ToolInvocation& call)> folders);
}
