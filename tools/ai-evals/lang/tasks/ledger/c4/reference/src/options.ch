include "bedrock.ch"

namespace Ledger
{
	struct Options
	{
		public Core::String file;
		public Core::String command;
		public Core::String account;   // empty: every account
		public Core::String month;     // YYYY-MM; empty: every month
	}

	// The command line after the program name; false when it is not "<file> <command> [--account NAME] [--month YYYY-MM]".
	bool ParseOptions(int32 argc, char** argv, Options& options);
	// Four digits, '-', and a month from 01 to 12.
	bool IsMonth(Core::StringView text);
}
