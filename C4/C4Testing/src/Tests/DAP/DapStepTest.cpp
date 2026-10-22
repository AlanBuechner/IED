#include "DapStepTest.h"
#include "Target/Step.h"

using namespace C4DAP::Target;

namespace Testing
{
	namespace
	{
		using Action = Step::Action;
		constexpr DWORD kThread = 7;
		constexpr uint64_t kRsp = 0x1000;

		ThreadState At(uint64_t rip, uint32_t line, uint64_t rsp = kRsp, uint64_t top = 0)
		{
			return ThreadState{ rip, rsp, top, line ? SourcePosition{ "main.cf", line } : SourcePosition{} };
		}

		// The thread right after a 5-byte call at `from` into `callee`.
		ThreadState AfterCall(uint64_t from, uint64_t callee, uint32_t calleeLine)
		{
			return At(callee, calleeLine, kRsp - 8, from + 5);
		}

		// The thread inside `depth` calls the optimizer inlined, on a line of the innermost.
		ThreadState Inlined(uint64_t rip, uint32_t line, uint32_t depth)
		{
			ThreadState state = At(rip, line);
			state.inlineDepth = depth;
			return state;
		}
	}

	void DapStepTest::OverAndIntoSingleStepOutRunsToTheCaller()
	{
		TEST_ASSERT(Step(StepKind::Over, kThread, At(0x10, 3)).Begin(0, 0).action == Action::SingleStep);
		TEST_ASSERT(Step(StepKind::Into, kThread, At(0x10, 3)).Begin(0, 0).action == Action::SingleStep);
		const Step::Decision out = Step(StepKind::Out, kThread, At(0x10, 3)).Begin(0x500, 0x1100);
		TEST_ASSERT(out.action == Action::RunToReturn);
		TEST_ASSERT_EQ(out.returnAddress, uint64_t(0x500));
		TEST_ASSERT_EQ(out.returnRsp, uint64_t(0x1100));
		TEST_PASSED();
	}

	void DapStepTest::TheSameLineKeepsStepping()
	{
		const Step step(StepKind::Over, kThread, At(0x10, 3));
		TEST_ASSERT(step.AfterInstruction(At(0x10, 3), At(0x14, 3)).action == Action::SingleStep);
		TEST_ASSERT(step.AfterInstruction(At(0x14, 3), At(0x18, 0)).action == Action::SingleStep); // no line here: on
		TEST_PASSED();
	}

	void DapStepTest::ANewLineStops()
	{
		const Step step(StepKind::Over, kThread, At(0x10, 3));
		TEST_ASSERT(step.AfterInstruction(At(0x10, 3), At(0x20, 4)).action == Action::Stop);
		TEST_ASSERT(step.AfterInstruction(At(0x10, 3), At(0x08, 2)).action == Action::Stop); // a loop back
		ThreadState otherFile = At(0x20, 3);
		otherFile.position.path = "other.ch";
		TEST_ASSERT(step.AfterInstruction(At(0x10, 3), otherFile).action == Action::Stop);
		TEST_PASSED();
	}

	void DapStepTest::OverRunsACallToItsReturn()
	{
		const Step step(StepKind::Over, kThread, At(0x10, 3));
		const Step::Decision decision = step.AfterInstruction(At(0x10, 3), AfterCall(0x10, 0x900, 20));
		TEST_ASSERT(decision.action == Action::RunToReturn);
		TEST_ASSERT_EQ(decision.returnAddress, uint64_t(0x15));
		TEST_ASSERT_EQ(decision.returnRsp, kRsp); // back where the call left it: a deeper recursion has less
		TEST_PASSED();
	}

	void DapStepTest::IntoStopsInACalleeWithLines()
	{
		const Step step(StepKind::Into, kThread, At(0x10, 3));
		TEST_ASSERT(step.AfterInstruction(At(0x10, 3), AfterCall(0x10, 0x900, 20)).action == Action::Stop);
		TEST_PASSED();
	}

	void DapStepTest::IntoRunsThroughACalleeWithoutLines()
	{
		const Step step(StepKind::Into, kThread, At(0x10, 3));
		const Step::Decision decision = step.AfterInstruction(At(0x10, 3), AfterCall(0x10, 0x900, 0));
		TEST_ASSERT(decision.action == Action::RunToReturn);
		TEST_ASSERT_EQ(decision.returnAddress, uint64_t(0x15));
		TEST_PASSED();
	}

	void DapStepTest::ReturningStopsInTheCaller()
	{
		const Step step(StepKind::Over, kThread, At(0x90, 4));
		const ThreadState beforeRet = At(0x94, 4, kRsp, 0x15);
		TEST_ASSERT(step.AfterInstruction(beforeRet, At(0x15, 8, kRsp + 8)).action == Action::Stop);
		TEST_PASSED();
	}

