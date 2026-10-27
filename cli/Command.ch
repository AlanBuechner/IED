// The command line of `ied` (features F-SRV-7), read into a Command.
//     ied [<path>[:<line>[:<col>]]]     open a folder or file (default: the current folder)
//     ied status | url | stop
//     ied --control <name> ...          another server's control pipe (tests)
//     ied --no-browser ...              print the URL instead of opening it
//     ied --browser ...                 open it in the default browser, not the desktop app

include "Launcher.ch"

namespace Cli
{
	enum class Verb { Open, Status, Url, Stop, Help }

	class Command
	{
		public Verb m_Verb;
		public Core::String m_Path;      // open: as written, without its line and column
		public int64 m_Line;             // 0: none
		public int64 m_Col;
		public Core::String m_Control;   // empty: this user's server
		public bool m_NoBrowser;
		public bool m_Browser;           // the default browser even when the desktop app is beside ied
	}

	// `command` from the arguments after the program's name; false with `error` saying what is wrong.
	bool ParseCommand(const Core::Array<Core::String>& arguments, Command& command, Core::String& error);
	const char* Usage();
}
