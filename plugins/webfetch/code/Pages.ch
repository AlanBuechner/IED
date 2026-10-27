// web_fetch (docs/29 "Fetch"): a page fetched, made text (Markdown, plain text, or as received) and handed out a window
// at a time; the last kKeptPages pages are kept for kKeptNs, so reading on through a long page fetches it once.

include "Transport.ch"
include "api/Plugin.ch"

namespace WebFetch
{
	const int64 c_KeptPages = 16;
	const int64 c_KeptNs = 600000000000;
	const int64 c_ErrorChars = 2000;

	enum class PageKind { Html, Text, Other }

	struct FetchedPage
	{
		public Core::String m_Url;          // as asked
		public Core::String m_Format;       // markdown, text or raw
		public int32 m_Status;
		public Core::String m_ContentType;
		public Core::String m_FinalUrl;
		public Core::String m_Title;
		public Core::String m_Text;
		public bool m_Cut;
		public int64 m_BodyBytes;
		public int64 m_FetchedNs;
	}

	// What a body of `contentType` is to web_fetch; a missing type is told from the body.
	PageKind KindOf(Core::StringView contentType, Core::StringView body);
	// The body as UTF-8: read as Windows-1252 when it says it is Latin-1 or Windows-1252 (in its type or a <meta>), or
	// says nothing and is not UTF-8.
	Core::String AsUtf8(Core::StringView body, Core::StringView contentType);
	// Whether the page is a bot check (Cloudflare's "Just a moment...", a CAPTCHA) rather than what was asked for.
	bool IsBotCheck(const FetchedPage& page);
	// What the model reads: the page's header, characters [start, start + count) of its text, and how to read on.
	Core::String WindowText(const FetchedPage& page, int64 start, int64 count);

	class Fetcher
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.m_Context = context; }

		// webfetch.fetch: {url, format?, start?, max_chars?} -> {text, isError}. On the plugin's thread.
		public Core::String Fetch(const Serialization::DomNode& args);

		private FetchedPage* Load(Core::StringView url, Core::StringView format, Core::String& error);
		private FetchedPage* Kept(Core::StringView url, Core::StringView format);
		private void Keep(FetchedPage* page);

		private IED::PluginContext* m_Context;
		private Core::Array<FetchedPage*> m_Kept;
	}
}
