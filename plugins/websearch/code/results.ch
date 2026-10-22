// A search as SearXNG takes it and its JSON answer as the model reads it (docs/29 "web_search") - text only, tested
// without a SearXNG.

include "bedrock.ch"

namespace WebSearch
{
	struct SearchRequest
	{
		public Core::String query;
		public Core::Array<Core::String> engines;   // lowercase names; all named in the request
		public int32 count;                          // results kept for the model
		public int32 page;                           // 1 and up
		public Core::String timeRange;               // "", day, week, month or year
		public Core::String language;                // "" or "auto": SearXNG's guess
		public int32 safeSearch;
	}

	struct SearchHit
	{
		public Core::String title;
		public Core::String url;
		public Core::String content;
		public Core::Array<Core::String> engines;
		public Core::String published;   // the date part of publishedDate; "" when none
	}

	// An engine that gave nothing, and SearXNG's reason ("access denied", "CAPTCHA", "timeout", "Suspended: ...").
	struct EngineFailure
	{
		public Core::String engine;
		public Core::String reason;
	}

	struct SearchResults
	{
		public Core::Array<SearchHit> hits;   // SearXNG's merged results, best first
		public Core::Array<Core::String> answers;
		public Core::Array<Core::String> suggestions;
		public Core::Array<EngineFailure> failed;
	}

	// Percent-encoding for a query string's value (UTF-8 bytes; unreserved characters kept).
	Core::String UrlEncode(Core::StringView text);
	// "/search?q=...&format=json&engines=a,b&pageno=1&safesearch=0" with time_range and language when given.
	Core::String SearchTarget(const SearchRequest& request);
	// SearXNG's JSON answer into `results`; false with `error` when it is not one.
	bool ParseResults(Core::StringView json, SearchResults& results, Core::String& error);
	// Whether an engine's failure is its refusing this address (a CAPTCHA, access denied, too many requests) - which a
	// new Tor circuit may get past - rather than a timeout or an error.
	bool IsBlock(Core::StringView reason);
	// How many of the requested engines refused (IsBlock).
	int64 BlockedCount(const SearchResults& results);
	// What the model reads: the first `request.count` hits numbered, the answers, suggestions and failed engines.
	Core::String ResultsText(const SearchRequest& request, const SearchResults& results, bool viaTor);
	// The same as JSON, for the websearch.search binding and method:
	//     {query, viaTor, text, results: [{title, url, content, engines, published?}], total, answers, suggestions,
	//      failed: [[engine, reason]]} - `text` being ResultsText's.
	Core::String ResultsJson(const SearchRequest& request, const SearchResults& results, bool viaTor);
}
