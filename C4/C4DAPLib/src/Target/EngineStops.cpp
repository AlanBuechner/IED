#include "Target/Engine.h"
#include "Target/CatchHandlers.h"
#include "Target/ThrownType.h"
#include <algorithm>
#include <cstdio>

namespace C4DAP::Target
{
	namespace
	{
		constexpr DWORD kTrapFlag = 0x100;
		constexpr size_t kReturnFrames = 2;            // the current frame and the one it returns to
		constexpr size_t kMaxCatchFrames = 256;

		std::string ExceptionText(DWORD code)
		{
			switch (code)
			{
			case EXCEPTION_ACCESS_VIOLATION: return "access violation";
			case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
			case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero";
			case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
			default:
			{
				char text[32];
				std::snprintf(text, sizeof(text), "exception 0x%08lx", static_cast<unsigned long>(code));
				return text;
			}
			}
		}
	}

	void Engine::ExceptionRaised(const DEBUG_EVENT& event)
	{
		const EXCEPTION_RECORD& record = event.u.Exception.ExceptionRecord;
		const uint64_t address = reinterpret_cast<uint64_t>(record.ExceptionAddress);
		if (m_Terminating) { PassOn(event, DBG_CONTINUE); return; }
		switch (record.ExceptionCode)
		{
		case EXCEPTION_BREAKPOINT: BreakpointReached(event, address); break;
		case EXCEPTION_SINGLE_STEP: SingleStepped(event); break;
		default: ProgramException(event); break;
		}
	}

