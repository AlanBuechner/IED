// Reference docs (docs/13 "Reference docs"): a language plugin's Markdown documentation of its language and library,
// which a model knows less well than the code it reads - C4 and its Bedrock, a game engine's API. Plugins name the
// folders (`aiDocs`); docs_search finds sections by words and docs_read reads one, so the model looks a name up
// instead of guessing it from another language or grepping the files through the shell.

include "tool.ch"

namespace Ai
{
	const int64 kDocsSearchMost = 6;          // sections one search gives
	const int64 kDocsMatchLines = 2;          // lines shown under each
	const int64 kDocsReadMostLines = 250;
	const int64 kDocsLineBytes = 220;

	// One plugin's docs: what the model calls them (`id`, the first part of their paths) and where they are.
	class DocsFolder
	{
		public Core::String id;
		public Core::String title;
		public FS::Path folder;
	}

	// A part of a Markdown file under one heading, up to the next heading of any level.
	class DocSection
	{
		public Core::String path;      // "<id>/<path in the folder>"
		public Core::String heading;   // without its #s; the file's name for text before the first heading
		public int64 first;            // its lines, 1-based
		public int64 last;
	}

	// The sections of one Markdown text; `path` names them. Headings inside fenced code are not headings.
	Core::Array<DocSection> SplitSections(Core::StringView text, Core::StringView path);
	// How well a section matches the query's words (`words` lowercased): 0 for no word at all.
	int64 SectionScore(Core::StringView heading, Core::StringView loweredBody, const Core::Array<Core::String>& words, Core::StringView loweredQuery);
	// The query's words, lowercased, without the small words that match everything.
	Core::Array<Core::String> QueryWords(Core::StringView query);
	// The system message's list of them: "- c4: C4 reference".
	Core::String DocsIndex(const Core::Array<DocsFolder*>& folders);
	// docs_search and docs_read, reading the folders `folders` gives for the call's workspace.
	void AddDocsTools(ToolRegistry& registry, func<Core::Array<DocsFolder*>(const ToolInvocation& call)> folders);
}
