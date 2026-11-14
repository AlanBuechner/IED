#ifndef _WIN32
#include "Target/LinuxEngine.h"

namespace C4DAP::Target
{
	namespace
	{
		const char* const kNoDebugger = "C4DAP cannot debug programs on Linux yet: its debugger is the Win32 debug API's, and "
			"Linux's (ptrace and the program's DWARF) is still to be written";
	}

	std::string Engine::Launch(const LaunchOptions&)
	{
		return kNoDebugger;
	}

	std::vector<BoundBreakpoint> Engine::SetBreakpoints(const std::string& path, const std::vector<uint32_t>& lines)
	{
		std::vector<BoundBreakpoint> bound;
		for (uint32_t line : lines)
			bound.push_back(BoundBreakpoint{ m_NextBreakpoint++, false, line, path, kNoDebugger });
		return bound;
	}

	std::vector<FrameInfo> Engine::StackTrace(ThreadId, uint32_t, uint32_t, uint32_t& total)
	{
		total = 0;
		return {};
	}

	EvaluateResult Engine::Evaluate(const std::string&, uint32_t)
	{
		return EvaluateResult{ kNoDebugger, {} };
	}
} // namespace C4DAP::Target
#endif
