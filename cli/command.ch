// The command line of `ied` (features F-SRV-7), read into a Command.
//     ied [<path>[:<line>[:<col>]]]     open a folder or file (default: the current folder)
//     ied status | url | stop
//     ied --control <name> ...          another server's control pipe (tests)
//     ied --no-browser ...              print the URL instead of opening it

include "bedrock.ch"

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
	}

	// `command` from the arguments after the program's name; false with `error` saying what is wrong.
	bool ParseCommand(const Core::Array<Core::String>& arguments, Command& command, Core::String& error);
	// "C:\src\a.cf:42:3" to the path, line and column; a Windows drive's colon stays in the path.
	void SplitLocation(Core::StringView text, Core::String& path, int64& line, int64& col);
	const char* Usage();
}
