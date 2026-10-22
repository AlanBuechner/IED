#pragma once
#include "Target/Frames.h"
#include "Target/Launch.h"
#include "Target/OutputPipes.h"
#include "Target/Patches.h"
#include "Target/Process.h"
#include "Target/SourceBreakpoints.h"
#include "Target/Step.h"
#include "Target/Symbols.h"
#include "Target/Values.h"
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace C4DAP::Target
{
	enum class StopReason { Entry, Breakpoint, Step, Pause, Exception };

	struct StopInfo
	{
		StopReason reason = StopReason::Pause;
		DWORD thread = 0;
		std::vector<uint32_t> breakpoints;
		std::string description;
	};

	/// What the engine reports, on the engine's thread.
	struct EngineEvents
	{
		std::function<void(const StopInfo&)> stopped;
		std::function<void(DWORD thread, bool started)> thread;
		std::function<void(const BoundBreakpoint&)> breakpointChanged;
		std::function<void(const char* category, const std::string& text)> output;
		std::function<void(uint32_t exitCode)> exited;
		/// The program's process exists (its id), so a client can watch what it uses.
		std::function<void(DWORD processId)> processStarted;
	};

	struct ThreadInfo
	{
		DWORD id = 0;
		std::string name;
	};

	struct FrameInfo
	{
		uint32_t id = 0;
		std::string name;
		SourcePosition position;
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

	/// One debug session: the program, its symbols, breakpoints and steps, and the frames and values of the current
	/// stop (docs/tools/03-editor-lsp-dap.md, "The debugger"). Single-threaded: every call is made on the thread
	/// that launched the program, which is the one Windows sends its debug events to.
	class Engine
	{
	public:
		explicit Engine(EngineEvents events);
		~Engine();
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		/// Starts the program and holds it at its start, its code loaded; "" or why it did not start.
		std::string Launch(const LaunchOptions& options);
		/// The client has set its breakpoints: the program runs (to its entry, with stopOnEntry).
		void Configured();
		std::vector<BoundBreakpoint> SetBreakpoints(const std::string& path, const std::vector<uint32_t>& lines);
		void SetExceptionFilters(bool thrown, bool uncaught);

		void Continue();
		void Step(StepKind kind, DWORD thread);
		void Pause();
		void Terminate();

		std::vector<ThreadInfo> Threads() const;
		std::vector<FrameInfo> StackTrace(DWORD thread, uint32_t start, uint32_t count, uint32_t& total);
		/// The `Locals` scope's reference, or 0 for an unknown frame.
		uint32_t LocalsOf(uint32_t frameId);
		std::vector<VariableInfo> Variables(uint32_t reference);
		EvaluateResult Evaluate(const std::string& expression, uint32_t frameId);

		/// The program is running: debug events are due (Poll waits for one).
		bool Running() const { return m_State == State::Running; }
		bool Ended() const { return m_State == State::Ended; }
		void Poll(DWORD milliseconds);

	private:
		enum class State { Idle, Running, Stopped, Ended };
		// A patch a thread steps over: the original instruction runs with the other threads suspended (so none
		// passes the breakpoint unseen), then the patch goes back.
		struct Rearm
		{
			uint64_t address = 0;
			std::vector<DWORD> suspended;
		};
		struct ActiveStep
		{
			Target::Step step;
			ThreadState before;     // the thread before its last single step
			uint64_t returnPatch = 0;
			uint64_t returnRsp = 0;
			uint64_t frameRsp = 0;  // the stepped frame's stack pointer: it and its callers are at or above it
			std::vector<uint64_t> catchPatches; // a throw left the frame: its handlers' and its callers'
		};
		struct FrameRecord
		{
			DWORD thread = 0;
			size_t depth = 0;
			FrameRegisters frame;
		};
		struct LocalsReference { size_t frame = 0; };
		using Reference = std::variant<LocalsReference, ValueRef>;

		void Handle(const DEBUG_EVENT& event);
		void ProcessCreated(const DEBUG_EVENT& event);
		void ThreadCreated(const DEBUG_EVENT& event);
		void ProcessExited(const DEBUG_EVENT& event);
		void DebugOutput(const DEBUG_EVENT& event);
		void ExceptionRaised(const DEBUG_EVENT& event);
		void BreakpointReached(const DEBUG_EVENT& event, uint64_t address);
		void PatchReached(const DEBUG_EVENT& event, uint64_t address);
		void SingleStepped(const DEBUG_EVENT& event);
		void ProgramException(const DEBUG_EVENT& event);
		void StepDecided(const DEBUG_EVENT& event, const Target::Step::Decision& decision);

		void PassOn(const DEBUG_EVENT& event, DWORD status);
		void Stop(const DEBUG_EVENT& event, DWORD status, StopInfo stop);
		void Resume(DWORD thread, bool trap);
		void StepOverPatch(DWORD thread, uint64_t address);
		bool Rearmed(DWORD thread);
		bool LandedOnBreakpoint(const DEBUG_EVENT& event);
		void CancelStep();
		void StopAtCatches(DWORD thread);
		void ReleaseCatchStops();
		void ArmUncaughtHook();
		void ClearStopTables();
		ThreadState Observe(DWORD thread) const;
		uint32_t Remember(const Reference& reference);
		VariableInfo Describe(const std::string& name, const ValueRef& value);
		std::optional<ValueRef> Variable(const std::string& name, const FrameRecord* frame) const;

		EngineEvents m_Events;
		Process m_Process;
		Symbols m_Symbols;
		Values m_Values{ m_Process, m_Symbols };
		Patches m_Patches;
		SourceBreakpoints m_Breakpoints;
		std::unique_ptr<OutputPipes> m_Output;

		State m_State = State::Idle;
		bool m_SymbolsLoaded = false;
		bool m_StartReached = false;   // the loader's breakpoint, where the program is held until configured
		bool m_Configured = false;
		bool m_StopOnEntry = false;
		bool m_StopOnThrown = false;
		bool m_StopOnUncaught = true;
		bool m_PauseRequested = false;
		bool m_Terminating = false;
		uint32_t m_ExitCode = 0;
		DEBUG_EVENT m_Held{};          // the event the program is stopped at
		DWORD m_HeldStatus = DBG_CONTINUE;
		std::unordered_map<DWORD, Rearm> m_Rearms;
		std::optional<ActiveStep> m_Step;
		uint64_t m_EntryPatch = 0;
		uint64_t m_UncaughtPatch = 0;
		uint64_t m_BreakInStart = 0;   // where the thread a pause injects starts
		std::set<DWORD> m_BreakInThreads;
		std::unordered_map<DWORD, std::string> m_LastThrown;

		std::vector<FrameRecord> m_Frames;
		std::unordered_map<DWORD, std::vector<uint32_t>> m_FramesOfThread;
		std::vector<Reference> m_References;
	};
} // namespace C4DAP::Target
