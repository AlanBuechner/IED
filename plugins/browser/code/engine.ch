// The browser the server drives (docs/27 "Engine"): Edge - or Chrome - headless, with IED's own profile, its DevTools
// protocol on a loopback port it picks itself. The process lives in the server's job, so it ends with the server. A
// browser still on the profile (one an earlier server left, which a new one would only hand its arguments to) is closed
// first; a browser that hands off to another process is reached through the port that one writes; stopping asks the
// browser to close before its process is ended.

include "cdp.ch"

namespace Browser
{
	const int64 kEngineStartNs = 20000000000;
	const int64 kEngineHandOffNs = 5000000000;    // after the started process exits, how long another may take to open the port
	const int64 kEngineCloseHeldNs = 10000000000;  // how long a browser left on the profile may take to close

	// The browser to run: `configured` (the browser.executable setting) when it is a file, else the first of Edge and
	// Chrome found where they install; empty when there is none.
	FS::Path FindBrowser(Core::StringView configured);
	// The command line after the program: headless, the profile, the DevTools port (0: the browser picks).
	Core::Array<Core::String> BrowserArguments(Core::StringView profile);
	// "ws://127.0.0.1:<port><path>" from the text of a profile's DevToolsActivePort file; empty when it is not one.
	Core::String EndpointFromActivePort(Core::StringView text);

	// The command that ends every process whose command line names `profile` (Windows PowerShell): the last resort for a
	// browser left on IED's profile whose DevTools port is not known.
	Core::String EndProfileHoldersScript(Core::StringView profile);

	class Engine
	{
		// Starts the browser and connects to it; false with `error`.
		public bool Start(const FS::Path& executable, const FS::Path& profile, Core::String& error);
		public bool Running() const { return cdp != null && cdp->IsOpen(); }
		public void Stop();

		public CdpConnection* cdp;
		public Process::Child* process;

	private:
		private bool Launch(const FS::Path& executable, const FS::Path& profile, Core::String& error);
	}
}
