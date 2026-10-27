// What can be done to a page beyond navigating (docs/27 "AI tools"): a person's input forwarded from the Browser tab,
// and the AI's - reading the page as an accessibility tree with element refs, as text or as HTML, a screenshot, acting
// on an element (by ref or CSS selector), evaluating script. Each AI action is told to the page's viewers.

include "Browser.ch"

namespace Browser
{
	const int64 c_SnapshotBytes = 60000;
	const int64 c_HtmlBytes = 100000;
	const int64 c_EvalBytes = 20000;

	// The Browser tab's input events (docs/27 "Protocol": mouse, key, text), in page CSS pixels.
	void DispatchInput(Browsers* browsers, Page* page, const Serialization::DomNode& events);

	// The page as `mode`: "a11y" (indented roles and names, with refs browser_act takes), "text" or "dom".
	Core::String Snapshot(Browsers* browsers, Page* page, Core::StringView mode);
	// A PNG of the viewport (or the whole page) saved under the workspace's browser folder; its workspace-relative path,
	// with its size in `width` and `height`.
	Core::String Screenshot(Browsers* browsers, Page* page, const IED::Workspace& ws, bool fullPage, int64& width, int64& height);
	// click, type, press, select, scroll or hover - on the element `ref` or `selector` names (scroll and press may name
	// none); what was done, in words. Throws RpcError for a missing element or an unknown action.
	Core::String Act(Browsers* browsers, Page* page, Core::StringView action, Core::StringView ref, Core::StringView selector, Core::StringView text);
	// The script's value as JSON, or its exception; `failed` says which.
	Core::String Evaluate(Browsers* browsers, Page* page, Core::StringView script, bool& failed);
	// The page in a line: its id, title, URL and whether it loads.
	Core::String Described(Browsers* browsers, Page* page);
	// Console entries after `since` (and the failed requests, when `network`), as lines.
	Core::String ConsoleLines(Browsers* browsers, Page* page, int64 since, bool network, int64& last);
}
