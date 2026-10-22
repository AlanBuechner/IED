include "bedrock.ch"

namespace Ledger
{
	struct Options
	{
		public Core::String file;
		public Core::String command;
		public Core::String account;   // empty: every account
	}

	// The command line after the program name; false when it is not "<file> <command> [--account NAME]".
	bool ParseOptions(int32 argc, char** argv, Options& options);
}
