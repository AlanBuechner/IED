// The desktop app (docs/27): one process with many windows, one WebView2 environment for them, the server reached
// (and started) off the UI thread, and the watch that starts it again. Everything but the resolver threads and the
// watch runs on the UI thread; what those threads hand over goes through `lock` and a posted message.

include "window.ch"
include "watch.ch"
include "options.ch"

namespace Desktop
{
	const char* kWindowClass = "IedDesktopWindow";
	const char* kMessageClass = "IedDesktopMessages";
	const char* kDesktopVersion = "0.1.0";
	const uint32 kServerBackMessage = 0x8002;     // WM_APP + 2
	const uint32 kServerFailedMessage = 0x8003;   // WM_APP + 3

	class App
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(const Options& options) { this.options = options; data = DataFolder(options); }

		// Runs until the last window closes; the process's exit code.
		public int32 Run();

		public void OpenHome();
		public void OpenLocation(Core::StringView path, int64 line, int64 col);
		// A window straight on a URL of the server (the profile is signed in already).
		public void OpenUrl(Core::StringView url);

		// From windows (UI thread).
		public void DeliverResolved(Window* window);
		public void Closing(Window* window);
		public void Closed(Window* window, HWND__* hwnd);
		public void Retry();
		public void BrowserExited();
		public void Report(Core::StringView what, int32 hresult);
		// The message window's work for one message; false: the default handling.
		public bool Handle(uint32 message);

		// From other threads.
		public Net::LocalStream EnsureServer(Core::String& error);
		public void ServerBack();
		public void ServerFailed(Core::StringView why);

		public Options options;
		public FS::Path data;

	private:
		private bool RegisterClasses();
		private bool RuntimeInstalled();
		private int32 OpenInBrowserInstead();
		private void CreateEnvironment();
		private void EnvironmentReady(int32 error, void* created);
		private Window* NewWindow();
		// Asks the server for the window's URL on a thread of its own; `onlyIfMoved`: show it only when the server's
		// origin changed or the window shows a status page.
		private void Resolve(Window* window, Core::StringView method, Core::StringView params, bool onlyIfMoved);

		private HINSTANCE__* instance;
		private HWND__* messages;
		private ICoreWebView2Environment* environment;
		private Core::Array<Window*> windows;
		private CallbackSet callbacks;
		private Watch watch;
		private Threading::Mutex lock;         // what resolver threads and the watch hand over
		private Threading::Mutex startLock;    // one server start at a time
		private Core::String watchError;
		private bool quitting;
	}

	int64 WindowProcedure(HWND__* hwnd, uint32 message, uint64 wParam, int64 lParam);
	int64 MessageProcedure(HWND__* hwnd, uint32 message, uint64 wParam, int64 lParam);
}
