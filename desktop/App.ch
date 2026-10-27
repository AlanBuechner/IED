// The desktop app (docs/27): one process with many windows, one WebView2 environment for them, the server reached
// (and started) off the UI thread, and the watch that starts it again. Everything but the resolver threads and the
// watch runs on the UI thread; what those threads hand over goes through `lock` and a posted message.

include "Window.ch"
include "Folder.ch"
include "Relaunch.ch"
include "Watch.ch"
include "Options.ch"

namespace Desktop
{
	const char* c_WindowClass = "IedDesktopWindow";
	const char* c_MessageClass = "IedDesktopMessages";
	const char* c_DesktopVersion = "0.1.0";
	const uint32 c_ServerBackMessage = 0x8002;     // WM_APP + 2
	const uint32 c_ServerFailedMessage = 0x8003;   // WM_APP + 3
	const uint32 c_ServerRelaunchMessage = 0x8006; // WM_APP + 6: the server asked to be relaunched
	const int32 c_ServerEndWaitMs = 3000;          // how long a server that closed its pipe gets to exit, for its code

	class App
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(const Options& options) { this.m_Options = options; m_Data = DataFolder(options); }

		// Runs until the last window closes; the process's exit code.
		public int32 Run();

		public void OpenHome();
		public void OpenLocation(Core::StringView path, int64 line, int64 col);
		// A window straight on a URL of the server (the profile is signed in already).
		public void OpenUrl(Core::StringView url);
		// File > Open Folder: the folder the person picks, opened on the server and shown in `window`.
		public void OpenFolder(Window* window);
		// The workspace rooted at `root` in the system's default browser, through a one-time sign-in URL.
		public void OpenInWeb(Core::StringView root);
		// Onto the newest install (Relaunch.ch): this process writes its windows down, stops its server, starts its
		// program again and exits.
		public void Relaunch();

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
		// The watch found nobody on the control pipe: how the server ended goes to the log.
		public void ServerGone();
		public void ServerBack();
		public void ServerFailed(Core::StringView why);
		// The server asked to be relaunched and the watch claimed it: Relaunch, on the UI thread.
		public void ServerAsksRelaunch();
		// The watch's last `status` reply from the server (the workspaces it has open), kept so that a server started
		// again opens the windows' workspaces again.
		public void Remember(Core::StringView status);

		public Options m_Options;
		public FS::Path m_Data;
		public Launch::LogFile m_Log;   // desktop.log: the app's starts and the server's starts, ends and failures

	private:
		private bool RegisterClasses();
		private bool RuntimeInstalled();
		private int32 OpenInBrowserInstead();
		private void CreateEnvironment();
		private void EnvironmentReady(int32 error, void* created);
		private Window* NewWindow();
		private Window* NewWindowAt(const Placement& placement);
		// The windows a relaunch file names, once the process that wrote it is gone; none without one.
		private Core::Array<RelaunchWindow> TakeRelaunch();
		private void OpenRelaunched(const Core::Array<RelaunchWindow>& relaunched);
		private bool StartAgain(const FS::Path& file, Core::String& error);
		// Asks the server for the window's URL on a thread of its own; `arrival` says what the window does with it.
		private void Resolve(Window* window, Core::StringView method, Core::StringView params, Arrival arrival);
		// The window back on a server started again: the workspace it showed, when the server had it open, is opened
		// again and shown; any other window resolves its URL as `arrival` says.
		private void Reconnect(Window* window, Arrival arrival);

		private HINSTANCE__* m_Instance;
		private HWND__* m_Messages;
		private ICoreWebView2Environment* m_Environment;
		private Core::Array<Window*> m_Windows;
		private CallbackSet m_Callbacks;
		private Watch m_Watch;
		private Threading::Mutex m_Lock;         // what resolver threads and the watch hand over
		private Threading::Mutex m_StartLock;    // one server start at a time, and `server`
		private Process::Child m_Server;         // the server this app started last, for its exit code
		private Core::String m_WatchError;
		private Core::String m_LastStatus;       // the server's last `status` reply, under `lock` (Remember)
		private bool m_Quitting;
	}

	int64 WindowProcedure(HWND__* hwnd, uint32 message, uint64 wParam, int64 lParam);
	int64 MessageProcedure(HWND__* hwnd, uint32 message, uint64 wParam, int64 lParam);
}
