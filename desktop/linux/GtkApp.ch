// The desktop app on Linux (docs/27 "Linux"): one process with many windows (GtkWindow.ch), the server reached (and
// started) off the UI thread, and the watch that starts it again - the Windows app's (desktop/App.ch) behaviour on GTK
// and WebKitGTK. Everything but the resolver threads and the watch runs on the UI thread; what those threads hand over
// goes through `lock` and shell_post.
#if defined(PLATFORM_LINUX)
include "GtkWindow.ch"
include "Relaunch.ch"
include "Watch.ch"
include "Options.ch"

namespace Desktop
{
	const char* c_DesktopVersion = "0.1.0";
	const int32 c_ServerEndWaitMs = 3000;   // how long a server that closed its pipe gets to exit, for its code
	const int32 c_ErrorBytes = 1024;

	class App
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(const Options& options) { this.m_Options = options; m_Data = DataFolder(options); }

		// Runs until the last window closes; the process's exit code.
		public int32 Run();

		public void OpenHome();
		public void OpenLocation(const Launch::OpenRequest& request);
		public void OpenUrl(Core::StringView url, bool atCursor);
		public void OpenFolder(Window* window);
		public void OpenInWeb(Core::StringView root);
		public void Relaunch();

		// From windows (UI thread).
		public void DeliverResolved(Window* window);
		public void Closing(Window* window);
		public void Closed(Window* window);
		public void Retry();
		public void Report(Core::StringView what);

		// From other threads.
		public Net::LocalStream EnsureServer(Core::String& error);
		public void ServerGone();
		public void ServerBack();
		public void ServerFailed(Core::StringView why);
		public void ServerAsksRelaunch();
		public void Remember(Core::StringView status);

		// What those threads post to the UI thread (GtkApp.cf's callbacks).
		public void ReconnectAll();
		public void ShowServerFailure();
		public void ShowPendingAlert();

		public Options m_Options;
		public FS::Path m_Data;
		public Launch::LogFile m_Log;   // desktop.log: the app's starts and the server's starts, ends and failures

	private:
		// No WebKitGTK or no display: why goes to the log and the terminal, and IED opens in the default browser.
		private int32 OpenInBrowserInstead(Core::StringView why);
		private Window* NewWindow();
		private Window* NewWindowAt(const Placement& placement);
		private Placement PlacementFor(Core::StringView url, bool atCursor);
		private Placement NextPlacement();
		private Core::Array<RelaunchWindow> TakeRelaunch();
		private void OpenRelaunched(const Core::Array<RelaunchWindow>& relaunched);
		private bool StartAgain(const FS::Path& file, Core::String& error);
		private void Resolve(Window* window, Core::StringView method, Core::StringView params, Arrival arrival);
		private void Reconnect(Window* window, Arrival arrival);

		private Core::Array<Window*> m_Windows;      // the open ones
		private Core::Array<Window*> m_Made;         // every one made: GTK and posted work hold their pointers
		private Watch m_Watch;
		private Threading::Mutex m_Lock;             // what resolver threads and the watch hand over
		private Threading::Mutex m_StartLock;        // one server start at a time, and `server`
		private Process::Child m_Server;             // the server this app started last, for its exit code
		private Core::String m_WatchError;
		private Core::String m_LastStatus;           // the server's last `status` reply, under `lock` (Remember)
		private Core::String m_PendingAlert;         // a message a thread left for the UI thread, under `lock`
		private bool m_Quitting;
	}
}
#endif
