// A page (docs/27 "Pages"): server state with an id, owned by a workspace, kept in its .ied/local/browser/pages.json;
// loaded into the browser (a CDP target with a flattened session) when someone looks at it. Its fields change inside
// PluginContext::Atomically, with the events that tell of them, so the topics' pulled snapshots read them whole; its
// frames are the Browsers' frame lock's.

include "bedrock.ch"
include "api/Plugin.ch"

namespace Browser
{
	const int64 c_ConsoleKept = 500;
	const int64 c_NetworkKept = 200;
	const int64 c_RequestsTracked = 1000;
	const int32 c_DefaultPageWidth = 1280;
	const int32 c_DefaultPageHeight = 800;

	class ConsoleEntry
	{
		public int64 m_Seq;
		public Core::String m_Level;    // log, info, warning, error, debug
		public Core::String m_Text;
		public Core::String m_Url;
		public int64 m_Line;            // 1-based; 0: none
		public int64 m_Time;            // ms since the epoch
	}

	class NetworkEntry
	{
		public int64 m_Seq;
		public Core::String m_Method;
		public Core::String m_Url;
		public int64 m_Status;          // 0: the request failed before a response
		public Core::String m_Error;    // the network error, or the status text
		public Core::String m_Type;     // Document, Script, XHR, Fetch, ...
	}

	// An element a snapshot named with a ref ("e12"): its DOM node and how it was shown ("button \"Sign in\"").
	class RefTarget
	{
		public int64 m_BackendNodeId;
		public Core::String m_Label;
	}

	class Page
	{
		public Core::String m_PageId;
		public Core::String m_WsId;
		public Core::String m_Url;
		public Core::String m_Title;
		public Core::String m_Opener;   // "user" or "ai"
		public Core::String m_Parent;   // the page that opened it (window.open, target="_blank"); "" for none
		public Core::String m_ChatId;   // the chat whose AI opened it; "" for a person's page
		public Core::String m_ToldChat; // the title that chat last heard it listed under; "" while it is not listed
		public bool m_Loading;
		public bool m_CanGoBack;
		public bool m_CanGoForward;
		public int32 m_Width;
		public int32 m_Height;
		public int32 m_ScalePercent;    // the device scale, 100 = 1.0

		public Core::String m_TargetId; // while it is loaded
		public Core::String m_SessionId;
		public bool m_Live;

		public Core::Array<ConsoleEntry> m_Console;
		public int64 m_ConsoleSeq;
		public Core::Array<NetworkEntry> m_Network;
		public int64 m_NetworkSeq;
		public Core::HashMap<Core::String, Core::String> m_Requests;   // requestId -> "METHOD url", while tracked
		public Core::String m_LastAction;                             // the last action event's JSON, "" for none
		public Core::HashMap<Core::String, RefTarget> m_Refs;         // the last snapshot's refs

		// The frame lock's.
		public int64 m_FrameSeq;
		public Core::String m_FrameData;
		public int32 m_FrameWidth;
		public int32 m_FrameHeight;
		public bool m_Screencasting;
		public int64 m_LastPullNs;
		public int64 m_LastUsedNs;      // the last call to its session (an AI tool, a person's input) or load
		public int64 m_Loads;           // load events seen

		public Core::String Topic() const { return Core::String("page:") + m_PageId.View(); }
		// {pageId, wsId, url, title, loading, canGoBack, canGoForward, width, height, scale, opener, parent, live}
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
