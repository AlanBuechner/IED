// Search (docs/10 "Files"): fs.search over a workspace, run by a thread of its own so the server lock is free while it
// reads the disk; the open documents are searched as they are, from a copy taken when the search starts. Members run
// with the server lock held.

include "fs/search.ch"
include "documents.ch"

namespace Ied
{
	const int64 kSearchDefaultResults = 2000;
	const int64 kSearchMostResults = 20000;

	// A search in flight: the worker fills it and wakes the request.
	class SearchJob
	{
		public bool done;
		public SearchResult result;
	}

	class Search
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

	private:
		private Core::HashMap<Core::String, Core::String> OpenTexts(Workspace* ws) const;
		private Core::String SearchMethod(Call& call, const Params& params);

		private Server* server;
	}

	// The query of fs.search's params; throws bad_request for an empty query or a regex that does not compile.
	SearchQuery SearchQueryOf(const Params& params);
	// {matches: [{path, line, col, preview}], truncated}
	Core::String SearchResultJson(const SearchResult& result);
}
