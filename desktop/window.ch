// One IED window (docs/27 "The window"): a Win32 top-level window and the WebView2 in it showing the web client.
// Its methods run on the app's UI thread; the window procedure finds the object in GWLP_USERDATA, the app's list keeps
// it alive.

include "com.ch"
include "placement.ch"
include "pages.ch"

namespace Desktop
{
	const uint32 kResolvedMessage = 0x8001;   // WM_APP + 1: a resolver thread left a URL or an error

	class Window
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(App* app) { this.app = app; }

		// The Win32 window, at `placement` or where the system puts it; false when Windows refused.
		public bool Create(HINSTANCE__* instance, const Placement& placement);
		// The WebView2 in the window, from the app's environment.
		public void Attach(ICoreWebView2Environment* environment);
		// Navigates to an IED URL now, or once the WebView2 is ready.
		public void Show(Core::StringView url);
		// A status page in place of the web client (the server is unreachable).
		public void ShowStatus(Core::StringView title, Core::StringView detail);
		// What a resolver thread left: a URL to show, or why there is none; `onlyIfMoved` keeps the page when the server
		// came back where it was (the web client reconnects by itself).
		public void Resolved(Core::StringView url, Core::StringView error, bool onlyIfMoved);
		public Placement CurrentPlacement() const;
		public void Close();

		// The window procedure's work for one message; false: the default handling.
		public bool Handle(uint32 message, uint64 wParam, int64 lParam, int64& result);

		public App* app;
		public HWND__* hwnd;
		public ICoreWebView2Controller* controller;
		public ICoreWebView2* webview;
		public Core::String origin;        // the server's origin this window shows
		public Core::String resolvedUrl;   // a resolver thread's result, under the app's lock
		public Core::String resolveError;
		public bool resolveOnlyIfMoved;

	private:
		private void ControllerReady(int32 error, void* created);
		private void Configure();
		private void Listen();
		private void Fit();
		private void ShowPending();
		private void TitleChanged();
		private int32 NavigationStarting(ICoreWebView2NavigationStartingEventArgs* args);
		private int32 NavigationCompleted(ICoreWebView2NavigationCompletedEventArgs* args);
		private int32 NewWindowRequested(ICoreWebView2NewWindowRequestedEventArgs* args);
		private int32 MessageReceived(ICoreWebView2WebMessageReceivedEventArgs* args);
		private int32 PermissionRequested(ICoreWebView2PermissionRequestedEventArgs* args);
		private int32 ProcessFailed(ICoreWebView2ProcessFailedEventArgs* args);

		private CallbackSet callbacks;
		private Core::String pendingUrl;
		private Core::String pendingHtml;
		private bool showingStatus;
	}

	// Win32's string for `text`, null-terminated.
	Core::Array<char16> Wide(Core::StringView text);
	// A string WebView2 allocated, as UTF-8; it is freed.
	Core::String TakeWide(char16* text);
}
