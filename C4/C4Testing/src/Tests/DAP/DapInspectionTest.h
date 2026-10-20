#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// What a stopped program's values look like through the debug adapter: every kind of value with its C4 type
	// name and children, and `evaluate` over fields, pointers, indexes, bases and globals.
	CLASS(GROUP = Testing) DapInspectionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapInspectionTest);

		TEST_FUNC_TIMEOUT(Dap.Inspection, 60)
		void EveryKindOfValueShows();
		TEST_FUNC_TIMEOUT(Dap.Inspection, 60)
		void ChildrenOpenObjectsPointersAndArrays();
		TEST_FUNC_TIMEOUT(Dap.Inspection, 60)
		void EvaluateFollowsPaths();
		TEST_FUNC_TIMEOUT(Dap.Inspection, 60)
		void EvaluateExplainsWhatItCannotRead();
	};
} // namespace Testing
