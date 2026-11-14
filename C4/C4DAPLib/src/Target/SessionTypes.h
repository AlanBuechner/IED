#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// What the debug adapter's server and its engine say to each other (Target/Engine.h): the same on every system, whose
// engine is its own (the Win32 debug API's on Windows; Target/LinuxEngine.h on Linux).
namespace C4DAP::Target
{
	/// A thread's or a process's id as the system numbers them.
#ifdef _WIN32
	using ThreadId = unsigned long; // DWORD
#else
	using ThreadId = uint32_t;
#endif

	struct SourcePosition
	{
		std::string path;
		uint32_t line = 0;
		bool Known() const { return line != 0; }
	};

	struct LaunchOptions
	{
		std::string program;
		std::vector<std::string> args;
		std::string cwd;
		bool stopOnEntry = false;
	};

	/// A source breakpoint as the client sees it: bound (verified) to the line it stops on, or not with why.
	struct BoundBreakpoint
	{
		uint32_t id = 0;
		bool verified = false;
		uint32_t line = 0;
		std::string source;
		std::string message;
	};

	enum class StepKind { Over, Into, Out };

	enum class StopReason { Entry, Breakpoint, Step, Pause, Exception };

	struct StopInfo
	{
		StopReason reason = StopReason::Pause;
		ThreadId thread = 0;
		std::vector<uint32_t> breakpoints;
		std::string description;
	};

	/// What the engine reports, on the engine's thread.
	struct EngineEvents
	{
		std::function<void(const StopInfo&)> stopped;
		std::function<void(ThreadId thread, bool started)> thread;
		std::function<void(const BoundBreakpoint&)> breakpointChanged;
		std::function<void(const char* category, const std::string& text)> output;
		std::function<void(uint32_t exitCode)> exited;
		/// The program's process exists (its id), so a client can watch what it uses.
		std::function<void(ThreadId processId)> processStarted;
	};

	struct ThreadInfo
	{
		ThreadId id = 0;
		std::string name;
	};

	struct FrameInfo
	{
		uint32_t id = 0;
		std::string name;
		SourcePosition position;
		std::string instructionPointer; // the frame's pc: inlined calls share their caller's
	};

	struct VariableInfo
	{
		std::string name;
		std::string value;
		std::string type;
		uint32_t reference = 0; // non-zero: the value has children
	};

	struct EvaluateResult
	{
		std::string error; // non-empty: no value
		VariableInfo value;
	};
} // namespace C4DAP::Target
