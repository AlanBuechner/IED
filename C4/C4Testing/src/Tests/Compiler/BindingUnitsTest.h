#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R8: CBind's bindings of a header are a unit of their own, which each file that binds the header includes - never text
	// appended to that file.
	CLASS(GROUP = Testing) BindingUnitsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BindingUnitsTest);

		TEST_FUNC(Compiler.BindingUnits)
		void BindingIsRegisteredOnceForEveryFileThatBindsIt();

		TEST_FUNC(Compiler.BindingUnits)
		void BindingIsIncludedByItsAbsolutePathOnly();

		TEST_FUNC(Compiler.BindingUnits)
		void IncluderAndItsIncludersSeeTheBindings();

		TEST_FUNC(Compiler.BindingUnits)
		void TwoBindingsOfOneDeclarationAreOne();

		TEST_FUNC(Compiler.BindingUnits)
		void IncludersLinesAreItsOwn();

		TEST_FUNC(Compiler.BindingUnits)
		void PriorParseIsTakenOnlyForTheSameBindings();
	};
}