	void DapStepTest::ReturningIntoCodeWithoutLinesRuns()
	{
		const Step step(StepKind::Over, kThread, At(0x90, 10));
		const ThreadState beforeRet = At(0x94, 10, kRsp, 0x7000);
		TEST_ASSERT(step.AfterInstruction(beforeRet, At(0x7000, 0, kRsp + 8)).action == Action::Run);
		TEST_PASSED();
	}

	void DapStepTest::APushIsNotACall()
	{
		// A push lowers rsp by 8 too, but what it stores is not the address of the next instruction.
		const Step step(StepKind::Over, kThread, At(0x10, 3));
		TEST_ASSERT(step.AfterInstruction(At(0x10, 3), At(0x11, 3, kRsp - 8, 0x42)).action == Action::SingleStep);
		TEST_PASSED();
	}

	void DapStepTest::AfterTheReturnBreakpoint()
	{
		TEST_ASSERT(Step(StepKind::Over, kThread, At(0x10, 3)).AfterReturn(At(0x15, 3)).action == Action::SingleStep);
		TEST_ASSERT(Step(StepKind::Over, kThread, At(0x10, 3)).AfterReturn(At(0x15, 4)).action == Action::Stop);
		TEST_ASSERT(Step(StepKind::Out, kThread, At(0x10, 3)).AfterReturn(At(0x15, 8)).action == Action::Stop);
		TEST_ASSERT(Step(StepKind::Out, kThread, At(0x10, 3)).AfterReturn(At(0x7000, 0)).action == Action::Run);
		TEST_PASSED();
	}

	// BUG-352: a call the optimizer inlined has no call instruction; the inline depth says the step entered it.
	void DapStepTest::OverRunsThroughAnInlinedCall()
	{
		const Step step(StepKind::Over, kThread, At(0x10, 9));
		TEST_ASSERT(step.AfterInstruction(At(0x10, 9), Inlined(0x14, 3, 1)).action == Action::SingleStep);
		TEST_ASSERT(step.AfterInstruction(Inlined(0x14, 3, 1), Inlined(0x18, 4, 2)).action == Action::SingleStep);
		TEST_ASSERT(step.AfterInstruction(Inlined(0x18, 4, 2), At(0x1c, 10)).action == Action::Stop);
		const Step inside(StepKind::Over, kThread, Inlined(0x14, 3, 1));
		TEST_ASSERT(inside.AfterInstruction(Inlined(0x14, 3, 1), Inlined(0x16, 4, 1)).action == Action::Stop); // its next line
		TEST_ASSERT(inside.AfterInstruction(Inlined(0x14, 3, 1), At(0x1c, 10)).action == Action::Stop);         // back in the caller
		TEST_PASSED();
	}

	void DapStepTest::IntoStopsInAnInlinedCall()
	{
		const Step step(StepKind::Into, kThread, At(0x10, 9));
		TEST_ASSERT(step.AfterInstruction(At(0x10, 9), Inlined(0x14, 3, 1)).action == Action::Stop);
		TEST_PASSED();
	}

	void DapStepTest::OutOfAnInlinedCallEndsWhereItDoes()
	{
		const Step step(StepKind::Out, kThread, Inlined(0x14, 3, 1));
		TEST_ASSERT(step.OutOfInlined());
		TEST_ASSERT(!Step(StepKind::Out, kThread, At(0x10, 9)).OutOfInlined());
		TEST_ASSERT(step.Begin(0, 0).action == Action::SingleStep);
		TEST_ASSERT(step.AfterInstruction(Inlined(0x14, 3, 1), Inlined(0x16, 4, 1)).action == Action::SingleStep);
		TEST_ASSERT(step.AfterInstruction(Inlined(0x16, 4, 1), Inlined(0x18, 0, 0)).action == Action::SingleStep); // no line yet
		TEST_ASSERT(step.AfterInstruction(Inlined(0x18, 0, 0), At(0x1c, 9)).action == Action::Stop);
		const Step::Decision call = step.AfterInstruction(Inlined(0x16, 4, 1), AfterCall(0x16, 0x900, 20));
		TEST_ASSERT(call.action == Action::RunToReturn); // a real call inside it runs to its return
		TEST_ASSERT(step.AfterReturn(Inlined(0x1b, 4, 1)).action == Action::SingleStep);
		TEST_ASSERT(step.AfterReturn(At(0x1c, 9)).action == Action::Stop);
		TEST_PASSED();
	}
} // namespace Testing
