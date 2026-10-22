#include "Target/Step.h"

namespace C4DAP::Target
{
	namespace
	{
		constexpr uint64_t kReturnAddressBytes = 8;
		constexpr uint64_t kMaxInstructionBytes = 15; // an x64 instruction is at most this long

		// The instruction just executed was a call: it pushed the address of the instruction after it.
		bool Called(const ThreadState& before, const ThreadState& now)
		{
			return now.rsp == before.rsp - kReturnAddressBytes && now.top > before.rip && now.top <= before.rip + kMaxInstructionBytes;
		}

		// The instruction just executed was a return: it popped the address it went to.
		bool Returned(const ThreadState& before, const ThreadState& now)
		{
			return now.rip == before.top && now.rsp == before.rsp + kReturnAddressBytes;
		}

		bool SameLine(const SourcePosition& a, const SourcePosition& b) { return a.line == b.line && a.path == b.path; }
	}

	Step::Step(StepKind kind, DWORD thread, const ThreadState& start)
		: m_Kind(kind), m_Thread(thread), m_Start(start.position), m_StartDepth(start.inlineDepth) {}

	Step::Decision Step::Begin(uint64_t returnAddress, uint64_t returnRsp) const
	{
		if (m_Kind == StepKind::Out && !OutOfInlined()) return Decision{ Action::RunToReturn, returnAddress, returnRsp };
		return Decision{ Action::SingleStep };
	}

	// Calls the optimizer inlined are stepped like calls: over runs through them, into stops in them, and out of one
	// ends where the instruction is no longer in it.
	Step::Decision Step::WithinLine(const ThreadState& now) const
	{
		if (m_Kind == StepKind::Out)
			return now.inlineDepth < m_StartDepth && now.position.Known() ? Decision{ Action::Stop } : Decision{ Action::SingleStep };
		if (m_Kind == StepKind::Over && now.inlineDepth > m_StartDepth) return Decision{ Action::SingleStep };
		if (now.position.Known() && !SameLine(now.position, m_Start)) return Decision{ Action::Stop };
		return Decision{ Action::SingleStep };
	}

	Step::Decision Step::AfterInstruction(const ThreadState& before, const ThreadState& now) const
	{
		if (Called(before, now))
		{
			if (m_Kind == StepKind::Into && now.position.Known()) return Decision{ Action::Stop };
			return Decision{ Action::RunToReturn, now.top, now.rsp + kReturnAddressBytes };
		}
		if (Returned(before, now))
			return now.position.Known() ? Decision{ Action::Stop } : Decision{ Action::Run };
		return WithinLine(now);
	}

	Step::Decision Step::AfterReturn(const ThreadState& now) const
	{
		if (m_Kind == StepKind::Out && !OutOfInlined()) return now.position.Known() ? Decision{ Action::Stop } : Decision{ Action::Run };
		return WithinLine(now);
	}
} // namespace C4DAP::Target
