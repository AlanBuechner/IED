// A search as SearXNG takes it and its JSON answer as the model reads it (docs/29 "web_search") - text only, tested
// without a SearXNG.

include "bedrock.ch"

namespace WebSearch
{
	struct SearchRequest
	{
		public Core::String m_Query;
		public Core::Array<Core::String> m_Engines;   // lowercase names; all named in the request
		public int32 m_Count;                          // results kept for the model
		public int32 m_Page;                           // 1 and up
		public Core::String m_TimeRange;               // "", day, week, month or year
		public Core::String m_Language;                // "" or "auto": SearXNG's guess
		public int32 m_SafeSearch;
		public int64 m_StartWaitNs;                    // how long it waits for SearXNG and Tor to start
	}

	struct SearchHit
	{
		public Core::String m_Title;
		public Core::String m_Url;
		public Core::String m_Content;
		public Core::Array<Core::String> m_Engines;
		public Core::String m_Published;   // the date part of publishedDate; "" when none
	}

	// An engine that gave nothing, and SearXNG's reason ("access denied", "CAPTCHA", "timeout", "Suspended: ...").
	struct EngineFailure
	{
		public Core::String m_Engine;
		public Core::String m_Reason;
	}

	struct SearchResults
	{
		public Core::Array<SearchHit> m_Hits;   // SearXNG's merged results, best first
		public Core::Array<Core::String> m_Answers;
		public Core::Array<Core::String> m_Suggestions;
		public Core::Array<EngineFailure> m_Failed;
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
