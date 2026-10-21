// A page (docs/27 "Pages"): server state with an id, owned by a workspace, kept in its .ied/local/browser/pages.json;
// loaded into the browser (a CDP target with a flattened session) when someone looks at it. Its fields change inside
// PluginContext::Atomically, with the events that tell of them, so the topics' pulled snapshots read them whole; its
// frames are the Browsers' frame lock's.

include "bedrock.ch"
include "api/plugin.ch"

namespace Browser
{
	const int64 kConsoleKept = 500;
	const int64 kNetworkKept = 200;
	const int64 kRequestsTracked = 1000;
	const int32 kDefaultPageWidth = 1280;
	const int32 kDefaultPageHeight = 800;

	class ConsoleEntry
	{
		public int64 seq;
		public Core::String level;    // log, info, warning, error, debug
		public Core::String text;
		public Core::String url;
		public int64 line;            // 1-based; 0: none
		public int64 time;            // ms since the epoch
	}

	class NetworkEntry
	{
		public int64 seq;
		public Core::String method;
		public Core::String url;
		public int64 status;          // 0: the request failed before a response
		public Core::String error;    // the network error, or the status text
		public Core::String type;     // Document, Script, XHR, Fetch, ...
	}

	// An element a snapshot named with a ref ("e12"): its DOM node and how it was shown ("button \"Sign in\"").
	class RefTarget
	{
		public int64 backendNodeId;
		public Core::String label;
	}

	class Page
	{
		public Core::String pageId;
		public Core::String wsId;
		public Core::String url;
		public Core::String title;
		public Core::String opener;   // "user" or "ai"
		public bool loading;
		public bool canGoBack;
		public bool canGoForward;
		public int32 width;
		public int32 height;
		public int32 scalePercent;    // the device scale, 100 = 1.0

		public Core::String targetId; // while it is loaded
		public Core::String sessionId;
		public bool live;

		public Core::Array<ConsoleEntry> console;
		public int64 consoleSeq;
		public Core::Array<NetworkEntry> network;
		public int64 networkSeq;
		public Core::HashMap<Core::String, Core::String> requests;   // requestId -> "METHOD url", while tracked
		public Core::String lastAction;                             // the last action event's JSON, "" for none
		public Core::HashMap<Core::String, RefTarget> refs;         // the last snapshot's refs

		// The frame lock's.
		public int64 frameSeq;
		public Core::String frameData;
		public int32 frameWidth;
		public int32 frameHeight;
		public bool screencasting;
		public int64 lastPullNs;
		public int64 lastUsedNs;      // the last call to its session (an AI tool, a person's input) or load
		public int64 loads;           // load events seen

		public Core::String Topic() const { return Core::String("page:") + pageId.View(); }
		// {pageId, wsId, url, title, loading, canGoBack, canGoForward, width, height, scale, opener, live}
		public Core::String SummaryJson() const;
		// {page, console, network, lastAction}
		public Core::String SnapshotJson() const;
		// Appends to the console ring (kConsoleKept); the entry's seq is set.
		public ConsoleEntry& AddConsole(ConsoleEntry entry);
		public NetworkEntry& AddNetwork(NetworkEntry entry);
		public void TrackRequest(Core::StringView requestId, Core::StringView described);
	}

	Core::String ConsoleJson(const ConsoleEntry& entry);
	Core::String NetworkJson(const NetworkEntry& entry);
	// The device scale as JSON ("1", "1.25").
	Core::String ScaleText(int32 percent);
	// A fresh page id, "pg-" and 12 hex digits.
	Core::String NewPageId();
}
