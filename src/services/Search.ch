// Search (docs/10 "Files"): fs.search over a workspace's text and fs.files over its file names, each run by a thread of
// its own so the server lock is free while it reads the disk; the open documents are searched as they are, from a copy
// taken when the search starts. Members run with the server lock held.

include "fs/Search.ch"
include "fs/Fuzzy.ch"
include "Documents.ch"

namespace Ied
{
	const int64 c_SearchDefaultResults = 2000;
	const int64 c_SearchMostResults = 20000;
	const int64 c_FilesDefaultResults = 50;
	const int64 c_FilesMostResults = 500;

	// A search in flight: the worker fills it and wakes the request.
	class SearchJob
	{
		public bool m_Done;
		public SearchResult m_Result;
		public Core::Array<FileMatch> m_Files;
	}

	class Search
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

	private:
		private Core::HashMap<Core::String, Core::String> OpenTexts(Workspace* ws) const;
		private Core::String SearchMethod(Call& call, const Params& params);
		private Core::String FilesMethod(Call& call, const Params& params);
		// Waits, the lock released, until the worker finishes `job`; throws cancelled when the request is.
		private void Await(Call& call, SearchJob* job);

		private Server* m_Server;
	}

	// The query of fs.search's params; throws bad_request for an empty query or a regex that does not compile.
	SearchQuery SearchQueryOf(const Params& params);
	// {matches: [{path, line, col, preview}], truncated}
	Core::String SearchResultJson(const SearchResult& result);
}