	void Engine::BreakpointReached(const DEBUG_EVENT& event, uint64_t address)
	{
		if (!m_StartReached)
		{
			// The loader's breakpoint: the program waits here, unreported, until the client is configured.
			m_StartReached = true;
			m_Held = event;
			m_HeldStatus = DBG_CONTINUE;
			m_State = State::Stopped;
			return;
		}
		if (m_Patches.Has(address)) { PatchReached(event, address); return; }
		if (m_PauseRequested || m_BreakInThreads.count(event.dwThreadId))
		{
			m_PauseRequested = false;
			CancelStep();
			Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Pause, m_Process.MainThread() });
			return;
		}
		Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Exception, event.dwThreadId, {}, "the program executed a breakpoint instruction" });
	}

	void Engine::PatchReached(const DEBUG_EVENT& event, uint64_t address)
	{
		const DWORD thread = event.dwThreadId;
		CONTEXT registers;
		m_Process.Registers(thread, registers);
		registers.Rip = address; // back onto the patched instruction
		m_Process.SetRegisters(thread, registers);

		if (m_Step && address == m_Step->returnPatch && m_Step->step.Thread() == thread && registers.Rsp == m_Step->returnRsp)
		{
			m_Patches.Release(m_Process, m_Step->returnPatch);
			m_Step->returnPatch = 0;
			ReleaseCatchStops(); // the call returned: whatever it threw was caught inside it
			StepDecided(event, m_Step->step.AfterReturn(Observe(thread)));
			return;
		}
		if (m_Step && m_Step->step.Thread() == thread &&
			std::find(m_Step->catchPatches.begin(), m_Step->catchPatches.end(), address) != m_Step->catchPatches.end())
		{
			CancelStep();
			Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Step, thread }); // a throw ended the step in a catch
			return;
		}
		if (address == m_EntryPatch)
		{
			m_Patches.Release(m_Process, m_EntryPatch);
			m_EntryPatch = 0;
			Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Entry, thread });
			return;
		}
		if (address == m_UncaughtPatch)
		{
			CancelStep();
			const std::string& thrown = m_LastThrown[thread];
			Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Exception, thread, {}, (thrown.empty() ? "an exception" : thrown) + " was thrown and not caught" });
			return;
		}
		std::vector<uint32_t> ids = m_Breakpoints.IdsAt(address);
		if (!ids.empty())
		{
			CancelStep();
			StopAtBreakpoint(event, thread, address, std::move(ids));
			return;
		}
		// A step's return breakpoint reached by another thread or a deeper call: not a stop.
		m_Held = event;
		m_HeldStatus = DBG_CONTINUE;
		Resume(thread, false);
	}

	void Engine::SingleStepped(const DEBUG_EVENT& event)
	{
		const DWORD thread = event.dwThreadId;
		const bool rearmed = Rearmed(thread);
		if (m_Step && m_Step->step.Thread() == thread && m_Step->returnPatch == 0)
		{
			if (LandedOnBreakpoint(event)) return;
			StepDecided(event, m_Step->step.AfterInstruction(m_Step->before, Observe(thread)));
			return;
		}
		if (rearmed) { PassOn(event, DBG_CONTINUE); return; }
		ProgramException(event);
	}

	void Engine::ProgramException(const DEBUG_EVENT& event)
	{
		const EXCEPTION_RECORD& record = event.u.Exception.ExceptionRecord;
		const DWORD thread = event.dwThreadId;
		const bool firstChance = event.u.Exception.dwFirstChance != 0;
		if (record.ExceptionCode == kCppExceptionCode)
		{
			const std::string thrown = ThrownType(m_Process, record);
			const std::string what = thrown.empty() ? "an exception" : thrown;
			m_LastThrown[thread] = thrown;
			if (firstChance && m_StopOnThrown)
			{
				CancelStep();
				Stop(event, DBG_EXCEPTION_NOT_HANDLED, StopInfo{ StopReason::Exception, thread, {}, what + " was thrown" });
				return;
			}
			if (firstChance && m_Step && m_Step->step.Thread() == thread && m_Step->catchPatches.empty()) StopAtCatches(thread);
			// With a debugger attached, Windows hands an uncaught exception to it (a second chance) instead of the
			// program's terminate handler; the stack is still the one the throw left.
			if (!firstChance && !m_StopOnUncaught) { PassOn(event, DBG_EXCEPTION_NOT_HANDLED); return; }
			if (!firstChance)
			{
				CancelStep();
				Stop(event, DBG_EXCEPTION_NOT_HANDLED, StopInfo{ StopReason::Exception, thread, {}, what + " was thrown and not caught" });
				return;
			}
		}
		if (firstChance) { PassOn(event, DBG_EXCEPTION_NOT_HANDLED); return; } // the program's handlers come first
		CancelStep();
		Stop(event, DBG_EXCEPTION_NOT_HANDLED, StopInfo{ StopReason::Exception, thread, {}, ExceptionText(record.ExceptionCode) + " nothing handled" });
	}

	void Engine::StepDecided(const DEBUG_EVENT& event, const Target::Step::Decision& decision)
	{
		const DWORD thread = m_Step->step.Thread();
		m_Held = event;
		m_HeldStatus = DBG_CONTINUE;
		switch (decision.action)
		{
		case Target::Step::Action::SingleStep:
			m_Step->before = Observe(thread);
			Resume(thread, true);
			break;
		case Target::Step::Action::RunToReturn:
			if (m_Patches.Add(m_Process, decision.returnAddress))
			{
				m_Step->returnPatch = decision.returnAddress;
				m_Step->returnRsp = decision.returnRsp;
				Resume(thread, false);
			}
			else
			{
				CancelStep();
				Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Step, thread });
			}
			break;
		case Target::Step::Action::Stop:
			CancelStep();
			Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Step, thread });
			break;
		case Target::Step::Action::Run:
			CancelStep();
			Resume(thread, false);
			break;
		}
	}

	void Engine::Stop(const DEBUG_EVENT& event, DWORD status, StopInfo stop)
	{
		ClearStopTables();
		m_Held = event;
		m_HeldStatus = status;
		m_State = State::Stopped;
		m_Events.stopped(stop);
	}

	// Runs the held program on; `thread` single-steps when `trap` is set or when it sits on a patch.
	void Engine::Resume(DWORD thread, bool trap)
	{
		ClearStopTables();
		CONTEXT registers;
		if (m_Process.Registers(thread, registers))
		{
			if (m_Patches.Has(registers.Rip))
			{
				StepOverPatch(thread, registers.Rip);
				trap = true;
			}
			if (trap)
			{
				registers.EFlags |= kTrapFlag;
				m_Process.SetRegisters(thread, registers);
			}
		}
		ContinueDebugEvent(m_Held.dwProcessId, m_Held.dwThreadId, m_HeldStatus);
		m_State = State::Running;
	}

	void Engine::StepOverPatch(DWORD thread, uint64_t address)
	{
		Rearm rearm{ address, {} };
		for (DWORD other : m_Process.Threads())
			if (other != thread && SuspendThread(m_Process.Thread(other)) != static_cast<DWORD>(-1)) rearm.suspended.push_back(other);
		m_Patches.Lift(m_Process, address);
		m_Rearms[thread] = std::move(rearm);
	}

	bool Engine::Rearmed(DWORD thread)
	{
		auto found = m_Rearms.find(thread);
		if (found == m_Rearms.end()) return false;
		m_Patches.Restore(m_Process, found->second.address);
		for (DWORD other : found->second.suspended)
			if (const HANDLE handle = m_Process.Thread(other)) ResumeThread(handle);
		m_Rearms.erase(found);
		return true;
	}

	// A step that reaches a breakpoint off the line it started on (a breakpoint at the start of a function it steps
	// over, say) stops there, as that breakpoint.
	bool Engine::LandedOnBreakpoint(const DEBUG_EVENT& event)
	{
		const ThreadState now = Observe(event.dwThreadId);
		std::vector<uint32_t> ids = m_Breakpoints.IdsAt(now.rip);
		if (ids.empty() || (now.position.line == m_Step->step.Start().line && now.position.path == m_Step->step.Start().path)) return false;
		CancelStep();
		StopAtBreakpoint(event, event.dwThreadId, now.rip, std::move(ids));
		return true;
	}

	// A breakpoint on the line of a call the optimizer inlined stops in the inlined function's code: the stop shows
	// the thread in the frame whose line the breakpoint is on.
	void Engine::StopAtBreakpoint(const DEBUG_EVENT& event, DWORD thread, uint64_t address, std::vector<uint32_t> ids)
	{
		const size_t leftOut = InlinedInsideBreakpointLine(thread, address);
		Stop(event, DBG_CONTINUE, StopInfo{ StopReason::Breakpoint, thread, std::move(ids) });
		m_InlinedLeftOut = leftOut; // after Stop, which clears what the last stop knew
	}

	size_t Engine::InlinedInsideBreakpointLine(DWORD thread, uint64_t address) const
	{
		const std::vector<FrameRegisters> top = WalkStack(m_Process, thread, 1);
		if (top.empty()) return 0;
		const std::vector<uint32_t>& inlined = top[0].inlined;
		for (size_t depth = 0; depth <= inlined.size(); ++depth)
		{
			const uint32_t context = depth < inlined.size() ? inlined[depth] : 0;
			if (m_Breakpoints.SetOn(address, m_Symbols.PositionOf(CodePlace{ address, context }))) return depth;
		}
		return 0;
	}

	void Engine::Continue()
	{
		if (m_State == State::Stopped) Resume(m_Held.dwThreadId, false);
	}

	void Engine::Step(StepKind kind, DWORD thread)
	{
		if (m_State != State::Stopped || !m_Process.Thread(thread)) return;
		const ThreadState start = ObserveStopped(thread);
		if (!start.position.Known()) kind = StepKind::Out; // no lines to step through here: out to the caller
		const Target::Step step(kind, thread, start);
		uint64_t returnAddress = 0, returnRsp = 0;
		if (kind == StepKind::Out && !step.OutOfInlined())
		{
			const std::vector<FrameRegisters> frames = WalkStack(m_Process, thread, kReturnFrames);
			if (frames.size() < kReturnFrames) { Continue(); return; }
			returnAddress = frames[1].pc;
			returnRsp = frames[1].registers.Rsp;
		}
		m_Step.emplace(ActiveStep{ step, start });
		m_Step->frameRsp = start.rsp;
		const Target::Step::Decision first = m_Step->step.Begin(returnAddress, returnRsp);
		if (first.action == Target::Step::Action::RunToReturn)
		{
			if (!m_Patches.Add(m_Process, first.returnAddress)) { m_Step.reset(); Continue(); return; }
			m_Step->returnPatch = first.returnAddress;
			m_Step->returnRsp = first.returnRsp;
			Resume(thread, false);
			return;
		}
		Resume(thread, true);
	}

	void Engine::Pause()
	{
		if (m_State != State::Running) return;
		m_PauseRequested = true;
		DebugBreakProcess(m_Process.Handle());
	}

	void Engine::CancelStep()
	{
		if (!m_Step) return;
		if (m_Step->returnPatch) m_Patches.Release(m_Process, m_Step->returnPatch);
		ReleaseCatchStops();
		m_Step.reset();
	}

	// A throw is leaving the call the step runs over: the step ends in whichever catch of the stepped frame or of
	// its callers takes it (a catch inside the call returns to the step's return breakpoint as usual).
	void Engine::StopAtCatches(DWORD thread)
	{
		const std::vector<FrameRegisters> frames = WalkStack(m_Process, thread, kMaxCatchFrames);
		for (size_t depth = 0; depth < frames.size(); ++depth)
		{
			if (frames[depth].registers.Rsp < m_Step->frameRsp) continue; // deeper than the stepped frame
			for (uint64_t handler : CatchHandlersOf(m_Process, frames[depth].Lookup(depth)))
			{
				const uint64_t body = m_Symbols.NextStatementFrom(handler); // past the funclet's prologue
				if (m_Patches.Add(m_Process, body)) m_Step->catchPatches.push_back(body);
			}
		}
	}

	void Engine::ReleaseCatchStops()
	{
		if (!m_Step) return;
		for (uint64_t handler : m_Step->catchPatches) m_Patches.Release(m_Process, handler);
		m_Step->catchPatches.clear();
	}

	ThreadState Engine::Observe(DWORD thread) const
	{
		ThreadState state;
		CONTEXT registers;
		if (!m_Process.Registers(thread, registers)) return state;
		state.rip = registers.Rip;
		state.rsp = registers.Rsp;
		m_Process.Read(registers.Rsp, state.top);
		const InlinedCalls inlined = m_Symbols.InlinedAt(registers.Rip);
		state.position = m_Symbols.PositionOf(CodePlace{ registers.Rip, inlined.innermost });
		state.inlineDepth = inlined.depth;
		return state;
	}

	// The thread as its stop shows it: outside the inlined calls a breakpoint on their call's line stopped in.
	ThreadState Engine::ObserveStopped(DWORD thread) const
	{
		ThreadState state = Observe(thread);
		if (thread != m_Held.dwThreadId || m_InlinedLeftOut == 0) return state;
		const std::vector<FrameRegisters> top = WalkStack(m_Process, thread, 1);
		if (top.empty() || m_InlinedLeftOut > top[0].inlined.size()) return state;
		const std::vector<uint32_t>& inlined = top[0].inlined;
		const uint32_t context = m_InlinedLeftOut < inlined.size() ? inlined[m_InlinedLeftOut] : 0;
		state.position = m_Symbols.PositionOf(CodePlace{ state.rip, context });
		state.inlineDepth = static_cast<uint32_t>(inlined.size() - m_InlinedLeftOut);
		return state;
	}
} // namespace C4DAP::Target
