#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The macro pass over a whole program (Macro/MacroPass, Compiler/SourceMacros): includes carry macros, each file
	// is expanded once however many files include it, sources given as text are expanded too, and an interface file
	// is passed through with the macros it exports.
	CLASS(GROUP = Testing) MacroPassTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MacroPassTest);

		TEST_FUNC(Macro.Pass)
		void IncludesCarryTheirMacros();
		TEST_FUNC(Macro.Pass)
		void AHeaderIsExpandedOnce();
		TEST_FUNC(Macro.Pass)
		void SourcesGivenAsTextAreExpanded();
		TEST_FUNC(Macro.Pass)
		void InterfaceFilesPassThroughWithTheirMacros();
	};
} // namespace Testing
