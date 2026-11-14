#pragma once
#include "Target/SessionTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace C4DAP::Target
{
	/// The debug session on Linux, where C4DAP has no debugger yet: the Windows engine is the Win32 debug API's, and
	/// Linux's (ptrace, the program's DWARF) is still to be written. It speaks the same interface so the adapter runs, but
	/// a launch fails saying so, and with no program there is nothing to stop, step or inspect.
	class Engine
	{
	public:
		explicit Engine(EngineEvents) {} // with no program there is nothing to report
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		/// Why the program does not start: there is no debugger on Linux yet.
		std::string Launch(const LaunchOptions& options);
		void Configured() {}
		/// Each breakpoint unverified, saying why.
		std::vector<BoundBreakpoint> SetBreakpoints(const std::string& path, const std::vector<uint32_t>& lines);
		void SetExceptionFilters(bool, bool) {}

		void Continue() {}
		void Step(StepKind, ThreadId) {}
		void Pause() {}
		void Terminate() {}

		std::vector<ThreadInfo> Threads() const { return {}; }
		std::vector<FrameInfo> StackTrace(ThreadId thread, uint32_t start, uint32_t count, uint32_t& total);
		uint32_t LocalsOf(uint32_t) { return 0; }
		std::vector<VariableInfo> Variables(uint32_t) { return {}; }
		EvaluateResult Evaluate(const std::string& expression, uint32_t frameId);

		bool Running() const { return false; }
		bool Ended() const { return false; }
		void Poll(uint32_t) {}

	private:
		uint32_t m_NextBreakpoint = 1;
	};
} // namespace C4DAP::Target
