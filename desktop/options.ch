// The command line of ied-desktop (docs/27):
//     ied-desktop [<path>[:<line>[:<col>]]]     a window on that folder or file (default: IED's home)
//     --control <name>                          another server's control pipe (tests)
//     --data <dir>                              the app's state and WebView2 profile elsewhere (tests)
//     --debug-port <n>                          the WebView2's DevTools protocol on loopback port n (tests)
//     --register | --unregister                 put IED in the user's Start menu, or take it out, and exit

include "launcher.ch"

namespace Desktop
{
	class Options
	{
		public Core::String path;      // empty: IED's home
		public int64 line;             // 0: none
		public int64 col;
		public Core::String control;   // empty: this user's server
		public Core::String dataDir;   // empty: <LOCALAPPDATA>\IED\desktop
		public int32 debugPort;        // 0: none
		public bool help;
		public bool registering;       // --register
		public bool unregistering;     // --unregister
	}

	// `options` from the arguments after the program's name; false with `error` saying what is wrong.
	bool ParseOptions(const Core::Array<Core::String>& arguments, Options& options, Core::String& error);
	const char* Usage();
	// Where the app keeps its window placement and WebView2 profile.
	FS::Path DataFolder(const Options& options);
}
