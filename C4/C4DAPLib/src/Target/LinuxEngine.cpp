#ifndef _WIN32
#include "Target/LinuxEngine.h"
#include <filesystem>

namespace C4DAP::Target
{
	namespace
	{
		const std::string kNoDebugger = "C4DAP cannot debug programs on Linux yet: its debugger is the Win32 debug API's, and "
			"Linux's (ptrace and the program's DWARF) is still to be written";
	}

	std::string Engine::Launch(const LaunchOptions& options)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(options.program, ec)) return "no program at " + options.program;
		return kNoDebugger;
	}

	std::vector<BoundBreakpoint> Engine::SetBreakpoints(const std::string& path, const std::vector<uint32_t>& lines)
	{
		std::vector<BoundBreakpoint> bound;
		for (uint32_t line : lines)
			bound.push_back(BoundBreakpoint{ m_NextBreakpoint++, false, line, path, "the program's code is not loaded (" + kNoDebugger + ")" });
		return bound;
	}

	std::vector<FrameInfo> Engine::StackTrace(ThreadId, uint32_t, uint32_t, uint32_t& total)
	{
		total = 0;
		return {};
	}

	EvaluateResult Engine::Evaluate(const std::string&, uint32_t)
	{
		return EvaluateResult{ "the program is not stopped (" + kNoDebugger + ")", {} };
	}
} // namespace C4DAP::Target
#endif
