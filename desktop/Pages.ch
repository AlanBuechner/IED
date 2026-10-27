// What the desktop app decides about URLs, and the pages and script it puts in a window itself (docs/27).

include "bedrock.ch"

namespace Desktop
{
	enum class Destination { Here, NewWindow, External, Refused }

	// "http://127.0.0.1:8765" from "http://127.0.0.1:8765/w/x?t=1#a" (scheme, host and port; empty when it has none).
	Core::String OriginOf(Core::StringView url);
	// Where a top-level navigation to `url` goes, from a window on `serverOrigin`: the server's pages and the app's own
	// (about:, data:) stay; http, https and mailto leave for the default browser; anything else is refused.
	Destination NavigationTarget(Core::StringView url, Core::StringView serverOrigin);
	// Where a page's new window for `url` opens: an IED window for the server's pages, the default browser otherwise.
	Destination NewWindowTarget(Core::StringView url, Core::StringView serverOrigin);
	// `url` showing IED's start page (its fragment "start", any other fragment replaced): what a window opened without a
	// path shows, even while workspaces are open on the server.
	Core::String StartPageUrl(Core::StringView url);

	// The script added before every document's own: window.iedDesktop, carried by chrome.webview.postMessage.
	Core::String BridgeScript(Core::StringView version);
	// A page in IED's colors saying what is happening, with `detail` (escaped) under it and a Retry button when `retry`.
	Core::String StatusPage(Core::StringView title, Core::StringView detail, bool retry);
	Core::String EscapeHtml(Core::StringView text);
}
