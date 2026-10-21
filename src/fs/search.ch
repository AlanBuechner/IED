// Search in files (docs/10 "Files", fs.search): a query - a literal or a regex in RE2's syntax (Core::Regex), with or
// without case - run over the files of a folder, with the open documents searched as they are rather than as on
// disk. The first match of each line is reported, its column in UTF-16 units. Everything here is honest: the caller
// gathers what to search, so a worker thread can run it without the server lock.

include "walk.ch"

namespace Ied
{
	const int64 kSearchFileLimitBytes = 4194304;   // bigger files are skipped: generated bundles, logs, data
	const int64 kSearchPreviewChars = 200;
	const int64 kSearchBinaryProbeBytes = 8000;    // a NUL among the first bytes marks a binary file

	struct SearchQuery
	{
		public Core::String text;
		public bool regex;
		public bool caseSensitive;
		public Core::Array<Core::String> include;   // globs on the workspace-relative path; empty: every file
		public Core::Array<Core::String> exclude;   // globs; a folder they match is not entered
		public bool useIgnoreFiles;                 // .gitignore files skip what they ignore
		public int64 maxResults;
	}

	struct SearchMatch
	{
		public Core::String path;
		public int64 line;
		public int64 col;
		public Core::String preview;
	}

	class SearchResult
	{
		public Core::Array<SearchMatch> matches;
		public bool truncated;
	}

	// A query compiled once and matched line by line.
	class LineMatcher
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(const SearchQuery& query);

		public bool IsValid() const { return pattern.IsValid(); }
		public const char* Error() const { return pattern.Error(); }
		// The UTF-16 column of the first match in `line`, or -1 for none.
		public int64 Find(Core::StringView line) const;
		// Whether `text` can hold a match at all: a literal query is looked for in the whole text at once, so a file
		// without it is passed over without reading it line by line.
		public bool MayMatch(Core::StringView text) const;

		private Core::Regex pattern;
		private Core::String literal;   // the query when it is not a regex, ASCII-folded when not case-sensitive
		private bool folded;
	}

	// The folders searched by default when a query names no exclusions: a repository's own, and the ones that hold
	// what a build or a package manager made.
	Core::Array<Core::String> DefaultSearchExcludes();
	// `text` with every regex metacharacter escaped, so it matches itself.
	Core::String EscapeRegex(Core::StringView text);
	// Whether a workspace-relative path is searched under the query's include and exclude globs.
	bool IsSearched(Core::StringView path, const SearchQuery& query);
	// The matches of `text` (the file at `path`) added to `result`, until it holds `limit`.
	void SearchText(Core::StringView path, Core::StringView text, const LineMatcher& matcher, int64 limit, SearchResult& result);
	// Every searched file under `root` in path order, an open document's text (`open`: path -> text) instead of the
	// file's, what .gitignore files ignore skipped when the query says so; it stops at the query's limit, or soon after
	// `cancel` is set.
	SearchResult SearchFolder(const FS::Path& root, const SearchQuery& query, const Core::HashMap<Core::String, Core::String>& open,
		const Threading::Atomic<bool>* cancel);
}
