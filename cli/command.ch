// The command line of `ied` (features F-SRV-7), read into a Command.
//     ied [<path>[:<line>[:<col>]]]     open a folder or file (default: the current folder)
//     ied status | url | stop
//     ied --control <name> ...          another server's control pipe (tests)
//     ied --no-browser ...              print the URL instead of opening it
//     ied --browser ...                 open it in the default browser, not the desktop app

include "launcher.ch"

namespace Cli
{
	enum class Verb { Open, Status, Url, Stop, Help }

	class Command
	{
		public Verb verb;
		public Core::String path;      // open: as written, without its line and column
		public int64 line;             // 0: none
		public int64 col;
		public Core::String control;   // empty: this user's server
		public bool noBrowser;
		public bool browser;           // the default browser even when the desktop app is beside ied
	}

	// `command` from the arguments after the program's name; false with `error` saying what is wrong.
	bool ParseCommand(const Core::Array<Core::String>& arguments, Command& command, Core::String& error);
	const char* Usage();
}
