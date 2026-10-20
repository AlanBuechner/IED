#pragma once
#include "Target/Symbols.h"
#include <windows.h>
#include <cstdint>

namespace C4DAP::Target
{
	enum class StepKind { Over, Into, Out };

	/// What the stepping thread looks like between two instructions.
	struct ThreadState
	{
		uint64_t rip = 0;
		uint64_t rsp = 0;
		uint64_t top = 0; // the word at rsp: a call's return address right after the call
		SourcePosition position;
	};

	/// One source-line step of one thread (docs/tools/03-editor-lsp-dap.md, "Stepping"). It only decides; the
	/// engine single-steps, plants the return breakpoint and stops or runs as told.
	class Step
	{
	public:
		enum class Action { SingleStep, RunToReturn, Stop, Run };
		struct Decision
		{
			Action action = Action::Stop;
			uint64_t returnAddress = 0; // RunToReturn: where the call returns
			uint64_t returnRsp = 0;     // ... and the stack pointer once it has (a deeper recursion has another)
		};

		Step(StepKind kind, DWORD thread, const ThreadState& start);

		/// The first move: single-step for over and into, run to `returnAddress` (the frame's caller) for out.
		Decision Begin(uint64_t returnAddress, uint64_t returnRsp) const;
		/// After one instruction, from `before` to `now`.
		Decision AfterInstruction(const ThreadState& before, const ThreadState& now) const;
		/// The return breakpoint was reached by this thread with the expected stack pointer.
		Decision AfterReturn(const ThreadState& now) const;

		StepKind Kind() const { return m_Kind; }
		const SourcePosition& Start() const { return m_Start; }
		DWORD Thread() const { return m_Thread; }

	private:
		Decision WithinLine(const ThreadState& now) const;

		StepKind m_Kind;
		DWORD m_Thread;
		SourcePosition m_Start;
	};
} // namespace C4DAP::Target
