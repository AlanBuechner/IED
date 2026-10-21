// The browser the server drives (docs/27 "Engine"): Edge - or Chrome - headless, with IED's own profile, its DevTools
// protocol on a loopback port it picks itself. The process lives in the server's job, so it ends with the server.

include "cdp.ch"

namespace Browser
{
	const int64 kEngineStartNs = 20000000000;

	// The browser to run: `configured` (the browser.executable setting) when it is a file, else the first of Edge and
	// Chrome found where they install; empty when there is none.
	FS::Path FindBrowser(Core::StringView configured);
	// The command line after the program: headless, the profile, the DevTools port (0: the browser picks).
	Core::Array<Core::String> BrowserArguments(Core::StringView profile);
	// "ws://127.0.0.1:<port><path>" from the text of a profile's DevToolsActivePort file; empty when it is not one.
	Core::String EndpointFromActivePort(Core::StringView text);

	class Engine
	{
		// Starts the browser and connects to it; false with `error`.
		public bool Start(const FS::Path& executable, const FS::Path& profile, Core::String& error);
		public bool Running() const { return cdp != null && cdp->IsOpen(); }
		public void Stop();

		public CdpConnection* cdp;
		public Process::Child* process;
	}
}
