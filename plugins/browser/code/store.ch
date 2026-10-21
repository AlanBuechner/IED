// A workspace's pages on disk (docs/27 "Pages"): .ied/local/browser/pages.json keeps what a page needs to be loaded
// again after the server restarts - its id, URL, title, viewport and opener.
//     {"pages": [{"pageId": "pg-3f9a12c4e0b1", "url": "http://localhost:5173/", "title": "App", "width": 1280,
//                 "height": 800, "scale": 1, "opener": "user"}]}

include "page.ch"

namespace Browser
{
	Core::String PagesJson(const Core::Array<Page*>& pages, Core::StringView wsId);
	// The pages in `json` as pages of workspace `wsId`, none loaded; what is not a page is left out.
	Core::Array<Page*> ParsePages(Core::StringView json, Core::StringView wsId);
	Core::Array<Page*> LoadPages(const IED::Workspace& ws);
	void SavePages(const IED::Workspace& ws, Core::StringView json);
	// The local path a file: URL names ("file:///C:/a%20b/x.html" -> "C:/a b/x.html"); empty when it names none.
	Core::String FilePath(Core::StringView url);
	// Where a workspace's browser files go: <root>/.ied/local/browser.
	FS::Path BrowserFolder(const IED::Workspace& ws);
}
