#pragma once
#include "Target/OutputPipes.h"
#include <string>
#include <vector>

namespace C4DAP::Target
{
	struct LaunchOptions
	{
		std::string program;
		std::vector<std::string> args;
		std::string cwd;
		bool stopOnEntry = false;
	};

	/// The command line Windows hands the program: each argument quoted so the C runtime splits it back the same.
	std::wstring CommandLine(const std::string& program, const std::vector<std::string>& args);

	/// Starts the program under the debugger (the calling thread receives its debug events) with stdout and stderr
	/// on `output`'s pipes and stdin on NUL. Only those handles are inherited. Returns "" or why it did not start.
	std::string StartDebugged(const LaunchOptions& options, const OutputPipes& output);
} // namespace C4DAP::Target
