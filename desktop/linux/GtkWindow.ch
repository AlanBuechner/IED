// One IED window on Linux (docs/27 "Linux"): a GTK window with WebKitGTK showing the web client, through Shell.h. Its
// methods run on the UI thread; the app keeps every window it made, so the pointer GTK hands back stays good.
#if defined(PLATFORM_LINUX)
include "GtkShell.ch"
include "Placement.ch"
include "Pages.ch"

namespace Desktop { class App; }

namespace Desktop
{
	// Why a window asked the server for a URL (as on Windows, desktop/Window.ch): its first page; IED's start page; the
	// server back after the watch started it again; a folder the person picked; its workspace opened again on a new
	// server.
	enum class Arrival { First, StartPage, ServerBack, Folder, Reopened }

	class Window
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(App* app) { this.m_App = app; }

		// The window at `placement`, or where the system puts it; false when GTK made none.
		public bool Create(const Placement& placement);
		// Navigates to an IED URL.
		public void Show(Core::StringView url);
		// A status page in place of the web client (the server is unreachable).
		public void ShowStatus(Core::StringView title, Core::StringView detail);
		// What a resolver thread left: a URL to show, or why there is none (Window.ch's).
		public void Resolved(Core::StringView url, Core::StringView error, Arrival arrival);
		public void Alert(Core::StringView text);
		public void BringToFront();
		public Placement CurrentPlacement() const;
		// The page's address now (as its script told it, else WebKit's); empty before it has one.
		public Core::String CurrentUrl() const;
		public void Close();
		// False once it closed.
		public bool IsOpen() const { return m_Shell != null; }

		// Shell.h's events (GtkWindow.cf's callbacks).
		public void Message(Core::StringView source, Core::StringView text);
		public bool Navigation(Core::StringView url, bool newWindow, bool gesture);
		public void TitleChanged(Core::StringView title);
		public bool Failed(Core::StringView url, Core::StringView why);
		public void Closing();
		public void Closed();
		public void Crashed();

		public App* m_App;
		public void* m_Shell;                // Shell.h's handle; null once the window closed
		public Core::String m_Origin;        // the server's origin this window shows
		public Core::String m_PageUrl;       // the page's address as its script last told it (history changes included)
		public Core::String m_DockQuery;     // "win=...": the docked window a URL being resolved brings back
		public Core::String m_ResolvedUrl;   // a resolver thread's result, under the app's lock
		public Core::String m_ResolveError;
		public Arrival m_ResolveArrival;
		public Core::String m_PendingStatus; // a status page to show once the failed load's signal is over

	private:
		private bool m_ShowingStatus;
	}
}
#endif
