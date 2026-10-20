#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// C4 exceptions under the debug adapter: an uncaught one stops the program with the throw's stack, the
	// `thrown` filter stops at every throw, and with no filter a caught throw does not stop.
	CLASS(GROUP = Testing) DapExceptionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapExceptionTest);

		TEST_FUNC_TIMEOUT(Dap.Exception, 60)
		void AnUncaughtExceptionStopsTheProgram();
		TEST_FUNC_TIMEOUT(Dap.Exception, 60)
		void TheThrownFilterStopsAtEachThrow();
		TEST_FUNC_TIMEOUT(Dap.Exception, 60)
		void WithoutFiltersNothingStops();
	};
} // namespace Testing
