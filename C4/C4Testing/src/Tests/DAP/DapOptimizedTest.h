#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Optimized code through the debug adapter (BUG-344, BUG-345, BUG-348, BUG-351, BUG-352): DbgHelp's flags to storage,
	// every part of a register, constants and variables optimized away, an -O3 loop whose counters show their values,
	// and calls the optimizer inlined, which stop, show and step as calls.
	CLASS(GROUP = Testing) DapOptimizedTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapOptimizedTest);

		TEST_FUNC(Dap.Optimized)
		void StorageFollowsDbgHelpsFlags();
		TEST_FUNC(Dap.Optimized)
		void RegistersReadTheirParts();
		TEST_FUNC(Dap.Optimized)
		void ConstantsAndOptimizedAwayHaveNoAddress();
		TEST_FUNC_TIMEOUT(Dap.Optimized, 60)
		void AnOptimizedLoopShowsItsLocals();
		TEST_FUNC_TIMEOUT(Dap.Optimized, 60)
		void AnInlinedCallIsAFrameOfItsOwn();
		TEST_FUNC_TIMEOUT(Dap.Optimized, 60)
		void ABreakpointOnAnInlinedCallsLineStopsThere();
	};
} // namespace Testing
