#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Optimized debug info for CodeView (BUG-344, Compiler/CodeViewLocations.h): a variable's constant locations go
	// when it has another location or its constants disagree, so LLVM keeps its register and stack ranges.
	CLASS(GROUP = Testing) CodeViewLocationsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CodeViewLocationsTest);

		TEST_FUNC(Compiler.CodeViewLocations)
		void AVariableWithAnotherLocationLosesItsConstants();
		TEST_FUNC(Compiler.CodeViewLocations)
		void AConstantVariableKeepsItsValue();
		TEST_FUNC(Compiler.CodeViewLocations)
		void ConstantsThatDisagreeAreDropped();
		TEST_FUNC(Compiler.CodeViewLocations)
		void AKilledLocationCountsAsAnother();
		TEST_FUNC(Compiler.CodeViewLocations)
		void AFragmentWithAnotherLocationLosesItsConstants();
		TEST_FUNC(Compiler.CodeViewLocations)
		void WithoutCodeViewNothingChanges();
	};
} // namespace Testing
