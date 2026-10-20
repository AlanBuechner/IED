#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Debug information (-g, B28): what the module tells a debugger about the program's functions, variables,
	// types and lines.
	CLASS(GROUP = Testing) DebugInfoTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DebugInfoTest);

		TEST_FUNC(Semantic.DebugInfo)
		void TheModuleIsCodeView();
		TEST_FUNC(Semantic.DebugInfo)
		void AWrittenFunctionHasItsNameAndLine();
		TEST_FUNC(Semantic.DebugInfo)
		void ParametersAndLocalsAreNamedButTemporariesAreNot();
		TEST_FUNC(Semantic.DebugInfo)
		void ALocalFunctionIsDescribed();
		TEST_FUNC(Semantic.DebugInfo)
		void AMethodHasThis();
		TEST_FUNC(Semantic.DebugInfo)
		void EveryCallHasALine();
		TEST_FUNC(Semantic.DebugInfo)
		void ObjectsAndEnumsAreDescribed();
		TEST_FUNC(Semantic.DebugInfo)
		void UnionsAndCallablesAreDescribed();
		TEST_FUNC(Semantic.DebugInfo)
		void Char8IsTheNarrowCharacter();
		TEST_FUNC(Semantic.DebugInfo)
		void ACatchHandlerStartsOnItsCatchLine();
		TEST_FUNC(Semantic.DebugInfo)
		void GlobalsHaveTheirScopedNamesOnce();
		TEST_FUNC(Semantic.DebugInfo)
		void WithoutDebugInfoThereIsNone();
	};
} // namespace Testing
