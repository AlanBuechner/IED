// The command line of ied-desktop (docs/27):
//     ied-desktop [<path>[:<line>[:<col>]]]     a window on that folder or file (default: IED's home)
//     --control <name>                          another server's control pipe (tests)
//     --data <dir>                              the app's state and WebView2 profile elsewhere (tests)
//     --debug-port <n>                          the WebView2's DevTools protocol on loopback port n (tests)
//     --register | --unregister                 put IED in the user's Start menu, or take it out, and exit

include "Launcher.ch"

namespace Desktop
{
	class Options
	{
		public Core::String m_Path;      // empty: IED's home
		public int64 m_Line;             // 0: none
		public int64 m_Col;
		public Core::String m_Control;   // empty: this user's server
		public Core::String m_DataDir;   // empty: <LOCALAPPDATA>\IED\desktop
		public int32 m_DebugPort;        // 0: none
		public bool m_Help;
		public bool m_Registering;       // --register
		public bool m_Unregistering;     // --unregister
		public Core::String m_Relaunch;  // --relaunch <file>: the windows of the process this one replaces (Relaunch.ch)
	}

	// `options` from the arguments after the program's name; false with `error` saying what is wrong.
	bool ParseOptions(const Core::Array<Core::String>& arguments, Options& options, Core::String& error);
	const char* Usage();
	// Where the app keeps its window placement and WebView2 profile.
	FS::Path DataFolder(const Options& options);
	// The app's log: %LOCALAPPDATA%\IED\logs\desktop.log beside the server's, or desktop.log in --data's folder.
	FS::Path LogFilePath(const Options& options);
}
