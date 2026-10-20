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
} // namespace Testing
