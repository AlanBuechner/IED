// Fetching a page for web_fetch (docs/29 "Transport"): by a fetcher that writes the body to standard output and a report
// - status, type, the address the redirects ended at, exit code, message - to standard error after kReportMarker. The
// fetcher is the private Python running service/fetch.py, whose curl_cffi impersonates a browser so bot checks that refuse
// curl let it through, or - without it, or with webfetch.impersonate off - the system's curl.exe with --write-out. Both
// follow redirects, decode compressed bodies, take http and https only and stop at a time and a size. Direct, never
// through Tor (owner, docs/29).

include "bedrock.ch"

namespace WebFetch
{
	const char* c_ReportMarker = "IED-WEBFETCH";

	struct FetchOptions
	{
		public Core::String m_UserAgent;   // curl's; an impersonated browser sends its own
		public int64 m_Seconds;
		public int64 m_MaxBytes;
	}

	struct Fetched
	{
		public int32 m_Status;             // 0: no response
		public Core::String m_ContentType;
		public Core::String m_FinalUrl;
		public Core::String m_Body;
		public bool m_Cut;                 // the body is the start of a longer one
		public Core::String m_Error;       // why there is no response; "" when there is one
	}

	// curl's arguments (after the program) for `url`.
	Core::Array<Core::String> CurlArguments(Core::StringView url, const FetchOptions& options);
	// The Python fetcher's arguments (after python.exe): `script` (service/fetch.py), the folder curl_cffi is in, and the
	// browser to impersonate (chrome, edge, safari, firefox).
	Core::Array<Core::String> ImpersonatorArguments(Core::StringView url, Core::StringView script, Core::StringView packages, Core::StringView browser,
		const FetchOptions& options);
	// The report after kReportMarker in a fetcher's standard error into `fetched`, `exitCode` and `message`; false when
	// `errors` has none.
	bool ReadReport(Core::StringView errors, Fetched& fetched, int64& exitCode, Core::String& message);
	// Runs a fetcher, `program` with `arguments`, for at most `options.seconds` and 10 more.
	Fetched RunFetcher(Core::StringView program, const Core::Array<Core::String>& arguments, const FetchOptions& options);
	// The curl to run: `configured` when set, else Windows' own (%SystemRoot%\System32\curl.exe), else "curl" on PATH.
	Core::String CurlProgram(Core::StringView configured);
}
